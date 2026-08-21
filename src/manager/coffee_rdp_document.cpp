#include "coffee_rdp_document.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <tuple>

/* For CONNECTION_TYPE_* only -- header constants, nothing linked. Taken from
 * FreeRDP rather than hardcoded so they can't drift from the library this is
 * actually built against. */
#include <freerdp/settings_types.h>

#include <sys/stat.h>

namespace
{
std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

/* Key names, matching FreeRDP's own (client/common/file.c). */
constexpr const char* kFullAddress = "full address";
constexpr const char* kServerPort = "server port";
constexpr const char* kUsername = "username";
constexpr const char* kDomain = "domain";
constexpr const char* kUseMultimon = "use multimon";
constexpr const char* kScreenModeId = "screen mode id";
/* The AAD/Entra flag. Preserved verbatim for an *unmodeled* key by this
 * file's line-retention alone, but that only helps a profile linked to the
 * file -- an imported profile is unlinked, so the flag has to be modeled to
 * survive the trip into CoffeeProfile and back out as /sec:aad. */
constexpr const char* kEnableRdsAadAuth = "enablerdsaadauth";

/* Standard performance / bandwidth keys, also FreeRDP's and mstsc's. These
 * are what make a CoffeeRDP-written file behave correctly in *other*
 * clients: a quality preset is CoffeeRDP's own abstraction, but the
 * settings it stands for are all expressible in stock .rdp keys, so we
 * write those too rather than only our shorthand. */
constexpr const char* kConnectionType = "connection type";
/* Both autodetect keys are written, always in agreement. FreeRDP's parser
 * (client/common/file.c) logs a warning and self-corrects when they
 * disagree -- "Add networkautodetect:i:1 to your RDP file to eliminate this
 * warning" -- and derives its final NetworkAutoDetect setting from
 * (bandwidth != 0) || (network != 0). mstsc only writes networkautodetect;
 * following that alone would leave our own session logging warnings about
 * files we wrote, so FreeRDP's convention wins here. */
constexpr const char* kNetworkAutoDetect = "networkautodetect";
constexpr const char* kBandwidthAutoDetect = "bandwidthautodetect";
constexpr const char* kDisableWallpaper = "disable wallpaper";
constexpr const char* kDisableFullWindowDrag = "disable full window drag";
constexpr const char* kDisableMenuAnims = "disable menu anims";
constexpr const char* kDisableThemes = "disable themes";
constexpr const char* kDisableCursorSetting = "disable cursor setting";
constexpr const char* kAllowFontSmoothing = "allow font smoothing";
constexpr const char* kAllowDesktopComposition = "allow desktop composition";

/* CoffeeRDP's own keys -- kept deliberately few. `quality` is a *hint*
 * recording which named preset was chosen; the standard keys above carry
 * the actual settings, so a client that has never heard of CoffeeRDP still
 * gets the right behavior from a file we wrote. The idle keep-alive keys
 * have no standard equivalent at all (no RDP client exposes this; Remmina
 * keeps it in its own config, not in .rdp), so they stay custom by
 * necessity -- other clients ignore unknown keys, exactly as we do.
 *
 * These MUST be written with type `s`: the session reads them via
 * coffee_rdp_file_scan_key() (src/session/coffee_rdp_file.cpp), which only
 * matches type 's' lines. `idle keypress time:i:30` would parse as valid
 * .rdp but be invisible to the session -- the setting would silently not
 * apply. */
constexpr const char* kQuality = "quality";
constexpr const char* kIdleTime = "idle keypress time";
constexpr const char* kIdleCombo = "idle keypress combo";
/* Must match the literal string sdl_freerdp.cpp scans for when a .rdp file
 * is launched directly (not through the manager, which always passes
 * /disable-shortcuts explicitly and so never depends on this key at
 * session-launch time) -- no shared header between the two binaries for
 * this one key, same as the three above. */
constexpr const char* kDisableShortcuts = "disable shortcuts";
/* Same reasoning/pairing as kDisableShortcuts above -- must match the
 * literal string sdl_freerdp.cpp scans for on a direct .rdp launch. Unlike
 * /disable-shortcuts, sessionArgs() maps this to a genuine FreeRDP flag
 * (/cert:ignore) rather than a CoffeeRDP-invented one, but the key still has
 * to be custom: FreeRDP's own .rdp file parser has no key for
 * IgnoreCertificate at all (confirmed against client/common/file.c). */
constexpr const char* kIgnoreCertificateErrors = "ignore certificate errors";

constexpr int kScreenModeFullscreen = 2;
constexpr int kScreenModeWindowed = 1;

/** The stock-.rdp expression of each CoffeeRDP quality preset, derived from
 *  the PerformanceFlags/connection-type table in PLAN.md §2.4 (and matching
 *  coffee_quality.cpp's table, which is what the session actually applies).
 *
 *  Caveat: PERF_DISABLE_CURSOR_SHADOW (0x20, part of Speed's 0x6f mask) has
 *  **no standard .rdp key** -- mstsc doesn't expose it. So the standard keys
 *  express Speed *almost* exactly; the `quality` hint is what lets the
 *  session reproduce the mask bit-for-bit. That's the reason both are
 *  written rather than picking one. */
struct QualityStandardKeys
{
	const char* name;
	int connectionType;
	/* Kept equal to each other by construction -- see kBandwidthAutoDetect.
	 * FreeRDP writes bandwidthautodetect as (ConnectionType >=
	 * CONNECTION_TYPE_AUTODETECT), which is exactly "auto" and nothing else. */
	int autoDetect;
	int disableWallpaper;
	int disableFullWindowDrag;
	int disableMenuAnims;
	int disableThemes;
	int disableCursorSetting;
	int allowFontSmoothing;
	int allowDesktopComposition;
};

constexpr QualityStandardKeys kQualityTable[] = {
	/* name        connection type                 auto wall drag anim theme cur font comp */
	{ "speed",     CONNECTION_TYPE_BROADBAND_LOW,  0,   1,   1,   1,   1,    1,  0,   0 },
	{ "balanced",  CONNECTION_TYPE_BROADBAND_HIGH, 0,   1,   1,   1,   0,    0,  0,   0 },
	{ "quality",   CONNECTION_TYPE_WAN,            0,   1,   0,   0,   0,    0,  0,   0 },
	{ "best",      CONNECTION_TYPE_LAN,            0,   0,   0,   0,   0,    0,  1,   0 },
	{ "auto",      CONNECTION_TYPE_AUTODETECT,     1,   1,   1,   1,   0,    0,  0,   0 },
};

const QualityStandardKeys* qualityByName(const std::string& name)
{
	const auto want = lower(name);
	for (const auto& row : kQualityTable)
	{
		if (want == row.name)
			return &row;
	}
	return nullptr;
}

void writeQualityStandardKeys(CoffeeRdpDocument& doc, const QualityStandardKeys& q)
{
	doc.setInt(kConnectionType, q.connectionType);
	doc.setInt(kNetworkAutoDetect, q.autoDetect);
	doc.setInt(kBandwidthAutoDetect, q.autoDetect);
	doc.setInt(kDisableWallpaper, q.disableWallpaper);
	doc.setInt(kDisableFullWindowDrag, q.disableFullWindowDrag);
	doc.setInt(kDisableMenuAnims, q.disableMenuAnims);
	doc.setInt(kDisableThemes, q.disableThemes);
	doc.setInt(kDisableCursorSetting, q.disableCursorSetting);
	doc.setInt(kAllowFontSmoothing, q.allowFontSmoothing);
	doc.setInt(kAllowDesktopComposition, q.allowDesktopComposition);
}

/** Recovers a preset name from stock .rdp keys, for files written by mstsc,
 *  Remmina, FreeRDP itself, or an earlier CoffeeRDP. Requires an exact match
 *  on every key the preset defines: a near-miss deliberately returns empty
 *  ("Default") rather than guessing, because guessing wrong would silently
 *  change the connection's behavior on the next save. An empty result is
 *  safe -- the file's own standard keys still apply through FreeRDP's
 *  parser, we simply don't claim they're one of our named presets. */
std::string inferQualityFromStandardKeys(const CoffeeRdpDocument& doc)
{
	const auto connectionType = doc.getInt(kConnectionType);
	if (!connectionType)
		return "";

	auto matches = [&](const char* key, int expected)
	{
		const auto actual = doc.getInt(key);
		/* An absent key means the client left it at its default, which for
		 * every one of these is 0/off. */
		return actual.value_or(0) == expected;
	};

	for (const auto& row : kQualityTable)
	{
		if (*connectionType != row.connectionType)
			continue;
		if (matches(kDisableWallpaper, row.disableWallpaper) &&
		    matches(kDisableFullWindowDrag, row.disableFullWindowDrag) &&
		    matches(kDisableMenuAnims, row.disableMenuAnims) &&
		    matches(kDisableThemes, row.disableThemes) &&
		    matches(kDisableCursorSetting, row.disableCursorSetting) &&
		    matches(kAllowFontSmoothing, row.allowFontSmoothing) &&
		    matches(kAllowDesktopComposition, row.allowDesktopComposition))
			return row.name;
	}
	return "";
}

/** Splits "host" or "host:port". Only treats a trailing :N as a port when N
 *  parses cleanly as a number, so IPv6-ish or odd hostnames aren't mangled. */
void splitHostPort(const std::string& value, std::string& host, std::optional<unsigned>& port)
{
	host = value;
	port.reset();

	const auto colon = value.find_last_of(':');
	if (colon == std::string::npos || colon + 1 >= value.size())
		return;

	const auto portStr = value.substr(colon + 1);
	if (portStr.find_first_not_of("0123456789") != std::string::npos)
		return;

	errno = 0;
	char* end = nullptr;
	const unsigned long parsed = std::strtoul(portStr.c_str(), &end, 10);
	if ((errno != 0) || !end || (*end != '\0') || (parsed == 0) || (parsed > 65535))
		return;

	host = value.substr(0, colon);
	port = static_cast<unsigned>(parsed);
}

std::string parentDir(const std::string& path)
{
	const auto slash = path.find_last_of('/');
	if (slash == std::string::npos)
		return "";
	return path.substr(0, slash);
}
} // namespace

bool CoffeeRdpDocument::load(const std::string& path)
{
	_lines.clear();
	_crlf = false;

	std::ifstream in(path, std::ios::binary);
	if (!in.is_open())
	{
		struct stat st{};
		if (stat(path.c_str(), &st) != 0)
			return true; // missing file: empty document, created on save
		return false;
	}

	std::string line;
	while (std::getline(in, line))
	{
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
			_crlf = true; // remember, so a rewrite keeps the file's own convention
		}

		Line entry;
		entry.raw = line;

		/* Same shape check as FreeRDP's parse_line(): name:type:value with a
		 * single-character type. Anything else (comments, blanks, malformed)
		 * is retained verbatim as a non-key line. */
		const auto d1 = line.find(':');
		const auto d2 = (d1 == std::string::npos) ? std::string::npos : line.find(':', d1 + 1);
		if ((d1 != std::string::npos) && (d2 != std::string::npos) && (d2 - d1 == 2) && (d1 > 0))
		{
			entry.isKey = true;
			entry.nameOriginal = line.substr(0, d1);
			entry.nameLower = lower(entry.nameOriginal);
			entry.type = line[d1 + 1];
			entry.value = line.substr(d2 + 1);
		}

		_lines.push_back(std::move(entry));
	}

	return true;
}

bool CoffeeRdpDocument::save(const std::string& path) const
{
	const auto dir = parentDir(path);
	if (!dir.empty())
	{
		struct stat st{};
		if ((stat(dir.c_str(), &st) != 0) || !S_ISDIR(st.st_mode))
			return false; // don't invent directories for a path the user typed
	}

	const std::string tmp = path + ".coffeerdp.tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out.is_open())
			return false;

		const char* eol = _crlf ? "\r\n" : "\n";
		for (const auto& line : _lines)
		{
			if (line.isKey)
				out << line.nameOriginal << ":" << line.type << ":" << line.value << eol;
			else
				out << line.raw << eol;
		}

		out.flush();
		if (!out.good())
		{
			out.close();
			std::remove(tmp.c_str());
			return false;
		}
	}

	/* Mirror the original file's permissions if it already existed, so a
	 * rewrite doesn't quietly widen or narrow access to it. */
	struct stat existing{};
	if (stat(path.c_str(), &existing) == 0)
		std::ignore = chmod(tmp.c_str(), existing.st_mode & 07777);

	if (std::rename(tmp.c_str(), path.c_str()) != 0)
	{
		std::remove(tmp.c_str());
		return false;
	}
	return true;
}

const CoffeeRdpDocument::Line* CoffeeRdpDocument::findLine(const std::string& key) const
{
	const auto want = lower(key);
	for (const auto& line : _lines)
	{
		if (line.isKey && (line.nameLower == want))
			return &line;
	}
	return nullptr;
}

CoffeeRdpDocument::Line* CoffeeRdpDocument::findLine(const std::string& key)
{
	const auto want = lower(key);
	for (auto& line : _lines)
	{
		if (line.isKey && (line.nameLower == want))
			return &line;
	}
	return nullptr;
}

bool CoffeeRdpDocument::has(const std::string& key) const
{
	return findLine(key) != nullptr;
}

std::optional<std::string> CoffeeRdpDocument::getString(const std::string& key) const
{
	const auto* line = findLine(key);
	if (!line)
		return std::nullopt;
	return line->value;
}

std::optional<int> CoffeeRdpDocument::getInt(const std::string& key) const
{
	const auto* line = findLine(key);
	if (!line || line->value.empty())
		return std::nullopt;

	errno = 0;
	char* end = nullptr;
	const long parsed = std::strtol(line->value.c_str(), &end, 10);
	if ((errno != 0) || !end || (*end != '\0'))
		return std::nullopt;
	return static_cast<int>(parsed);
}

void CoffeeRdpDocument::setRaw(const std::string& key, char type, const std::string& value)
{
	if (auto* line = findLine(key))
	{
		/* Update in place: keeps the key's position in the file and the
		 * spelling the file already used for its name. */
		line->type = type;
		line->value = value;
		return;
	}

	Line entry;
	entry.isKey = true;
	entry.nameOriginal = key;
	entry.nameLower = lower(key);
	entry.type = type;
	entry.value = value;
	_lines.push_back(std::move(entry));
}

void CoffeeRdpDocument::setString(const std::string& key, const std::string& value)
{
	setRaw(key, 's', value);
}

void CoffeeRdpDocument::setInt(const std::string& key, int value)
{
	setRaw(key, 'i', std::to_string(value));
}

void CoffeeRdpDocument::remove(const std::string& key)
{
	const auto want = lower(key);
	_lines.erase(std::remove_if(_lines.begin(), _lines.end(),
	                            [&](const Line& l) { return l.isKey && (l.nameLower == want); }),
	             _lines.end());
}

void coffee_rdp_document_to_profile(const CoffeeRdpDocument& doc, CoffeeProfile& profile)
{
	if (const auto addr = doc.getString(kFullAddress))
	{
		std::string host;
		std::optional<unsigned> port;
		splitHostPort(*addr, host, port);
		profile.host = host;
		if (port)
			profile.port = *port;
	}

	/* An explicit `server port` key wins over a port embedded in the
	 * address, matching FreeRDP's own precedence. */
	if (const auto port = doc.getInt(kServerPort))
	{
		if ((*port > 0) && (*port <= 65535))
			profile.port = static_cast<unsigned>(*port);
	}

	if (const auto user = doc.getString(kUsername))
		profile.username = *user;
	if (const auto domain = doc.getString(kDomain))
		profile.domain = *domain;

	if (const auto aad = doc.getInt(kEnableRdsAadAuth))
		profile.aadAuth = (*aad != 0);

	if (const auto multimon = doc.getInt(kUseMultimon))
		profile.multimon = (*multimon != 0);
	if (const auto mode = doc.getInt(kScreenModeId))
		profile.fullscreen = (*mode == kScreenModeFullscreen);

	/* CoffeeRDP's own `quality` hint is authoritative when present -- it
	 * names the exact preset, including the one bit (cursor shadow) that
	 * stock keys can't express. Otherwise fall back to reading the standard
	 * keys, which is what makes a file from mstsc/Remmina/FreeRDP land in
	 * the right preset in our UI instead of showing "Default". */
	if (const auto quality = doc.getString(kQuality))
		profile.quality = *quality;
	else
	{
		const auto inferred = inferQualityFromStandardKeys(doc);
		if (!inferred.empty())
			profile.quality = inferred;
	}

	if (const auto idle = doc.getString(kIdleTime))
	{
		errno = 0;
		char* end = nullptr;
		const unsigned long parsed = std::strtoul(idle->c_str(), &end, 10);
		if ((errno == 0) && end && (*end == '\0') && !idle->empty())
			profile.idleKeepAliveSeconds = static_cast<unsigned>(parsed);
	}
	if (const auto combo = doc.getString(kIdleCombo))
		profile.idleKeepAliveCombo = *combo;

	if (const auto shortcuts = doc.getString(kDisableShortcuts))
		profile.disableShortcuts = (*shortcuts == "1");

	if (const auto ignoreCert = doc.getString(kIgnoreCertificateErrors))
		profile.ignoreCertificateErrors = (*ignoreCert == "1");
}

void coffee_rdp_document_from_profile(const CoffeeProfile& profile, CoffeeRdpDocument& doc)
{
	doc.setString(kFullAddress, profile.host);

	/* Only carry an explicit port key when it's meaningful: writing
	 * `server port:i:3389` into a file that never had it is noise, but if
	 * the file already used the key, keep it in sync rather than leaving a
	 * stale value that would silently win over the address. */
	if ((profile.port != 3389) || doc.has(kServerPort))
		doc.setInt(kServerPort, static_cast<int>(profile.port));

	/* Empty means "not set" -- remove the key rather than writing an empty
	 * value, which FreeRDP would read as an explicit empty username/domain
	 * and could suppress a prompt that should happen. */
	if (profile.username.empty())
		doc.remove(kUsername);
	else
		doc.setString(kUsername, profile.username);

	if (profile.domain.empty())
		doc.remove(kDomain);
	else
		doc.setString(kDomain, profile.domain);

	/* Written when on, or when the file already carried the key -- so turning
	 * the switch off writes an explicit `:i:0` into a file that had `:i:1`
	 * (leaving the 1 there would let the file silently re-enable what the user
	 * just disabled), while a file that never mentioned AAD doesn't gain a
	 * key it doesn't need. Same reasoning as `server port` above. */
	if (profile.aadAuth || doc.has(kEnableRdsAadAuth))
		doc.setInt(kEnableRdsAadAuth, profile.aadAuth ? 1 : 0);

	doc.setInt(kUseMultimon, profile.multimon ? 1 : 0);
	doc.setInt(kScreenModeId, profile.fullscreen ? kScreenModeFullscreen : kScreenModeWindowed);

	if (profile.quality.empty())
	{
		/* "Default" = CoffeeRDP has no opinion. Drop our hint, but leave the
		 * standard performance keys exactly as they are: they may well have
		 * come from mstsc or another client, and deleting them would destroy
		 * settings the user never asked us to touch. */
		doc.remove(kQuality);
	}
	else if (const auto* q = qualityByName(profile.quality))
	{
		/* Write both: the standard keys so any other client (and FreeRDP's
		 * own parser) behaves correctly, and our hint so the exact preset --
		 * including the cursor-shadow bit no stock key covers -- round-trips
		 * back into this UI and into the session. */
		doc.setString(kQuality, profile.quality);
		writeQualityStandardKeys(doc, *q);
	}
	else
	{
		/* An unrecognized preset name: record it but don't invent standard
		 * keys for a preset we can't map. */
		doc.setString(kQuality, profile.quality);
	}

	/* Always written, including 0: the session's built-in default is 30s, so
	 * omitting the key for a profile that deliberately disabled keep-alive
	 * would silently re-enable it. Same reasoning as
	 * CoffeeProfileStore::sessionArgs(). Type is `s`, not `i` -- see the
	 * comment on kIdleTime above. */
	doc.setString(kIdleTime, std::to_string(profile.idleKeepAliveSeconds));

	if (profile.idleKeepAliveCombo.empty())
		doc.remove(kIdleCombo);
	else
		doc.setString(kIdleCombo, profile.idleKeepAliveCombo);

	/* Written when on, or when the file already carried the key -- so
	 * turning shortcuts back on writes an explicit "0" over a file's stale
	 * "1" rather than leaving it to silently keep disabling them next
	 * launch. Same reasoning as kEnableRdsAadAuth above; type `s`, not `i`,
	 * for the same reason as kIdleTime -- this is a CoffeeRDP-custom key,
	 * not one FreeRDP's own parser reads. */
	if (profile.disableShortcuts || doc.has(kDisableShortcuts))
		doc.setString(kDisableShortcuts, profile.disableShortcuts ? "1" : "0");

	/* Same "write an explicit 0 rather than just stop writing 1" reasoning
	 * as kDisableShortcuts/kEnableRdsAadAuth above -- a stale "1" left in
	 * the file would silently keep ignoring certificate errors after the
	 * user turned it back off. */
	if (profile.ignoreCertificateErrors || doc.has(kIgnoreCertificateErrors))
		doc.setString(kIgnoreCertificateErrors, profile.ignoreCertificateErrors ? "1" : "0");
}

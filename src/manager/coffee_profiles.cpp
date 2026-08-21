#include "coffee_profiles.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace
{
std::string trim(const std::string& s)
{
	size_t begin = 0;
	size_t end = s.size();
	while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])))
		begin++;
	while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
		end--;
	return s.substr(begin, end - begin);
}

bool parseBool(const std::string& v, bool fallback)
{
	auto s = trim(v);
	std::transform(s.begin(), s.end(), s.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (s == "true" || s == "1" || s == "yes")
		return true;
	if (s == "false" || s == "0" || s == "no")
		return false;
	return fallback;
}

unsigned parseUnsigned(const std::string& v, unsigned fallback)
{
	errno = 0;
	char* end = nullptr;
	const auto s = trim(v);
	const unsigned long parsed = std::strtoul(s.c_str(), &end, 10);
	if ((errno != 0) || !end || (*end != '\0') || s.empty())
		return fallback;
	return static_cast<unsigned>(parsed);
}

const char* boolToString(bool v)
{
	return v ? "true" : "false";
}

/** Creates `path` and any missing parents, like `mkdir -p`. Directories are
 *  0700: this holds profile data §2.3 treats as sensitive. */
bool makeDirs(const std::string& path)
{
	if (path.empty())
		return false;

	std::string partial;
	partial.reserve(path.size());
	size_t i = 0;
	if (path[0] == '/')
	{
		partial = "/";
		i = 1;
	}

	while (i < path.size())
	{
		size_t slash = path.find('/', i);
		if (slash == std::string::npos)
			slash = path.size();

		partial += path.substr(i, slash - i);
		if (!partial.empty() && (mkdir(partial.c_str(), 0700) != 0) && (errno != EEXIST))
			return false;
		partial += "/";
		i = slash + 1;
	}
	return true;
}

std::string parentDir(const std::string& path)
{
	const auto slash = path.find_last_of('/');
	if (slash == std::string::npos)
		return "";
	return path.substr(0, slash);
}
} // namespace

std::string CoffeeProfileStore::defaultPath()
{
	const char* xdg = std::getenv("XDG_CONFIG_HOME");
	std::string base;
	if (xdg && (xdg[0] != '\0'))
		base = xdg;
	else
	{
		const char* home = std::getenv("HOME");
		if (!home || (home[0] == '\0'))
			return "";
		base = std::string(home) + "/.config";
	}
	return base + "/coffeerdp/profiles.ini";
}

bool CoffeeProfileStore::validate(const CoffeeProfile& profile, std::string& error)
{
	const auto name = trim(profile.name);
	if (name.empty())
	{
		error = "Name cannot be empty.";
		return false;
	}
	/* ']' would terminate the INI section header early and newlines would
	 * split it across lines -- either way the file wouldn't read back as
	 * written. Rejecting here beats silently mangling the name on save. */
	if (name.find(']') != std::string::npos)
	{
		error = "Name cannot contain ']'.";
		return false;
	}
	if (name.find('\n') != std::string::npos || name.find('\r') != std::string::npos)
	{
		error = "Name cannot contain line breaks.";
		return false;
	}

	/* A profile needs somewhere to connect: either a host, or a .rdp file
	 * that carries one. */
	if (trim(profile.host).empty() && trim(profile.rdpFile).empty())
	{
		error = "Either a host or a .rdp file is required.";
		return false;
	}

	if ((profile.port == 0) || (profile.port > 65535))
	{
		error = "Port must be between 1 and 65535.";
		return false;
	}

	for (const auto* field : { &profile.host, &profile.username, &profile.domain, &profile.quality,
		                       &profile.idleKeepAliveCombo, &profile.rdpFile })
	{
		if (field->find('\n') != std::string::npos || field->find('\r') != std::string::npos)
		{
			error = "Fields cannot contain line breaks.";
			return false;
		}
	}

	return true;
}

bool CoffeeProfileStore::load(const std::string& path)
{
	_profiles.clear();

	std::ifstream in(path);
	if (!in.is_open())
	{
		/* Missing file is the normal first-run state, not an error. Only a
		 * file that exists but won't open counts as a failure. */
		struct stat st{};
		if (stat(path.c_str(), &st) != 0)
			return true;
		return false;
	}

	CoffeeProfile current;
	bool haveCurrent = false;

	auto flush = [&]()
	{
		if (haveCurrent)
			_profiles.push_back(current);
		current = CoffeeProfile{};
		haveCurrent = false;
	};

	std::string line;
	while (std::getline(in, line))
	{
		auto trimmed = trim(line);
		if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';')
			continue;

		if (trimmed.front() == '[')
		{
			const auto close = trimmed.find(']');
			if (close == std::string::npos)
				continue; // malformed section header, skip rather than abort
			flush();
			current.name = trim(trimmed.substr(1, close - 1));
			haveCurrent = !current.name.empty();
			continue;
		}

		if (!haveCurrent)
			continue; // key outside any section

		const auto eq = trimmed.find('=');
		if (eq == std::string::npos)
			continue; // not a key=value line

		const auto key = trim(trimmed.substr(0, eq));
		const auto value = trim(trimmed.substr(eq + 1));

		if (key == "host")
			current.host = value;
		else if (key == "port")
			current.port = parseUnsigned(value, 3389);
		else if (key == "username")
			current.username = value;
		else if (key == "domain")
			current.domain = value;
		else if (key == "aad_auth")
			current.aadAuth = parseBool(value, false);
		else if (key == "disable_shortcuts")
			current.disableShortcuts = parseBool(value, false);
		else if (key == "quality")
			current.quality = value;
		else if (key == "multimon")
			current.multimon = parseBool(value, false);
		else if (key == "fullscreen")
			current.fullscreen = parseBool(value, true);
		else if (key == "idle_keepalive_seconds")
			current.idleKeepAliveSeconds = parseUnsigned(value, 30);
		else if (key == "idle_keepalive_combo")
			current.idleKeepAliveCombo = value;
		else if (key == "rdp_file")
			current.rdpFile = value;
		else if (!key.empty())
			current.unknownKeys[key] = value; // preserved on save, see header
	}
	flush();

	return true;
}

bool CoffeeProfileStore::save(const std::string& path) const
{
	const auto dir = parentDir(path);
	if (!dir.empty() && !makeDirs(dir))
		return false;

	/* Write to a temp file and rename: rename(2) is atomic within a
	 * filesystem, so an interrupted or failed write can't leave a truncated
	 * profiles.ini behind where a complete one used to be. */
	const std::string tmp = path + ".tmp";
	{
		std::ofstream out(tmp, std::ios::trunc);
		if (!out.is_open())
			return false;

		out << "# CoffeeRDP connection profiles\n";
		out << "# Written by coffeerdp -- see PLAN.md Phase 7.\n";

		for (const auto& p : _profiles)
		{
			out << "\n[" << p.name << "]\n";
			out << "host = " << p.host << "\n";
			out << "port = " << p.port << "\n";
			out << "username = " << p.username << "\n";
			out << "domain = " << p.domain << "\n";
			out << "aad_auth = " << boolToString(p.aadAuth) << "\n";
			out << "disable_shortcuts = " << boolToString(p.disableShortcuts) << "\n";
			out << "quality = " << p.quality << "\n";
			out << "multimon = " << boolToString(p.multimon) << "\n";
			out << "fullscreen = " << boolToString(p.fullscreen) << "\n";
			out << "idle_keepalive_seconds = " << p.idleKeepAliveSeconds << "\n";
			out << "idle_keepalive_combo = " << p.idleKeepAliveCombo << "\n";
			out << "rdp_file = " << p.rdpFile << "\n";
			for (const auto& kv : p.unknownKeys)
				out << kv.first << " = " << kv.second << "\n";
		}

		out.flush();
		if (!out.good())
		{
			out.close();
			std::remove(tmp.c_str());
			return false;
		}
	}

	/* 0600 before the rename, so the file is never briefly world-readable
	 * at its final path. */
	if (chmod(tmp.c_str(), 0600) != 0)
	{
		std::remove(tmp.c_str());
		return false;
	}

	if (std::rename(tmp.c_str(), path.c_str()) != 0)
	{
		std::remove(tmp.c_str());
		return false;
	}

	return true;
}

const CoffeeProfile* CoffeeProfileStore::find(const std::string& name) const
{
	for (const auto& p : _profiles)
	{
		if (p.name == name)
			return &p;
	}
	return nullptr;
}

bool CoffeeProfileStore::add(const CoffeeProfile& profile, std::string& error)
{
	auto copy = profile;
	copy.name = trim(copy.name);

	if (!validate(copy, error))
		return false;

	if (find(copy.name))
	{
		error = "A profile named \"" + copy.name + "\" already exists.";
		return false;
	}

	_profiles.push_back(copy);
	return true;
}

bool CoffeeProfileStore::update(const std::string& originalName, const CoffeeProfile& profile,
                                std::string& error)
{
	auto copy = profile;
	copy.name = trim(copy.name);

	if (!validate(copy, error))
		return false;

	auto it = std::find_if(_profiles.begin(), _profiles.end(),
	                       [&](const CoffeeProfile& p) { return p.name == originalName; });
	if (it == _profiles.end())
	{
		error = "No profile named \"" + originalName + "\".";
		return false;
	}

	/* A rename is fine, but not onto a name some *other* profile holds. */
	if ((copy.name != originalName) && find(copy.name))
	{
		error = "A profile named \"" + copy.name + "\" already exists.";
		return false;
	}

	*it = copy;
	return true;
}

bool CoffeeProfileStore::remove(const std::string& name)
{
	auto it = std::find_if(_profiles.begin(), _profiles.end(),
	                       [&](const CoffeeProfile& p) { return p.name == name; });
	if (it == _profiles.end())
		return false;
	_profiles.erase(it);
	return true;
}

std::vector<std::string> CoffeeProfileStore::sessionArgs(const CoffeeProfile& profile)
{
	std::vector<std::string> args;

	/* An imported .rdp carries its own settings (Phase 7.2); passing the
	 * file directly is exactly how the session client is already run today,
	 * so it wins over the individual fields when set. */
	if (!trim(profile.rdpFile).empty())
		args.push_back(profile.rdpFile);
	else
	{
		std::string v = "/v:" + profile.host;
		if (profile.port != 3389)
			v += ":" + std::to_string(profile.port);
		args.push_back(v);

		if (!trim(profile.username).empty())
			args.push_back("/u:" + profile.username);
		if (!trim(profile.domain).empty())
			args.push_back("/d:" + profile.domain);

		/* Entra ID: the CLI equivalent of `enablerdsaadauth:i:1`. FreeRDP's
		 * nego starts at NEGO_STATE_AAD whenever AadSecurity is set, so this
		 * is what makes the session request PROTOCOL_RDSAAD (and pop the
		 * coffee-rdp-auth login) instead of going straight to NLA.
		 *
		 * The list form is deliberate, and the comma matters. A bare
		 * `/sec:aad` is parsed by FreeRDP as "this protocol *only*" and turns
		 * tls/nla/rdp off (cmdline.c, the singleOptionWithoutOnOff branch),
		 * which is stricter than what the .rdp key means -- file.c sets
		 * AadSecurity and leaves the rest at their defaults. Naming the
		 * fallbacks keeps the two paths behaving identically: AAD is tried
		 * first, and a server that rejects RDSAAD still falls back rather than
		 * failing outright. `/sec:aad:on` is not an option -- FreeRDP matches
		 * "aad" with option_equals(), not option_starts_with(), so the :on
		 * suffix is rejected as an unknown protocol. */
		if (profile.aadAuth)
			args.push_back("/sec:aad,tls,nla");
		if (profile.multimon)
			args.push_back("/multimon");
		else if (profile.fullscreen)
			args.push_back("/f");
	}

	if (!trim(profile.quality).empty())
		args.push_back("/quality:" + profile.quality);

	/* Always passed explicitly, including 0 (which disables it): the
	 * session's own default is 30s, so omitting this for a profile that
	 * deliberately set 0 would silently re-enable keep-alive. */
	args.push_back("/idle-keypress-time:" + std::to_string(profile.idleKeepAliveSeconds));
	if (!trim(profile.idleKeepAliveCombo).empty())
		args.push_back("/idle-keypress-combo:" + profile.idleKeepAliveCombo);

	/* Presence-only, like /multimon -- there's no "explicitly re-enable"
	 * form because the session's own out-of-the-box default is already
	 * enabled, so omitting the flag already means that. A dropdown toggle
	 * in the floatbar can still flip this mid-session (sdl_input.cpp); this
	 * only sets where the session starts. */
	if (profile.disableShortcuts)
		args.push_back("/disable-shortcuts");

	return args;
}

/**
 * Unit test for coffee_rdp_document (PLAN.md Phase 7, steps 7.1/7.2):
 * editing a .rdp file in place without destroying what CoffeeRDP doesn't
 * model. Assert-based, matching the other test_*.cpp in this project.
 *
 * The fixture is deliberately the real cw-nbr-one-dev.rdp's contents:
 * `enablerdsaadauth:i:1` surviving a write-back is the single most
 * important property here -- lose it and AAD auth silently breaks.
 */
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "../coffee_rdp_document.hpp"

namespace
{
int failures = 0;

void expect(bool cond, const char* what)
{
	if (!cond)
	{
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

std::string tempDir()
{
	char tmpl[] = "/tmp/coffeerdp-rdpdoc-test-XXXXXX";
	const char* dir = mkdtemp(tmpl);
	if (!dir)
	{
		std::fprintf(stderr, "FAIL: mkdtemp failed\n");
		failures++;
		return "";
	}
	return dir;
}

void writeFile(const std::string& path, const std::string& contents)
{
	std::ofstream out(path, std::ios::binary);
	out << contents;
}

std::string readFile(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

bool containsLine(const std::string& contents, const std::string& line)
{
	std::stringstream ss(contents);
	std::string cur;
	while (std::getline(ss, cur))
	{
		if (!cur.empty() && cur.back() == '\r')
			cur.pop_back();
		if (cur == line)
			return true;
	}
	return false;
}

/* Verbatim contents of the real ~/Documents/cw-nbr-one-dev.rdp. */
const char* kRealFile = "full address:s:cw-nbr-one-dev\n"
                        "username:s:etemplin@crestwood.com\n"
                        "enablerdsaadauth:i:1\n"
                        "screen mode id:i:2\n"
                        "use multimon:i:1\n";

void check_parse_real_file()
{
	const auto dir = tempDir();
	if (dir.empty())
		return;
	const auto path = dir + "/test.rdp";
	writeFile(path, kRealFile);

	CoffeeRdpDocument doc;
	expect(doc.load(path), "load() reads the real-world file");
	expect(doc.lineCount() == 5, "all five lines retained");

	CoffeeProfile p;
	coffee_rdp_document_to_profile(doc, p);
	expect(p.host == "cw-nbr-one-dev", "host parsed from 'full address'");
	expect(p.port == 3389, "port defaults to 3389 when the file doesn't specify one");
	expect(p.username == "etemplin@crestwood.com", "username parsed");
	expect(p.multimon, "'use multimon:i:1' parsed as true");
	expect(p.fullscreen, "'screen mode id:i:2' parsed as fullscreen");
	expect(p.aadAuth, "'enablerdsaadauth:i:1' reaches the profile -- an imported profile is "
	                  "unlinked from the file, so this flag is the only thing that makes the "
	                  "session ask for Entra sign-in instead of NLA");

	std::remove(path.c_str());
	rmdir(dir.c_str());
}

void check_unknown_keys_survive_write_back()
{
	const auto dir = tempDir();
	if (dir.empty())
		return;
	const auto path = dir + "/test.rdp";
	writeFile(path, kRealFile);

	CoffeeRdpDocument doc;
	expect(doc.load(path), "precondition: loaded");

	CoffeeProfile p;
	coffee_rdp_document_to_profile(doc, p);
	p.host = "new-host";
	p.quality = "best";
	coffee_rdp_document_from_profile(p, doc);
	expect(doc.save(path), "save() succeeds");

	const auto written = readFile(path);
	expect(containsLine(written, "enablerdsaadauth:i:1"),
	       "enablerdsaadauth SURVIVES the write-back -- losing it would silently break AAD auth");
	expect(containsLine(written, "full address:s:new-host"), "edited host written");
	expect(containsLine(written, "quality:s:best"), "new CoffeeRDP key appended");

	// And it still reads back cleanly.
	CoffeeRdpDocument reloaded;
	expect(reloaded.load(path), "reload after save succeeds");
	expect(reloaded.getInt("enablerdsaadauth").value_or(-1) == 1,
	       "enablerdsaadauth still parses as 1 after the round-trip");

	std::remove(path.c_str());
	rmdir(dir.c_str());
}

void check_comments_and_blanks_preserved()
{
	const auto dir = tempDir();
	if (dir.empty())
		return;
	const auto path = dir + "/test.rdp";
	writeFile(path, "# my notes about this connection\n"
	                "\n"
	                "full address:s:host-a\n"
	                "some future key:s:whatever\n");

	CoffeeRdpDocument doc;
	expect(doc.load(path), "precondition: loaded");

	CoffeeProfile p;
	coffee_rdp_document_to_profile(doc, p);
	p.host = "host-b";
	coffee_rdp_document_from_profile(p, doc);
	expect(doc.save(path), "save() succeeds");

	const auto written = readFile(path);
	expect(containsLine(written, "# my notes about this connection"), "comment preserved");
	expect(containsLine(written, "some future key:s:whatever"), "unknown key preserved verbatim");
	expect(containsLine(written, "full address:s:host-b"), "edit applied");
	expect(written.find("\n\n") != std::string::npos, "blank line preserved");

	std::remove(path.c_str());
	rmdir(dir.c_str());
}

void check_key_position_and_spelling_preserved()
{
	const auto dir = tempDir();
	if (dir.empty())
		return;
	const auto path = dir + "/test.rdp";
	// Mixed-case key name, and a key after it we must not reorder past.
	writeFile(path, "Full Address:s:host-a\n"
	                "zzz last key:s:sentinel\n");

	CoffeeRdpDocument doc;
	expect(doc.load(path), "precondition: loaded");
	expect(doc.getString("full address").value_or("") == "host-a",
	       "key lookup is case-insensitive, matching FreeRDP");

	doc.setString("full address", "host-b");
	expect(doc.save(path), "save() succeeds");

	const auto written = readFile(path);
	expect(containsLine(written, "Full Address:s:host-b"),
	       "original key spelling preserved on rewrite, not normalized");

	std::stringstream ss(written);
	std::string first;
	std::getline(ss, first);
	expect(first.find("Full Address") == 0, "edited key kept its original position in the file");

	std::remove(path.c_str());
	rmdir(dir.c_str());
}

void check_port_handling()
{
	CoffeeRdpDocument doc;

	// Port embedded in the address.
	doc.setString("full address", "some-host:3390");
	CoffeeProfile p;
	coffee_rdp_document_to_profile(doc, p);
	expect(p.host == "some-host", "host split from an embedded port");
	expect(p.port == 3390, "embedded port parsed");

	// An explicit server port key wins.
	doc.setInt("server port", 3391);
	CoffeeProfile p2;
	coffee_rdp_document_to_profile(doc, p2);
	expect(p2.port == 3391, "explicit 'server port' takes precedence over the embedded port");

	// A default port shouldn't introduce the key when it wasn't there.
	CoffeeRdpDocument fresh;
	CoffeeProfile p3;
	p3.host = "h";
	p3.port = 3389;
	coffee_rdp_document_from_profile(p3, fresh);
	expect(!fresh.has("server port"),
	       "default port does not add a 'server port' key to a file that lacked it");

	// But an existing key is kept in sync rather than left stale.
	CoffeeRdpDocument had;
	had.setInt("server port", 9999);
	coffee_rdp_document_from_profile(p3, had);
	expect(had.getInt("server port").value_or(-1) == 3389,
	       "an existing 'server port' key is updated, not left stale");
}

void check_aad_flag_round_trips()
{
	// Off by default, and not invented for a file that never mentioned it.
	CoffeeRdpDocument fresh;
	CoffeeProfile p;
	p.host = "h";
	coffee_rdp_document_from_profile(p, fresh);
	expect(!fresh.has("enablerdsaadauth"),
	       "a non-AAD profile does not add enablerdsaadauth to a file that lacked it");

	p.aadAuth = true;
	coffee_rdp_document_from_profile(p, fresh);
	expect(fresh.getInt("enablerdsaadauth").value_or(-1) == 1,
	       "turning Entra ID on writes enablerdsaadauth:i:1");

	/* Turning it off must write an explicit 0, not just stop writing 1:
	 * leaving the file's old `:i:1` in place would let the file re-enable AAD
	 * behind the user's back on the next launch. */
	p.aadAuth = false;
	coffee_rdp_document_from_profile(p, fresh);
	expect(fresh.getInt("enablerdsaadauth").value_or(-1) == 0,
	       "turning Entra ID off writes an explicit 0 over the file's 1");

	// `:i:0` in a file must read back as off, not merely "key present".
	CoffeeRdpDocument disabled;
	disabled.setInt("enablerdsaadauth", 0);
	CoffeeProfile readBack;
	readBack.aadAuth = true;
	coffee_rdp_document_to_profile(disabled, readBack);
	expect(!readBack.aadAuth, "enablerdsaadauth:i:0 parses as off");
}

void check_disable_shortcuts_round_trips()
{
	// Off by default, and not invented for a file that never mentioned it.
	CoffeeRdpDocument fresh;
	CoffeeProfile p;
	p.host = "h";
	coffee_rdp_document_from_profile(p, fresh);
	expect(!fresh.has("disable shortcuts"),
	       "a non-disabling profile does not add 'disable shortcuts' to a file that lacked it");

	p.disableShortcuts = true;
	coffee_rdp_document_from_profile(p, fresh);
	expect(fresh.getString("disable shortcuts").value_or("") == "1",
	       "disabling shortcuts writes 'disable shortcuts:s:1'");

	/* Same reasoning as the AAD flag: turning it off must write an explicit
	 * 0, not just stop writing 1, or the file's stale 1 would silently keep
	 * disabling shortcuts on the next launch. Type is `s`, not `i` -- this
	 * is a CoffeeRDP-custom key the session reads via
	 * coffee_rdp_file_scan_key(), which only matches type 's' lines. */
	p.disableShortcuts = false;
	coffee_rdp_document_from_profile(p, fresh);
	expect(fresh.getString("disable shortcuts").value_or("") == "0",
	       "re-enabling shortcuts writes an explicit 0 over the file's 1");

	// A bare "0" in a file must read back as off, not merely "key present".
	CoffeeRdpDocument disabled;
	disabled.setString("disable shortcuts", "0");
	CoffeeProfile readBack;
	readBack.disableShortcuts = true;
	coffee_rdp_document_to_profile(disabled, readBack);
	expect(!readBack.disableShortcuts, "'disable shortcuts:s:0' parses as off");
}

void check_ignore_certificate_errors_round_trips()
{
	// Off by default, and not invented for a file that never mentioned it.
	CoffeeRdpDocument fresh;
	CoffeeProfile p;
	p.host = "h";
	coffee_rdp_document_from_profile(p, fresh);
	expect(!fresh.has("ignore certificate errors"),
	       "a non-ignoring profile does not add 'ignore certificate errors' to a file that "
	       "lacked it");

	p.ignoreCertificateErrors = true;
	coffee_rdp_document_from_profile(p, fresh);
	expect(fresh.getString("ignore certificate errors").value_or("") == "1",
	       "ignoring certificate errors writes 'ignore certificate errors:s:1'");

	/* Same reasoning as the other CoffeeRDP-custom flags: turning it off
	 * must write an explicit 0, not just stop writing 1, or the file's
	 * stale 1 would silently keep skipping certificate checks -- the
	 * MITM-detection tradeoff this field makes should never outlive the
	 * user turning it back off. */
	p.ignoreCertificateErrors = false;
	coffee_rdp_document_from_profile(p, fresh);
	expect(fresh.getString("ignore certificate errors").value_or("") == "0",
	       "re-enabling certificate checks writes an explicit 0 over the file's 1");

	// A bare "0" in a file must read back as off, not merely "key present".
	CoffeeRdpDocument disabled;
	disabled.setString("ignore certificate errors", "0");
	CoffeeProfile readBack;
	readBack.ignoreCertificateErrors = true;
	coffee_rdp_document_to_profile(disabled, readBack);
	expect(!readBack.ignoreCertificateErrors, "'ignore certificate errors:s:0' parses as off");
}

void check_empty_values_remove_keys()
{
	CoffeeRdpDocument doc;
	doc.setString("username", "someone");
	doc.setString("domain", "somedomain");

	CoffeeProfile p;
	p.host = "h";
	p.username = "";
	p.domain = "";
	coffee_rdp_document_from_profile(p, doc);

	expect(!doc.has("username"),
	       "clearing username removes the key rather than writing an empty one");
	expect(!doc.has("domain"), "clearing domain removes the key");
}

void check_op_reference_round_trips()
{
	// Not invented for a file that never had one.
	CoffeeRdpDocument fresh;
	CoffeeProfile p;
	p.host = "h";
	coffee_rdp_document_from_profile(p, fresh);
	expect(!fresh.has("op reference"),
	       "a profile with no 1Password reference does not add 'op reference' to a file that "
	       "lacked it");

	p.onePasswordRef = "op://Employee/Crestwood/password";
	coffee_rdp_document_from_profile(p, fresh);
	expect(fresh.getString("op reference").value_or("") == "op://Employee/Crestwood/password",
	       "setting a reference writes 'op reference:s:op://...'");

	CoffeeProfile readBack;
	coffee_rdp_document_to_profile(fresh, readBack);
	expect(readBack.onePasswordRef == "op://Employee/Crestwood/password",
	       "op reference reads back from the file -- this is what keeps the editor from "
	       "blanking a linked profile's reference on reopen (onRdpPathChanged reloads the "
	       "whole form from the file)");

	// Clearing it removes the key, same as username/domain.
	p.onePasswordRef = "";
	coffee_rdp_document_from_profile(p, fresh);
	expect(!fresh.has("op reference"), "clearing the reference removes the key");
}

void check_coffeerdp_keys_use_string_type()
{
	CoffeeRdpDocument doc;
	CoffeeProfile p;
	p.host = "h";
	p.quality = "speed";
	p.idleKeepAliveSeconds = 45;
	p.idleKeepAliveCombo = "win+tab";
	coffee_rdp_document_from_profile(p, doc);

	const auto dir = tempDir();
	if (dir.empty())
		return;
	const auto path = dir + "/test.rdp";
	expect(doc.save(path), "save() succeeds");

	const auto written = readFile(path);
	/* Type must be 's': the session reads these via
	 * coffee_rdp_file_scan_key(), which ignores any other type. An 'i' here
	 * would parse fine as .rdp but the setting would silently not apply. */
	expect(containsLine(written, "quality:s:speed"), "quality written with type 's'");
	expect(containsLine(written, "idle keypress time:s:45"),
	       "idle keypress time written with type 's', not 'i' -- the session only reads 's'");
	expect(containsLine(written, "idle keypress combo:s:win+tab"),
	       "idle keypress combo written with type 's'");

	std::remove(path.c_str());
	rmdir(dir.c_str());
}

void check_keepalive_zero_is_written()
{
	CoffeeRdpDocument doc;
	CoffeeProfile p;
	p.host = "h";
	p.idleKeepAliveSeconds = 0;
	coffee_rdp_document_from_profile(p, doc);

	expect(doc.getString("idle keypress time").value_or("") == "0",
	       "keep-alive 0 is written explicitly -- omitting it would let the session's own "
	       "30s default silently re-enable it");
}

void check_round_trip_through_profile()
{
	CoffeeRdpDocument doc;
	CoffeeProfile out;
	out.host = "round-trip-host";
	out.port = 3390;
	out.username = "user";
	out.domain = "dom";
	out.quality = "quality";
	out.multimon = false;
	out.fullscreen = false;
	out.idleKeepAliveSeconds = 15;
	out.idleKeepAliveCombo = "ctrl+alt+del";
	coffee_rdp_document_from_profile(out, doc);

	CoffeeProfile back;
	coffee_rdp_document_to_profile(doc, back);

	expect(back.host == out.host, "host round-trips");
	expect(back.port == out.port, "port round-trips");
	expect(back.username == out.username, "username round-trips");
	expect(back.domain == out.domain, "domain round-trips");
	expect(back.quality == out.quality, "quality round-trips");
	expect(back.multimon == out.multimon, "multimon=false round-trips");
	expect(back.fullscreen == out.fullscreen, "fullscreen=false round-trips");
	expect(back.idleKeepAliveSeconds == out.idleKeepAliveSeconds, "idle seconds round-trip");
	expect(back.idleKeepAliveCombo == out.idleKeepAliveCombo, "idle combo round-trips");
}

void check_missing_file_then_create()
{
	const auto dir = tempDir();
	if (dir.empty())
		return;
	const auto path = dir + "/brand-new.rdp";

	CoffeeRdpDocument doc;
	expect(doc.load(path), "loading a not-yet-existing .rdp is not an error");
	expect(doc.lineCount() == 0, "missing file yields an empty document");

	CoffeeProfile p;
	p.host = "created-host";
	coffee_rdp_document_from_profile(p, doc);
	expect(doc.save(path), "saving creates the file");
	expect(containsLine(readFile(path), "full address:s:created-host"), "created file has content");

	std::remove(path.c_str());
	rmdir(dir.c_str());
}

void check_crlf_preserved()
{
	const auto dir = tempDir();
	if (dir.empty())
		return;
	const auto path = dir + "/crlf.rdp";
	writeFile(path, "full address:s:host-a\r\nenablerdsaadauth:i:1\r\n");

	CoffeeRdpDocument doc;
	expect(doc.load(path), "precondition: loaded a CRLF file");
	expect(doc.getString("full address").value_or("") == "host-a",
	       "CRLF line endings don't leak into parsed values");

	doc.setString("full address", "host-b");
	expect(doc.save(path), "save() succeeds");

	const auto written = readFile(path);
	expect(written.find("\r\n") != std::string::npos,
	       "CRLF line endings preserved -- a Windows-authored file isn't silently converted");

	std::remove(path.c_str());
	rmdir(dir.c_str());
}

void check_quality_writes_standard_keys()
{
	CoffeeRdpDocument doc;
	CoffeeProfile p;
	p.host = "h";
	p.quality = "speed";
	coffee_rdp_document_from_profile(p, doc);

	/* Stock keys, so mstsc/Remmina/FreeRDP all behave correctly on a file
	 * we wrote -- not just CoffeeRDP's own shorthand. */
	expect(doc.getInt("connection type").value_or(-1) == 2,
	       "speed writes 'connection type:i:2' (BROADBAND_LOW)");
	expect(doc.getInt("disable wallpaper").value_or(-1) == 1, "speed disables wallpaper");
	expect(doc.getInt("disable themes").value_or(-1) == 1, "speed disables themes");
	expect(doc.getInt("disable menu anims").value_or(-1) == 1, "speed disables menu anims");
	expect(doc.getInt("allow font smoothing").value_or(-1) == 0, "speed leaves font smoothing off");

	/* FreeRDP warns and self-corrects when these two disagree, so they must
	 * always be written in agreement -- mstsc only writes the first. */
	expect(doc.getInt("networkautodetect").value_or(-1) == 0,
	       "explicit preset sets networkautodetect:i:0");
	expect(doc.getInt("bandwidthautodetect").value_or(-1) == 0,
	       "explicit preset sets bandwidthautodetect:i:0 -- kept in agreement per FreeRDP");

	// And the hint survives too, for the bit stock keys can't express.
	expect(doc.getString("quality").value_or("") == "speed", "the CoffeeRDP preset hint is kept");

	CoffeeRdpDocument autoDoc;
	CoffeeProfile ap;
	ap.host = "h";
	ap.quality = "auto";
	coffee_rdp_document_from_profile(ap, autoDoc);
	expect(autoDoc.getInt("networkautodetect").value_or(-1) == 1,
	       "auto sets networkautodetect:i:1");
	expect(autoDoc.getInt("bandwidthautodetect").value_or(-1) == 1,
	       "auto sets bandwidthautodetect:i:1, matching FreeRDP's own writer");
}

void check_quality_inferred_from_standard_keys()
{
	// A file from another client: stock keys, no CoffeeRDP hint at all.
	CoffeeRdpDocument doc;
	doc.setString("full address", "h");
	doc.setInt("connection type", 6); // LAN
	doc.setInt("allow font smoothing", 1);

	CoffeeProfile p;
	coffee_rdp_document_to_profile(doc, p);
	expect(p.quality == "best",
	       "a foreign file's stock keys map onto the matching CoffeeRDP preset ('best')");

	// Near-miss must NOT be guessed into a preset.
	CoffeeRdpDocument odd;
	odd.setString("full address", "h");
	odd.setInt("connection type", 6);
	odd.setInt("allow font smoothing", 1);
	odd.setInt("disable wallpaper", 1); // not part of 'best'
	CoffeeProfile p2;
	coffee_rdp_document_to_profile(odd, p2);
	expect(p2.quality.empty(),
	       "a near-miss stays 'Default' rather than being guessed into a preset that would "
	       "silently change settings on the next save");
}

void check_quality_default_preserves_foreign_perf_keys()
{
	// Imported file with performance keys we didn't write.
	CoffeeRdpDocument doc;
	doc.setString("full address", "h");
	doc.setInt("connection type", 3); // SATELLITE -- no CoffeeRDP preset matches
	doc.setInt("disable wallpaper", 1);

	CoffeeProfile p;
	coffee_rdp_document_to_profile(doc, p);
	expect(p.quality.empty(), "precondition: unmatched settings read as Default");

	// Saving with Default selected must not delete them.
	coffee_rdp_document_from_profile(p, doc);
	expect(doc.getInt("connection type").value_or(-1) == 3,
	       "'Default' leaves a foreign connection type alone rather than clobbering it");
	expect(doc.getInt("disable wallpaper").value_or(-1) == 1,
	       "'Default' leaves foreign performance keys alone");
	expect(!doc.has("quality"), "no CoffeeRDP hint is written for Default");
}

/* The migration case: a realistic file such as mstsc or a corporate portal
 * would produce, full of features CoffeeRDP has no model for. Every one of
 * them must survive a load/edit/save cycle untouched -- that's what makes
 * "point CoffeeRDP at an existing .rdp" safe, and what keeps planned-but-
 * unbuilt features (gateway, redirection, audio, reconnect, smart sizing)
 * intact until we actually implement them. */
void check_realistic_foreign_file_fully_preserved()
{
	const auto dir = tempDir();
	if (dir.empty())
		return;
	const auto path = dir + "/foreign.rdp";

	const char* foreign =
	    "screen mode id:i:2\n"
	    "use multimon:i:1\n"
	    "desktopwidth:i:1920\n"
	    "desktopheight:i:1080\n"
	    "session bpp:i:32\n"
	    "full address:s:some-host\n"
	    "username:s:someone\n"
	    "enablerdsaadauth:i:1\n"
	    "gatewayhostname:s:gw.example.com\n"
	    "gatewayusagemethod:i:1\n"
	    "gatewaycredentialssource:i:4\n"
	    "gatewayprofileusagemethod:i:1\n"
	    "promptcredentialonce:i:1\n"
	    "authentication level:i:2\n"
	    "audiomode:i:0\n"
	    "audiocapturemode:i:1\n"
	    "redirectclipboard:i:1\n"
	    "redirectprinters:i:0\n"
	    "redirectsmartcards:i:1\n"
	    "drivestoredirect:s:*\n"
	    "devicestoredirect:s:*\n"
	    "autoreconnection enabled:i:1\n"
	    "smart sizing:i:1\n"
	    "dynamic resolution:i:1\n"
	    "selectedmonitors:s:0,1\n"
	    "keyboardhook:i:2\n"
	    "administrative session:i:0\n"
	    "alternate shell:s:\n"
	    "shell working directory:s:\n";

	writeFile(path, foreign);

	CoffeeRdpDocument doc;
	expect(doc.load(path), "precondition: foreign file loads");

	CoffeeProfile p;
	coffee_rdp_document_to_profile(doc, p);
	expect(p.host == "some-host", "known key still parsed out of a foreign file");
	expect(p.multimon, "known key parsed");

	// Make an edit and save, exactly as the GUI would.
	p.quality = "balanced";
	coffee_rdp_document_from_profile(p, doc);
	expect(doc.save(path), "save() succeeds");

	const auto written = readFile(path);

	// Every foreign key must still be present, value intact.
	const char* mustSurvive[] = {
		"enablerdsaadauth:i:1",         "gatewayhostname:s:gw.example.com",
		"gatewayusagemethod:i:1",       "gatewaycredentialssource:i:4",
		"gatewayprofileusagemethod:i:1", "promptcredentialonce:i:1",
		"authentication level:i:2",     "audiomode:i:0",
		"audiocapturemode:i:1",         "redirectclipboard:i:1",
		"redirectprinters:i:0",         "redirectsmartcards:i:1",
		"drivestoredirect:s:*",         "devicestoredirect:s:*",
		"autoreconnection enabled:i:1", "smart sizing:i:1",
		"dynamic resolution:i:1",       "selectedmonitors:s:0,1",
		"keyboardhook:i:2",             "administrative session:i:0",
		"desktopwidth:i:1920",          "desktopheight:i:1080",
		"session bpp:i:32",             "alternate shell:s:",
		"shell working directory:s:",
	};
	for (const char* line : mustSurvive)
	{
		if (!containsLine(written, line))
		{
			std::fprintf(stderr, "FAIL: foreign key lost on round-trip: %s\n", line);
			failures++;
		}
	}

	// And the edit actually applied.
	expect(containsLine(written, "quality:s:balanced"), "the edit was applied");
	expect(doc.getInt("connection type").value_or(-1) == 4,
	       "standard keys for the chosen preset were written alongside it");

	std::remove(path.c_str());
	rmdir(dir.c_str());
}

void check_save_rejects_missing_directory()
{
	CoffeeRdpDocument doc;
	doc.setString("full address", "h");
	expect(!doc.save("/tmp/coffeerdp-no-such-dir-98765/sub/test.rdp"),
	       "saving into a non-existent directory fails rather than silently inventing it");
}
} // namespace

int main()
{
	check_parse_real_file();
	check_unknown_keys_survive_write_back();
	check_comments_and_blanks_preserved();
	check_key_position_and_spelling_preserved();
	check_port_handling();
	check_aad_flag_round_trips();
	check_disable_shortcuts_round_trips();
	check_ignore_certificate_errors_round_trips();
	check_empty_values_remove_keys();
	check_op_reference_round_trips();
	check_coffeerdp_keys_use_string_type();
	check_keepalive_zero_is_written();
	check_round_trip_through_profile();
	check_missing_file_then_create();
	check_crlf_preserved();
	check_quality_writes_standard_keys();
	check_quality_inferred_from_standard_keys();
	check_quality_default_preserves_foreign_perf_keys();
	check_realistic_foreign_file_fully_preserved();
	check_save_rejects_missing_directory();

	if (failures == 0)
		std::printf("OK: all coffee_rdp_document checks passed\n");
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

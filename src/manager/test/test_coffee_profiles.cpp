/**
 * Unit test for coffee_profiles (PLAN.md Phase 7, step 7.1): "round-trips a
 * profile", plus the validation/CLI-arg logic the UI depends on.
 * Assert-based, matching test_coffee_floatbar.cpp's style.
 *
 * Uses a temp directory so it never touches the real
 * ~/.config/coffeerdp/profiles.ini.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "../coffee_profiles.hpp"

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
	char tmpl[] = "/tmp/coffeerdp-profiles-test-XXXXXX";
	const char* dir = mkdtemp(tmpl);
	if (!dir)
	{
		std::fprintf(stderr, "FAIL: mkdtemp failed\n");
		failures++;
		return "";
	}
	return dir;
}

CoffeeProfile sampleProfile()
{
	CoffeeProfile p;
	p.name = "CW NBR One Dev";
	p.host = "cw-nbr-one-dev";
	p.port = 3389;
	p.username = "etemplin";
	p.domain = "crestwood";
	p.quality = "balanced";
	p.aadAuth = true;
	p.disableShortcuts = true;
	p.ignoreCertificateErrors = true;
	p.multimon = true;
	p.fullscreen = true;
	p.idleKeepAliveSeconds = 30;
	p.idleKeepAliveCombo = "alt+tab";
	p.onePasswordRef = "op://Private/CW NBR One Dev/password";
	return p;
}

void check_add_and_find()
{
	CoffeeProfileStore store;
	std::string err;

	expect(store.profiles().empty(), "a fresh store is empty");
	expect(store.add(sampleProfile(), err), "adding a valid profile succeeds");
	expect(store.profiles().size() == 1, "store holds one profile after add");

	const auto* found = store.find("CW NBR One Dev");
	expect(found != nullptr, "find() locates the profile by name");
	if (found)
		expect(found->host == "cw-nbr-one-dev", "found profile carries its host");

	expect(store.find("nope") == nullptr, "find() returns nullptr for an unknown name");

	// Duplicate names rejected.
	err.clear();
	expect(!store.add(sampleProfile(), err), "adding a duplicate name fails");
	expect(!err.empty(), "duplicate-name failure sets a human-readable error");
	expect(store.profiles().size() == 1, "a rejected add doesn't modify the store");
}

void check_validation()
{
	std::string err;
	CoffeeProfile p = sampleProfile();

	p.name = "";
	expect(!CoffeeProfileStore::validate(p, err), "empty name rejected");

	p = sampleProfile();
	p.name = "bad]name";
	expect(!CoffeeProfileStore::validate(p, err),
	       "']' in name rejected -- it would break the INI section header");

	p = sampleProfile();
	p.name = "bad\nname";
	expect(!CoffeeProfileStore::validate(p, err), "newline in name rejected");

	p = sampleProfile();
	p.host = "";
	p.rdpFile = "";
	expect(!CoffeeProfileStore::validate(p, err), "a profile with neither host nor .rdp rejected");

	p = sampleProfile();
	p.host = "";
	p.rdpFile = "/home/evan/Documents/cw-nbr-one-dev.rdp";
	expect(CoffeeProfileStore::validate(p, err), "a .rdp file alone satisfies the host requirement");

	p = sampleProfile();
	p.port = 0;
	expect(!CoffeeProfileStore::validate(p, err), "port 0 rejected");
	p.port = 70000;
	expect(!CoffeeProfileStore::validate(p, err), "port above 65535 rejected");

	p = sampleProfile();
	p.username = "bad\nuser";
	expect(!CoffeeProfileStore::validate(p, err), "newline in a value field rejected");

	p = sampleProfile();
	p.onePasswordRef = "bad\nref";
	expect(!CoffeeProfileStore::validate(p, err), "newline in the 1Password reference rejected");
}

void check_normalize_one_password_ref()
{
	expect(CoffeeProfileStore::normalizeOnePasswordRef("op://Employee/Crestwood/password") ==
	           "op://Employee/Crestwood/password",
	       "an unquoted reference is left untouched");
	expect(CoffeeProfileStore::normalizeOnePasswordRef("\"op://Employee/Crestwood/password\"") ==
	           "op://Employee/Crestwood/password",
	       "1Password's own double-quoted 'Copy Secret Reference' output is unwrapped");
	expect(CoffeeProfileStore::normalizeOnePasswordRef("'op://Employee/Crestwood/password'") ==
	           "op://Employee/Crestwood/password",
	       "single-quote wrapping is unwrapped too");
	expect(CoffeeProfileStore::normalizeOnePasswordRef("  \"op://x/y/z\"  ") == "op://x/y/z",
	       "surrounding whitespace from a paste is trimmed along with the quotes");
	expect(CoffeeProfileStore::normalizeOnePasswordRef("") == "", "empty stays empty");
	expect(CoffeeProfileStore::normalizeOnePasswordRef("\"unterminated") == "\"unterminated",
	       "a lone quote on one side only is left alone rather than mangled");
}

void check_round_trip()
{
	const auto dir = tempDir();
	if (dir.empty())
		return;
	const auto path = dir + "/profiles.ini";

	CoffeeProfileStore store;
	std::string err;
	auto p = sampleProfile();
	p.multimon = true;
	p.fullscreen = false;
	p.idleKeepAliveSeconds = 0;
	p.idleKeepAliveCombo = "win+tab";
	expect(store.add(p, err), "precondition: profile added");

	CoffeeProfile second;
	second.name = "Second";
	second.host = "other-host";
	second.port = 3390;
	expect(store.add(second, err), "precondition: second profile added");

	expect(store.save(path), "save() succeeds");

	CoffeeProfileStore reloaded;
	expect(reloaded.load(path), "load() succeeds");
	expect(reloaded.profiles().size() == 2, "both profiles survive the round-trip");

	const auto* back = reloaded.find("CW NBR One Dev");
	expect(back != nullptr, "first profile found after reload");
	if (back)
	{
		expect(back->host == "cw-nbr-one-dev", "host round-trips");
		expect(back->port == 3389, "port round-trips");
		expect(back->username == "etemplin", "username round-trips");
		expect(back->domain == "crestwood", "domain round-trips");
		expect(back->quality == "balanced", "quality round-trips");
		expect(back->aadAuth, "aad_auth=true round-trips -- losing it drops the profile to NLA");
		expect(back->disableShortcuts,
		       "disable_shortcuts=true round-trips -- losing it silently re-enables shortcuts");
		expect(back->ignoreCertificateErrors,
		       "ignore_certificate_errors=true round-trips -- losing it brings back the "
		       "changed-certificate prompt on every connection");
		expect(back->multimon, "multimon=true round-trips");
		expect(!back->fullscreen, "fullscreen=false round-trips (not silently defaulted to true)");
		expect(back->idleKeepAliveSeconds == 0,
		       "idle keep-alive 0 round-trips -- must not fall back to the 30s default");
		expect(back->idleKeepAliveCombo == "win+tab", "combo round-trips");
		expect(back->onePasswordRef == "op://Private/CW NBR One Dev/password",
		       "1Password reference round-trips");
	}

	const auto* other = reloaded.find("Second");
	expect(other != nullptr, "second profile found after reload");
	if (other)
		expect(other->port == 3390, "non-default port round-trips");

	std::remove(path.c_str());
	rmdir(dir.c_str());
}

void check_missing_file_is_not_an_error()
{
	CoffeeProfileStore store;
	expect(store.load("/tmp/coffeerdp-definitely-does-not-exist-12345/profiles.ini"),
	       "loading a missing file succeeds (normal first-run state)");
	expect(store.profiles().empty(), "missing file yields an empty store");
}

void check_unknown_keys_preserved()
{
	const auto dir = tempDir();
	if (dir.empty())
		return;
	const auto path = dir + "/profiles.ini";

	{
		std::ofstream out(path);
		out << "[Future]\n";
		out << "host = future-host\n";
		out << "some_future_key = some_value\n";
	}

	CoffeeProfileStore store;
	expect(store.load(path), "load() reads a file containing unknown keys");
	const auto* p = store.find("Future");
	expect(p != nullptr, "profile with unknown keys still loads");
	if (p)
		expect(p->unknownKeys.count("some_future_key") == 1, "unknown key retained in memory");

	expect(store.save(path), "save() succeeds");

	CoffeeProfileStore reloaded;
	expect(reloaded.load(path), "reload after save succeeds");
	const auto* back = reloaded.find("Future");
	expect(back != nullptr, "profile still present after save");
	if (back)
	{
		expect(back->host == "future-host", "known key survived");
		expect(back->unknownKeys.count("some_future_key") == 1,
		       "unknown key survived the round-trip -- a newer build's fields aren't dropped");
	}

	std::remove(path.c_str());
	rmdir(dir.c_str());
}

void check_malformed_lines_are_skipped()
{
	const auto dir = tempDir();
	if (dir.empty())
		return;
	const auto path = dir + "/profiles.ini";

	{
		std::ofstream out(path);
		out << "# a comment\n";
		out << "; another comment\n";
		out << "orphan_key = value_outside_any_section\n";
		out << "[Good]\n";
		out << "host = good-host\n";
		out << "this line has no equals sign\n";
		out << "[Unterminated\n";
		out << "[AlsoGood]\n";
		out << "host = also-good\n";
	}

	CoffeeProfileStore store;
	expect(store.load(path), "load() tolerates malformed lines");
	expect(store.find("Good") != nullptr, "well-formed profile before the damage loads");
	expect(store.find("AlsoGood") != nullptr,
	       "well-formed profile after the damage loads -- one bad line costs one profile, "
	       "not the whole file");

	std::remove(path.c_str());
	rmdir(dir.c_str());
}

void check_update_and_remove()
{
	CoffeeProfileStore store;
	std::string err;
	expect(store.add(sampleProfile(), err), "precondition: profile added");

	auto edited = sampleProfile();
	edited.host = "new-host";
	expect(store.update("CW NBR One Dev", edited, err), "update() edits in place");
	const auto* p = store.find("CW NBR One Dev");
	expect(p && p->host == "new-host", "edit took effect");

	// Rename.
	auto renamed = sampleProfile();
	renamed.name = "Renamed";
	expect(store.update("CW NBR One Dev", renamed, err), "update() can rename");
	expect(store.find("Renamed") != nullptr, "profile findable under the new name");
	expect(store.find("CW NBR One Dev") == nullptr, "old name no longer resolves");

	// Renaming onto another existing name must fail.
	CoffeeProfile other;
	other.name = "Other";
	other.host = "other-host";
	expect(store.add(other, err), "precondition: second profile added");
	auto collide = other;
	collide.name = "Renamed";
	err.clear();
	expect(!store.update("Other", collide, err), "renaming onto an existing name fails");
	expect(!err.empty(), "collision sets an error message");
	expect(store.find("Other") != nullptr, "the rejected rename left the original intact");

	expect(!store.update("does-not-exist", sampleProfile(), err), "updating an unknown name fails");

	expect(store.remove("Renamed"), "remove() deletes an existing profile");
	expect(store.find("Renamed") == nullptr, "removed profile is gone");
	expect(!store.remove("Renamed"), "removing a missing profile returns false");
}

bool contains(const std::vector<std::string>& args, const std::string& needle)
{
	for (const auto& a : args)
	{
		if (a == needle)
			return true;
	}
	return false;
}

void check_session_args()
{
	auto p = sampleProfile();
	p.multimon = true;
	auto args = CoffeeProfileStore::sessionArgs(p);

	expect(contains(args, "/v:cw-nbr-one-dev"),
	       "default port is not appended to /v: (matches how the session is invoked today)");
	expect(contains(args, "/u:etemplin"), "username becomes /u:");
	expect(contains(args, "/d:crestwood"), "domain becomes /d:");
	expect(contains(args, "/multimon"), "multimon becomes /multimon");
	expect(contains(args, "/quality:balanced"), "quality becomes /quality:");
	expect(contains(args, "/idle-keypress-time:30"), "idle time becomes /idle-keypress-time:");
	expect(contains(args, "/idle-keypress-combo:alt+tab"), "combo becomes /idle-keypress-combo:");

	/* The reason this field exists: an imported AAD profile that doesn't emit
	 * /sec: gets FreeRDP's default negotiation, which starts at NLA and never
	 * offers Entra sign-in at all. */
	expect(contains(args, "/sec:aad,tls,nla"), "Entra ID becomes /sec:aad, with fallbacks named");

	p.aadAuth = false;
	expect(!contains(CoffeeProfileStore::sessionArgs(p), "/sec:aad,tls,nla"),
	       "no /sec: flag at all when Entra ID is off -- FreeRDP's own default applies");
	p.aadAuth = true;

	expect(contains(args, "/disable-shortcuts"),
	       "disableShortcuts=true becomes /disable-shortcuts");
	p.disableShortcuts = false;
	expect(!contains(CoffeeProfileStore::sessionArgs(p), "/disable-shortcuts"),
	       "no /disable-shortcuts flag when shortcuts are left enabled (the session's own "
	       "out-of-the-box default)");
	p.disableShortcuts = true;

	// A .rdp-linked profile still gets it -- CoffeeRDP-specific flags apply
	// on top of a linked file, same as quality/idle-keepalive.
	{
		auto linked = p;
		linked.rdpFile = "/home/evan/Documents/cw-nbr-one-dev.rdp";
		expect(contains(CoffeeProfileStore::sessionArgs(linked), "/disable-shortcuts"),
		       "/disable-shortcuts is still passed when a .rdp file is linked");
	}

	expect(contains(args, "/cert:ignore"),
	       "ignoreCertificateErrors=true becomes /cert:ignore");
	p.ignoreCertificateErrors = false;
	expect(!contains(CoffeeProfileStore::sessionArgs(p), "/cert:ignore"),
	       "no /cert:ignore flag when certificate errors are left enforced (the safe default)");
	p.ignoreCertificateErrors = true;

	// Non-default port.
	p.port = 3390;
	args = CoffeeProfileStore::sessionArgs(p);
	expect(contains(args, "/v:cw-nbr-one-dev:3390"), "non-default port is appended to /v:");

	// Keep-alive explicitly disabled must still be passed, or the session's
	// own 30s default would silently re-enable it.
	p = sampleProfile();
	p.idleKeepAliveSeconds = 0;
	args = CoffeeProfileStore::sessionArgs(p);
	expect(contains(args, "/idle-keypress-time:0"),
	       "keep-alive disabled is passed explicitly, not omitted");

	// Fullscreen without multimon.
	p = sampleProfile();
	p.multimon = false;
	p.fullscreen = true;
	args = CoffeeProfileStore::sessionArgs(p);
	expect(contains(args, "/f"), "fullscreen without multimon becomes /f");
	expect(!contains(args, "/multimon"), "multimon flag absent when not requested");

	// A .rdp file wins over the individual connection fields.
	p = sampleProfile();
	p.rdpFile = "/home/evan/Documents/cw-nbr-one-dev.rdp";
	args = CoffeeProfileStore::sessionArgs(p);
	expect(contains(args, "/home/evan/Documents/cw-nbr-one-dev.rdp"), ".rdp path is passed through");
	expect(!contains(args, "/v:cw-nbr-one-dev"), "/v: is not also passed when a .rdp file is set");
	expect(contains(args, "/quality:balanced"),
	       "CoffeeRDP-specific flags still apply on top of a .rdp file");
}
} // namespace

int main()
{
	check_add_and_find();
	check_validation();
	check_normalize_one_password_ref();
	check_round_trip();
	check_missing_file_is_not_an_error();
	check_unknown_keys_preserved();
	check_malformed_lines_are_skipped();
	check_update_and_remove();
	check_session_args();

	if (failures == 0)
		std::printf("OK: all coffee_profiles checks passed\n");
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

/**
 * CoffeeRDP: connection profiles (PLAN.md Phase 7, step 7.1)
 *
 * The connection manager's data model and its on-disk store, kept free of
 * any GTK dependency for the same reason coffee_quality/coffee_idle/
 * coffee_floatbar are kept free of SDL: load/save/validation is the part
 * worth unit testing, and it shouldn't need a display server to test.
 *
 * Storage is `~/.config/coffeerdp/profiles.ini`, the path §2.3 already
 * reserved for this. Format is a minimal INI: one `[section]` per profile,
 * `key = value` lines within it. Deliberately hand-rolled rather than
 * GKeyFile so this stays GTK-free -- the format is small enough that the
 * parser is a few dozen lines, and being able to test it without a GLib
 * main loop is worth more than the dependency saves.
 *
 * Unknown keys are preserved across a load/save round-trip: a profile
 * written by a newer CoffeeRDP shouldn't silently lose fields when opened
 * by an older one. Values are stored verbatim; no escaping is applied, so
 * newlines are rejected at set time rather than corrupting the file.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

struct CoffeeProfile
{
	/** Display name; also the INI section name, so it must be unique and
	 *  free of ']' and newlines -- see CoffeeProfileStore::validate(). */
	std::string name;
	std::string host;
	unsigned port = 3389;
	std::string username;
	std::string domain;
	/** Entra ID (Azure AD) authentication -- the `enablerdsaadauth:i:1` key in
	 *  a .rdp file, which is the whole reason the AAD helper (Phase 4) exists.
	 *  Modeled explicitly rather than left to the linked file: an imported
	 *  profile is unlinked by default, so without a field to carry it the flag
	 *  would be dropped on import and the session would silently negotiate
	 *  NLA instead -- the exact failure coffee_rdp_document.hpp warns about,
	 *  arriving by a different route. */
	bool aadAuth = false;
	/** Disables the session's local Right-Shift+<key> shortcuts (minimize,
	 *  toggle fullscreen, toggle resizeable, toggle keyboard/mouse grab,
	 *  disconnect) for the duration of the session -- some remote apps or
	 *  keyboard layouts collide with them. Defaults off (shortcuts enabled),
	 *  matching the session's own out-of-the-box behavior when nothing
	 *  overrides it. A dropdown toggle in the floatbar can still flip this
	 *  mid-session; this field only controls the starting state. */
	bool disableShortcuts = false;
	/** Skips TLS certificate verification entirely (FreeRDP's own
	 *  `/cert:ignore`) -- for hosts behind Azure AD/AVD's P2P-Access broker,
	 *  which reissue a new short-lived host certificate roughly daily,
	 *  making the "certificate has changed" prompt fire on essentially every
	 *  connection even though nothing is actually wrong. FreeRDP's own
	 *  `/cert:tofu` (auto-accept-on-first-use) does NOT cover this: it only
	 *  skips the prompt for a certificate it has never seen before, not one
	 *  that changed from a previously-trusted one (confirmed against
	 *  libfreerdp/crypto/tls.c -- the AutoAcceptCertificate check only runs
	 *  in the "never seen" branch, not the "changed" one) -- ignore is the
	 *  only built-in way to suppress a *changed*-certificate prompt. This
	 *  also disables FreeRDP's ability to detect a real MITM attack, so it's
	 *  opt-in per profile, not a default. */
	bool ignoreCertificateErrors = false;
	/** One of the §2.4 preset names: speed|balanced|quality|best|auto.
	 *  Empty means "don't pass /quality:, let the session default apply". */
	std::string quality;
	bool multimon = false;
	bool fullscreen = true;
	/** 0 disables the idle keep-alive; matches /idle-keypress-time:. */
	unsigned idleKeepAliveSeconds = 30;
	std::string idleKeepAliveCombo = "alt+tab";
	/** Path to a .rdp file to launch instead of building args from the
	 *  fields above. When set, it wins -- this is how an imported .rdp
	 *  (Phase 7.2) keeps working without round-tripping every setting
	 *  through this struct. */
	std::string rdpFile;

	/** Keys read from disk that this build doesn't know about, preserved
	 *  verbatim on save so a newer build's fields survive an older build. */
	std::map<std::string, std::string> unknownKeys;
};

class CoffeeProfileStore
{
  public:
	/** Returns the default store path (`$XDG_CONFIG_HOME` or `~/.config`,
	 *  then `coffeerdp/profiles.ini`). Does not create anything. */
	[[nodiscard]] static std::string defaultPath();

	/** Replaces the in-memory contents with what's on disk. A missing file
	 *  is not an error -- it yields an empty store, which is the correct
	 *  first-run state. Returns false only on a file that exists but can't
	 *  be read. Malformed lines are skipped, not fatal: a partially
	 *  corrupt file should cost you one profile, not all of them. */
	[[nodiscard]] bool load(const std::string& path);

	/** Writes atomically (temp file + rename) so an interrupted save can't
	 *  truncate an existing profile list. Creates parent directories as
	 *  needed, and chmods the file 0600 -- profiles hold usernames and
	 *  hostnames, and §2.3 already treats this directory as sensitive. */
	[[nodiscard]] bool save(const std::string& path) const;

	[[nodiscard]] const std::vector<CoffeeProfile>& profiles() const
	{
		return _profiles;
	}

	/** Returns nullptr if not found. Name match is exact. */
	[[nodiscard]] const CoffeeProfile* find(const std::string& name) const;

	/** Adds a profile. Fails if the name is already taken or invalid --
	 *  `error` receives a human-readable reason suitable for showing in the
	 *  UI directly. */
	[[nodiscard]] bool add(const CoffeeProfile& profile, std::string& error);

	/** Replaces the profile named `originalName`. Renaming is allowed as
	 *  long as the new name isn't taken by a *different* profile. */
	[[nodiscard]] bool update(const std::string& originalName, const CoffeeProfile& profile,
	                          std::string& error);

	[[nodiscard]] bool remove(const std::string& name);

	/** Checks the invariants add()/update() enforce, without touching the
	 *  store -- exposed so a UI can validate a form before committing. */
	[[nodiscard]] static bool validate(const CoffeeProfile& profile, std::string& error);

	/** Builds the argv (excluding argv[0]) for launching
	 *  `coffee-rdp-session` for this profile. Mirrors the CLI flags Phases
	 *  2/3/5 established. */
	[[nodiscard]] static std::vector<std::string> sessionArgs(const CoffeeProfile& profile);

  private:
	std::vector<CoffeeProfile> _profiles;
};

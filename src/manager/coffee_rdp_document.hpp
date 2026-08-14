/**
 * CoffeeRDP: editable .rdp file document (PLAN.md Phase 7, steps 7.1/7.2)
 *
 * Reads a .rdp file, lets modeled keys be changed, and writes it back
 * **preserving every line it doesn't understand, verbatim and in order** --
 * comments, blank lines, and any key this build has never heard of.
 *
 * That preservation is the whole point, not a nicety. A real .rdp file here
 * contains `enablerdsaadauth:i:1`, the flag that selects the AAD/Entra auth
 * path this entire project exists to make work (§2.3, Phase 4). A naive
 * "parse into a struct, serialize the struct" round-trip would silently
 * drop it and break authentication with no error message. The same applies
 * to gateway settings, redirection flags, and everything else mstsc writes
 * that CoffeeRDP has no opinion about.
 *
 * Kept free of any GTK dependency, same reasoning as coffee_profiles.hpp:
 * the parse/merge/serialize logic is what's worth unit testing, and testing
 * it shouldn't need a display server.
 *
 * Format is FreeRDP's own (client/common/file.c parse_line()):
 * `name:type:value`, where type is a single character -- `s` for string,
 * `i` for integer. Key names are matched case-insensitively, matching
 * FreeRDP; the original spelling in the file is preserved on rewrite.
 *
 * Known gap: UTF-16LE .rdp files (what mstsc on Windows often exports) are
 * not handled -- this reads bytes as-is, so a UTF-16 file will not parse.
 * Fine for files written by CoffeeRDP or by hand on Linux; see PLAN.md
 * Phase 7.2 for where that would need addressing on real import.
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "coffee_profiles.hpp"

class CoffeeRdpDocument
{
  public:
	/** Replaces contents with the file's. A missing file is not an error --
	 *  it yields an empty document, so "link a profile to a .rdp path that
	 *  doesn't exist yet" creates it on save rather than failing. Returns
	 *  false only for a file that exists but can't be read. */
	[[nodiscard]] bool load(const std::string& path);

	/** Atomic (temp file + rename), so an interrupted write can't leave a
	 *  truncated .rdp where a working one used to be. Preserves the line
	 *  endings the file was read with (LF vs CRLF). */
	[[nodiscard]] bool save(const std::string& path) const;

	[[nodiscard]] bool has(const std::string& key) const;
	[[nodiscard]] std::optional<std::string> getString(const std::string& key) const;
	[[nodiscard]] std::optional<int> getInt(const std::string& key) const;

	/** Updates the key in place if present (keeping its position and the
	 *  file's original spelling of the name), otherwise appends it. */
	void setString(const std::string& key, const std::string& value);
	void setInt(const std::string& key, int value);

	/** No-op if absent. Used for values whose "unset" state should be an
	 *  absent key rather than an empty one -- an empty `username:s:` is not
	 *  the same as no username line at all. */
	void remove(const std::string& key);

	/** Total retained lines, including comments/unknown keys. Exposed for
	 *  tests asserting nothing was dropped. */
	[[nodiscard]] size_t lineCount() const
	{
		return _lines.size();
	}

  private:
	struct Line
	{
		/** Verbatim text as read (without its line terminator). Used as-is
		 *  for anything not recognized as a key line. */
		std::string raw;
		bool isKey = false;
		/** Lowercased name, for lookups. */
		std::string nameLower;
		/** Name exactly as spelled in the file, so a rewrite doesn't
		 *  reformat keys the user or mstsc wrote differently. */
		std::string nameOriginal;
		char type = 's';
		std::string value;
	};

	[[nodiscard]] const Line* findLine(const std::string& key) const;
	[[nodiscard]] Line* findLine(const std::string& key);
	void setRaw(const std::string& key, char type, const std::string& value);

	std::vector<Line> _lines;
	bool _crlf = false;
};

/** Fills the connection-related fields of `profile` from `doc`. Only fields
 *  the document actually specifies are touched, so a key the file omits
 *  leaves the profile's existing value alone rather than resetting it. */
void coffee_rdp_document_to_profile(const CoffeeRdpDocument& doc, CoffeeProfile& profile);

/** Writes `profile`'s connection fields into `doc`, leaving every other key
 *  in the document untouched. Empty string-valued fields remove their key
 *  rather than writing an empty one. */
void coffee_rdp_document_from_profile(const CoffeeProfile& profile, CoffeeRdpDocument& doc);

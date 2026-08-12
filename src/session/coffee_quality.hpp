/**
 * CoffeeRDP: connection quality presets
 *
 * See PLAN.md section 2.4 for the design and the exact flag/mask table this
 * implements. Kept free of any SDL/window dependency so the mapping itself
 * is unit-testable against a bare rdpSettings object.
 */
#pragma once

#include <optional>
#include <string>

#include <freerdp/settings.h>

enum class CoffeeQuality
{
	Speed,
	Balanced,
	Quality,
	Best,
	Auto
};

/** Parses a preset name ("speed", "balanced", "quality", "best", "auto",
 *  case-insensitive). Returns false and leaves `out` untouched on an
 *  unrecognized name. */
[[nodiscard]] bool coffee_quality_parse(const std::string& name, CoffeeQuality& out);

[[nodiscard]] const char* coffee_quality_name(CoffeeQuality preset);

/** Applies the preset's PerformanceFlags, connection type, and GFX
 *  codec/color-depth settings to `settings`. Returns false if any
 *  underlying freerdp_settings_set_* call fails. */
[[nodiscard]] bool coffee_quality_apply(rdpSettings* settings, CoffeeQuality preset);

/** Scans a .rdp file for a `quality:s:<preset>` line. FreeRDP's file parser
 *  has no hook for custom keys reachable from this client's entry point, so
 *  this reads the file directly rather than patching the vendored parser.
 *  Returns the raw preset string (unvalidated) if the key is present. */
[[nodiscard]] std::optional<std::string> coffee_quality_scan_rdp_file(const std::string& path);

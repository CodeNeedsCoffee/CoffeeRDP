/**
 * CoffeeRDP: minimal .rdp file key scanning
 *
 * FreeRDP's own file parser (client/common/file.c) has no hook for custom
 * keys reachable from this client's entry point, so CoffeeRDP-specific .rdp
 * keys (quality, idle-keypress-*) are read directly rather than through it.
 * Shared by coffee_quality.cpp and coffee_idle.cpp.
 */
#pragma once

#include <optional>
#include <string>

/** Scans a .rdp file for a `<keyName>:s:<value>` line (matching FreeRDP's
 *  own "name:type:value" format, client/common/file.c parse_line() --
 *  `s` for string-typed keys, the only type CoffeeRDP's own keys use).
 *  `keyName` match is case-insensitive, matching FreeRDP's own key names
 *  (e.g. "quality", "idle keypress time"). Returns the raw value
 *  (unvalidated) if found. */
[[nodiscard]] std::optional<std::string> coffee_rdp_file_scan_key(const std::string& path,
                                                                   const std::string& keyName);

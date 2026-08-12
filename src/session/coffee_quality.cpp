#include "coffee_quality.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <freerdp/settings.h>
#include <freerdp/client/cmdline.h>

namespace
{
struct Preset
{
	UINT32 performanceFlags;
	UINT32 connectionType;
	BOOL gfxProgressive;
	BOOL gfxH264;
	BOOL gfxAVC444;
	UINT32 colorDepth;
};

// clang-format off
constexpr Preset presets[] = {
    // Speed:    progressive codec, 16bpp, most desktop eye-candy disabled
    /* Speed    */ { 0x6f, CONNECTION_TYPE_BROADBAND_LOW, TRUE,  FALSE, FALSE, 16 },
    // Balanced: H.264 AVC420, 32bpp -- the default
    /* Balanced */ { 0x07, CONNECTION_TYPE_BROADBAND_HIGH, FALSE, TRUE,  FALSE, 32 },
    // Quality:  H.264 AVC444, 32bpp
    /* Quality  */ { 0x01, CONNECTION_TYPE_WAN,             FALSE, TRUE,  TRUE,  32 },
    // Best:     AVC444, 32bpp, font smoothing enabled, nothing disabled
    /* Best     */ { 0x80, CONNECTION_TYPE_LAN,             FALSE, TRUE,  TRUE,  32 },
    // Auto:     same codec/mask as Balanced, but let FreeRDP autodetect the link
    /* Auto     */ { 0x07, CONNECTION_TYPE_AUTODETECT,       FALSE, TRUE,  FALSE, 32 },
};
// clang-format on

const Preset& lookup(CoffeeQuality preset)
{
	return presets[static_cast<size_t>(preset)];
}
} // namespace

bool coffee_quality_parse(const std::string& name, CoffeeQuality& out)
{
	std::string lower;
	lower.resize(name.size());
	std::transform(name.begin(), name.end(), lower.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (lower == "speed")
		out = CoffeeQuality::Speed;
	else if (lower == "balanced")
		out = CoffeeQuality::Balanced;
	else if (lower == "quality")
		out = CoffeeQuality::Quality;
	else if (lower == "best")
		out = CoffeeQuality::Best;
	else if (lower == "auto")
		out = CoffeeQuality::Auto;
	else
		return false;
	return true;
}

const char* coffee_quality_name(CoffeeQuality preset)
{
	switch (preset)
	{
		case CoffeeQuality::Speed:
			return "speed";
		case CoffeeQuality::Balanced:
			return "balanced";
		case CoffeeQuality::Quality:
			return "quality";
		case CoffeeQuality::Best:
			return "best";
		case CoffeeQuality::Auto:
			return "auto";
		default:
			return "unknown";
	}
}

bool coffee_quality_apply(rdpSettings* settings, CoffeeQuality preset)
{
	if (!settings)
		return false;

	const Preset& p = lookup(preset);

	if (!freerdp_settings_set_uint32(settings, FreeRDP_PerformanceFlags, p.performanceFlags))
		return false;
	if (!freerdp_set_connection_type(settings, p.connectionType))
		return false;
	if (!freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, TRUE))
		return false;
	if (!freerdp_settings_set_bool(settings, FreeRDP_GfxProgressive, p.gfxProgressive))
		return false;
	if (!freerdp_settings_set_bool(settings, FreeRDP_GfxH264, p.gfxH264))
		return false;
	if (!freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444, p.gfxAVC444))
		return false;
	if (!freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444v2, p.gfxAVC444))
		return false;
	if (!freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, p.colorDepth))
		return false;

	return true;
}

std::optional<std::string> coffee_quality_scan_rdp_file(const std::string& path)
{
	std::ifstream in(path);
	if (!in.is_open())
		return std::nullopt;

	std::string line;
	while (std::getline(in, line))
	{
		// Matches FreeRDP's own .rdp line format (client/common/file.c
		// parse_line()): "name:type:value", type is a single character.
		auto d1 = line.find(':');
		if (d1 == std::string::npos)
			continue;
		auto d2 = line.find(':', d1 + 1);
		if (d2 == std::string::npos || d2 - d1 != 2)
			continue;

		std::string name = line.substr(0, d1);
		char type = line[d1 + 1];
		std::string value = line.substr(d2 + 1);
		while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
			value.pop_back();

		std::transform(name.begin(), name.end(), name.begin(),
		               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		if (type == 's' && name == "quality")
			return value;
	}
	return std::nullopt;
}

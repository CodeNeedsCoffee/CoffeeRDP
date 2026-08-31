#include "coffee_hwdecode.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace
{
/* Deliberately nonexistent: av_hwdevice_ctx_create() opens this path and
 * fails cleanly (ENOENT) when it doesn't resolve to a real DRI render node,
 * which is exactly the "falling back to software" path h264_ffmpeg.c already
 * exercises for a bad/missing device -- see this module's header comment. */
constexpr auto kForceSoftwareDecodeDevice = "/nonexistent";
} // namespace

bool coffee_hwdecode_parse(const std::string& name, CoffeeHwDecode& out)
{
	std::string lower;
	lower.resize(name.size());
	std::transform(name.begin(), name.end(), lower.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (lower == "auto")
		out = CoffeeHwDecode::Auto;
	else if (lower == "off")
		out = CoffeeHwDecode::Off;
	else
		return false;
	return true;
}

const char* coffee_hwdecode_name(CoffeeHwDecode mode)
{
	switch (mode)
	{
		case CoffeeHwDecode::Auto:
			return "auto";
		case CoffeeHwDecode::Off:
			return "off";
		default:
			return "unknown";
	}
}

void coffee_hwdecode_apply(CoffeeHwDecode mode)
{
	if (mode == CoffeeHwDecode::Off)
		setenv("FREERDP_VAAPI_DEVICE", kForceSoftwareDecodeDevice, 1);
}

/**
 * Unit test for coffee_hwdecode's parse/apply logic. Assert-based, matching
 * test_coffee_quality.cpp's style.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../coffee_hwdecode.hpp"

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

void check_parse()
{
	CoffeeHwDecode out{};
	expect(coffee_hwdecode_parse("auto", out) && out == CoffeeHwDecode::Auto, "parse auto");
	expect(coffee_hwdecode_parse("OFF", out) && out == CoffeeHwDecode::Off,
	       "parse OFF (case-insensitive)");
	expect(!coffee_hwdecode_parse("nonsense", out), "parse rejects unknown mode");
}

void check_name()
{
	expect(std::strcmp(coffee_hwdecode_name(CoffeeHwDecode::Auto), "auto") == 0, "name(Auto)");
	expect(std::strcmp(coffee_hwdecode_name(CoffeeHwDecode::Off), "off") == 0, "name(Off)");
}

void check_apply()
{
	// Auto must not touch an existing override -- FreeRDP's own default/
	// override behavior (get_vaapi_device()) is left alone.
	setenv("FREERDP_VAAPI_DEVICE", "/dev/dri/renderD128", 1);
	coffee_hwdecode_apply(CoffeeHwDecode::Auto);
	const char* afterAuto = std::getenv("FREERDP_VAAPI_DEVICE");
	expect(afterAuto && std::strcmp(afterAuto, "/dev/dri/renderD128") == 0,
	       "Auto leaves an existing FREERDP_VAAPI_DEVICE untouched");

	// Off must point at a path that cannot exist, so av_hwdevice_ctx_create()
	// fails and FreeRDP's own software fallback takes over.
	coffee_hwdecode_apply(CoffeeHwDecode::Off);
	const char* afterOff = std::getenv("FREERDP_VAAPI_DEVICE");
	expect(afterOff && std::strcmp(afterOff, "/dev/dri/renderD128") != 0,
	       "Off overrides FREERDP_VAAPI_DEVICE away from a real device");

	unsetenv("FREERDP_VAAPI_DEVICE");
}
} // namespace

int main()
{
	check_parse();
	check_name();
	check_apply();

	if (failures == 0)
		std::printf("OK: all coffee_hwdecode checks passed\n");
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

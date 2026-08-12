/**
 * Unit test for coffee_quality preset mapping (PLAN.md Phase 2, step 2.1:
 * "preset name -> expected flag mask"). Assert-based rather than a test
 * framework dependency, consistent with keeping the project minimal.
 */
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "../coffee_quality.hpp"

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

void check_preset(CoffeeQuality preset, UINT32 expectedFlags, UINT32 expectedConnType,
                  UINT32 expectedDepth)
{
	rdpSettings* settings = freerdp_settings_new(0);
	expect(settings != nullptr, "freerdp_settings_new");
	if (!settings)
		return;

	const char* name = coffee_quality_name(preset);

	expect(coffee_quality_apply(settings, preset), name);

	auto flags = freerdp_settings_get_uint32(settings, FreeRDP_PerformanceFlags);
	if (flags != expectedFlags)
	{
		std::fprintf(stderr, "FAIL: %s PerformanceFlags 0x%02x != expected 0x%02x\n", name, flags,
		            expectedFlags);
		failures++;
	}

	auto connType = freerdp_settings_get_uint32(settings, FreeRDP_ConnectionType);
	if (connType != expectedConnType)
	{
		std::fprintf(stderr, "FAIL: %s ConnectionType %u != expected %u\n", name, connType,
		            expectedConnType);
		failures++;
	}

	auto depth = freerdp_settings_get_uint32(settings, FreeRDP_ColorDepth);
	if (depth != expectedDepth)
	{
		std::fprintf(stderr, "FAIL: %s ColorDepth %u != expected %u\n", name, depth,
		            expectedDepth);
		failures++;
	}

	expect(freerdp_settings_get_bool(settings, FreeRDP_SupportGraphicsPipeline) == TRUE,
	       "SupportGraphicsPipeline enabled");

	freerdp_settings_free(settings);
}

void check_parse()
{
	CoffeeQuality out{};
	expect(coffee_quality_parse("speed", out) && out == CoffeeQuality::Speed, "parse speed");
	expect(coffee_quality_parse("BALANCED", out) && out == CoffeeQuality::Balanced,
	       "parse BALANCED (case-insensitive)");
	expect(coffee_quality_parse("Quality", out) && out == CoffeeQuality::Quality,
	       "parse Quality");
	expect(coffee_quality_parse("best", out) && out == CoffeeQuality::Best, "parse best");
	expect(coffee_quality_parse("auto", out) && out == CoffeeQuality::Auto, "parse auto");
	expect(!coffee_quality_parse("nonsense", out), "parse rejects unknown preset");
}

void write_file(const char* path, const char* content)
{
	std::ofstream out(path);
	out << content;
}

void check_scan_rdp_file()
{
	const char* withKey = "/tmp/coffee_quality_test_with_key.rdp";
	write_file(withKey, "full address:s:example.invalid\r\nquality:s:best\r\nusername:s:foo\r\n");
	auto q = coffee_quality_scan_rdp_file(withKey);
	expect(q.has_value() && *q == "best", "scan finds quality:s: key");

	const char* withoutKey = "/tmp/coffee_quality_test_without_key.rdp";
	write_file(withoutKey, "full address:s:example.invalid\r\nusername:s:foo\r\n");
	expect(!coffee_quality_scan_rdp_file(withoutKey).has_value(),
	       "scan returns nullopt when key is absent");

	expect(!coffee_quality_scan_rdp_file("/tmp/coffee_quality_test_does_not_exist.rdp").has_value(),
	       "scan returns nullopt for a missing file");
}
} // namespace

int main()
{
	check_parse();
	check_scan_rdp_file();

	// Table from PLAN.md section 2.4.
	check_preset(CoffeeQuality::Speed, 0x6f, CONNECTION_TYPE_BROADBAND_LOW, 16);
	check_preset(CoffeeQuality::Balanced, 0x07, CONNECTION_TYPE_BROADBAND_HIGH, 32);
	check_preset(CoffeeQuality::Quality, 0x01, CONNECTION_TYPE_WAN, 32);
	check_preset(CoffeeQuality::Best, 0x80, CONNECTION_TYPE_LAN, 32);
	check_preset(CoffeeQuality::Auto, 0x07, CONNECTION_TYPE_AUTODETECT, 32);

	if (failures == 0)
		std::printf("OK: all coffee_quality checks passed\n");
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

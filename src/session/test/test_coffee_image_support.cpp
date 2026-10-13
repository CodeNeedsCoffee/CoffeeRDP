/**
 * Guards clipboard image support in the vendored WinPR build.
 *
 * Linux screenshots (and most apps) put image/png on the clipboard. If WinPR
 * is built without PNG/JPEG/WebP conversion, CoffeeRDP can only move
 * image/bmp: the desktop is never offered image/png and the server's CF_DIB
 * request has nothing to be answered from, so a screenshot copy pastes as
 * "empty" in both directions. Nothing fails loudly at build time when the
 * CMake options silently stop being set, so this test does: it asserts the
 * formats are supported and does a real DIB -> PNG -> DIB round trip through
 * the same wClipboard API sdl_clip.cpp uses.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <winpr/clipboard.h>
#include <winpr/image.h>
#include <winpr/user.h>

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

void check_formats_compiled_in()
{
	expect(winpr_image_format_is_supported(WINPR_IMAGE_BITMAP), "BMP supported");
	expect(winpr_image_format_is_supported(WINPR_IMAGE_PNG),
	       "PNG supported (Linux screenshots are image/png)");
	expect(winpr_image_format_is_supported(WINPR_IMAGE_JPEG), "JPEG supported");
	expect(winpr_image_format_is_supported(WINPR_IMAGE_WEBP), "WebP supported");
}

/* A 4x4, 32 bpp, bottom-up DIB: BITMAPINFOHEADER followed by pixels. */
std::vector<unsigned char> make_dib()
{
	const int w = 4;
	const int h = 4;
	const unsigned hdr = 40;
	std::vector<unsigned char> dib(hdr + w * h * 4, 0);
	auto put32 = [&](size_t off, unsigned v)
	{
		for (int i = 0; i < 4; i++)
			dib[off + i] = static_cast<unsigned char>((v >> (8 * i)) & 0xff);
	};
	put32(0, hdr);
	put32(4, w);
	put32(8, h);
	dib[12] = 1;  // planes
	dib[14] = 32; // bit count
	put32(20, w * h * 4);
	for (int i = 0; i < w * h; i++)
	{
		dib[hdr + i * 4 + 0] = static_cast<unsigned char>(i * 16);       // B
		dib[hdr + i * 4 + 1] = static_cast<unsigned char>(255 - i * 16); // G
		dib[hdr + i * 4 + 2] = 0x40;                                     // R
		dib[hdr + i * 4 + 3] = 0xff;                                     // A
	}
	return dib;
}

void check_png_round_trip()
{
	wClipboard* clip = ClipboardCreate();
	expect(clip != nullptr, "ClipboardCreate");
	if (!clip)
		return;

	const UINT32 png = ClipboardRegisterFormat(clip, "image/png");
	const auto dib = make_dib();

	expect(ClipboardSetData(clip, CF_DIB, dib.data(), static_cast<UINT32>(dib.size())),
	       "store DIB");

	UINT32 pngSize = 0;
	auto* pngData = static_cast<unsigned char*>(ClipboardGetData(clip, png, &pngSize));
	expect(pngData != nullptr && pngSize > 8, "DIB -> image/png conversion produces data");

	static const unsigned char magic[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
	expect(pngData && pngSize >= 8 && std::memcmp(pngData, magic, 8) == 0,
	       "converted data is a real PNG (magic bytes)");

	if (pngData)
	{
		/* and back: what the server gets when Linux copies a PNG screenshot */
		expect(ClipboardSetData(clip, png, pngData, pngSize), "store PNG");
		UINT32 back = 0;
		void* dibBack = ClipboardGetData(clip, CF_DIB, &back);
		expect(dibBack != nullptr && back >= dib.size(),
		       "image/png -> CF_DIB conversion produces a DIB");
		std::free(dibBack);
	}

	std::free(pngData);
	ClipboardDestroy(clip);
}
} // namespace

int main()
{
	check_formats_compiled_in();
	check_png_round_trip();

	if (failures)
	{
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	std::puts("coffee image support: all checks passed");
	return EXIT_SUCCESS;
}

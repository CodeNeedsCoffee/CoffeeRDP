/**
 * CoffeeRDP: runtime toggle for GPU (VAAPI/NVDEC) H.264 decode
 *
 * FreeRDP's own VAAPI hookup (libfreerdp/codec/h264_ffmpeg.c) is compile-time
 * only (#ifdef WITH_VAAPI) with no settings-based on/off switch -- once built
 * with VAAPI support it always attempts hardware decode on first use, falling
 * back to software automatically if device init fails. That fallback is what
 * this module drives: `Off` deliberately points FREERDP_VAAPI_DEVICE (the one
 * runtime lever h264_ffmpeg.c's get_vaapi_device() reads) at a path that
 * can't exist, so hardware init fails cleanly and the already-existing
 * software fallback takes over -- no vendored FreeRDP source touched.
 *
 * Kept free of any SDL/FreeRDP dependency, same reasoning as
 * coffee_idle.hpp/coffee_quality.hpp: trivial to unit-test in isolation.
 */
#pragma once

#include <string>

enum class CoffeeHwDecode
{
	Auto,
	Off
};

/** Parses "auto"/"off" (case-insensitive). Returns false and leaves `out`
 *  untouched on an unrecognized name. */
[[nodiscard]] bool coffee_hwdecode_parse(const std::string& name, CoffeeHwDecode& out);

[[nodiscard]] const char* coffee_hwdecode_name(CoffeeHwDecode mode);

/** `Auto`: leaves FREERDP_VAAPI_DEVICE untouched, so FreeRDP's own
 *  attempt-hardware-then-fall-back-to-software behavior (and any device
 *  override the user's shell environment already set) applies unmodified.
 *  `Off`: sets FREERDP_VAAPI_DEVICE to a path that cannot exist, forcing
 *  hardware init to fail and software decode to take over. Must run once,
 *  early in main(), before the H.264 codec's first lazy init (first
 *  decoded frame) -- this module has no way to enforce that ordering
 *  itself, the caller owns it. */
void coffee_hwdecode_apply(CoffeeHwDecode mode);

/**
 * CoffeeRDP: idle keep-alive
 *
 * See PLAN.md section 2.1 for the design. Kept free of any SDL/FreeRDP
 * dependency so the timer and combo-string logic are unit-testable in
 * isolation -- the actual key injection (SDL_GetScancodeFromName +
 * freerdp_input_send_keyboard_event_ex) lives in sdl_context.cpp, which has
 * those available.
 *
 * Unlike Remmina's remmina_rdp_idle_keypress() (plugins/rdp/rdp_event.c),
 * which resets its timer only when the keypress fires -- making it a
 * periodic keypress rather than a genuinely idle-triggered one -- this timer
 * resets on every real input event, so it only fires during actual idleness.
 */
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

/** Splits a '+'-separated combo string ("alt+tab", "win+tab", ...) into SDL
 *  scancode name strings, resolving a small set of friendly aliases
 *  (alt/win/ctrl/shift + l/r variants, tab, esc, del) to SDL's canonical
 *  name form (e.g. "Left Alt"). Tokens not found in the alias table are
 *  passed through unchanged, so exact SDL scancode names still work --
 *  callers with access to SDL_GetScancodeFromName() do the final validation.
 *  Returns an empty vector for an empty or all-whitespace input. */
[[nodiscard]] std::vector<std::string> coffee_idle_parse_combo(const std::string& combo);

/** Scans a .rdp file for `idle keypress time:s:<seconds>` /
 *  `idle keypress combo:s:<combo>` lines (see coffee_rdp_file.hpp for why
 *  this reads the file directly rather than through FreeRDP's parser).
 *  Returns the raw values (unvalidated) if present. */
[[nodiscard]] std::optional<std::string> coffee_idle_scan_rdp_file_time(const std::string& path);
[[nodiscard]] std::optional<std::string> coffee_idle_scan_rdp_file_combo(const std::string& path);

class CoffeeIdleTimer
{
  public:
	/** intervalSeconds == 0 disables the timer entirely (due() always
	 *  returns false). */
	void configure(unsigned intervalSeconds);

	[[nodiscard]] bool enabled() const
	{
		return _intervalSeconds > 0;
	}

	/** Call on every real keyboard/mouse/touch event. */
	void noteInput(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

	/** Returns true if the idle interval has elapsed since the later of the
	 *  last real input or the last time this returned true (so it keeps
	 *  firing periodically through sustained idleness, not just once).
	 *  Calling this when it returns true counts as "fired now" for the
	 *  purpose of the next check. */
	[[nodiscard]] bool due(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

  private:
	unsigned _intervalSeconds = 0;
	std::chrono::steady_clock::time_point _last{};
	bool _haveLast = false;
};

/**
 * CoffeeRDP: floatbar overlay -- SDL rendering
 *
 * See PLAN.md section 2.2 / Phase 6. Unlike SdlWidgetList-derived dialogs
 * (sdl_connection_dialog.hpp etc.), which each own a separate always-on-top
 * SDL_Window, this draws directly onto the *session* window's own renderer
 * as a post-process step after the RDP frame is blitted (see
 * SdlWindow::updateSurface()'s overlay callback). A separate floating window
 * cannot be positioned on Wayland at all (same root cause as the multimon
 * placement bug fixed in Phase 5, see PLAN.md section 3), so an overlay is
 * the only design that behaves the same on both backends.
 *
 * The bar is a compact pill sized to its button row, not a full-width strip
 * -- like Windows' own RDP client (mstsc)'s floating connection bar -- and
 * can be dragged left/right along the top edge by its background (not its
 * buttons). The animation/hit-testing/drag state itself lives in
 * CoffeeFloatbarState (coffee_floatbar.hpp); this class owns the SDL-side
 * rendering and the SdlButtonList widgets already used by the connection
 * dialogs.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "sdl_buttons.hpp"
#include "../coffee_floatbar.hpp"

class SdlFloatbar
{
  public:
	enum ButtonId
	{
		BUTTON_DISCONNECT = 1,
		BUTTON_FULLSCREEN,
		BUTTON_CAPTURE_KEYBOARD,
		BUTTON_CTRL_ALT_DEL,
		BUTTON_SEND_SUPER,
		BUTTON_KEEPALIVE,
		BUTTON_PIN,
	};

	using ActionCallback = std::function<void(ButtonId)>;

	void setActionCallback(ActionCallback cb);

	/** (Re)targets the bar at the given window's renderer/width. Cheap to
	 *  call on every SDL_EVENT_WINDOW_MOUSE_ENTER -- switching `renderer` to
	 *  a different window's is how the bar "follows the pointer's window" in
	 *  a multimon session (PLAN.md Phase 6 step 6.5); switching only `width`
	 *  (e.g. a resize of the same window) repositions without resetting the
	 *  animation/pin state. `renderer` is not owned -- the SdlWindow that
	 *  created it keeps ownership for the session's lifetime. */
	[[nodiscard]] bool attach(SDL_Renderer* renderer, Sint32 windowWidth);
	void detach();
	[[nodiscard]] bool attached() const;

	/** Feed mouse state, in raw window-pixel coordinates (not RDP framebuffer
	 *  coordinates -- see sdl_context.cpp's handleEvent() for why those
	 *  differ). Returns true if the bar consumed the event, meaning the
	 *  caller should not forward it to the RDP session as session input. */
	[[nodiscard]] bool handleMouseMotion(float windowRelativeX, float windowRelativeY);
	[[nodiscard]] bool handleMouseButtonDown(float windowRelativeX, float windowRelativeY);
	[[nodiscard]] bool handleMouseButtonUp(float windowRelativeX, float windowRelativeY);
	void handleMouseLeft();

	/** Advances the slide animation by one step. Returns true if the caller
	 *  needs to trigger a redraw (offset changed) even without a new RDP
	 *  frame having arrived. */
	[[nodiscard]] bool tick();

	/** Draws the bar at its current animated position onto the attached
	 *  renderer. Caller must already have the renderer's render target set
	 *  to the window (nullptr target) -- see SdlWindow::updateSurface(). */
	[[nodiscard]] bool render();

	void setKeepAliveEnabled(bool enabled);
	[[nodiscard]] bool keepAliveEnabled() const
	{
		return _keepAliveEnabled;
	}

	/** Controls both the OS-level keyboard-shortcut grab (Super/Alt+Tab,
	 *  see SDL_SetWindowKeyboardGrab) and, separately, whether ordinary
	 *  keystrokes get forwarded to the remote session at all -- see
	 *  sdlInput::handleEvent(). Off means genuinely no keyboard input
	 *  reaches the session, not just "OS shortcuts aren't captured". */
	void setCaptureEnabled(bool enabled);
	[[nodiscard]] bool captureEnabled() const
	{
		return _captureEnabled;
	}

	/** Returns the new pinned state. */
	bool togglePin();
	[[nodiscard]] bool pinned() const;

  private:
	void rebuildButtons();

	CoffeeFloatbarState _state;
	std::shared_ptr<SDL_Renderer> _renderer; // non-owning: SdlWindow owns the real SDL_Renderer
	SdlButtonList _buttons;
	Sint32 _width = 0;
	bool _keepAliveEnabled = true;
	bool _captureEnabled = true;
	ActionCallback _onAction;

	/* Taller than the original 32px -- live testing found the click target
	 * too small/short, on top of the separate stay-open-while-clicking bug
	 * fixed in coffee_floatbar.cpp's tick() (see PLAN.md Phase 6). */
	static constexpr int kBarHeight = 38;
	static constexpr int kPeekPx = 4;
	static constexpr int kRevealZonePx = 14;
	static constexpr int kStepPx = 4;
	static constexpr Sint32 kButtonHeight = 30;
	/* Wide enough that even "Capture Kbd: On/Off" and "Keep-alive: On/Off"
	 * (the longest labels) fit without SdlWidget::render_text()'s "text too
	 * wide for the button, crop from the left" behavior kicking in --
	 * confirmed live at kButtonWidth=110 that it does (e.g. "Disconnect"
	 * rendered as "isconnect"). Widened again in step with kButtonHeight:
	 * that crop check scales text to the button's height, so a taller
	 * button needs proportionally more width for the same label to still
	 * fit -- there's no way to measure actual text pixel width from here to
	 * confirm exactly, so this carries margin on top of the estimate. */
	static constexpr Sint32 kButtonWidth = 200;
	static constexpr Sint32 kButtonCount = 7; // must match rebuildButtons()'s ids/labels vectors
	/* Mirrors SdlButtonList::populate()'s (sdl_buttons.cpp) own internal
	 * button-to-button spacing constant, also named hpadding there. */
	static constexpr Sint32 kButtonHPadding = 10;
	static constexpr Sint32 kButtonRowWidth =
	    kButtonCount * (kButtonWidth + kButtonHPadding) + kButtonHPadding;
	/* Extra breathing room around the button row so there's a bar-background
	 * area to grab and drag that isn't also a button -- like the title-bar
	 * area of Windows' own floating RDP connection bar. */
	static constexpr Sint32 kOuterMargin = 10;
	static constexpr Sint32 kBarWidth = kButtonRowWidth + 2 * kOuterMargin;
};

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
 * The bar is a compact pill sized to its content, not a full-width strip --
 * like Windows' own RDP client (mstsc)'s floating connection bar -- and can
 * be dragged left/right along the top edge by its background (not its
 * buttons). The animation/hit-testing/drag state itself lives in
 * CoffeeFloatbarState (coffee_floatbar.hpp); this class owns the SDL-side
 * rendering and the SdlButtonList widgets already used by the connection
 * dialogs.
 *
 * Redesigned (still Phase 6, later pass) into a compact icon row -- Pin,
 * overflow menu, then (after a small gap and a divider line) the window
 * controls Minimize, Maximize/restore, Close (Windows' own left-to-right
 * order) -- with the less-frequently-used
 * actions (keyboard capture, Ctrl+Alt+Del, Send Super, keep-alive) relocated
 * into a dropdown opened from the overflow button, rather than all seven
 * sitting in the bar as equally-weighted text buttons. The dropdown is its
 * own SdlButtonList (_menuButtons), positioned below the bar and only
 * populated/hit-tested while open; CoffeeFloatbarState::setForceShown() is
 * how it keeps the bar from auto-hiding out from under a click travelling
 * down into it (see that method's doc comment). A six-dot grip in the right
 * margin marks where the bar can be dragged -- background clicks there
 * already moved it (see handleMouseButtonDown()); the grip just makes that
 * discoverable.
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
		BUTTON_MINIMIZE,
		BUTTON_SHORTCUTS,
		/** Opens/closes the dropdown. Never reaches ActionCallback -- purely
		 *  an SdlFloatbar-internal UI concern, handled entirely inside
		 *  handleMouseButtonUp(). Kept in the public enum anyway (rather than
		 *  a private-only id) so it reads the same way as every other button
		 *  here, including in the rebuildButtons() spec list. */
		BUTTON_MENU,
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

	/** Draws the bar (and, while open, the dropdown menu) at its current
	 *  animated position onto the attached renderer. Caller must already
	 *  have the renderer's render target set to the window (nullptr target)
	 *  -- see SdlWindow::updateSurface(). */
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

	/** Whether the session is currently fullscreen -- drives the maximize
	 *  button's icon (plain square vs. the "restore" pair) and its
	 *  persistent highlight, same idea as setKeepAliveEnabled()/
	 *  setCaptureEnabled() above. The caller (SdlContext) is the source of
	 *  truth for the actual state; this only affects what gets drawn. */
	void setFullscreenEnabled(bool enabled);
	[[nodiscard]] bool fullscreenEnabled() const
	{
		return _fullscreenEnabled;
	}

	/** Whether the session's Right-Shift+<key> shortcuts (sdlInput's hotkey
	 *  block -- minimize, fullscreen, resizeable, grab, disconnect) are
	 *  live. A profile can start a session with these off
	 *  (CoffeeProfile::disableShortcuts); this is what lets the dropdown's
	 *  "Local Shortcuts" row show and flip the *current* state regardless of
	 *  where it started. sdlInput is the actual source of truth (see
	 *  sdlInput::setHotkeysEnabled()) -- same division of responsibility as
	 *  setCaptureEnabled() above, this only affects what gets drawn. */
	void setShortcutsEnabled(bool enabled);
	[[nodiscard]] bool shortcutsEnabled() const
	{
		return _shortcutsEnabled;
	}

	/** Returns the new pinned state. */
	bool togglePin();
	[[nodiscard]] bool pinned() const;

  private:
	void rebuildButtons();
	/** Closes the dropdown if open (no-op otherwise) and rebuilds so the
	 *  change is reflected on the next render(). */
	void closeMenu();

	CoffeeFloatbarState _state;
	std::shared_ptr<SDL_Renderer> _renderer; // non-owning: SdlWindow owns the real SDL_Renderer
	SdlButtonList _buttons;                  // primary row: Pin, Menu, Minimize, Maximize, Close
	/** The relocated actions, laid out as a single column. Only populated
	 *  (and only considered for hit-testing/rendering) while _menuOpen. */
	SdlButtonList _menuButtons;
	Sint32 _width = 0;
	bool _keepAliveEnabled = true;
	bool _captureEnabled = true;
	bool _fullscreenEnabled = false;
	bool _shortcutsEnabled = true;
	bool _menuOpen = false;
	/** Valid only while _menuOpen; recomputed by rebuildButtons(). */
	SDL_FRect _menuRect{};
	/** X of the divider between the Pin/Menu group and the window-control
	 *  group (Maximize/Minimize/Close); recomputed by rebuildButtons(). */
	float _groupDividerX = 0.0f;
	/** Set when a button-down dismisses an open menu, so the matching
	 *  button-up (which by then sees _menuOpen already false) is still
	 *  consumed instead of falling through and being forwarded to the RDP
	 *  session as an unmatched click-release. Cleared at the top of the next
	 *  button-down. */
	bool _suppressNextButtonUp = false;
	ActionCallback _onAction;

	/* Taller than the original 32px -- live testing found the click target
	 * too small/short, on top of the separate stay-open-while-clicking bug
	 * fixed in coffee_floatbar.cpp's tick() (see PLAN.md Phase 6). Bumped
	 * again in the icon-row redesign's second pass for a bigger, easier
	 * click target now that the bar only needs to fit a handful of icons
	 * rather than seven text buttons. */
	static constexpr int kBarHeight = 52;
	static constexpr int kPeekPx = 4;
	static constexpr int kRevealZonePx = 14;
	static constexpr int kStepPx = 4;

	/* Icon buttons are square and have no visible resting background of
	 * their own (see sdl_button.cpp) -- they read as part of the bar, sized
	 * for a comfortable click/tap target without needing the wide text
	 * buttons the previous seven-button layout required. */
	static constexpr Sint32 kIconButtonSize = 38;
	static constexpr Sint32 kIconButtonGap = 8;
	static constexpr Sint32 kIconButtonCount = 5; // Pin, Menu, Maximize, Minimize, Close
	/* Extra gap (on top of kIconButtonGap) between Menu and Maximize --
	 * visually separates "app actions" (Pin, the overflow menu) from the
	 * window-control cluster (Maximize/Minimize/Close), same grouping
	 * mstsc/most OS titlebars use. Also where the divider line (render())
	 * sits, at the middle of this gap -- see rebuildButtons()'s
	 * _groupDividerX derivation. */
	static constexpr Sint32 kGroupGapExtra = 16;
	/* Margin on each side of the icon row, also the bar's own drag handle --
	 * background area that isn't a button, same purpose as the original
	 * design's kOuterMargin. Sized to comfortably fit the grip glyph (see
	 * render()) in the right margin, not just a minimal gutter. */
	static constexpr Sint32 kOuterMargin = 22;
	static constexpr Sint32 kButtonRowWidth =
	    kIconButtonCount * (kIconButtonSize + kIconButtonGap) + kIconButtonGap + kGroupGapExtra;
	static constexpr Sint32 kBarWidth = kButtonRowWidth + 2 * kOuterMargin;
	static constexpr float kBarCornerRadius = 16.0f;

	/* Wide enough that "Capture Kbd: On/Off" (the longest label) doesn't hit
	 * SdlWidget::render_text()'s "text too wide for the button, crop from the
	 * left" behavior -- confirmed live that a narrower width does (renders
	 * as "apture Kbd: On"). Rows are taller than the original design's text
	 * buttons (kMenuRowHeight=40 vs. the old kButtonHeight=30), and that
	 * matters here: render_text() scales the glyph to the button's *height*,
	 * so a taller row needs proportionally more width for the same string,
	 * same relationship the original design's own kButtonWidth comment
	 * describes. */
	static constexpr Sint32 kMenuWidth = 320;
	static constexpr Sint32 kMenuRowHeight = 40;
	/* Capture Kbd, Ctrl+Alt+Del, Send Super, Keep-alive, Local Shortcuts --
	 * Fullscreen moved to its own Maximize/restore icon button in the
	 * primary row. Matches rebuildButtons()'s menu spec list. */
	static constexpr Sint32 kMenuButtonCount = 5;
	static constexpr Sint32 kMenuInnerPadding = 6;
	static constexpr Sint32 kMenuGapBelowBar = 8;
	static constexpr float kMenuCornerRadius = 12.0f;
};

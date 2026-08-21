/**
 * CoffeeRDP: floatbar overlay -- slide/reveal/pin/drag state
 *
 * See PLAN.md section 2.2 / Phase 6 for the design. Kept free of any
 * SDL/FreeRDP dependency, same reasoning as coffee_idle.hpp: the animation
 * and hit-testing logic is unit-testable in isolation, while the actual
 * rendering (SdlButtonList, drawing onto the session window's renderer)
 * lives in dialogs/sdl_floatbar.hpp, which owns an instance of this class.
 *
 * Two independent axes:
 *
 * - Vertical (auto-hide/reveal/pin): mirrors xf_floatbar.c's hide/show model
 *   (client/X11/xf_floatbar.c, xf_floatbar_hide_and_show()) -- an unpinned
 *   bar slides by one step per call toward hidden while the pointer is away
 *   from the top edge, and toward shown while the pointer is within the
 *   reveal zone -- driven by real mouse motion events (see tick()), not a
 *   separate timer, same as upstream. Pinning ("locked" in xf_floatbar)
 *   freezes it fully shown regardless of pointer position, and so does an
 *   in-progress drag (see below). Full retraction leaves a `peekPx`-tall
 *   sliver on screen rather than hiding completely, matching xf_floatbar's
 *   "-FLOATBAR_HEIGHT + 1" behavior, so the bar can always be found again.
 *
 * - Horizontal (drag): like the floating connection bar in Windows' own RDP
 *   client (mstsc), the bar is a compact pill sized to its content -- not a
 *   full-width strip -- that can be dragged left/right along the top edge.
 *   It cannot be moved vertically; only the auto-hide logic above changes
 *   its Y position. Defaults to horizontally centered until the user drags
 *   it, after which its position survives width/window-size changes
 *   (reclamped, not recentered) for the rest of the session.
 */
#pragma once

class CoffeeFloatbarState
{
  public:
	/** (Re)initializes the bar to fully retracted, unpinned, horizontally
	 *  centered. `barWidthPx`/`windowWidthPx` are needed up front for that
	 *  initial centering and for drag clamping. */
	void configure(int barHeightPx, int barWidthPx, int windowWidthPx, int peekPx = 2,
	               int revealZonePx = 10, int stepPx = 4);

	/** Updates the bar's own width (its button-row content doesn't change
	 *  size at runtime today, but this keeps the state self-consistent if it
	 *  ever does). Recenters if the user hasn't dragged the bar yet this
	 *  session, otherwise just reclamps the existing position. */
	void setBarWidth(int barWidthPx);

	/** Call whenever the attached window's width changes (e.g. a resize).
	 *  Same recenter-or-reclamp behavior as setBarWidth(). */
	void setWindowWidth(int windowWidthPx);

	/** Keeps the bar fully shown regardless of pointer position, same as
	 *  pinned/dragging -- but independent of both, so it doesn't affect
	 *  pinned()'s value (which drives the pin icon/button and must reflect
	 *  only what the user actually pinned). For the dropdown menu (Phase 6's
	 *  redesign): the menu panel renders below the bar's own tracked rect, so
	 *  a click travelling down into it would otherwise cross out of the
	 *  reveal/stay-open zone and start retracting out from under the click --
	 *  the same class of bug the "whole bar height counts as stay open"
	 *  comment in tick() already describes for the bar's own buttons, just
	 *  one level up. The menu owner is expected to set this while open and
	 *  clear it on close. */
	void setForceShown(bool force);

	void setPinned(bool pinned);
	[[nodiscard]] bool pinned() const
	{
		return _pinned;
	}
	/** Returns the new pinned state. */
	bool togglePinned()
	{
		setPinned(!_pinned);
		return _pinned;
	}

	/** Call on every mouse-motion event delivered to the window the bar is
	 *  attached to, with the window-relative (not RDP framebuffer) pointer Y. */
	void noteMouseY(int windowRelativeY);

	/** Call when the pointer leaves the window entirely
	 *  (SDL_EVENT_WINDOW_MOUSE_LEAVE) -- same effect as a motion event far
	 *  below the reveal zone, and cancels an in-progress drag (no button-up
	 *  will ever arrive to end it otherwise). */
	void noteMouseLeft();

	/** Advances the slide animation by one step toward its current target
	 *  (shown if pinned, being dragged, or the pointer is in the reveal
	 *  zone; hidden otherwise). Returns true if the offset actually changed,
	 *  so the caller knows a redraw is needed even without a new RDP frame
	 *  having arrived. */
	bool tick();

	/** Starts a horizontal drag: `windowRelativeX` is where the pointer
	 *  grabbed the bar, used to keep the grab point fixed under the pointer
	 *  as it moves. Caller is responsible for only calling this when the
	 *  point is on the bar's background, not one of its buttons. */
	void beginDrag(int windowRelativeX);
	/** Moves the bar so the original grab point stays under `windowRelativeX`,
	 *  clamped so the bar never goes fully off-window. No-op if not dragging. */
	void updateDrag(int windowRelativeX);
	void endDrag();
	[[nodiscard]] bool dragging() const
	{
		return _dragging;
	}

	/** Current left-edge X offset in pixels. */
	[[nodiscard]] int offsetX() const
	{
		return _offsetX;
	}

	/** Current top-edge Y offset in pixels: 0 == fully shown, negative ==
	 *  slid up by that many pixels (never less than -(barHeightPx - peekPx)). */
	[[nodiscard]] int offsetY() const
	{
		return _offsetY;
	}

	[[nodiscard]] int barWidth() const
	{
		return _barWidthPx;
	}

	/** True once configured with a positive bar height -- the bar (at least
	 *  its peek sliver) is always somewhere on screen, there is no separate
	 *  fully-hidden state. */
	[[nodiscard]] bool visible() const;

	/** Hit test in window-local pixel coordinates against the bar's current
	 *  on-screen rect (which is sized to its content, not the whole window --
	 *  see offsetX()/barWidth()), for deciding whether a click/motion
	 *  belongs to the bar or should pass through to the RDP session. */
	[[nodiscard]] bool containsPoint(int x, int y) const;

  private:
	void recenterIfNotUserPositioned();
	void clampOffsetX();

	int _barHeightPx = 0;
	int _barWidthPx = 0;
	int _windowWidthPx = 0;
	int _peekPx = 0;
	int _revealZonePx = 0;
	int _stepPx = 1;
	int _offsetY = 0;
	int _offsetX = 0;
	int _lastMotionY = 1'000'000; // "away" until the first real motion event
	bool _pinned = false;
	bool _forceShown = false;
	bool _dragging = false;
	int _dragGrabX = 0;
	bool _userPositioned = false;
};

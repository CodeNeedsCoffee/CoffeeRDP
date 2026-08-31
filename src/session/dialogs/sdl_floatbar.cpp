#include <algorithm>
#include <tuple>

#include <winpr/assert.h>

#include "sdl_floatbar.hpp"
#include "sdl_blend_mode_guard.hpp"
#include "sdl_vector_icons.hpp"

using namespace sdl_palette;

namespace
{
bool pointInRect(float x, float y, const SDL_FRect& r)
{
	return (x >= r.x) && (x < r.x + r.w) && (y >= r.y) && (y < r.y + r.h);
}
} // namespace

void SdlFloatbar::setActionCallback(ActionCallback cb)
{
	_onAction = std::move(cb);
}

bool SdlFloatbar::attach(SDL_Renderer* renderer, Sint32 windowWidth)
{
	if (!renderer || windowWidth <= 0)
	{
		detach();
		return false;
	}

	const bool sameRenderer = _renderer && (_renderer.get() == renderer);
	_width = windowWidth;

	/* A menu positioned for the old width/window would be stale, and
	 * following the pointer to a different window (multimon) mid-click isn't
	 * a case worth preserving an open dropdown across. */
	_menuOpen = false;
	_suppressNextButtonUp = false;

	if (!sameRenderer)
	{
		// Non-owning: the SdlWindow that created this renderer keeps
		// ownership for as long as the window (and thus the session) lives.
		_renderer = std::shared_ptr<SDL_Renderer>(renderer, [](SDL_Renderer*) {});
		_state.configure(kBarHeight, kBarWidth, windowWidth, kPeekPx, kRevealZonePx, kStepPx);
	}
	else
	{
		_state.setWindowWidth(windowWidth);
	}

	rebuildButtons();
	return true;
}

void SdlFloatbar::detach()
{
	_renderer.reset();
	_buttons.clear();
	_menuButtons.clear();
	_menuOpen = false;
	_width = 0;
}

bool SdlFloatbar::attached() const
{
	return static_cast<bool>(_renderer);
}

bool SdlFloatbar::handleMouseMotion(float windowRelativeX, float windowRelativeY)
{
	if (!_renderer)
		return false;

	_state.noteMouseMotion(static_cast<int>(windowRelativeX), static_cast<int>(windowRelativeY));

	if (_state.dragging())
	{
		/* Reposition in place rather than rebuildButtons(): a drag calls this
		 * on every mouse-motion event, and rebuildButtons() tears down and
		 * reconstructs all five SdlButton objects -- including a full
		 * from-scratch vector-icon redraw for each (the pin's ring alone is
		 * ~28 segments) -- for a change that's only ever a position shift,
		 * never an appearance change. That redundant work (every button,
		 * every pixel of drag movement) was the actual lag; the eventual
		 * redraw at the new position still happens exactly once, normally,
		 * via the caller's render() after this returns. _groupDividerX is
		 * a raw float (not a widget), so it just needs the same delta. */
		const int beforeX = _state.offsetX();
		_state.updateDrag(static_cast<int>(windowRelativeX));
		const float dx = static_cast<float>(_state.offsetX() - beforeX);
		if (dx != 0.0f)
		{
			_buttons.moveBy(dx, 0.0f);
			_groupDividerX += dx;
		}
		return true; // a drag captures the pointer regardless of exact hit-test
	}

	const bool inBar =
	    _state.containsPoint(static_cast<int>(windowRelativeX), static_cast<int>(windowRelativeY));
	_buttons.set_mouseover(inBar ? windowRelativeX : -1.0f, inBar ? windowRelativeY : -1.0f);

	if (_menuOpen)
	{
		const bool inMenu = pointInRect(windowRelativeX, windowRelativeY, _menuRect);
		_menuButtons.set_mouseover(inMenu ? windowRelativeX : -1.0f, inMenu ? windowRelativeY : -1.0f);
		return inBar || inMenu;
	}

	return inBar;
}

bool SdlFloatbar::handleMouseButtonDown(float windowRelativeX, float windowRelativeY)
{
	if (!_renderer)
		return false;

	_suppressNextButtonUp = false;

	if (_menuOpen)
	{
		/* Any click that isn't on a menu row dismisses the menu -- including
		 * one that also lands on a bar button underneath (Pin, say): the
		 * dismiss wins, full stop, rather than also firing that button, so
		 * "click away to close the menu" behaves the same everywhere and
		 * never has a surprise side effect. See _suppressNextButtonUp's doc
		 * comment for why button-up needs the flag rather than just
		 * re-checking _menuOpen (which this closeMenu() call has already
		 * flipped to false by the time button-up arrives). */
		if (!pointInRect(windowRelativeX, windowRelativeY, _menuRect))
		{
			closeMenu();
			_suppressNextButtonUp = true;
			return true;
		}
		return true; // on a menu row -- resolved on button-up, like the bar's own buttons
	}

	const int x = static_cast<int>(windowRelativeX);
	const int y = static_cast<int>(windowRelativeY);
	if (!_state.containsPoint(x, y))
		return false;

	// Only start a drag if the click landed on the bar's own background, not
	// one of its buttons -- otherwise every button click would also nudge
	// the bar instead of firing its action.
	if (!_buttons.get_selected(windowRelativeX, windowRelativeY))
		_state.beginDrag(x);

	return true;
}

bool SdlFloatbar::handleMouseButtonUp(float windowRelativeX, float windowRelativeY)
{
	if (!_renderer)
		return false;

	if (_suppressNextButtonUp)
	{
		_suppressNextButtonUp = false;
		return true;
	}

	if (_state.dragging())
	{
		_state.endDrag();
		rebuildButtons();
		return true;
	}

	if (_menuOpen && pointInRect(windowRelativeX, windowRelativeY, _menuRect))
	{
		auto btn = _menuButtons.get_selected(windowRelativeX, windowRelativeY);
		closeMenu();
		if (btn && _onAction)
			_onAction(static_cast<ButtonId>(btn->id()));
		return true;
	}

	const bool inside = _state.containsPoint(static_cast<int>(windowRelativeX),
	                                         static_cast<int>(windowRelativeY));
	if (inside)
	{
		auto btn = _buttons.get_selected(windowRelativeX, windowRelativeY);
		if (btn)
		{
			const auto id = static_cast<ButtonId>(btn->id());
			if (id == BUTTON_MENU)
			{
				_menuOpen = !_menuOpen;
				rebuildButtons();
			}
			else if (_onAction)
				_onAction(id);
		}
	}
	return inside;
}

void SdlFloatbar::handleMouseLeft()
{
	_state.noteMouseLeft(); // also cancels an in-progress drag, see coffee_floatbar.hpp
	_buttons.set_mouseover(-1.0f, -1.0f);
	_menuButtons.set_mouseover(-1.0f, -1.0f);
	closeMenu();
}

bool SdlFloatbar::tick()
{
	if (!_renderer)
		return false;
	_state.setForceShown(_menuOpen);
	if (!_state.tick())
		return false;
	rebuildButtons();
	return true;
}

bool SdlFloatbar::render()
{
	if (!_renderer)
		return true;

	SdlBlendModeGuard guard(_renderer, SDL_BLENDMODE_BLEND);

	/* Best-effort from here down, same reasoning as SdlButtonList::update():
	 * the caller (drawFloatbarOverlay()) already ignores this return value,
	 * so a transient failure on one piece (say, the grip icon) has no
	 * reason to also skip the divider, the buttons, and -- worst of all,
	 * because it was drawn last -- the just-opened dropdown menu entirely.
	 * Drawing everything we can and reporting overall success afterward
	 * avoids that cascade. */
	bool ok = true;

	const SDL_FRect barRect{ static_cast<float>(_state.offsetX()), static_cast<float>(_state.offsetY()),
		                     static_cast<float>(_state.barWidth()), static_cast<float>(kBarHeight) };
	ok = sdl_icons::fillRoundedRect(_renderer.get(), barRect, kBarCornerRadius, kBarBg) && ok;

	/* Purely decorative -- the right margin (kOuterMargin, between the last
	 * button and the bar's rounded edge) is already drag-enabled background
	 * (see handleMouseButtonDown()'s "not a button = background = drag"
	 * rule), this just marks it so that's discoverable instead of a click
	 * you have to already know about. Right side, not left: it reads as
	 * trailing "handle" past the window controls, rather than competing with
	 * Pin for the bar's leading edge. */
	const SDL_FRect gripRect{ barRect.x + barRect.w - kOuterMargin, barRect.y,
		                      static_cast<float>(kOuterMargin), barRect.h };
	ok = sdl_icons::drawGripDotsIcon(_renderer.get(), gripRect, kMutedColor) && ok;

	/* Divider between the app-action group (Pin, overflow menu) and the
	 * window-control group (Maximize/Minimize/Close) -- see
	 * rebuildButtons()'s _groupDividerX derivation. */
	const float dividerTop = barRect.y + barRect.h * 0.24f;
	const float dividerBottom = barRect.y + barRect.h * 0.76f;
	ok = sdl_icons::fillThickLine(_renderer.get(), { _groupDividerX, dividerTop },
	                              { _groupDividerX, dividerBottom }, 1.6f, kDivider) &&
	    ok;

	ok = _buttons.update() && ok;

	if (_menuOpen)
	{
		ok = sdl_icons::fillRoundedRect(_renderer.get(), _menuRect, kMenuCornerRadius, kMenuBg) && ok;
		ok = _menuButtons.update() && ok;
	}

	return ok;
}

void SdlFloatbar::setKeepAliveEnabled(bool enabled)
{
	if (_keepAliveEnabled == enabled)
		return;
	_keepAliveEnabled = enabled;
	rebuildButtons();
}

void SdlFloatbar::setCaptureEnabled(bool enabled)
{
	if (_captureEnabled == enabled)
		return;
	_captureEnabled = enabled;
	rebuildButtons();
}

void SdlFloatbar::setFullscreenEnabled(bool enabled)
{
	if (_fullscreenEnabled == enabled)
		return;
	_fullscreenEnabled = enabled;
	rebuildButtons();
}

void SdlFloatbar::setShortcutsEnabled(bool enabled)
{
	if (_shortcutsEnabled == enabled)
		return;
	_shortcutsEnabled = enabled;
	rebuildButtons();
}

bool SdlFloatbar::togglePin()
{
	const bool p = _state.togglePinned();
	rebuildButtons();
	return p;
}

bool SdlFloatbar::pinned() const
{
	return _state.pinned();
}

void SdlFloatbar::closeMenu()
{
	if (!_menuOpen)
		return;
	_menuOpen = false;
	rebuildButtons();
}

void SdlFloatbar::rebuildButtons()
{
	if (!_renderer)
		return;

	const Sint32 barX = static_cast<Sint32>(_state.offsetX());
	const Sint32 barY = static_cast<Sint32>(_state.offsetY());
	const Sint32 buttonRowX = barX + kOuterMargin;
	constexpr Sint32 kButtonYPad = (kBarHeight - kIconButtonSize) / 2;
	const Sint32 buttonY = barY + kButtonYPad;

	/* SdlButtonList::populate()'s own offsetX formula is
	 * `total_width - button_width`. Passing (buttonRowX + kButtonRowWidth)
	 * makes that resolve to exactly buttonRowX, i.e. the row starts flush
	 * against the bar's current (possibly dragged) left edge plus margin.
	 * Minimize's extraGapBefore is the group break -- see kGroupGapExtra's
	 * doc comment. Window-control order (Minimize, Maximize, Close) matches
	 * Windows' own left-to-right convention. */
	const std::vector<SdlButtonSpec> specs = {
		{ "Pin", BUTTON_PIN, SdlButtonIcon::Pin, _state.pinned(), 0 },
		{ "More actions", BUTTON_MENU, SdlButtonIcon::Menu, false, 0 },
		{ "Minimize", BUTTON_MINIMIZE, SdlButtonIcon::Minimize, false, kGroupGapExtra },
		{ "Maximize", BUTTON_FULLSCREEN, SdlButtonIcon::Maximize, _fullscreenEnabled, 0 },
		{ "Disconnect", BUTTON_DISCONNECT, SdlButtonIcon::Close, false, 0 },
	};
	WINPR_ASSERT(specs.size() == kIconButtonCount);
	std::ignore = _buttons.populate(_renderer, specs, buttonRowX + kButtonRowWidth, buttonY,
	                                kIconButtonSize, kIconButtonSize, kIconButtonGap);

	/* Center of the same extraGapBefore gap populate() just laid out before
	 * Minimize (the third spec, index 2): Menu's right edge is at
	 * buttonRowX + 2*(size+gap); that gap is kIconButtonGap wide normally,
	 * plus kGroupGapExtra from Minimize's spec. */
	const Sint32 menuRightEdge = buttonRowX + 2 * (kIconButtonSize + kIconButtonGap);
	_groupDividerX =
	    static_cast<float>(menuRightEdge) + static_cast<float>(kIconButtonGap + kGroupGapExtra) / 2.0f;

	if (!_menuOpen)
	{
		_menuButtons.clear();
		return;
	}

	const std::vector<SdlButtonSpec> menuSpecs = {
		{ _captureEnabled ? "Capture Kbd: On" : "Capture Kbd: Off", BUTTON_CAPTURE_KEYBOARD,
		 SdlButtonIcon::None, false, 0 },
		{ "Ctrl+Alt+Del", BUTTON_CTRL_ALT_DEL, SdlButtonIcon::None, false, 0 },
		{ "Send Super", BUTTON_SEND_SUPER, SdlButtonIcon::None, false, 0 },
		{ _keepAliveEnabled ? "Keep-alive: On" : "Keep-alive: Off", BUTTON_KEEPALIVE,
		 SdlButtonIcon::None, false, 0 },
		{ _shortcutsEnabled ? "Local Shortcuts: On" : "Local Shortcuts: Off", BUTTON_SHORTCUTS,
		 SdlButtonIcon::None, false, 0 },
	};
	WINPR_ASSERT(menuSpecs.size() == kMenuButtonCount);

	const Sint32 menuHeight = kMenuButtonCount * kMenuRowHeight + 2 * kMenuInnerPadding;

	/* Left-aligned under the bar's current (possibly dragged) position, but
	 * clamped so it can't run off the right edge of the window -- the bar
	 * itself is narrow enough that it can sit near the right edge, while the
	 * menu (with real text labels) is wider than the bar. */
	Sint32 menuX = barX;
	if (_width > 0)
	{
		const Sint32 maxX = std::max<Sint32>(0, _width - kMenuWidth - kOuterMargin);
		menuX = std::clamp(menuX, Sint32{ 0 }, maxX);
	}
	const Sint32 menuY = barY + kBarHeight + kMenuGapBelowBar;
	_menuRect = { static_cast<float>(menuX), static_cast<float>(menuY), static_cast<float>(kMenuWidth),
		          static_cast<float>(menuHeight) };

	std::ignore =
	    _menuButtons.populateVertical(_renderer, menuSpecs, menuX + kMenuInnerPadding,
	                                  menuY + kMenuInnerPadding, kMenuWidth - 2 * kMenuInnerPadding,
	                                  kMenuRowHeight);
}

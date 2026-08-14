#include <tuple>

#include <winpr/assert.h>

#include "sdl_floatbar.hpp"
#include "sdl_blend_mode_guard.hpp"

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

	_state.noteMouseY(static_cast<int>(windowRelativeY));

	if (_state.dragging())
	{
		_state.updateDrag(static_cast<int>(windowRelativeX));
		rebuildButtons();
		return true; // a drag captures the pointer regardless of exact hit-test
	}

	const bool inside =
	    _state.containsPoint(static_cast<int>(windowRelativeX), static_cast<int>(windowRelativeY));
	if (inside)
		_buttons.set_mouseover(windowRelativeX, windowRelativeY);
	else
		_buttons.set_mouseover(-1.0f, -1.0f);

	return inside;
}

bool SdlFloatbar::handleMouseButtonDown(float windowRelativeX, float windowRelativeY)
{
	if (!_renderer)
		return false;

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

	if (_state.dragging())
	{
		_state.endDrag();
		rebuildButtons();
		return true;
	}

	const bool inside =
	    _state.containsPoint(static_cast<int>(windowRelativeX), static_cast<int>(windowRelativeY));
	if (inside)
	{
		auto btn = _buttons.get_selected(windowRelativeX, windowRelativeY);
		if (btn && _onAction)
			_onAction(static_cast<ButtonId>(btn->id()));
	}
	return inside;
}

void SdlFloatbar::handleMouseLeft()
{
	_state.noteMouseLeft(); // also cancels an in-progress drag, see coffee_floatbar.hpp
	_buttons.set_mouseover(-1.0f, -1.0f);
}

bool SdlFloatbar::tick()
{
	if (!_renderer)
		return false;
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

	const SDL_FRect barRect{ static_cast<float>(_state.offsetX()), static_cast<float>(_state.offsetY()),
		                     static_cast<float>(_state.barWidth()), static_cast<float>(kBarHeight) };
	if (!SDL_SetRenderDrawColor(_renderer.get(), 0x1f, 0x2a, 0x33, 0xe0))
		return false;
	if (!SDL_RenderFillRect(_renderer.get(), &barRect))
		return false;

	return _buttons.update();
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

void SdlFloatbar::rebuildButtons()
{
	if (!_renderer)
		return;

	const std::vector<int> ids = {
		BUTTON_DISCONNECT, BUTTON_FULLSCREEN, BUTTON_CAPTURE_KEYBOARD, BUTTON_CTRL_ALT_DEL,
		BUTTON_SEND_SUPER, BUTTON_KEEPALIVE,  BUTTON_PIN,
	};
	const std::vector<std::string> labels = {
		"Disconnect",
		"Fullscreen",
		_captureEnabled ? "Capture Kbd: On" : "Capture Kbd: Off",
		"Ctrl+Alt+Del",
		"Send Super",
		_keepAliveEnabled ? "Keep-alive: On" : "Keep-alive: Off",
		_state.pinned() ? "Unpin" : "Pin",
	};
	WINPR_ASSERT(ids.size() == kButtonCount);

	const Sint32 barX = static_cast<Sint32>(_state.offsetX());
	const Sint32 buttonRowX = barX + kOuterMargin;

	constexpr Sint32 kButtonYPad = (kBarHeight - kButtonHeight) / 2;
	const auto buttonY = static_cast<Sint32>(_state.offsetY()) + kButtonYPad;

	/* SdlButtonList::populate()'s (sdl_buttons.cpp) own offsetX formula is
	 * `total_width - button_width`. Passing (buttonRowX + kButtonRowWidth)
	 * makes that resolve to exactly buttonRowX, i.e. the row starts flush
	 * against the bar's current (possibly dragged) left edge plus margin. */
	std::ignore = _buttons.populate(_renderer, labels, ids, buttonRowX + kButtonRowWidth, buttonY,
	                                kButtonWidth, kButtonHeight);
}

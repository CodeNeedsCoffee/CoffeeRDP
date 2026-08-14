#include "coffee_floatbar.hpp"

#include <algorithm>

void CoffeeFloatbarState::configure(int barHeightPx, int barWidthPx, int windowWidthPx, int peekPx,
                                    int revealZonePx, int stepPx)
{
	_barHeightPx = barHeightPx;
	_peekPx = std::clamp(peekPx, 0, barHeightPx);
	_revealZonePx = revealZonePx;
	_stepPx = stepPx > 0 ? stepPx : 1;
	_offsetY = -(_barHeightPx - _peekPx);
	_lastMotionY = revealZonePx + 1;
	_pinned = false;
	_dragging = false;
	_userPositioned = false;

	_barWidthPx = barWidthPx;
	_windowWidthPx = windowWidthPx;
	recenterIfNotUserPositioned();
}

void CoffeeFloatbarState::setBarWidth(int barWidthPx)
{
	_barWidthPx = barWidthPx;
	if (_userPositioned)
		clampOffsetX();
	else
		recenterIfNotUserPositioned();
}

void CoffeeFloatbarState::setWindowWidth(int windowWidthPx)
{
	_windowWidthPx = windowWidthPx;
	if (_userPositioned)
		clampOffsetX();
	else
		recenterIfNotUserPositioned();
}

void CoffeeFloatbarState::recenterIfNotUserPositioned()
{
	const int maxX = std::max(0, _windowWidthPx - _barWidthPx);
	_offsetX = maxX / 2;
}

void CoffeeFloatbarState::clampOffsetX()
{
	const int maxX = std::max(0, _windowWidthPx - _barWidthPx);
	_offsetX = std::clamp(_offsetX, 0, maxX);
}

void CoffeeFloatbarState::setPinned(bool pinned)
{
	_pinned = pinned;
}

void CoffeeFloatbarState::noteMouseY(int windowRelativeY)
{
	_lastMotionY = windowRelativeY;
}

void CoffeeFloatbarState::noteMouseLeft()
{
	/* Must be far enough away to lose against tick()'s dynamic stay-open
	 * zone too (up to barHeightPx once shown, not just revealZonePx) --
	 * "the pointer left the window" has to always mean "start retracting",
	 * unlike an ordinary motion event elsewhere in the window. Same
	 * sentinel convention as the "no motion yet" default. */
	_lastMotionY = 1'000'000;
	_dragging = false;
}

bool CoffeeFloatbarState::tick()
{
	const int hiddenY = -(_barHeightPx - _peekPx);
	const int before = _offsetY;

	/* revealZonePx is deliberately a thin strip right at the top edge --
	 * that's what makes the bar reveal only from a deliberate approach, not
	 * any time the pointer passes near the top of the window. But using
	 * that same thin strip as the *stay open* test too was a real bug: once
	 * shown, the bar's own buttons occupy the full barHeightPx, and moving
	 * the pointer down into the bar to click one read as "left the top
	 * edge" and started retracting out from under the click. So: once the
	 * bar has started opening (not fully retracted to its peek sliver),
	 * the whole bar height counts as "stay open", not just the original
	 * reveal strip. Only a fully-retracted bar needs the narrow strip to
	 * start opening again. */
	const bool alreadyShown = _offsetY > hiddenY;
	const int stayOpenZone = alreadyShown ? _barHeightPx : _revealZonePx;

	if (_pinned || _dragging || (_lastMotionY <= stayOpenZone))
		_offsetY = std::min(0, _offsetY + _stepPx);
	else
		_offsetY = std::max(hiddenY, _offsetY - _stepPx);

	return _offsetY != before;
}

void CoffeeFloatbarState::beginDrag(int windowRelativeX)
{
	_dragging = true;
	_dragGrabX = windowRelativeX - _offsetX;
}

void CoffeeFloatbarState::updateDrag(int windowRelativeX)
{
	if (!_dragging)
		return;
	_offsetX = windowRelativeX - _dragGrabX;
	clampOffsetX();
	_userPositioned = true;
}

void CoffeeFloatbarState::endDrag()
{
	_dragging = false;
}

bool CoffeeFloatbarState::visible() const
{
	return _barHeightPx > 0;
}

bool CoffeeFloatbarState::containsPoint(int x, int y) const
{
	if (!visible())
		return false;
	if ((x < _offsetX) || (x >= _offsetX + _barWidthPx))
		return false;
	return (y >= _offsetY) && (y < _offsetY + _barHeightPx);
}

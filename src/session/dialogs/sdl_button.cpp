/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * SDL Client Channels
 *
 * Copyright 2023 Armin Novak <armin.novak@thincast.com>
 * Copyright 2023 Thincast Technologies GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cassert>
#include <utility>

#include "sdl_button.hpp"

using namespace sdl_palette;

SdlButton::SdlButton(std::shared_ptr<SDL_Renderer>& renderer, const std::string& label, int id,
                     const SDL_FRect& rect, SdlButtonIcon icon, bool toggledOn)
    : SdlSelectableWidget(renderer, rect), _id(id), _icon(icon), _toggledOn(toggledOn)
{
	/* Icon buttons: no visible resting background of their own (they read as
	 * part of the bar, not as separate boxes) -- just a rounded accent tint
	 * on hover/highlight/pinned. Text buttons (icon == None, e.g. the
	 * relocated dropdown actions, and every other dialog's buttons) keep the
	 * original solid look. */
	const bool isIcon = (icon != SdlButtonIcon::None);
	_backgroundcolor = isIcon ? SDL_Color{ 0, 0, 0, 0 } : SDL_Color{ 0x22, 0x27, 0x2f, 0xff };
	_highlightcolor = { kAccent.r, kAccent.g, kAccent.b, 0x50 };
	_mouseovercolor = (icon == SdlButtonIcon::Close) ? SDL_Color{ kDanger.r, kDanger.g, kDanger.b, 0x50 }
	                                                 : SDL_Color{ kAccent.r, kAccent.g, kAccent.b, 0x30 };
	_fontcolor = kIconColor;
	std::ignore = update_text(isIcon ? "" : label);
	std::ignore = update();
}

SdlButton::SdlButton(SdlButton&& other) noexcept = default;

SdlButton::~SdlButton() = default;

int SdlButton::id() const
{
	return _id;
}

bool SdlButton::clear() const
{
	/* SdlWidget::clear() strokes the widget's rect in _backgroundcolor under
	 * BLENDMODE_NONE -- a raw overwrite, not an alpha blend, so an icon
	 * button's transparent (alpha 0) resting background does NOT come out
	 * invisible: NONE-mode ignores the alpha intent and the RGB channel
	 * (0,0,0) paints a visible black outline box around every icon button,
	 * confirmed against a headless render during development. Icon buttons
	 * paint their own background (nothing at rest, a rounded highlight on
	 * hover/pin) in updateInternal(), so they skip the base outline
	 * entirely rather than trying to make transparent color survive a
	 * blend mode designed for opaque fills. */
	if (_icon != SdlButtonIcon::None)
		return true;
	return SdlWidget::clear();
}

bool SdlButton::updateInternal()
{
	/* Text buttons (every existing dialog, plus the relocated dropdown
	 * actions) keep the original flat-fill rendering unchanged. */
	if (_icon == SdlButtonIcon::None)
		return SdlSelectableWidget::updateInternal();

	/* Icon buttons deliberately bypass SdlSelectableWidget::updateInternal():
	 * its fill() paints via BLENDMODE_NONE, a straight overwrite -- correct
	 * for the opaque backgrounds text buttons use, but for an icon button's
	 * *transparent* resting background (alpha 0, see the constructor) that
	 * overwrite would zero out the alpha of whatever the bar already painted
	 * underneath it, punching a visible rectangular hole in the bar. Drawing
	 * the highlight ourselves with the vector-icon helpers (which blend, see
	 * sdl_vector_icons.cpp) avoids that entirely, and gets a rounded
	 * highlight in the bargain -- the flat rect fill() draws wasn't going to
	 * look "modern" here anyway. update_text() with an empty string (see the
	 * constructor) is already a no-op, so nothing text-related needs calling. */
	const SDL_FRect& r = rect();

	/* A toggled-on button's persistent tint, distinct from the transient
	 * mouseover/highlight rings below -- this one has to stay visible with
	 * the pointer elsewhere, which is the whole point of a state indicator
	 * (Pin: pinned; Maximize: already fullscreen). */
	if (_toggledOn)
	{
		const SDL_FRect glow = { r.x + 3, r.y + 4, r.w - 6, r.h - 8 };
		if (!sdl_icons::fillRoundedRect(_renderer.get(), glow, 9.0f,
		                                { kAccent.r, kAccent.g, kAccent.b, 0x40 }))
			return false;
	}

	if (_highlight || _mouseover)
	{
		const SDL_Color tint = _highlight ? _highlightcolor : _mouseovercolor;
		const SDL_FRect hi = { r.x + 3, r.y + 4, r.w - 6, r.h - 8 };
		if (!sdl_icons::fillRoundedRect(_renderer.get(), hi, 9.0f, tint))
			return false;
	}

	switch (_icon)
	{
		case SdlButtonIcon::Pin:
			return sdl_icons::drawPinIcon(_renderer.get(), r, _toggledOn,
			                             _toggledOn ? kAccent : kMutedColor);
		case SdlButtonIcon::Menu:
			return sdl_icons::drawKebabIcon(_renderer.get(), r, kIconColor);
		case SdlButtonIcon::Minimize:
			return sdl_icons::drawMinimizeIcon(_renderer.get(), r, kIconColor);
		case SdlButtonIcon::Close:
			return sdl_icons::drawCloseIcon(_renderer.get(), r, kIconColor);
		case SdlButtonIcon::Maximize:
			return sdl_icons::drawMaximizeIcon(_renderer.get(), r, _toggledOn,
			                                  _toggledOn ? kAccent : kIconColor);
		default:
			return true;
	}
}

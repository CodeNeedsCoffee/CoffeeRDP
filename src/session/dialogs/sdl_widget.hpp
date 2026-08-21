/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * SDL Client helper dialogs
 *
 * Copyright 2023 Armin Novak <armin.novak@thincast.com>
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

#pragma once

#include <string>
#include <memory>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

#if !defined(HAS_NOEXCEPT)
#if defined(__clang__)
#if __has_feature(cxx_noexcept)
#define HAS_NOEXCEPT
#endif
#elif defined(__GXX_EXPERIMENTAL_CXX0X__) && __GNUC__ * 10 + __GNUC_MINOR__ >= 46 || \
    defined(_MSC_FULL_VER) && _MSC_FULL_VER >= 190023026
#define HAS_NOEXCEPT
#endif
#endif

#ifndef HAS_NOEXCEPT
#define noexcept
#endif

class SdlWidget
{
  public:
	SdlWidget(std::shared_ptr<SDL_Renderer>& renderer, const SDL_FRect& rect);
#if defined(WITH_SDL_IMAGE_DIALOGS)
	SdlWidget(std::shared_ptr<SDL_Renderer>& renderer, const SDL_FRect& rect, SDL_IOStream* ops);
#endif
	SdlWidget(const SdlWidget& other) = delete;
	SdlWidget(SdlWidget&& other) noexcept;
	virtual ~SdlWidget();

	SdlWidget& operator=(const SdlWidget& other) = delete;
	SdlWidget& operator=(SdlWidget&& other) = delete;

	[[nodiscard]] bool fill(SDL_Color color) const;
	[[nodiscard]] bool fill(const std::vector<SDL_Color>& colors) const;
	[[nodiscard]] bool update_text(const std::string& text);

	[[nodiscard]] bool wrap() const;
	[[nodiscard]] bool set_wrap(bool wrap = true, size_t width = 0);
	[[nodiscard]] const SDL_FRect& rect() const;

	/** Shifts the widget's rect in place -- no redraw, no cache
	 *  invalidation. Cheap on purpose: for a widget being dragged every
	 *  mouse-motion event (the floatbar row), rebuilding from scratch each
	 *  time is the actual cost (see SdlFloatbar::handleMouseMotion()), not
	 *  the eventual redraw at the new position, which happens once via the
	 *  normal update()/render() path regardless.
	 *
	 *  Only safe to call on a widget with no *position-dependent* render
	 *  cache -- update_text()'s _renderedDst is cached in absolute
	 *  coordinates and won't follow the move until the text itself next
	 *  changes. Fine for icon buttons (empty text, nothing cached there);
	 *  a text widget would need that cache invalidated too. */
	void moveBy(float dx, float dy);

	[[nodiscard]] bool update();

#define widget_log_error(res, what) SdlWidget::error_ex(res, what, __FILE__, __LINE__, __func__)
	static bool error_ex(bool success, const char* what, const char* file, size_t line,
	                     const char* fkt);

  protected:
	std::shared_ptr<SDL_Renderer> _renderer;
	SDL_Color _backgroundcolor = { 0x56, 0x56, 0x56, 0xff };
	SDL_Color _fontcolor = { 0xd1, 0xcf, 0xcd, 0xff };
	mutable std::string _text;

	virtual bool clear() const;
	virtual bool updateInternal();

  private:
	[[nodiscard]] bool draw_rect(const SDL_FRect& rect, SDL_Color color) const;
	[[nodiscard]] std::shared_ptr<SDL_Texture>
	render_text(const std::string& text, SDL_Color fgcolor, SDL_FRect& src, SDL_FRect& dst) const;
	[[nodiscard]] std::shared_ptr<SDL_Texture> render_text_wrapped(const std::string& text,
	                                                               SDL_Color fgcolor,
	                                                               SDL_FRect& src,
	                                                               SDL_FRect& dst) const;

	std::shared_ptr<TTF_Font> _font = nullptr;
	std::shared_ptr<SDL_Texture> _image = nullptr;
	std::shared_ptr<TTF_TextEngine> _engine = nullptr;
	SDL_FRect _rect = {};
	bool _wrap = false;
	size_t _text_width = 0;

	/* Cache of the last text render: update_text() re-rasterizes via TTF and
	 * re-uploads a fresh GPU texture on every call by default, which is fine
	 * for a dialog whose text changes and redraws rarely, but far too
	 * expensive for something redrawn on every mouse-motion event (the
	 * floatbar) with text that usually hasn't actually changed since the
	 * last redraw. Reused whenever the incoming text matches what's already
	 * cached; invalidated by set_wrap() since that changes how the same
	 * text string would be laid out. */
	std::string _renderedText;
	std::shared_ptr<SDL_Texture> _renderedTexture;
	SDL_FRect _renderedSrc = {};
	SDL_FRect _renderedDst = {};
};

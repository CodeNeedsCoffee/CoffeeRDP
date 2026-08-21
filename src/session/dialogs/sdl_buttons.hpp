#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <string>

#include "sdl_button.hpp"

/** One button's full description, for the icon-capable populate() overload
 *  below. The plain label/id overload is a thin wrapper that fills these in
 *  with icon == None, so both go through the exact same layout math. */
struct SdlButtonSpec
{
	std::string label;
	int id = 0;
	SdlButtonIcon icon = SdlButtonIcon::None;
	bool toggledOn = false;
	/** Extra horizontal gap inserted before this button, on top of the
	 *  normal `hpadding` -- how populate() renders a group break (e.g. the
	 *  floatbar's divider between Pin/Menu and the window controls) without
	 *  a separate layout mode. 0 (default) reproduces the original uniform
	 *  spacing exactly. Horizontal layout only; populateVertical() has no
	 *  use for it. */
	Sint32 extraGapBefore = 0;
};

class SdlButtonList
{
  public:
	SdlButtonList() = default;
	SdlButtonList(const SdlButtonList& other) = delete;
	SdlButtonList(SdlButtonList&& other) = delete;
	virtual ~SdlButtonList();

	SdlButtonList& operator=(const SdlButtonList& other) = delete;
	SdlButtonList& operator=(SdlButtonList&& other) = delete;

	[[nodiscard]] bool populate(std::shared_ptr<SDL_Renderer>& renderer,
	                            const std::vector<std::string>& labels, const std::vector<int>& ids,
	                            Sint32 total_width, Sint32 offsetY, Sint32 width, Sint32 height);

	/** `hpadding` defaults to the original hardcoded 10px gap between
	 *  buttons; the floatbar's icon row passes a tighter value to match its
	 *  more compact, icon-only layout. */
	[[nodiscard]] bool populate(std::shared_ptr<SDL_Renderer>& renderer,
	                            const std::vector<SdlButtonSpec>& specs, Sint32 total_width,
	                            Sint32 offsetY, Sint32 width, Sint32 height, Sint32 hpadding = 10);

	/** A single column, top to bottom, each row `width` wide -- for the
	 *  floatbar's dropdown menu. Unlike populate() above, there's no
	 *  right-alignment math: rows always start flush at (x, startY). */
	[[nodiscard]] bool populateVertical(std::shared_ptr<SDL_Renderer>& renderer,
	                                    const std::vector<SdlButtonSpec>& specs, Sint32 x,
	                                    Sint32 startY, Sint32 width, Sint32 rowHeight,
	                                    Sint32 vpadding = 0);

	/** Shifts every button's rect by (dx, dy) -- see SdlWidget::moveBy(). For
	 *  repositioning a whole row cheaply (a drag in progress) instead of
	 *  tearing it down and reconstructing it via populate()/populateVertical(),
	 *  which also re-renders every button's icon from scratch. */
	void moveBy(float dx, float dy);

	[[nodiscard]] bool update();
	[[nodiscard]] std::shared_ptr<SdlButton> get_selected(const SDL_MouseButtonEvent& button);
	[[nodiscard]] std::shared_ptr<SdlButton> get_selected(float x, float y);

	bool set_highlight_next(bool reset = false);
	bool set_highlight(size_t index);
	bool set_mouseover(float x, float y);

	void clear();

  private:
	std::vector<std::shared_ptr<SdlButton>> _list;
	std::shared_ptr<SdlButton> _highlighted = nullptr;
	size_t _highlight_index = 0;
	std::shared_ptr<SdlButton> _mouseover = nullptr;
};

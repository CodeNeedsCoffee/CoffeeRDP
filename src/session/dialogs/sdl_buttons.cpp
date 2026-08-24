#include <cassert>
#include <algorithm>

#include "sdl_buttons.hpp"

SdlButtonList::~SdlButtonList() = default;

bool SdlButtonList::populate(std::shared_ptr<SDL_Renderer>& renderer,
                             const std::vector<std::string>& labels, const std::vector<int>& ids,
                             Sint32 total_width, Sint32 offsetY, Sint32 width, Sint32 height)
{
	assert(labels.size() == ids.size());

	std::vector<SdlButtonSpec> specs;
	specs.reserve(ids.size());
	for (size_t x = 0; x < ids.size(); x++)
		specs.push_back({ labels.at(x), ids.at(x), SdlButtonIcon::None, false, 0 });

	return populate(renderer, specs, total_width, offsetY, width, height);
}

bool SdlButtonList::populate(std::shared_ptr<SDL_Renderer>& renderer,
                             const std::vector<SdlButtonSpec>& specs, Sint32 total_width,
                             Sint32 offsetY, Sint32 width, Sint32 height, Sint32 hpadding)
{
	assert(renderer);
	assert(width >= 0);
	assert(height >= 0);

	_list.clear();

	Sint32 totalExtraGap = 0;
	for (const auto& spec : specs)
		totalExtraGap += spec.extraGapBefore;

	/* Same shape as the original closed-form formula (button_width =
	 * n*(width+hpadding)+hpadding), just with any group-break gaps folded
	 * in -- reduces to exactly the original for extraGapBefore == 0
	 * everywhere, which is every caller except the floatbar's icon row. */
	const size_t button_width =
	    specs.size() * (static_cast<size_t>(width) + hpadding) + hpadding + totalExtraGap;
	const size_t offsetX = static_cast<size_t>(total_width) -
	                       std::min<size_t>(static_cast<size_t>(total_width), button_width);

	size_t cursor = offsetX;
	for (const auto& spec : specs)
	{
		cursor += static_cast<size_t>(spec.extraGapBefore);
		const SDL_FRect rect = { static_cast<float>(cursor), static_cast<float>(offsetY),
			                     static_cast<float>(width), static_cast<float>(height) };
		auto button =
		    std::make_shared<SdlButton>(renderer, spec.label, spec.id, rect, spec.icon, spec.toggledOn);
		_list.emplace_back(button);
		cursor += static_cast<size_t>(width) + hpadding;
	}
	return true;
}

bool SdlButtonList::populateVertical(std::shared_ptr<SDL_Renderer>& renderer,
                                     const std::vector<SdlButtonSpec>& specs, Sint32 x,
                                     Sint32 startY, Sint32 width, Sint32 rowHeight, Sint32 vpadding)
{
	assert(renderer);
	assert(width >= 0);
	assert(rowHeight >= 0);

	_list.clear();
	for (size_t i = 0; i < specs.size(); i++)
	{
		const auto& spec = specs.at(i);
		const Sint32 y = startY + static_cast<Sint32>(i) * (rowHeight + vpadding);
		const SDL_FRect rect = { static_cast<float>(x), static_cast<float>(y),
			                     static_cast<float>(width), static_cast<float>(rowHeight) };
		auto button =
		    std::make_shared<SdlButton>(renderer, spec.label, spec.id, rect, spec.icon, spec.toggledOn);
		_list.emplace_back(button);
	}
	return true;
}

void SdlButtonList::moveBy(float dx, float dy)
{
	for (auto& btn : _list)
		btn->moveBy(dx, dy);
}

std::shared_ptr<SdlButton> SdlButtonList::get_selected(const SDL_MouseButtonEvent& button)
{
	const auto x = button.x;
	const auto y = button.y;

	return get_selected(x, y);
}

std::shared_ptr<SdlButton> SdlButtonList::get_selected(float x, float y)
{
	for (auto& btn : _list)
	{
		auto r = btn->rect();
		if ((x >= r.x) && (x <= r.x + r.w) && (y >= r.y) && (y <= r.y + r.h))
			return btn;
	}
	return nullptr;
}

bool SdlButtonList::set_highlight_next(bool reset)
{
	if (reset)
		_highlighted = nullptr;
	else
	{
		auto next = _highlight_index++;
		_highlight_index %= _list.size();
		auto& element = _list.at(next);
		_highlighted = element;
	}
	return true;
}

bool SdlButtonList::set_highlight(size_t index)
{
	if (index >= _list.size())
	{
		_highlighted = nullptr;
		return false;
	}
	auto& element = _list.at(index);
	_highlighted = element;
	_highlight_index = ++index % _list.size();
	return true;
}

bool SdlButtonList::set_mouseover(float x, float y)
{
	_mouseover = get_selected(x, y);
	return _mouseover != nullptr;
}

void SdlButtonList::clear()
{
	_list.clear();
	_mouseover = nullptr;
	_highlighted = nullptr;
	_highlight_index = 0;
}

bool SdlButtonList::update()
{
	/* Best-effort across the whole list: a single button hitting a
	 * transient draw failure (e.g. a texture/font hiccup) used to make this
	 * return early, silently leaving every later button in the list
	 * completely undrawn for that frame -- no background, no text. The
	 * caller (SdlFloatbar::render()) ignores this return value already, so
	 * there's no reason one bad button should blank out the rest; drawing
	 * everything we can and reporting overall success/failure afterward is
	 * strictly better. */
	bool ok = true;
	for (auto& btn : _list)
	{
		ok = btn->highlight(btn == _highlighted) && ok;
		ok = btn->mouseover(btn == _mouseover) && ok;
		ok = btn->update() && ok;
	}

	return ok;
}

#pragma once

#include <string>
#include <memory>

#include "sdl_selectable_widget.hpp"
#include "sdl_vector_icons.hpp"

/** None: a plain text button (the original/default). Anything else: an
 *  icon-only button -- `label` is still stored (matters for none of the
 *  current icon buttons, but a screen-reader-visible label costs nothing to
 *  keep around), the drawn glyph replaces it. See sdl_vector_icons.hpp for
 *  what each one looks like. */
enum class SdlButtonIcon
{
	None,
	Pin,
	Menu,
	Minimize,
	Close,
	/** Single square outline when off, two offset outlines ("restore") when
	 *  on -- see sdl_vector_icons.hpp's drawMaximizeIcon(). */
	Maximize,
};

class SdlButton : public SdlSelectableWidget
{
  public:
	/** `toggledOn` is only meaningful for icons with two states (Pin,
	 *  Maximize): draws the "on" variant (accent-colored, persistent
	 *  background tint, and for Pin/Maximize a shape change too) instead of
	 *  the neutral resting one. This reflects a real toggled state the
	 *  button represents -- not `mouseover`/`highlight` on the base class,
	 *  which are transient and only true while the pointer happens to be
	 *  over it. */
	SdlButton(std::shared_ptr<SDL_Renderer>& renderer, const std::string& label, int id,
	          const SDL_FRect& rect, SdlButtonIcon icon = SdlButtonIcon::None,
	          bool toggledOn = false);
	SdlButton(SdlButton&& other) noexcept;
	SdlButton(const SdlButton& other) = delete;
	~SdlButton() override;

	SdlButton& operator=(const SdlButton& other) = delete;
	SdlButton& operator=(SdlButton&& other) = delete;

	[[nodiscard]] int id() const;

  protected:
	[[nodiscard]] bool clear() const override;
	[[nodiscard]] bool updateInternal() override;

  private:
	int _id;
	SdlButtonIcon _icon;
	bool _toggledOn;
};

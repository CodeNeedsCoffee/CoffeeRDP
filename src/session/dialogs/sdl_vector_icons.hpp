/**
 * CoffeeRDP: small procedural icon set for the floatbar redesign
 *
 * The floatbar draws directly onto the session window's SDL_Renderer (see
 * sdl_floatbar.hpp) rather than through any widget toolkit, so there is no
 * icon-font or SVG rasterizer available to it -- OpenSans (the only font
 * SdlWidget loads, see sdl_widget.cpp) has no glyph coverage for pin/kebab/
 * window-control symbols anyway, and pulling in an SVG renderer for four
 * glyphs would be a much bigger dependency than drawing them.
 *
 * So these are built from SDL_RenderGeometry() triangle fans instead: filled
 * circles, filled rounded rectangles, and filled "thick lines" (quads with
 * round caps), composed into the handful of glyphs the bar needs. All shapes
 * are filled, no stroked/outline rendering -- simpler, and every glyph here
 * reads fine solid at the sizes the bar uses (confirmed against a headless
 * software-rendered preview during development).
 *
 * Coordinates for the icon-drawing functions are in the caller's `box`
 * rect's own space; each glyph is centered within it.
 */
#pragma once

#include <vector>

#include <SDL3/SDL.h>

/** The floatbar's palette. Centralized here (rather than duplicated between
 *  sdl_button.cpp, which uses it for icon/highlight colors, and
 *  sdl_floatbar.cpp, which uses it for the bar/menu panel backgrounds) so the
 *  two can't drift apart. */
namespace sdl_palette
{
constexpr SDL_Color kAccent = { 0x22, 0xd3, 0xee, 0xff };  // pinned / hovered / on
constexpr SDL_Color kDanger = { 0xef, 0x44, 0x44, 0xff };  // close button hover
constexpr SDL_Color kBarBg = { 0x12, 0x16, 0x1d, 0xe8 };
constexpr SDL_Color kMenuBg = { 0x16, 0x1b, 0x23, 0xf2 };
constexpr SDL_Color kIconColor = { 0xe8, 0xea, 0xed, 0xff };
constexpr SDL_Color kMutedColor = { 0x9a, 0xa3, 0xb0, 0xff }; // pin, unpinned
constexpr SDL_Color kDivider = { 0x9a, 0xa3, 0xb0, 0x45 };    // between button groups
} // namespace sdl_palette

namespace sdl_icons
{

/** Fills a convex (or star-shaped-around-its-centroid) polygon via a
 *  triangle fan from its centroid -- every shape below reduces to this. */
bool fillPolygon(SDL_Renderer* renderer, const std::vector<SDL_FPoint>& points, SDL_Color color);

bool fillCircle(SDL_Renderer* renderer, SDL_FPoint center, float radius, SDL_Color color);

/** `radius` is clamped to half the rect's shorter side, so an oversized
 *  radius degrades to a stadium/circle shape instead of a malformed one. */
bool fillRoundedRect(SDL_Renderer* renderer, SDL_FRect rect, float radius, SDL_Color color);

/** A filled quad along the a-b segment, `thickness` px wide, with a filled
 *  circle capping each end so joins in multi-segment glyphs (the X in the
 *  close icon) don't leave a visible notch at the crossing. */
bool fillThickLine(SDL_Renderer* renderer, SDL_FPoint a, SDL_FPoint b, float thickness,
                   SDL_Color color);

/** Pushpin-at-an-angle glyph -- a ring "head" with a tapering body/point,
 *  styled after Bootstrap Icons' pin-angle / pin-angle-fill (not traced from
 *  them; approximated with this file's fill-only primitives, which can't
 *  follow their bezier paths, but aiming for the same silhouette). Both
 *  states share the same tilt, matching the reference -- the caller is
 *  expected to also pass a different (accent) color for the pinned state and
 *  to paint a persistent highlight behind the button, so pinned/unpinned
 *  reads three ways at once: the diagonal strike, plus color, plus
 *  background. */
bool drawPinIcon(SDL_Renderer* renderer, SDL_FRect box, bool pinned, SDL_Color color);

/** Six-dot grip -- the conventional "grab here to drag" affordance (Trello
 *  cards, Notion blocks, etc.), two columns of three. Deliberately a
 *  different dot count/layout than drawKebabIcon()'s three-in-a-column, so
 *  the two aren't confusable at a glance. */
bool drawGripDotsIcon(SDL_Renderer* renderer, SDL_FRect box, SDL_Color color);

/** Three-dot "kebab" overflow-menu glyph. */
bool drawKebabIcon(SDL_Renderer* renderer, SDL_FRect box, SDL_Color color);

/** Single horizontal bar, positioned low in `box` -- the conventional
 *  window-minimize glyph. */
bool drawMinimizeIcon(SDL_Renderer* renderer, SDL_FRect box, SDL_Color color);

/** X glyph, for the close/disconnect button. */
bool drawCloseIcon(SDL_Renderer* renderer, SDL_FRect box, SDL_Color color);

/** Window maximize/restore glyph: a single square outline when `!restore`
 *  (click to enter fullscreen), two overlapping square outlines when
 *  `restore` (click to leave it) -- the conventional pair, same idea as
 *  Windows' own maximize/restore-down icons. The overlap isn't occluded
 *  (that would need a same-color "eraser" fill matching whatever's already
 *  behind the icon, including any highlight tint painted under it -- not
 *  worth the fragility for this); two plainly offset outlines already read
 *  clearly as "restore" at these sizes. */
bool drawMaximizeIcon(SDL_Renderer* renderer, SDL_FRect box, bool restore, SDL_Color color);

} // namespace sdl_icons

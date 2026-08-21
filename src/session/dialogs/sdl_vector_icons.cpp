#include "sdl_vector_icons.hpp"

#include <cmath>

namespace sdl_icons
{

namespace
{

SDL_FColor toFColor(SDL_Color c)
{
	return { static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
		     static_cast<float>(c.b) / 255.0f, static_cast<float>(c.a) / 255.0f };
}

std::vector<SDL_FPoint> circlePoints(SDL_FPoint center, float radius, int segments = 24)
{
	std::vector<SDL_FPoint> pts;
	pts.reserve(static_cast<size_t>(segments));
	for (int i = 0; i < segments; i++)
	{
		const float a = static_cast<float>(i) / static_cast<float>(segments) * 2.0f *
		                static_cast<float>(M_PI);
		pts.push_back({ center.x + radius * std::cos(a), center.y + radius * std::sin(a) });
	}
	return pts;
}

/** A ring, approximated as N short fillThickLine segments walked around a
 *  circle -- a single fan-fill can't produce a hole (the fan's own center
 *  point sits inside it), so a stroke built from thick-line pieces is the
 *  straightforward way to get one out of fill-only primitives. This is what
 *  gives the pin glyph below its "pushpin head" ring instead of a solid
 *  disc. */
bool strokeCircle(SDL_Renderer* renderer, SDL_FPoint center, float radius, float thickness,
                  SDL_Color color, int segments = 28)
{
	bool ok = true;
	for (int i = 0; i < segments; i++)
	{
		const float a0 = static_cast<float>(i) / segments * 2.0f * static_cast<float>(M_PI);
		const float a1 = static_cast<float>(i + 1) / segments * 2.0f * static_cast<float>(M_PI);
		const SDL_FPoint p0 = { center.x + radius * std::cos(a0), center.y + radius * std::sin(a0) };
		const SDL_FPoint p1 = { center.x + radius * std::cos(a1), center.y + radius * std::sin(a1) };
		ok = ok && fillThickLine(renderer, p0, p1, thickness, color);
	}
	return ok;
}

/** Unfilled square, `halfSize` from center to each edge -- the maximize
 *  icon, and half of the restore icon (two of these, offset). */
bool strokeSquare(SDL_Renderer* renderer, SDL_FPoint center, float halfSize, float thickness,
                  SDL_Color color)
{
	const SDL_FPoint p0 = { center.x - halfSize, center.y - halfSize };
	const SDL_FPoint p1 = { center.x + halfSize, center.y - halfSize };
	const SDL_FPoint p2 = { center.x + halfSize, center.y + halfSize };
	const SDL_FPoint p3 = { center.x - halfSize, center.y + halfSize };
	bool ok = true;
	ok = ok && fillThickLine(renderer, p0, p1, thickness, color);
	ok = ok && fillThickLine(renderer, p1, p2, thickness, color);
	ok = ok && fillThickLine(renderer, p2, p3, thickness, color);
	ok = ok && fillThickLine(renderer, p3, p0, thickness, color);
	return ok;
}

} // namespace

bool fillPolygon(SDL_Renderer* renderer, const std::vector<SDL_FPoint>& points, SDL_Color color)
{
	if (points.size() < 3)
		return false;

	float cx = 0.0f;
	float cy = 0.0f;
	for (const auto& p : points)
	{
		cx += p.x;
		cy += p.y;
	}
	cx /= static_cast<float>(points.size());
	cy /= static_cast<float>(points.size());

	const SDL_FColor fc = toFColor(color);

	std::vector<SDL_Vertex> verts;
	verts.reserve(points.size() + 1);
	verts.push_back({ { cx, cy }, fc, { 0, 0 } });
	for (const auto& p : points)
		verts.push_back({ p, fc, { 0, 0 } });

	std::vector<int> indices;
	const int n = static_cast<int>(points.size());
	indices.reserve(static_cast<size_t>(n) * 3);
	for (int i = 0; i < n; i++)
	{
		indices.push_back(0);
		indices.push_back(1 + i);
		indices.push_back(1 + ((i + 1) % n));
	}

	return SDL_RenderGeometry(renderer, nullptr, verts.data(), static_cast<int>(verts.size()),
	                          indices.data(), static_cast<int>(indices.size()));
}

bool fillCircle(SDL_Renderer* renderer, SDL_FPoint center, float radius, SDL_Color color)
{
	return fillPolygon(renderer, circlePoints(center, radius), color);
}

bool fillRoundedRect(SDL_Renderer* renderer, SDL_FRect rect, float radius, SDL_Color color)
{
	radius = std::min(radius, std::min(rect.w, rect.h) / 2.0f);
	const float x0 = rect.x;
	const float y0 = rect.y;
	const float x1 = rect.x + rect.w;
	const float y1 = rect.y + rect.h;

	struct Corner
	{
		float cx;
		float cy;
		float startDeg;
	};
	const Corner corners[4] = {
		{ x1 - radius, y0 + radius, -90.0f },
		{ x1 - radius, y1 - radius, 0.0f },
		{ x0 + radius, y1 - radius, 90.0f },
		{ x0 + radius, y0 + radius, 180.0f },
	};

	constexpr int segsPerCorner = 8;
	std::vector<SDL_FPoint> pts;
	pts.reserve(4 * (segsPerCorner + 1));
	for (const auto& c : corners)
	{
		for (int i = 0; i <= segsPerCorner; i++)
		{
			const float deg = c.startDeg + 90.0f * static_cast<float>(i) / segsPerCorner;
			const float rad = deg * static_cast<float>(M_PI) / 180.0f;
			pts.push_back({ c.cx + radius * std::cos(rad), c.cy + radius * std::sin(rad) });
		}
	}
	return fillPolygon(renderer, pts, color);
}

bool fillThickLine(SDL_Renderer* renderer, SDL_FPoint a, SDL_FPoint b, float thickness,
                   SDL_Color color)
{
	const float dx = b.x - a.x;
	const float dy = b.y - a.y;
	const float len = std::sqrt(dx * dx + dy * dy);
	if (len < 1e-4f)
		return fillCircle(renderer, a, thickness / 2.0f, color);

	const float nx = -dy / len * thickness / 2.0f;
	const float ny = dx / len * thickness / 2.0f;
	const std::vector<SDL_FPoint> quad = { { a.x + nx, a.y + ny },
		                                   { b.x + nx, b.y + ny },
		                                   { b.x - nx, b.y - ny },
		                                   { a.x - nx, a.y - ny } };

	bool ok = fillPolygon(renderer, quad, color);
	ok = ok && fillCircle(renderer, a, thickness / 2.0f, color);
	ok = ok && fillCircle(renderer, b, thickness / 2.0f, color);
	return ok;
}

bool drawPinIcon(SDL_Renderer* renderer, SDL_FRect box, bool pinned, SDL_Color color)
{
	const SDL_FPoint center = { box.x + box.w / 2.0f, box.y + box.h / 2.0f };

	/* Local-space geometry, numbers out of a headless-rendered preview
	 * during development, not a formula: a ring "head" up and to the right,
	 * a tapering wedge "body" running down to a point at lower-left -- same
	 * silhouette family as Bootstrap Icons' pin-angle, approximated with
	 * this file's fill-only primitives (no bezier paths available here). */
	constexpr SDL_FPoint ringCenter = { 3.6f, -5.6f };
	constexpr float ringRadius = 3.6f;
	constexpr float ringThickness = 2.1f;
	constexpr SDL_FPoint neckA = { 0.7f, -2.8f };
	constexpr SDL_FPoint neckB = { 4.0f, -1.7f };
	constexpr SDL_FPoint apex = { -5.6f, 7.6f };

	auto place = [&](SDL_FPoint p) { return SDL_FPoint{ p.x + center.x, p.y + center.y }; };

	bool ok = strokeCircle(renderer, place(ringCenter), ringRadius, ringThickness, color);
	const std::vector<SDL_FPoint> body = { place(neckA), place(neckB), place(apex) };
	ok = ok && fillPolygon(renderer, body, color);

	/* Unpinned only: a diagonal strike, same role as the reference's added
	 * line over the outline glyph -- distinguishes the two states by more
	 * than color alone (the pinned/unpinned tilt itself doesn't change,
	 * matching the reference, where both variants share one path). */
	if (!pinned)
	{
		constexpr SDL_FPoint strikeA = { -7.3f, -8.4f };
		constexpr SDL_FPoint strikeB = { 8.4f, 8.4f };
		ok = ok && fillThickLine(renderer, place(strikeA), place(strikeB), 1.5f, color);
	}

	return ok;
}

bool drawGripDotsIcon(SDL_Renderer* renderer, SDL_FRect box, SDL_Color color)
{
	const SDL_FPoint c = { box.x + box.w / 2.0f, box.y + box.h / 2.0f };
	constexpr float colSpacing = 4.2f;
	constexpr float rowSpacing = 5.6f;
	constexpr float dotRadius = 1.5f;

	bool ok = true;
	for (int col = -1; col <= 1; col += 2)
		for (int row = -1; row <= 1; row++)
			ok = ok && fillCircle(renderer, { c.x + col * colSpacing / 2.0f, c.y + row * rowSpacing },
			                      dotRadius, color);
	return ok;
}

bool drawKebabIcon(SDL_Renderer* renderer, SDL_FRect box, SDL_Color color)
{
	const SDL_FPoint c = { box.x + box.w / 2.0f, box.y + box.h / 2.0f };
	bool ok = true;
	ok = ok && fillCircle(renderer, { c.x, c.y - 6.5f }, 2.0f, color);
	ok = ok && fillCircle(renderer, { c.x, c.y }, 2.0f, color);
	ok = ok && fillCircle(renderer, { c.x, c.y + 6.5f }, 2.0f, color);
	return ok;
}

bool drawMinimizeIcon(SDL_Renderer* renderer, SDL_FRect box, SDL_Color color)
{
	const SDL_FPoint c = { box.x + box.w / 2.0f, box.y + box.h / 2.0f + 5.0f };
	return fillThickLine(renderer, { c.x - 8, c.y }, { c.x + 8, c.y }, 2.8f, color);
}

bool drawCloseIcon(SDL_Renderer* renderer, SDL_FRect box, SDL_Color color)
{
	const SDL_FPoint c = { box.x + box.w / 2.0f, box.y + box.h / 2.0f };
	bool ok = true;
	ok = ok && fillThickLine(renderer, { c.x - 7, c.y - 7 }, { c.x + 7, c.y + 7 }, 2.6f, color);
	ok = ok && fillThickLine(renderer, { c.x + 7, c.y - 7 }, { c.x - 7, c.y + 7 }, 2.6f, color);
	return ok;
}

bool drawMaximizeIcon(SDL_Renderer* renderer, SDL_FRect box, bool restore, SDL_Color color)
{
	const SDL_FPoint c = { box.x + box.w / 2.0f, box.y + box.h / 2.0f };

	if (!restore)
		return strokeSquare(renderer, c, 6.5f, 2.2f, color);

	/* Two offset outlines rather than one -- see the header's note on why
	 * the overlap isn't occluded. */
	bool ok = strokeSquare(renderer, { c.x + 2.1f, c.y - 2.1f }, 5.0f, 2.0f, color);
	ok = ok && strokeSquare(renderer, { c.x - 1.5f, c.y + 1.5f }, 5.0f, 2.0f, color);
	return ok;
}

} // namespace sdl_icons

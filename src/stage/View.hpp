#pragma once
#include <algorithm>
#include <cmath>
#include "../kit/Painter.hpp"
#include "Layout.hpp"

// Stage's screen, independent of Rack so tools/mockup can draw it too. The edges label the soft buttons and knobs
// beside them; the middle is a list (bindings, macros, groups) or the edit page.
namespace stage {

enum Page { BINDINGS_PAGE, EDIT_PAGE, MACROS_PAGE, GROUPS_PAGE, PAGES };
inline const char* const PAGE_NAMES[] = {"BINDINGS", "EDIT", "MACROS", "GROUPS"};

struct Row {
	std::string index, source, label, note; // note replaces the bar
	bool alert = false, dim = false;
	float value = NAN, base = NAN; // 0..1
};

struct KnobLabel {
	std::string name, value;
};

struct ViewState {
	int page = BINDINGS_PAGE;
	bool focused = false, shift = false;
	std::string actions[SOFT];
	KnobLabel knobs[KNOBS];
	int cursor = 0;
	std::vector<Row> rows;                                  // list pages
	std::string heading;                                    // edit page: the binding
	std::vector<std::pair<std::string, std::string>> items; // edit page: path and settings
	std::vector<float> source, result;                      // edit page history, oldest first: volts/5 and 0..1
};

constexpr float EDGE_T = 11, EDGE_B = 11, EDGE_L = 30, EDGE_R = 42, ROW = 13, SIZE = 7, SMALL = 5.5f;

inline std::string clip(const std::string& s, float width, float size) {
	return s.substr(0, std::max(0, int(width / (0.52f * size))));
}

/** The first of `n` rows to show so `cursor` stays in view. */
inline int scroll(int cursor, int n, int fits) {
	return std::clamp(cursor - fits / 2, 0, std::max(0, n - fits));
}

/** Which content row a click at `y` lands on, given what's scrolled into view; -1 outside the list. */
inline int hit(float y, float height, int cursor, int n) {
	int fits = int((height - EDGE_T - EDGE_B) / ROW);
	int i = int((y - EDGE_T) / ROW);
	if (y < EDGE_T || i >= fits)
		return -1;
	i += scroll(cursor, n, fits);
	return i < n ? i : -1;
}

inline void box(kit::Painter& p, float x0, float y0, float x1, float y1, kit::Rgba c) {
	p.stroke({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, c, 0.5f, true);
}

inline void paintRows(kit::Painter& p, const kit::Theme& t, const ViewState& s, float x0, float x1) {
	const float h = p.height - EDGE_T - EDGE_B;
	const int fits = int(h / ROW), first = scroll(s.cursor, int(s.rows.size()), fits);
	for (int i = first; i < std::min(int(s.rows.size()), first + fits); i++) {
		const Row& r = s.rows[i];
		const float y = EDGE_T + (i - first) * ROW, mid = y + ROW / 2;
		const bool sel = i == s.cursor;
		if (sel)
			box(p, x0, y + 0.5f, x1, y + ROW - 0.5f, t.ring * 0.7f);
		const kit::Rgba ink = r.dim ? t.guide : t.field;
		p.label({x0 + 2, mid - 1.5f}, r.index, SMALL, sel ? t.ring : t.guide, -1);
		p.label({x0 + 14, mid - 1.5f}, r.source, SMALL, ink, -1);
		const float lx = x0 + 34, w = x1 - lx - 3;
		p.label({lx, mid - 2}, clip(r.label, w, SIZE), SIZE, ink, -1);
		const float by = y + ROW - 2.5f;
		if (!r.note.empty())
			p.label({x1 - 2, mid - 2}, r.note, SMALL, r.alert ? t.ring : t.guide, 1);
		else if (!std::isnan(r.value)) {
			const float bx = x1 - 50;
			p.line({bx, by}, {x1 - 3, by}, t.guide * 0.5f, 0.5f);
			float from = std::isnan(r.base) ? 0.f : r.base;
			p.line({bx + (x1 - 3 - bx) * from, by}, {bx + (x1 - 3 - bx) * std::clamp(r.value, 0.f, 1.f), by}, t.field, 1.2f);
			if (!std::isnan(r.base))
				p.line({bx + (x1 - 3 - bx) * r.base, by - 2.5f}, {bx + (x1 - 3 - bx) * r.base, by + 2.5f}, t.ring * 0.8f, 0.5f);
		}
	}
	if (s.rows.empty())
		p.label({(x0 + x1) / 2, EDGE_T + h / 2}, "NOTHING HERE YET", SIZE, t.guide);
}

inline void paintEdit(kit::Painter& p, const kit::Theme& t, const ViewState& s, float x0, float x1) {
	p.label({x0 + 2, EDGE_T + 5}, clip(s.heading, x1 - x0 - 4, SIZE), SIZE, t.ring, -1);
	const float listX1 = x0 + (x1 - x0) * 0.55f, top = EDGE_T + 11, h = p.height - EDGE_B - top;
	const int fits = int(h / 10), first = scroll(s.cursor, int(s.items.size()), fits);
	for (int i = first; i < std::min(int(s.items.size()), first + fits); i++) {
		const float y = top + (i - first) * 10 + 5;
		const bool sel = i == s.cursor;
		if (sel)
			box(p, x0, y - 4.5f, listX1, y + 4.5f, t.ring * 0.7f);
		p.label({x0 + 2, y}, s.items[i].first, SMALL, sel ? t.ring : t.guide, -1);
		const float vx = x0 + 2 + 12.5f * 0.52f * SMALL + 2;
		p.label({vx, y}, clip(s.items[i].second, listX1 - vx - 2, SMALL), SMALL, t.field, -1);
	}
	// History: the source in volts (±5 V full height) behind the result, the target's position.
	const float gx0 = listX1 + 4, gx1 = x1 - 2, gy0 = top, gy1 = p.height - EDGE_B - 2;
	box(p, gx0, gy0, gx1, gy1, t.guide * 0.6f);
	p.line({gx0, (gy0 + gy1) / 2}, {gx1, (gy0 + gy1) / 2}, t.guide * 0.3f, 0.5f);
	auto trace = [&](const std::vector<float>& v, bool bipolar, kit::Rgba c) {
		std::vector<kit::Vec2f> pts;
		for (size_t i = 0; i < v.size(); i++) {
			if (std::isnan(v[i]))
				continue;
			float u = bipolar ? 0.5f + 0.5f * std::clamp(v[i], -1.f, 1.f) : std::clamp(v[i], 0.f, 1.f);
			pts.push_back({gx0 + (gx1 - gx0) * i / std::max<size_t>(1, v.size() - 1), gy1 - u * (gy1 - gy0)});
		}
		p.stroke(pts, c, 0.8f);
	};
	trace(s.source, true, t.ring * 0.6f);
	trace(s.result, false, t.field);
	p.label({gx0 + 2, gy0 + 4}, "CV", SMALL, t.ring * 0.6f, -1);
	p.label({gx1 - 2, gy0 + 4}, "TARGET", SMALL, t.field, 1);
}

inline void paint(kit::Painter& p, const kit::Theme& t, const ViewState& s) {
	const float w = p.width, h = p.height, quarterX = (w) / SOFT, quarterY = h / SOFT;
	for (int i = 0; i < SOFT; i++) {
		const float cx = (i + 0.5f) * quarterX, cy = (i + 0.5f) * quarterY;
		p.label({cx, 5}, PAGE_NAMES[i], SMALL, i == s.page ? t.ring : t.guide);
		if (i == s.page)
			p.line({cx - 14, 9.5f}, {cx + 14, 9.5f}, t.ring, 0.8f);
		p.label({2, cy}, clip(s.actions[i], EDGE_L - 3, SMALL), SMALL, t.field * 0.8f, -1);
		const KnobLabel& b = s.knobs[i];
		if (!b.name.empty())
			p.label({cx, h - 5}, clip(b.name + " " + b.value, quarterX - 4, SMALL), SMALL, t.field * 0.8f);
		const KnobLabel& r = s.knobs[SOFT + i];
		p.label({w - 2, cy - 3.5f}, clip(r.name, EDGE_R - 3, SMALL), SMALL, t.guide, 1);
		p.label({w - 2, cy + 3.5f}, clip(r.value, EDGE_R - 3, SMALL), SMALL, t.field, 1);
	}
	if (s.page == EDIT_PAGE)
		paintEdit(p, t, s, EDGE_L, w - EDGE_R);
	else
		paintRows(p, t, s, EDGE_L, w - EDGE_R);
	if (s.shift)
		p.label({w - 2, 5}, "SHIFT", SMALL, t.ring, 1);
	// Keyboard focus lights the glass's rim.
	if (s.focused)
		box(p, 0.8f, 0.8f, w - 0.8f, h - 0.8f, t.ring * 0.8f);
}

} // namespace stage

#pragma once
#include <algorithm>
#include <cmath>
#include "../kit/Painter.hpp"
#include "Layout.hpp"

// Strings' screen, independent of Rack so tools/mockup can draw it too.
namespace strings {

struct Row {
	std::string label; // what the channel drives; empty for an unbound channel
	std::string tag;   // mode or target count
	std::string note;  // shown instead of the bar: a problem, or the learn prompt
	bool alert = false;
	float value = NAN, base = NAN; // 0..1; the target's position and the hand's
};

struct ViewState {
	Row rows[CHANNELS];
	int selected = 0;
	bool editing = false;
	std::vector<std::pair<std::string, std::string>> items; // the selected channel's settings, when editing
	int cursor = 0;
	std::string left, right; // what the two knobs do, shown over their wires
};

// Share Tech Mono is monospaced, so text is cut to a character count rather than measured.
inline std::string clip(const std::string& s, float width, float size) {
	return s.substr(0, std::max(1, int(width / (0.52f * size))));
}

inline void paintRow(kit::Painter& p, const kit::Theme& t, const Row& r, int index, bool selected, float y, float h) {
	constexpr float PAD = 4, SIZE = 8, SMALL = 6.5f, NUMBER = 9;
	const float w = p.width, x = PAD + NUMBER;
	if (selected)
		p.stroke({{1.5f, y + 1.5f}, {w - 1.5f, y + 1.5f}, {w - 1.5f, y + h - 1.5f}, {1.5f, y + h - 1.5f}}, t.ring * 0.7f, 0.5f, true);
	p.label({PAD, y + 9}, std::to_string(index + 1), SIZE, selected ? t.ring : t.guide, -1);
	if (r.label.empty() && r.note.empty()) {
		p.line({x, y + h / 2}, {w - PAD, y + h / 2}, t.guide * 0.5f, 0.5f);
		return;
	}
	const float tagWidth = r.tag.empty() ? 0 : 0.52f * SMALL * r.tag.size() + 4;
	p.label({x, y + 9}, clip(r.label, w - PAD - x - tagWidth, SIZE), SIZE, t.field, -1);
	p.label({w - PAD, y + 9}, r.tag, SMALL, t.field * 0.6f, 1);
	if (!r.note.empty()) {
		p.label({x, y + 21}, clip(r.note, w - PAD - x, SIZE), SIZE, r.alert ? t.ring : t.field * 0.6f, -1);
		return;
	}
	const float x1 = w - PAD, y0 = y + 18, y1 = y + 24;
	p.stroke({{x, y0}, {x1, y0}, {x1, y1}, {x, y1}}, t.guide, 0.5f, true);
	if (!std::isnan(r.value)) {
		const float v = x + std::clamp(r.value, 0.f, 1.f) * (x1 - x);
		p.line({x, (y0 + y1) / 2}, {v, (y0 + y1) / 2}, t.field, 1.6f);
		p.line({v, y0 - 1}, {v, y1 + 1}, t.probe, 0.7f);
	}
	if (!std::isnan(r.base)) {
		const float b = x + std::clamp(r.base, 0.f, 1.f) * (x1 - x);
		p.line({b, y0 - 2}, {b, y1 + 2}, t.ring, 0.7f);
	}
}

/** Where the edit page's list of settings sits, shared by painting and clicking. */
struct Geometry {
	float row, foot, top, line;

	Geometry(float width, float height, size_t items) {
		const float scale = width / (GLASS.max.x - GLASS.min.x); // screen pixels per panel millimetre
		row = ROW * scale;
		foot = height - FOOTER * scale;
		top = row + 8;
		line = std::min(15.f, (foot - top) / std::max<size_t>(1, items));
	}
	/** The channel row, or the setting when editing, under `y`; -1 for neither. */
	int hit(float y, bool editing, int count) const {
		const int i = int(std::floor(editing ? (y - top) / line : y / row));
		return i >= 0 && i < count ? i : -1;
	}
};

inline void paint(kit::Painter& p, const kit::Theme& t, const ViewState& s) {
	constexpr float SIZE = 8, PAD = 4;
	const Geometry g(p.width, p.height, s.items.size());

	if (!s.editing) {
		for (int i = 0; i < CHANNELS; i++)
			paintRow(p, t, s.rows[i], i, i == s.selected, i * g.row, g.row);
	}
	else {
		paintRow(p, t, s.rows[s.selected], s.selected, true, 0, g.row);
		for (int i = 0; i < int(s.items.size()); i++) {
			const float y = g.top + (i + 0.5f) * g.line;
			const bool on = i == s.cursor;
			if (on)
				p.label({PAD, y}, ">", SIZE, t.ring, -1);
			p.label({PAD + 8, y}, s.items[i].first, SIZE, on ? t.ring : t.field * 0.7f, -1);
			p.label({p.width - PAD, y}, s.items[i].second, SIZE, on ? t.probe : t.field, 1);
		}
	}

	// The footer reads as labels for the two knobs wired to it.
	p.line({0, g.foot}, {p.width, g.foot}, t.guide, 0.5f);
	const float y = (g.foot + p.height) / 2;
	const float item = (ITEM_X - GLASS.min.x) * p.width / (GLASS.max.x - GLASS.min.x);
	p.label({item, y}, clip(s.left, item - PAD, 7), 7, t.ring, 1);
	p.label({p.width - PAD, y}, s.right, 7, t.probe, 1);
}

} // namespace strings

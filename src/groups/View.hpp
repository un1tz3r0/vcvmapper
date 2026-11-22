#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include "../kit/Painter.hpp"
#include "Layout.hpp"

// The group modules' screens, independent of Rack so tools/mockup can draw them too.
namespace troupe {

struct Macro {
	std::string label; // what it drives, empty when unbound
	float knob = 0.f;  // -1..1
};

struct ViewState {
	std::string name;
	int members = 0;
	Macro macros[MACROS];
	int selected = 0;
	std::string note; // replaces the selected macro's label: the learn prompt or a refusal
	bool alert = false;
};

constexpr float HEAD = 19.f, ROW = 13.f, SIZE = 6.5f;

/** The macro row at `y`, or -1 for the name. */
inline int hit(float y) {
	return y < HEAD ? -1 : std::clamp(int((y - HEAD) / ROW), 0, MACROS - 1);
}

inline std::string clip(const std::string& s, float width, float size) {
	return s.substr(0, std::max(1, int(width / (0.52f * size))));
}

inline void paint(kit::Painter& p, const kit::Theme& t, const ViewState& s) {
	constexpr float PAD = 3;
	const float w = p.width - 2 * PAD;
	p.label({p.width / 2, 7}, clip(s.name.empty() ? "UNNAMED" : s.name, w, 8), 8, s.name.empty() ? t.guide : t.ring);
	p.label({p.width / 2, 15}, s.members == 1 ? "1 MODULE" : std::to_string(s.members) + " MODULES", 5.5f, t.field * 0.6f);
	for (int i = 0; i < MACROS; i++) {
		const Macro& m = s.macros[i];
		const float y = HEAD + i * ROW, mid = p.width / 2, bar = y + ROW - 3;
		const bool sel = i == s.selected;
		if (sel)
			p.stroke({{1, y + 0.5f}, {p.width - 1, y + 0.5f}, {p.width - 1, y + ROW - 0.5f}, {1, y + ROW - 0.5f}}, t.ring * 0.7f, 0.5f, true);
		std::string text = sel && !s.note.empty() ? s.note : m.label.empty() ? "-" : m.label;
		p.label({PAD, y + 4.5f}, clip(std::to_string(i + 1) + " " + text, w, SIZE), SIZE,
			sel && s.alert ? t.ring : m.label.empty() ? t.guide : t.field, -1);
		p.line({PAD, bar}, {PAD + w, bar}, t.guide * 0.5f, 0.5f);
		p.line({mid, bar}, {mid + std::clamp(m.knob, -1.f, 1.f) * w / 2, bar}, t.field, 1.2f);
	}
}

} // namespace troupe

namespace tag {

struct ViewState {
	std::string name;
	bool right = false, found = false; // which side it names, and whether a module touches that side
};

/** The name down the glass a letter at a time, under an arrow to the module it names. */
inline void paint(kit::Painter& p, const kit::Theme& t, const ViewState& s) {
	const float cx = p.width / 2, d = s.right ? 1.f : -1.f;
	const kit::Rgba arrow = s.found ? t.ring : t.guide;
	p.stroke({{cx - 3 * d, 4}, {cx + 3 * d, 8}, {cx - 3 * d, 12}}, arrow, 1.f);
	const std::string name = s.name.empty() ? "NAME" : s.name;
	const float step = 10.f;
	const size_t fits = size_t(std::max(1.f, (p.height - 24) / step));
	for (size_t i = 0; i < std::min(name.size(), fits); i++)
		p.label({cx, 24 + i * step}, std::string(1, char(std::toupper((unsigned char) name[i]))), 9, s.name.empty() ? t.guide : t.field);
}

} // namespace tag

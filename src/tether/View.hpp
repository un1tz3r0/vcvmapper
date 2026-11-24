#pragma once
#include <algorithm>
#include <cmath>
#include "../kit/Painter.hpp"

// Tether's screen, independent of Rack so tools/mockup can draw it too: what the cord reaches, and where it sits.
namespace tether {

struct ViewState {
	std::string top, bottom; // module and control, or a message on two lines
	std::string tag;         // mode and lane count
	bool alert = false;
	float value = NAN, base = NAN; // the first target's position and the hand's, 0..1
};

inline std::string clip(const std::string& s, float width, float size) {
	return s.substr(0, std::max(1, int(width / (0.52f * size))));
}

inline void paint(kit::Painter& p, const kit::Theme& t, const ViewState& s) {
	constexpr float PAD = 3, SIZE = 7;
	const float w = p.width - 2 * PAD;
	p.label({PAD, 7}, clip(s.top, w, SIZE), SIZE, s.alert ? t.ring : t.field, -1);
	p.label({PAD, 16}, clip(s.bottom, w, SIZE), SIZE, s.alert ? t.ring : t.ring * 0.9f, -1);
	const float y = 26;
	p.line({PAD, y}, {PAD + w, y}, t.guide * 0.5f, 0.5f);
	if (!std::isnan(s.value))
		p.line({PAD, y}, {PAD + w * std::clamp(s.value, 0.f, 1.f), y}, t.field, 1.2f);
	if (!std::isnan(s.base))
		p.line({PAD + w * s.base, y - 3}, {PAD + w * s.base, y + 3}, t.ring * 0.8f, 0.6f);
	p.label({PAD, 37}, clip(s.tag, w, 5.5f), 5.5f, t.field * 0.6f, -1);
}

} // namespace tether

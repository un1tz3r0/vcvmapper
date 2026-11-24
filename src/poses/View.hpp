#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include "../kit/Painter.hpp"
#include "Layout.hpp"

// Poses' screen, independent of Rack so tools/mockup can draw it too: the crossfader's ends, then the poses in two
// columns, each with how many controls it holds.
namespace poses {

struct ViewState {
	int counts[POSES] = {}; // controls each pose holds, 0 for empty
	int selected = 0, a = -1, b = -1;
	bool muteA = false, muteB = false;
	float fade = 0.f;
	std::string line; // what the capture set holds, or a prompt
	bool alert = false;
};

constexpr float HEAD = 24.f, ROW = 11.f, SIZE = 6.5f, SMALL = 5.5f;

/** The pose at `pos`, or -1 above the list. */
inline int hit(float x, float y, float width) {
	if (y < HEAD)
		return -1;
	return std::clamp(int((y - HEAD) / ROW), 0, POSES / 2 - 1) + (x < width / 2 ? 0 : POSES / 2);
}

inline void paint(kit::Painter& p, const kit::Theme& t, const ViewState& s) {
	constexpr float PAD = 3;
	const float w = p.width, half = w / 2;
	auto end = [&](int pose, bool mute, float x, int align) {
		std::string text = mute ? "MUTE" : pose < 0 ? "-" : std::to_string(pose + 1);
		p.label({x, 7}, text, SIZE, mute || pose < 0 ? t.guide : t.ring, align);
	};
	end(s.a, s.muteA, PAD, -1);
	end(s.b, s.muteB, w - PAD, 1);
	const float x0 = PAD + 24, x1 = w - PAD - 24, at = x0 + (x1 - x0) * std::clamp(s.fade, 0.f, 1.f);
	p.line({x0, 7}, {x1, 7}, t.guide * 0.5f, 0.5f);
	p.line({at, 3}, {at, 11}, t.field, 1.4f);
	p.label({PAD, 17}, s.line.substr(0, size_t(std::max(1.f, (w - 2 * PAD) / (0.52f * SMALL)))), SMALL, s.alert ? t.ring : t.field * 0.7f, -1);
	for (int i = 0; i < POSES; i++) {
		const float x = i < POSES / 2 ? 0 : half, y = HEAD + (i % (POSES / 2)) * ROW, mid = y + ROW / 2;
		if (i == s.selected)
			p.stroke({{x + 1, y + 0.5f}, {x + half - 1, y + 0.5f}, {x + half - 1, y + ROW - 0.5f}, {x + 1, y + ROW - 0.5f}}, t.ring * 0.7f, 0.5f, true);
		const bool empty = !s.counts[i];
		p.label({x + PAD, mid}, std::to_string(i + 1), SMALL, i == s.selected ? t.ring : t.guide, -1);
		p.label({x + PAD + 13, mid}, empty ? "-" : std::to_string(s.counts[i]), SIZE, empty ? t.guide : t.field, -1);
		std::string ends = std::string(i == s.a ? "A" : "") + (i == s.b ? "B" : "");
		if (!ends.empty())
			p.label({x + half - PAD - 1, mid}, ends, SMALL, t.ring, 1);
	}
}

} // namespace poses

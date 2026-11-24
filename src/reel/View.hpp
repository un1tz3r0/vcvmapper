#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "../kit/Painter.hpp"
#include "Layout.hpp"

// Reel's screen, independent of Rack so tools/mockup can draw it too: the clips across the top, then the selected
// clip's lanes as curves with its crop and playhead.
namespace reel {

struct ViewState {
	int selected = 0;
	bool filled[CLIPS] = {}, playing[CLIPS] = {}, loop = false;
	float seconds = 0.f, start = 0.f, end = 1.f, head = NAN; // head, start and end in 0..1 of the clip
	std::vector<std::vector<kit::Vec2f>> curves;            // each lane's steps, both axes 0..1
	std::string line;                                        // status: armed, recording, or what the set holds
	bool alert = false;
};

constexpr float TOP = 12.f, SIZE = 6.5f, SMALL = 5.5f;

inline std::string clock(float seconds) {
	char s[16];
	std::snprintf(s, sizeof s, "%d:%04.1f", int(seconds) / 60, std::fmod(seconds, 60.f));
	return s;
}

/** The clip cell at the top under `x`, or -1 below the cells. */
inline int hit(float x, float y, float width) {
	return y < TOP ? std::clamp(int(x / width * CLIPS), 0, CLIPS - 1) : -1;
}

inline void paint(kit::Painter& p, const kit::Theme& t, const ViewState& s) {
	constexpr float PAD = 3;
	const float w = p.width, cell = w / CLIPS;
	for (int i = 0; i < CLIPS; i++) {
		const float cx = (i + 0.5f) * cell;
		p.label({cx, 6}, std::to_string(i + 1), SIZE, s.playing[i] ? t.ring : s.filled[i] ? t.field : t.guide);
		if (i == s.selected)
			p.line({cx - 6, 11}, {cx + 6, 11}, t.ring, 0.8f);
	}
	p.label({PAD, 18}, s.line, SMALL, s.alert ? t.ring : t.field * 0.7f, -1);
	if (s.seconds > 0)
		p.label({w - PAD, 18}, (s.loop ? "LOOP " : "") + clock(s.seconds), SMALL, t.field * 0.7f, 1);

	const float x0 = PAD, x1 = w - PAD, y0 = 24, y1 = p.height - PAD;
	auto X = [&](float u) { return x0 + (x1 - x0) * u; };
	auto Y = [&](float v) { return y1 - (y1 - y0) * v; };
	p.stroke({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, t.guide * 0.6f, 0.5f, true);
	if (s.curves.empty()) {
		p.label({(x0 + x1) / 2, (y0 + y1) / 2}, s.seconds > 0 ? "NO LANES MATCH" : "EMPTY", SIZE, t.guide);
		return;
	}
	// Each curve dim across the whole clip, and bright within the crop, whose edges are the handles.
	for (const auto& curve : s.curves) {
		std::vector<kit::Vec2f> all, kept;
		for (size_t i = 0; i < curve.size(); i++) {
			const kit::Vec2f& q = curve[i];
			all.push_back({X(q.x), Y(q.y)});
			if (q.x >= s.start && q.x <= s.end) {
				if (kept.empty() && i > 0)
					kept.push_back({X(s.start), Y(curve[i - 1].y)});
				kept.push_back({X(q.x), Y(q.y)});
			}
		}
		if (!kept.empty())
			kept.push_back({X(s.end), kept.back().y});
		p.stroke(all, t.guide * 0.7f, 0.6f);
		p.stroke(kept, t.field * 0.9f, 0.8f);
	}
	for (float u : {s.start, s.end})
		p.line({X(u), y0}, {X(u), y1}, t.ring, 0.8f);
	if (!std::isnan(s.head))
		p.line({X(s.head), y0}, {X(s.head), y1}, t.field, 1.2f);
}

} // namespace reel

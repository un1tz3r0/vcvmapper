#pragma once
#include <algorithm>
#include "../kit/Painter.hpp"

// Pin's screen, independent of Rack so tools/mockup can draw it too.
namespace pin {

struct ViewState {
	std::vector<std::string> message; // centred lines shown instead of the plot, when there is nothing to plot
	bool alert = false;                // draw the message in the highlight colour (learning)
	std::string module, name, value, tag;
	std::vector<float> trace; // target value history, oldest first, 0..1
	float base = -1;          // the hand position, drawn as a line when 0..1
};

// Share Tech Mono is monospaced, so text is cut to a character count rather than measured.
inline std::string clip(const std::string& s, float width, float size) {
	return s.substr(0, std::max(1, int(width / (0.52f * size))));
}

inline void paint(kit::Painter& p, const kit::Theme& t, const ViewState& s) {
	constexpr float PAD = 3, SIZE = 8;
	const float w = p.width - 2 * PAD, h = p.height;

	if (!s.message.empty()) {
		float y = h / 2 - 5.f * (s.message.size() - 1);
		for (const std::string& line : s.message) {
			p.label({p.width / 2, y}, line, SIZE, s.alert ? t.ring : t.field);
			y += 10;
		}
		return;
	}

	p.label({PAD, 8}, clip(s.module, w, SIZE), SIZE, t.field, -1);
	p.label({PAD, 17}, clip(s.name, w, SIZE), SIZE, t.ring, -1);

	const float y0 = 24, y1 = h - 22;
	const auto Y = [&](float v) { return y1 - std::clamp(v, 0.f, 1.f) * (y1 - y0); };
	p.stroke({{PAD, y0}, {PAD + w, y0}, {PAD + w, y1}, {PAD, y1}}, t.guide, 0.5f, true);
	if (s.base >= 0)
		p.line({PAD, Y(s.base)}, {PAD + w, Y(s.base)}, t.ring * 0.8f, 0.6f);
	if (s.trace.size() > 1) {
		std::vector<kit::Vec2f> pts;
		for (size_t i = 0; i < s.trace.size(); ++i)
			pts.push_back({PAD + w * i / (s.trace.size() - 1), Y(s.trace[i])});
		p.stroke(pts, t.field, 1);
	}

	p.label({PAD, h - 12}, clip(s.value, w, SIZE), SIZE, t.field, -1);
	p.label({PAD, h - 4.5f}, clip(s.tag, w, 6.5f), 6.5f, t.field * 0.6f, -1);
}

} // namespace pin

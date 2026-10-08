#pragma once
#include "Theme.hpp"

namespace puppeteer {

/** Black glass in a bezel. Subclasses draw their content in drawScreen(), on Rack's light layer,
so the screen stays lit when room brightness is turned down. */
struct OledDisplay : widget::Widget {
	static constexpr float PAD = 3.f;

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.5f);
		nvgFillColor(vg, nvgRGB(0x05, 0x05, 0x07));
		nvgFill(vg);
		nvgStrokeWidth(vg, 1.5f);
		nvgStrokeColor(vg, palette().bezel);
		nvgStroke(vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			auto font = monoFont();
			if (font && font->handle >= 0) {
				nvgSave(args.vg);
				nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
				nvgFontFaceId(args.vg, font->handle);
				drawScreen(args);
				nvgRestore(args.vg);
			}
		}
		Widget::drawLayer(args, layer);
	}

	virtual void drawScreen(const DrawArgs& args) = 0;
};

/** Text with a soft bloom under it, like a lit OLED pixel. */
inline void glowText(NVGcontext* vg, float x, float y, const std::string& s, NVGcolor c) {
	nvgFontBlur(vg, 3.f);
	nvgFillColor(vg, nvgTransRGBAf(c, 0.5f));
	nvgText(vg, x, y, s.c_str(), NULL);
	nvgFontBlur(vg, 0.f);
	nvgFillColor(vg, c);
	nvgText(vg, x, y, s.c_str(), NULL);
}

/** Strokes the current path twice: a wide faint halo, then the line itself. */
inline void glowStroke(NVGcontext* vg, NVGcolor c, float width) {
	nvgLineJoin(vg, NVG_ROUND);
	nvgStrokeColor(vg, nvgTransRGBAf(c, 0.22f));
	nvgStrokeWidth(vg, width * 3.5f);
	nvgStroke(vg);
	nvgStrokeColor(vg, c);
	nvgStrokeWidth(vg, width);
	nvgStroke(vg);
}

/** Truncates to the width available, OLED style (no ellipsis). */
inline std::string fit(NVGcontext* vg, std::string s, float width) {
	float bounds[4];
	while (s.size() > 1 && nvgTextBounds(vg, 0, 0, s.c_str(), NULL, bounds) > width)
		s.pop_back();
	return s;
}

} // namespace puppeteer

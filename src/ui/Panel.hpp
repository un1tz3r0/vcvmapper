#pragma once
#include "Theme.hpp"

namespace puppeteer {

/** A panel drawn in code instead of SVG, so labels follow the light/dark setting and need no text-to-path step. */
struct PanelWidget : widget::Widget {
	struct Label {
		Vec pos;
		std::string text;
		bool onIsland;
	};

	std::string title;
	std::vector<Label> labels;
	std::vector<math::Rect> islands;

	PanelWidget(int hp, std::string title) : title(std::move(title)) {
		box.size = Vec(hp * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
	}

	PanelWidget* label(Vec mm, std::string text, bool onIsland = false) {
		labels.push_back({mm2px(mm), std::move(text), onIsland});
		return this;
	}

	/** A dark rounded area, like the output strip on orbit. */
	PanelWidget* island(Vec mm, Vec sizeMm) {
		islands.push_back(math::Rect(mm2px(mm), mm2px(sizeMm)));
		return this;
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		const Palette p = palette();

		nvgBeginPath(vg);
		nvgRect(vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f);
		nvgFillColor(vg, p.face);
		nvgFill(vg);
		nvgStrokeWidth(vg, 1.f);
		nvgStrokeColor(vg, p.edge);
		nvgStroke(vg);

		for (const math::Rect& r : islands) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, 4.f);
			nvgFillColor(vg, p.island);
			nvgFill(vg);
		}

		auto font = labelFont();
		if (!font || font->handle < 0)
			return;
		nvgFontFaceId(vg, font->handle);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);

		nvgFontSize(vg, 14.f);
		nvgTextLetterSpacing(vg, 0.f);
		nvgFillColor(vg, p.ink);
		nvgText(vg, box.size.x / 2, mm2px(9.f), title.c_str(), NULL);

		nvgFontSize(vg, 6.5f);
		nvgTextLetterSpacing(vg, 0.6f);
		for (const Label& l : labels) {
			nvgFillColor(vg, l.onIsland ? p.islandInk : p.ink);
			nvgText(vg, l.pos.x, l.pos.y, l.text.c_str(), NULL);
		}

		nvgFontSize(vg, 6.f);
		nvgTextLetterSpacing(vg, 0.3f);
		nvgFillColor(vg, p.quiet);
		// Above the bottom screw row, which a centred brand would hit on narrow panels.
		nvgText(vg, box.size.x / 2, box.size.y - mm2px(6.f), "un1tz3r0", NULL);
	}
};

} // namespace puppeteer

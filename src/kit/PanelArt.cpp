#include <map>
#include "PanelArt.hpp"
#include "Nvg.hpp"

namespace kit {

namespace {

struct Canvas : widget::Widget {
	const PanelArt* art;
	void draw(const DrawArgs& args) override { art->paint(args.vg); }
};

Vec mm(kit::Vec2f v) { return mm2px(Vec(v.x, v.y)); }

std::shared_ptr<window::Svg> svgFrom(const std::string& text) {
	static std::map<std::string, std::shared_ptr<window::Svg>> cache;
	std::shared_ptr<window::Svg>& svg = cache[text];
	if (!svg) {
		svg = std::make_shared<window::Svg>();
		svg->loadString(text);
	}
	return svg;
}

// The panels' coloured knob, drawn by kit::knobSvg.
struct CapKnob : RoundKnob {
	void paint(float size, kit::Rgba cap) {
		setSvg(svgFrom(kit::knobSvg(size, cap, true)));
		bg->setSvg(svgFrom(kit::knobSvg(size, cap, false)));
	}
};

} // namespace

PanelArt::PanelArt(kit::Panel panel) : panel(std::move(panel)) {
	box.size = mm({this->panel.width, this->panel.height});
	auto* canvas = new Canvas;
	canvas->art = this;
	canvas->box.size = box.size;
	addChild(canvas);
}

void PanelArt::step() {
	if (dark != settings::preferDarkPanels) {
		dark = settings::preferDarkPanels;
		setDirty();
	}
	FramebufferWidget::step();
}

void PanelArt::paint(NVGcontext* vg) const {
	const kit::PanelColors& c = dark ? kit::DARK_PANEL : kit::LIGHT_PANEL;
	const auto rect = [&](kit::Vec2f from, kit::Vec2f to, float radius, kit::Rgba fill) {
		const Vec a = mm(from), b = mm(to);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, a.x, a.y, b.x - a.x, b.y - a.y, mm2px(radius));
		nvgFillColor(vg, nvg(fill));
		nvgFill(vg);
	};
	rect({0, 0}, {panel.width, panel.height}, 0, c.panel);
	const float bezel = kit::Panel::BEZEL;
	rect({panel.screen.min.x - bezel, panel.screen.min.y - bezel}, {panel.screen.max.x + bezel, panel.screen.max.y + bezel}, 1.6f, c.bezel);
	for (const kit::Panel::Rect& r : panel.boxes)
		rect(r.min, r.max, 1.6f, c.box);

	nvgStrokeColor(vg, nvg(c.wire));
	nvgStrokeWidth(vg, mm2px(0.25f));
	for (const kit::Panel::Wire& w : panel.wires) {
		const Vec a = mm(w.from), b = mm(w.to);
		nvgBeginPath(vg);
		nvgMoveTo(vg, a.x, a.y);
		nvgLineTo(vg, b.x, b.y);
		nvgStroke(vg);
	}

	std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
	if (!font || font->handle < 0)
		return;
	nvgFontFaceId(vg, font->handle);
	nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	for (const kit::Panel::Text& t : panel.texts) {
		const Vec at = mm(t.at);
		nvgFontSize(vg, mm2px(kit::Panel::FONT_SIZE[t.style]));
		nvgFillColor(vg, nvg(c.text(t.style)));
		nvgText(vg, at.x, at.y, t.text.c_str(), nullptr);
	}
}

void addPanel(ModuleWidget* widget, const kit::Panel& panel) {
	Module* module = widget->module;
	widget->setPanel(new PanelArt(panel));
	for (kit::Vec2f s : panel.screws)
		widget->addChild(createWidgetCentered<ThemedScrew>(mm(s)));

	for (const kit::Panel::Control& c : panel.controls) {
		const Vec at = mm(c.at);
		switch (c.kind) {
			case kit::Panel::KNOB:
			case kit::Panel::SELECTOR: {
				auto* knob = createParam<CapKnob>(Vec(), module, c.id);
				knob->paint(c.kind == kit::Panel::KNOB ? kit::Panel::KNOB_SIZE : kit::Panel::SELECTOR_SIZE, c.color);
				knob->box.pos = at.minus(knob->box.size.div(2));
				widget->addParam(knob);
				break;
			}
			case kit::Panel::TRIMPOT: widget->addParam(createParamCentered<Trimpot>(at, module, c.id)); break;
			case kit::Panel::BUTTON: widget->addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(at, module, c.id, c.light)); break;
			case kit::Panel::INPUT: widget->addInput(createInputCentered<ThemedPJ301MPort>(at, module, c.id)); break;
			case kit::Panel::OUTPUT: widget->addOutput(createOutputCentered<ThemedPJ301MPort>(at, module, c.id)); break;
		}
	}
}

} // namespace kit

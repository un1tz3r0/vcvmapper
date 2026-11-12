#pragma once
#include "../kit/Panel.hpp"

// Strings' ids and panel, shared by the module and tools/mockup.
namespace strings {

constexpr int CHANNELS = 8;

enum ParamId { LEARN_PARAM, EDIT_PARAM, ITEM_PARAM, VALUE_PARAM, PARAMS_LEN };
enum InputId { CV_INPUT, INPUTS_LEN = CV_INPUT + CHANNELS };
enum OutputId { CV_OUTPUT, OUTPUTS_LEN = CV_OUTPUT + CHANNELS };
enum LightId { LEARN_LIGHT, EDIT_LIGHT, LIGHTS_LEN };

// The screen is a table: a row per channel between its two jacks, and a footer over the two knobs that says what
// each one does right now.
constexpr float WIDTH = 71.12f, IN_X = 6.6f, OUT_X = WIDTH - IN_X;
constexpr float ROW = 10.5f, FOOTER = 5.f;
constexpr kit::Panel::Rect GLASS{{13.4f, 14.f}, {WIDTH - 13.4f, 14.f + CHANNELS * ROW + FOOTER}};
constexpr float ITEM_X = 40.f, VALUE_X = 55.f, CONTROL_Y = 112.5f;

inline float rowY(int i) {
	return GLASS.min.y + (i + 0.5f) * ROW;
}

inline kit::Panel panel() {
	kit::Panel p{WIDTH, 128.5f, GLASS};
	p.text({WIDTH / 2, 8.4f}, "strings", kit::Panel::TITLE);
	p.text({WIDTH / 2, 122.6f}, "un1tz3r0", kit::Panel::BRAND);
	p.text({IN_X, 11.4f}, "IN");
	p.text({OUT_X, 11.4f}, "OUT");

	const float bezel = kit::Panel::BEZEL;
	for (int i = 0; i < CHANNELS; i++) {
		const float y = rowY(i);
		p.controls.push_back({kit::Panel::INPUT, CV_INPUT + i, {IN_X, y}, {}});
		p.controls.push_back({kit::Panel::OUTPUT, CV_OUTPUT + i, {OUT_X, y}, {}});
		p.wires.push_back({{IN_X + 4.2f, y}, {GLASS.min.x - bezel, y}});
		p.wires.push_back({{GLASS.max.x + bezel, y}, {OUT_X - 4.2f, y}});
	}

	p.button(LEARN_PARAM, LEARN_LIGHT, {10.f, CONTROL_Y}, "LEARN");
	p.button(EDIT_PARAM, EDIT_LIGHT, {24.f, CONTROL_Y}, "EDIT");
	const kit::Rgba caps[] = {kit::hex(0x9fd8c8), kit::hex(0xf2c979)};
	const float xs[] = {ITEM_X, VALUE_X};
	for (int i = 0; i < 2; i++) {
		p.controls.push_back({kit::Panel::KNOB, ITEM_PARAM + i, {xs[i], CONTROL_Y}, caps[i]});
		p.wires.push_back({{xs[i], GLASS.max.y + bezel}, {xs[i], CONTROL_Y - kit::Panel::KNOB_SIZE / 2}});
	}
	return p;
}

} // namespace strings

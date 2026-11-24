#pragma once
#include "../kit/Panel.hpp"

// Stage's ids and panel, shared by the module and tools/mockup.
namespace stage {

constexpr int JACKS = 8, MACROS = 8, BINDINGS = 64, SOFT = 4, KNOBS = 8;
enum Key { UP, DOWN, LEFT, RIGHT, SELECT, BACK, SHIFT, KEYS };

enum ParamId {
	PAGE_PARAM,
	ACTION_PARAM = PAGE_PARAM + SOFT,
	KNOB_PARAM = ACTION_PARAM + SOFT, // 0-3 under the screen, 4-7 beside it
	KEY_PARAM = KNOB_PARAM + KNOBS,
	PARAMS_LEN = KEY_PARAM + KEYS
};
enum InputId { CV_INPUT, INPUTS_LEN = CV_INPUT + JACKS };
enum OutputId { CV_OUTPUT, OUTPUTS_LEN = CV_OUTPUT + JACKS };
enum LightId { PAGE_LIGHT, ACTION_LIGHT = PAGE_LIGHT + SOFT, KEY_LIGHT = ACTION_LIGHT + SOFT, LIGHTS_LEN = KEY_LIGHT + KEYS };

// Soft buttons and knobs sit on the screen's edges at quarter points, where the screen labels them.
constexpr float WIDTH = 152.4f;
constexpr kit::Panel::Rect GLASS{{18.f, 20.f}, {112.f, 84.f}};
constexpr float LEFT_X = 9.f, TOP_Y = 14.5f, RIGHT_X = 125.f, BOTTOM_Y = 95.f, SIDE_X = 145.f;
constexpr kit::Vec2f PAD{137.f, 90.f};
constexpr float JACK_Y = 117.5f, PITCH = 8.2f, IN_X = 10.f, OUT_X = 82.f;

inline float column(int i) {
	return GLASS.min.x + (i + 0.5f) * (GLASS.max.x - GLASS.min.x) / SOFT;
}
inline float row(int i) {
	return GLASS.min.y + (i + 0.5f) * (GLASS.max.y - GLASS.min.y) / SOFT;
}

inline kit::Panel panel() {
	using P = kit::Panel;
	P p{WIDTH, 128.5f, GLASS};
	p.text({SIDE_X - 9, 8.4f}, "stage", P::TITLE);
	p.text({SIDE_X - 9, 13.6f}, "un1tz3r0", P::BRAND);
	const float bezel = P::BEZEL, half = P::KNOB_SIZE / 2;
	const kit::Rgba caps[] = {kit::hex(0x9fd8c8), kit::hex(0xf2c979), kit::hex(0xe8a3a3), kit::hex(0xb7b0e8)};
	for (int i = 0; i < SOFT; i++) {
		p.controls.push_back({P::BUTTON, PAGE_PARAM + i, {column(i), TOP_Y}, {}, PAGE_LIGHT + i});
		p.controls.push_back({P::BUTTON, ACTION_PARAM + i, {LEFT_X, row(i)}, {}, ACTION_LIGHT + i});
		p.wires.push_back({{LEFT_X + 3.8f, row(i)}, {GLASS.min.x - bezel, row(i)}});
		p.controls.push_back({P::KNOB, KNOB_PARAM + i, {column(i), BOTTOM_Y}, caps[i]});
		p.wires.push_back({{column(i), GLASS.max.y + bezel}, {column(i), BOTTOM_Y - half}});
		p.controls.push_back({P::KNOB, KNOB_PARAM + SOFT + i, {RIGHT_X, row(i)}, caps[i]});
		p.wires.push_back({{GLASS.max.x + bezel, row(i)}, {RIGHT_X - half, row(i)}});
	}
	const kit::Vec2f pad[] = {{0, -7.5f}, {0, 7.5f}, {-7.5f, 0}, {7.5f, 0}, {0, 0}};
	for (int k = UP; k <= SELECT; k++)
		p.controls.push_back({P::BUTTON, KEY_PARAM + k, {PAD.x + pad[k].x, PAD.y + pad[k].y}, {}, KEY_LIGHT + k});
	p.button(KEY_PARAM + BACK, KEY_LIGHT + BACK, {SIDE_X, row(1)}, "BACK");
	p.button(KEY_PARAM + SHIFT, KEY_LIGHT + SHIFT, {SIDE_X, row(2) + 2}, "SHIFT");

	p.text({IN_X + 3.5f * PITCH, JACK_Y - 11.5f}, "INPUTS");
	p.boxes.push_back({{OUT_X - 4.6f, JACK_Y - 15.5f}, {OUT_X + 7 * PITCH + 4.6f, JACK_Y + 6.5f}});
	p.text({OUT_X + 3.5f * PITCH, JACK_Y - 11.5f}, "OUTPUTS", P::BOXED);
	const char* numbers[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
	for (int i = 0; i < JACKS; i++) {
		p.jack(P::INPUT, CV_INPUT + i, {IN_X + i * PITCH, JACK_Y}, numbers[i]);
		p.jack(P::OUTPUT, CV_OUTPUT + i, {OUT_X + i * PITCH, JACK_Y}, numbers[i], P::BOXED);
	}
	return p;
}

} // namespace stage

// Crew, the jack expander: eight more inputs, each with a readback output, to Stage's right. Up to CREWS chain on.
namespace crew {

constexpr int CREWS = 3, JACKS = stage::JACKS;
enum ParamId { PARAMS_LEN };
enum InputId { CV_INPUT, INPUTS_LEN = CV_INPUT + JACKS };
enum OutputId { CV_OUTPUT, OUTPUTS_LEN = CV_OUTPUT + JACKS };
enum LightId { LIGHTS_LEN };

constexpr float WIDTH = 30.48f, IN_X = 8.f, OUT_X = 22.f;
constexpr kit::Panel::Rect GLASS{{3.f, 13.f}, {WIDTH - 3.f, 21.f}};

inline float rowY(int i) {
	return 33.f + i * 11.4f;
}

inline kit::Panel panel() {
	using P = kit::Panel;
	P p{WIDTH, 128.5f, GLASS};
	p.text({WIDTH / 2, 8.4f}, "crew", P::TITLE);
	p.text({IN_X, 26.f}, "IN");
	p.boxes.push_back({{OUT_X - 5.5f, 22.8f}, {OUT_X + 5.5f, rowY(JACKS - 1) + 5.f}});
	p.text({OUT_X, 26.f}, "OUT", P::BOXED);
	const char* numbers[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
	for (int i = 0; i < JACKS; i++) {
		p.controls.push_back({P::INPUT, CV_INPUT + i, {IN_X, rowY(i)}, {}});
		p.controls.push_back({P::OUTPUT, CV_OUTPUT + i, {OUT_X, rowY(i)}, {}});
		p.text({(IN_X + OUT_X) / 2 - 1.2f, rowY(i)}, numbers[i]);
	}
	p.text({WIDTH / 2, 122.6f}, "un1tz3r0", P::BRAND);
	return p;
}

} // namespace crew

namespace stage {

/** Where a binding's CV comes from: -1 nowhere, 0-7 Stage's own inputs, 8-31 those of the Crews chained to its
right (A, B and C), 32-39 a macro. */
constexpr int NO_SOURCE = -1, PORTS = JACKS * (1 + crew::CREWS), MACRO_SOURCE = PORTS, SOURCES = PORTS + MACROS;

} // namespace stage


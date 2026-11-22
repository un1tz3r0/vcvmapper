#pragma once
#include "../kit/Panel.hpp"

// Tether's ids and panel, shared by the module and tools/mockup.
namespace tether {

enum ParamId { LEARN_PARAM, DEPTH_PARAM, PARAMS_LEN };
enum InputId { IN_INPUT, CV_INPUT, INPUTS_LEN };
enum OutputId { THRU_OUTPUT, OUTPUTS_LEN };
enum LightId { LEARN_LIGHT, LIGHTS_LEN };

constexpr float WIDTH = 20.32f, X = WIDTH / 2;

inline kit::Panel panel() {
	kit::Panel p{WIDTH, 128.5f, {{2.2f, 13.f}, {WIDTH - 2.2f, 31.f}}};
	p.screws = {{7.62f, 2.54f}, {12.7f, 125.96f}};
	p.text({X, 8.4f}, "tether", kit::Panel::TITLE);
	p.text({X, 121.4f}, "un1tz3r0", kit::Panel::BRAND);
	p.button(LEARN_PARAM, LEARN_LIGHT, {X, 42.f}, "LEARN");
	p.controls.push_back({kit::Panel::TRIMPOT, DEPTH_PARAM, {X, 56.f}, {}});
	p.text({X, 51.1f}, "DEPTH");
	p.jack(kit::Panel::INPUT, CV_INPUT, {X, 71.f}, "CV");
	p.jack(kit::Panel::INPUT, IN_INPUT, {X, 89.f}, "IN");
	// IN runs straight through to THRU, and THRU's cord picks the target.
	p.wires.push_back({{X, 93.2f}, {X, 101.f}});
	p.boxes.push_back({{3.16f, 101.f}, {WIDTH - 3.16f, 118.5f}});
	p.jack(kit::Panel::OUTPUT, THRU_OUTPUT, {X, 112.5f}, "THRU", kit::Panel::BOXED);
	return p;
}

} // namespace tether

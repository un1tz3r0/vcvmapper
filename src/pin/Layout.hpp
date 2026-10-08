#pragma once
#include "../kit/Panel.hpp"

// Pin's ids and panel, shared by the module and tools/mockup.
namespace pin {

enum ParamId { LEARN_PARAM, DEPTH_PARAM, PARAMS_LEN };
enum InputId { CV_INPUT, INPUTS_LEN };
enum OutputId { CV_OUTPUT, OUTPUTS_LEN };
enum LightId { LEARN_LIGHT, LIGHTS_LEN };

inline kit::Panel panel() {
	kit::Panel p{20.32f, 128.5f, {{2.2f, 13.f}, {18.12f, 52.f}}};
	// Two screws: four would crowd the title and brand on 4 HP.
	p.screws = {{7.62f, 2.54f}, {12.7f, 125.96f}};
	p.text({10.16f, 8.4f}, "pin", kit::Panel::TITLE);
	p.text({10.16f, 121.4f}, "un1tz3r0", kit::Panel::BRAND);

	p.button(LEARN_PARAM, LEARN_LIGHT, {10.16f, 64.f}, "LEARN");
	p.controls.push_back({kit::Panel::TRIMPOT, DEPTH_PARAM, {10.16f, 78.f}, {}});
	p.text({10.16f, 73.1f}, "DEPTH");
	p.jack(kit::Panel::INPUT, CV_INPUT, {10.16f, 92.5f}, "IN");
	p.boxes.push_back({{3.16f, 101.f}, {17.16f, 118.5f}});
	p.jack(kit::Panel::OUTPUT, CV_OUTPUT, {10.16f, 112.5f}, "OUT", kit::Panel::BOXED);
	return p;
}

} // namespace pin

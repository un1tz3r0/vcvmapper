#pragma once
#include "../kit/Panel.hpp"

// Ids and panels of the group modules, shared by the modules and tools/mockup: Troupe opens a group, Wing closes it
// and Tag names one neighbour.
namespace troupe {

constexpr int MACROS = 4;

enum ParamId { LEARN_PARAM, MACRO_PARAM, PARAMS_LEN = MACRO_PARAM + MACROS };
enum InputId { INPUTS_LEN };
enum OutputId { OUTPUTS_LEN };
enum LightId { LEARN_LIGHT, LIGHTS_LEN };

constexpr float WIDTH = 20.32f;
constexpr kit::Panel::Rect GLASS{{2.2f, 13.f}, {WIDTH - 2.2f, 40.f}};

inline float macroY(int i) {
	return 62.f + i * 15.5f;
}

inline kit::Panel panel() {
	kit::Panel p{WIDTH, 128.5f, GLASS};
	p.screws = {{7.62f, 2.54f}, {12.7f, 125.96f}};
	p.text({WIDTH / 2, 8.4f}, "troupe", kit::Panel::TITLE);
	p.text({WIDTH / 2, 121.4f}, "un1tz3r0", kit::Panel::BRAND);
	p.button(LEARN_PARAM, LEARN_LIGHT, {WIDTH / 2, 50.5f}, "LEARN");
	const kit::Rgba caps[] = {kit::hex(0x9fd8c8), kit::hex(0xf2c979), kit::hex(0xe8a3a3), kit::hex(0xb7b0e8)};
	const char* numbers[] = {"1", "2", "3", "4"};
	for (int i = 0; i < MACROS; i++) {
		p.controls.push_back({kit::Panel::KNOB, MACRO_PARAM + i, {11.4f, macroY(i)}, caps[i]});
		p.text({2.9f, macroY(i)}, numbers[i]);
	}
	return p;
}

} // namespace troupe

namespace tag {

enum ParamId { SIDE_PARAM, PARAMS_LEN };
enum InputId { INPUTS_LEN };
enum OutputId { OUTPUTS_LEN };
enum LightId { SIDE_LIGHT, LIGHTS_LEN };

constexpr float WIDTH = 10.16f;

inline kit::Panel panel() {
	kit::Panel p{WIDTH, 128.5f, {{1.6f, 13.f}, {WIDTH - 1.6f, 100.f}}};
	p.screws = {{WIDTH / 2, 2.54f}, {WIDTH / 2, 125.96f}};
	p.text({WIDTH / 2, 8.4f}, "tag", kit::Panel::TITLE);
	p.text({WIDTH / 2, 121.4f}, "un1tz3r0", kit::Panel::BRAND);
	p.button(SIDE_PARAM, SIDE_LIGHT, {WIDTH / 2, 111.f}, "SIDE", kit::Panel::LATCH);
	return p;
}

} // namespace tag

namespace wing {

constexpr float WIDTH = 5.08f;

// No controls: a bracket closing the group.
inline kit::Panel panel() {
	kit::Panel p{WIDTH, 128.5f, {}};
	p.screws = {{WIDTH / 2, 2.54f}, {WIDTH / 2, 125.96f}};
	const kit::Vec2f a{1.3f, 9.f}, b{3.3f, 9.f}, c{3.3f, 119.5f}, d{1.3f, 119.5f};
	p.wires = {{a, b}, {b, c}, {c, d}};
	return p;
}

} // namespace wing

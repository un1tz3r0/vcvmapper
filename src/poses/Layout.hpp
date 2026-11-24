#pragma once
#include "../kit/Panel.hpp"

// Poses' ids and panel, shared by the module and tools/mockup.
namespace poses {

constexpr int POSES = 16, BUTTONS = 8;

enum ParamId { POSE_PARAM, CAPTURE_PARAM = POSE_PARAM + BUTTONS, A_PARAM, B_PARAM, FADE_PARAM, GLIDE_PARAM, LEARN_PARAM, PARAMS_LEN };
enum InputId { FADE_INPUT, SELECT_INPUT, RECALL_INPUT, CAPTURE_INPUT, INPUTS_LEN };
enum OutputId { OUTPUTS_LEN };
enum LightId { POSE_LIGHT, CAPTURE_LIGHT = POSE_LIGHT + BUTTONS, A_LIGHT, B_LIGHT, LEARN_LIGHT, LIGHTS_LEN };

constexpr float WIDTH = 60.96f, X = WIDTH / 2;

inline kit::Panel panel() {
	using P = kit::Panel;
	P p{WIDTH, 128.5f, {{3.f, 13.f}, {WIDTH - 3.f, 52.f}}};
	p.text({X, 8.4f}, "poses", P::TITLE);
	p.text({X, 124.4f}, "un1tz3r0", P::BRAND);
	const char* numbers[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
	for (int i = 0; i < BUTTONS; i++)
		p.button(POSE_PARAM + i, POSE_LIGHT + i, {10.5f + (i % 4) * 13.32f, 63.f + (i / 4) * 13.f}, numbers[i]);
	// The crossfader between the two ends it blends.
	p.button(A_PARAM, A_LIGHT, {10.5f, 93.f}, "A");
	p.button(B_PARAM, B_LIGHT, {WIDTH - 10.5f, 93.f}, "B");
	p.controls.push_back({P::KNOB, FADE_PARAM, {X, 93.f}, kit::hex(0x9fd8c8)});
	p.text({X, P::labelAbove({X, 93.f})}, "FADE");
	p.wires.push_back({{15.f, 93.f}, {X - P::KNOB_SIZE / 2, 93.f}});
	p.wires.push_back({{X + P::KNOB_SIZE / 2, 93.f}, {WIDTH - 15.f, 93.f}});
	p.button(CAPTURE_PARAM, CAPTURE_LIGHT, {10.5f, 106.f}, "CAPTURE");
	p.button(LEARN_PARAM, LEARN_LIGHT, {X, 106.f}, "LEARN");
	p.controls.push_back({P::TRIMPOT, GLIDE_PARAM, {WIDTH - 10.5f, 106.f}, {}});
	p.text({WIDTH - 10.5f, 101.1f}, "GLIDE");
	const char* jacks[] = {"FADE", "SELECT", "RECALL", "CAPTURE"};
	for (int i = 0; i < INPUTS_LEN; i++)
		p.jack(P::INPUT, i, {9.f + i * 14.32f, 117.f}, jacks[i]);
	return p;
}

} // namespace poses

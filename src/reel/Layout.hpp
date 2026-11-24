#pragma once
#include "../kit/Panel.hpp"

// Reel's ids and panel, shared by the module and tools/mockup.
namespace reel {

constexpr int CLIPS = 8;

enum ParamId { CLIP_PARAM, ARM_PARAM = CLIP_PARAM + CLIPS, LOOP_PARAM, LEARN_PARAM, START_PARAM, END_PARAM, SCRUB_PARAM, RATE_PARAM, PARAMS_LEN };
enum InputId { PLAY_INPUT, REC_INPUT = PLAY_INPUT + CLIPS, RATE_INPUT, INPUTS_LEN };
enum OutputId { END_OUTPUT, PLAYING_OUTPUT = END_OUTPUT + CLIPS, OUTPUTS_LEN };
enum LightId { CLIP_LIGHT, ARM_LIGHT = CLIP_LIGHT + CLIPS, LOOP_LIGHT, LEARN_LIGHT, LIGHTS_LEN };

constexpr float WIDTH = 81.28f, X = WIDTH / 2, PITCH = 8.6f;

inline float column(int i) {
	return X + (i - 3.5f) * PITCH;
}

inline kit::Panel panel() {
	using P = kit::Panel;
	P p{WIDTH, 128.5f, {{3.f, 13.f}, {WIDTH - 3.f, 52.f}}};
	p.text({X, 8.4f}, "reel", P::TITLE);
	p.text({X, 126.4f}, "un1tz3r0", P::BRAND);
	// Three encoders on the selected clip, wired up to the screen they edit, and the rate for every clip.
	const char* knobs[] = {"START", "END", "SCRUB", "RATE"};
	const kit::Rgba caps[] = {kit::hex(0x9fd8c8), kit::hex(0x9fd8c8), kit::hex(0xf2c979), kit::hex(0xb7b0e8)};
	for (int i = 0; i < 4; i++) {
		const kit::Vec2f at{12.f + i * 19.f, 65.f};
		p.controls.push_back({P::KNOB, START_PARAM + i, at, caps[i]});
		p.text({at.x, P::labelAbove(at)}, knobs[i]);
	}
	p.button(ARM_PARAM, ARM_LIGHT, {9.f, 81.f}, "ARM");
	p.button(LOOP_PARAM, LOOP_LIGHT, {21.f, 81.f}, "LOOP");
	p.button(LEARN_PARAM, LEARN_LIGHT, {33.f, 81.f}, "LEARN");
	p.jack(P::INPUT, REC_INPUT, {46.f, 81.f}, "REC");
	p.jack(P::INPUT, RATE_INPUT, {58.f, 81.f}, "RATE");
	p.boxes.push_back({{65.6f, 72.8f}, {WIDTH - 2.6f, 86.f}});
	p.jack(P::OUTPUT, PLAYING_OUTPUT, {WIDTH - 9.f, 81.f}, "PLAYING", P::BOXED);
	const char* numbers[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
	for (int i = 0; i < CLIPS; i++) {
		p.button(CLIP_PARAM + i, CLIP_LIGHT + i, {column(i), 96.f}, numbers[i]);
		p.controls.push_back({P::INPUT, PLAY_INPUT + i, {column(i), 106.5f}, {}});
		p.controls.push_back({P::OUTPUT, END_OUTPUT + i, {column(i), 118.f}, {}});
	}
	p.text({3.7f, 106.5f}, "PLAY");
	p.boxes.push_back({{column(0) - 4.3f, 112.f}, {column(7) + 4.3f, 123.f}});
	p.text({3.4f, 118.f}, "END");
	return p;
}

} // namespace reel

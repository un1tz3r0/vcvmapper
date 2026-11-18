#pragma once
#include <cstdio>
#include <iterator>
#include <string>
#include "Shape.hpp"

struct json_t;

// A binding's settings and the list of them that screens edit, independent of Rack.
namespace puppeteer {

/** How a binding's CV and a hand on the same knob get along. */
enum class Mode { Modulate, Overwrite, PickUp };

struct Settings {
	Mode mode = Mode::Modulate;
	Shape shape;
	float depth = 1.f;    // -1..1, scales the CV around 0 V; modules with a depth knob leave this at 1
	float slew = 0.f;     // ms
	int interval = 32;    // frames between param writes
	bool autoRate = true; // write every frame while the CV runs at audio rate
	float hold = 1.f;     // ms a button stays down, however short the gate

	json_t* toJson() const;
	void fromJson(json_t* rootJ);
};

/** One setting as a screen shows and edits it: `turn` moves it by encoder clicks. */
struct Field {
	const char* name;
	std::string (*show)(const Settings&);
	void (*turn)(Settings&, int clicks);
};

namespace fields {

inline std::string text(const char* format, double v) {
	char s[32];
	std::snprintf(s, sizeof s, format, v);
	return s;
}
inline float nudge(float v, int clicks, float step, float lo, float hi) {
	return std::clamp(std::round(v / step + clicks) * step, lo, hi);
}
/** Moves `v` along a list of allowed values. */
template <typename T, size_t N>
T pick(const T (&list)[N], T v, int clicks) {
	int i = 0;
	for (int j = 0; j < int(N); j++)
		if (std::abs(list[j] - v) < std::abs(list[i] - v))
			i = j;
	return list[std::clamp(i + clicks, 0, int(N) - 1)];
}

constexpr int STEPS[] = {0, 2, 3, 4, 5, 6, 8, 12, 16, 24, 32};
constexpr int INTERVALS[] = {1, 4, 8, 16, 32, 64, 128};
constexpr float SLEWS[] = {0, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000};
constexpr float HOLDS[] = {0, 0.5f, 1, 2, 5, 10, 20, 50};
constexpr const char* MODES[] = {"MODULATE", "OVERWRITE", "PICK UP"};

inline const Field ALL[] = {
	{"DEPTH", [](const Settings& s) { return text("%.0f%%", s.depth * 100); },
		[](Settings& s, int n) { s.depth = nudge(s.depth, n, 0.02f, -1, 1); }},
	{"HAND+CV", [](const Settings& s) { return std::string(MODES[int(s.mode)]); },
		[](Settings& s, int n) { s.mode = Mode(std::clamp(int(s.mode) + n, 0, 2)); }},
	{"CV RANGE", [](const Settings& s) { return std::string(s.shape.bipolar ? "-5..5 V" : "0..10 V"); },
		[](Settings& s, int n) { s.shape.bipolar = n < 0; }},
	{"KNOB FROM", [](const Settings& s) { return text("%.0f%%", s.shape.lo * 100); },
		[](Settings& s, int n) { s.shape.lo = nudge(s.shape.lo, n, 0.02f, 0, 1); }},
	{"KNOB TO", [](const Settings& s) { return text("%.0f%%", s.shape.hi * 100); },
		[](Settings& s, int n) { s.shape.hi = nudge(s.shape.hi, n, 0.02f, 0, 1); }},
	{"CURVE", [](const Settings& s) { return text("%+.2f", s.shape.curve); },
		[](Settings& s, int n) { s.shape.curve = nudge(s.shape.curve, n, 0.05f, -1, 1); }},
	{"INVERT", [](const Settings& s) { return std::string(s.shape.invert ? "ON" : "OFF"); },
		[](Settings& s, int n) { s.shape.invert = n > 0; }},
	{"STEPS", [](const Settings& s) { return s.shape.steps ? std::to_string(s.shape.steps) : std::string("OFF"); },
		[](Settings& s, int n) { s.shape.steps = pick(STEPS, s.shape.steps, n); }},
	{"SLEW", [](const Settings& s) { return text("%g ms", s.slew); },
		[](Settings& s, int n) { s.slew = pick(SLEWS, s.slew, n); }},
	{"AUDIO RATE", [](const Settings& s) { return std::string(s.autoRate ? "AUTO" : "OFF"); },
		[](Settings& s, int n) { s.autoRate = n > 0; }},
	{"WRITE EVERY", [](const Settings& s) { return std::to_string(s.interval); },
		[](Settings& s, int n) { s.interval = pick(INTERVALS, s.interval, n); }},
	{"BUTTON HOLD", [](const Settings& s) { return text("%g ms", s.hold); },
		[](Settings& s, int n) { s.hold = pick(HOLDS, s.hold, n); }},
};
constexpr int COUNT = int(std::size(ALL));

} // namespace fields
} // namespace puppeteer

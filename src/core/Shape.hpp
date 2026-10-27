#pragma once
#include <algorithm>
#include <cmath>

// Per-lane signal shaping, independent of Rack.
namespace puppeteer {

/** Maps CV to a position along a knob's travel (0..1) and back. */
struct Shape {
	bool bipolar = true;      // CV spans -5..5 V, else 0..10 V
	float lo = 0.f, hi = 1.f; // the part of the knob's travel that the CV span covers
	float curve = 0.f;        // -1..1: below 0 rises fast then slow, above 0 slow then fast
	bool invert = false;
	int steps = 0;            // quantize to this many positions; 0 is off

	float bottom() const {
		return bipolar ? -5.f : 0.f;
	}

	float bend(float u, bool back = false) const {
		if (curve == 0.f)
			return u;
		float e = std::exp2(2.f * curve);
		return std::pow(u, back ? 1.f / e : e);
	}

	float forward(float volts) const {
		float u = bend(std::clamp((volts - bottom()) / 10.f, 0.f, 1.f));
		if (invert)
			u = 1.f - u;
		if (steps > 1)
			u = std::round(u * (steps - 1)) / (steps - 1);
		return lo + (hi - lo) * u;
	}

	/** The voltage that `forward` would map to `x`, quantizing aside. */
	float reverse(float x) const {
		float u = hi == lo ? 0.f : std::clamp((x - lo) / (hi - lo), 0.f, 1.f);
		if (invert)
			u = 1.f - u;
		return bottom() + 10.f * bend(u, true);
	}
};

/** One-pole lag with a time constant in milliseconds. 0 ms passes through. */
struct Lag {
	float y = NAN;

	float process(float x, float ms, float dt) {
		if (ms <= 0.f || std::isnan(y))
			return y = x;
		return y += (x - y) * std::min(1.f, dt * 1000.f / ms);
	}
};

/** Tells audio-rate CV from control-rate CV by how often it crosses its own running average. */
struct RateDetector {
	static constexpr float ABOVE = 80.f, BELOW = 40.f; // Hz: switch up above one, back down below the other
	static constexpr float NOISE = 0.01f;               // V of hysteresis around the average
	float mean = NAN, elapsed = 0.f;
	int crossings = 0;
	bool high = false, audio = false;

	bool process(float v, float dt) {
		mean = std::isnan(mean) ? v : mean + (v - mean) * std::min(1.f, dt * 125.66f); // ~20 Hz
		float x = v - mean;
		if (high ? x < -NOISE : x > NOISE) {
			high = !high;
			crossings++;
		}
		if ((elapsed += dt) >= 0.05f) {
			audio = crossings / (2.f * elapsed) > (audio ? BELOW : ABOVE);
			crossings = 0;
			elapsed = 0.f;
		}
		return audio;
	}
};

/** A momentary button that follows a gate but stays down for at least `hold` seconds. */
struct Press {
	bool gate = false, down = false;
	float held = 0.f;

	bool process(float volts, float hold, float dt) {
		gate = gate ? volts > 0.f : volts >= 1.f;
		if (gate && !down) {
			down = true;
			held = 0.f;
		}
		held += dt;
		if (down && !gate && held >= hold)
			down = false;
		return down;
	}
};

} // namespace puppeteer

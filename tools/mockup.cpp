// Panel mockups as SVG, drawn by the same layout and screen code as the plugin, so the look can be checked without Rack.
// Usage: mockup [rack-dir]   writes pin-*.svg and strings-*.svg; rack-dir (Rack's source or SDK) supplies its
// jacks, screws and label font.
#include <cmath>
#include <fstream>
#include "pin/Layout.hpp"
#include "pin/View.hpp"
#include "core/Settings.hpp"
#include "strings/View.hpp"
#include "PanelSvg.hpp"
#include "SvgPainter.hpp"

using namespace kit;

static void pinMockup(const std::string& path, bool dark, int accent, const std::string& rackDir) {
	const Panel p = pin::panel();
	const Panel::Rect& g = p.screen;
	constexpr float PX = 75 / 25.4f; // Rack's pixels per millimetre
	const Theme theme = Theme::accent(accent);

	pin::ViewState s;
	s.module = "VCO";
	s.name = "Frequency";
	s.value = "+0.42 V";
	s.tag = "MOD -5..5";
	s.base = 0.55f;
	for (int i = 0; i < 64; ++i)
		s.trace.push_back(0.55f + 0.3f * std::sin(i * 0.2f) * std::cos(i * 0.045f));

	SvgPainter screen((g.max.x - g.min.x) * PX, (g.max.y - g.min.y) * PX, theme.glass);
	screen.radius = 2.5f;
	pin::paint(screen, theme, s);
	char place[160];
	std::snprintf(place, sizeof place, "x='%g' y='%g' width='%g' height='%g'", g.min.x, g.min.y, g.max.x - g.min.x, g.max.y - g.min.y);
	std::ofstream(path) << panelSvg(p, dark, 6, screen.str(place), rackDir, [](int) { return 0.5f; });
}

static void stringsMockup(const std::string& path, bool dark, int accent, bool editing, const std::string& rackDir) {
	const Panel p = strings::panel();
	const Panel::Rect& g = p.screen;
	constexpr float PX = 75 / 25.4f;
	const Theme theme = Theme::accent(accent);

	strings::ViewState s;
	s.rows[0] = {"VCO Frequency", "MOD", "", false, 0.62f, 0.5f};
	s.rows[1] = {"VCF Cutoff frequency", "PICK", "", false, 0.31f, NAN};
	s.rows[2] = {"VCO Pulse width", "MOD x3", "", false, 0.8f, 0.7f};
	s.rows[3] = {"SEQ 3 Reset", "BTN", "", false, 0.f, NAN};
	s.rows[4] = {"ADSR Gate", "LIGHT", "", false, 0.9f, NAN};
	s.rows[5] = {"", "", "TOUCH A KNOB OR LIGHT", true, NAN, NAN};
	s.rows[6] = {"Mixer Level 1", "SET", "IN USE BY OTHER MAPPER", false, NAN, NAN};
	s.selected = editing ? 0 : 5;
	s.editing = editing;
	puppeteer::Settings settings;
	if (editing) {
		s.cursor = 5;
		settings.shape.curve = 0.35f;
		for (const puppeteer::Field& f : puppeteer::fields::ALL)
			s.items.push_back({f.name, f.show(settings)});
		s.left = puppeteer::fields::ALL[s.cursor].name;
		s.right = puppeteer::fields::ALL[s.cursor].show(settings);
	}
	else {
		s.left = "CH 6";
		s.right = "DEPTH 100%";
	}

	SvgPainter screen((g.max.x - g.min.x) * PX, (g.max.y - g.min.y) * PX, theme.glass);
	screen.radius = 2.5f;
	strings::paint(screen, theme, s);
	char place[160];
	std::snprintf(place, sizeof place, "x='%g' y='%g' width='%g' height='%g'", g.min.x, g.min.y, g.max.x - g.min.x, g.max.y - g.min.y);
	std::ofstream(path) << panelSvg(p, dark, 6, screen.str(place), rackDir, [](int) { return 0.5f; });
}

int main(int argc, char** argv) {
	const std::string rack = argc > 1 ? argv[1] : "";
	pinMockup("pin-light.svg", false, 0, rack);
	pinMockup("pin-dark.svg", true, 2, rack);
	stringsMockup("strings-light.svg", false, 0, false, rack);
	stringsMockup("strings-dark.svg", true, 3, true, rack);
}

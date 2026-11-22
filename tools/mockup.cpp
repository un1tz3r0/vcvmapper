// Panel mockups as SVG, drawn by the same layout and screen code as the plugin, so the look can be checked without Rack.
// Usage: mockup [rack-dir]   writes an SVG of each panel, light and dark; rack-dir (Rack's source or SDK) supplies its
// jacks, screws and label font.
#include <cmath>
#include <fstream>
#include "pin/Layout.hpp"
#include "pin/View.hpp"
#include "core/Settings.hpp"
#include "marionette/View.hpp"
#include "groups/View.hpp"
#include "tether/Layout.hpp"
#include "tether/View.hpp"
#include "PanelSvg.hpp"
#include "SvgPainter.hpp"

using namespace kit;

/** Writes `p` with `paint` drawing its screen, if it has one. */
template <typename Paint>
static void write(const std::string& path, const Panel& p, bool dark, int accent, const std::string& rackDir, Paint paint) {
	const Panel::Rect& g = p.screen;
	constexpr float PX = 75 / 25.4f; // Rack's pixels per millimetre
	std::string glass;
	if (p.hasScreen()) {
		const Theme theme = Theme::accent(accent);
		SvgPainter screen((g.max.x - g.min.x) * PX, (g.max.y - g.min.y) * PX, theme.glass);
		screen.radius = 2.5f;
		paint(screen, theme);
		char place[160];
		std::snprintf(place, sizeof place, "x='%g' y='%g' width='%g' height='%g'", g.min.x, g.min.y, g.max.x - g.min.x, g.max.y - g.min.y);
		glass = screen.str(place);
	}
	std::ofstream(path) << panelSvg(p, dark, 6, glass, rackDir, [](int) { return 0.5f; });
}

static void pinMockup(const std::string& path, bool dark, int accent, const std::string& rackDir) {
	const Panel p = pin::panel();

	pin::ViewState s;
	s.module = "VCO";
	s.name = "Frequency";
	s.value = "+0.42 V";
	s.tag = "MOD -5..5";
	s.base = 0.55f;
	for (int i = 0; i < 64; ++i)
		s.trace.push_back(0.55f + 0.3f * std::sin(i * 0.2f) * std::cos(i * 0.045f));

	write(path, p, dark, accent, rackDir, [&](Painter& painter, const Theme& theme) { pin::paint(painter, theme, s); });
}

static void marionetteMockup(const std::string& path, bool dark, int accent, bool editing, const std::string& rackDir) {
	const Panel p = marionette::panel();

	marionette::ViewState s;
	s.rows[0] = {"VCO Frequency", "MOD", "", false, 0.62f, 0.5f};
	s.rows[1] = {"VCF Cutoff frequency", "PICK", "", false, 0.31f, NAN};
	s.rows[2] = {"VCO Pulse width", "MOD x3", "", false, 0.8f, 0.7f};
	s.rows[3] = {"SEQ 3 Reset", "BTN", "", false, 0.f, NAN};
	s.rows[4] = {"ADSR Gate", "LIGHT", "", false, 0.9f, NAN};
	s.rows[5] = {"", "", "TOUCH A KNOB OR LIGHT", true, NAN, NAN};
	s.rows[6] = {"Mixer Level 1", "SET", "IN USE BY OTHER MAPPER", false, NAN, NAN};
	s.rows[7] = {"VCA Level", "MOD x4", "WAS 4 TARGETS, NOW 3", true, NAN, NAN};
	s.selected = editing ? 0 : 5;
	s.editing = editing;
	puppeteer::Settings settings;
	if (editing) {
		s.cursor = 6;
		settings.shape.curve = 0.35f;
		settings.twins = puppeteer::Twins::Row;
		settings.spread = 0.12f;
		s.items.push_back({"PATH", "VCO@row:Freq*"});
		for (const puppeteer::Field& f : puppeteer::fields::ALL)
			s.items.push_back({f.name, f.show(settings)});
		s.left = s.items[s.cursor].first;
		s.right = s.items[s.cursor].second;
	}
	else {
		s.left = "CH 6";
		s.right = "DEPTH 100%";
	}

	write(path, p, dark, accent, rackDir, [&](Painter& painter, const Theme& theme) { marionette::paint(painter, theme, s); });
}

static void groupMockups(bool dark, int accent, const std::string& rackDir) {
	const std::string suffix = dark ? "-dark.svg" : "-light.svg";
	troupe::ViewState t;
	t.name = "VOICE 1";
	t.members = 6;
	t.selected = 1;
	t.macros[0] = {"VCF Cutoff", 0.35f};
	t.macros[1] = {"3 targets", -0.6f};
	t.macros[2] = {"ADSR Release", 0.f};
	if (dark)
		t.note = "TOUCH A KNOB", t.alert = true;
	write("troupe" + suffix, troupe::panel(), dark, accent, rackDir, [&](Painter& p, const Theme& theme) { troupe::paint(p, theme, t); });
	tag::ViewState g{"lead", dark, true};
	write("tag" + suffix, tag::panel(), dark, accent, rackDir, [&](Painter& p, const Theme& theme) { tag::paint(p, theme, g); });
	write("wing" + suffix, wing::panel(), dark, accent, rackDir, [](Painter&, const Theme&) {});
}

static void tetherMockup(bool dark, int accent, const std::string& rackDir) {
	tether::ViewState s{"VCF", "Resonance", "0.62 x2", false, 0.62f, 0.5f};
	if (dark)
		s = {"VCO", "NO Resonance", "MOVE THE CORD", true};
	write(dark ? "tether-dark.svg" : "tether-light.svg", tether::panel(), dark, accent, rackDir,
		[&](Painter& p, const Theme& theme) { tether::paint(p, theme, s); });
}

int main(int argc, char** argv) {
	const std::string rack = argc > 1 ? argv[1] : "";
	pinMockup("pin-light.svg", false, 0, rack);
	pinMockup("pin-dark.svg", true, 2, rack);
	marionetteMockup("marionette-light.svg", false, 0, false, rack);
	marionetteMockup("marionette-dark.svg", true, 3, true, rack);
	groupMockups(false, 0, rack);
	groupMockups(true, 2, rack);
	tetherMockup(false, 0, rack);
	tetherMockup(true, 3, rack);
}

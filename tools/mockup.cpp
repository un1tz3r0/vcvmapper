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
#include "stage/View.hpp"
#include "poses/View.hpp"
#include "reel/View.hpp"
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

static void stageMockup(bool dark, int accent, const std::string& rackDir) {
	using namespace stage;
	ViewState s;
	s.focused = true;
	const char* bindingActions[] = {"LEARN", "LEARN+", "CLEAR", "EDIT"}, *editActions[] = {"LEARN", "LEARN+", "PATH", "CLEAR"};
	const KnobLabel quick[] = {{"KNOB FROM", "0%"}, {"KNOB TO", "100%"}, {"CURVE", "+0.35"}, {"SPREAD", "+12%"}};
	for (int i = 0; i < SOFT; i++) {
		s.actions[i] = dark ? editActions[i] : bindingActions[i];
		s.knobs[SOFT + i] = quick[i];
	}
	if (!dark) {
		const KnobLabel bottom[] = {{"ROW", ""}, {"SOURCE", "IN2"}, {"DEPTH", "100%"}, {"MODE", "MODULATE"}};
		std::copy(std::begin(bottom), std::end(bottom), s.knobs);
		s.cursor = 1;
		s.rows = {{"1", "IN1", "VCO: Frequency", "", false, false, 0.62f, 0.5f},
		          {"2", "IN2", "4 targets", "", false, false, 0.31f, 0.4f},
		          {"3", "M1", "VCF: Cutoff", "", false, false, 0.8f, NAN},
		          {"4", "M1", "VCF: Resonance", "", false, false, 0.45f, NAN},
		          {"5", "IN5", "Mixer: Level 1", "IN USE", true, false, NAN, NAN},
		          {"6", "IN6", "", "TOUCH A KNOB", true, false, NAN, NAN},
		          {"7", "IN7", "", "", false, true, NAN, NAN},
		          {"8", "IN8", "", "", false, true, NAN, NAN},
		          {"9", "--", "", "", false, true, NAN, NAN},
		          {"10", "--", "", "", false, true, NAN, NAN},
		          {"11", "--", "", "", false, true, NAN, NAN},
		          {"12", "--", "", "", false, true, NAN, NAN}};
	}
	else {
		s.page = EDIT_PAGE;
		const KnobLabel bottom[] = {{"ITEM", ""}, {"VALUE", ""}, {"BINDING", "2"}, {"SOURCE", "IN2"}};
		std::copy(std::begin(bottom), std::end(bottom), s.knobs);
		s.cursor = 6;
		s.heading = "2 IN2 4 targets";
		puppeteer::Settings settings;
		settings.shape.curve = 0.35f;
		settings.spread = 0.12f;
		s.items.push_back({"PATH", "VCO@Voice*:Freq*"});
		for (const puppeteer::Field& f : puppeteer::fields::ALL)
			s.items.push_back({f.name, f.show(settings)});
		for (int i = 0; i < 160; i++) {
			float cv = std::sin(i * 0.09f) * 0.8f;
			s.source.push_back(cv);
			s.result.push_back(0.4f + 0.3f * std::sin((i - 4) * 0.09f));
		}
	}
	write(dark ? "stage-dark.svg" : "stage-light.svg", panel(), dark, accent, rackDir, [&](Painter& p, const Theme& theme) { paint(p, theme, s); });
}

static void posesMockup(bool dark, int accent, const std::string& rackDir) {
	poses::ViewState s;
	const int counts[] = {12, 12, 12, 9, 0, 12, 0, 0, 4, 0, 0, 0, 0, 0, 0, 12};
	std::copy(std::begin(counts), std::end(counts), s.counts);
	s.selected = 3, s.a = 0, s.b = 5, s.fade = 0.35f;
	s.line = "VCF: 12 TARGETS";
	if (dark)
		s.muteA = true, s.line = "CAPTURE INTO WHICH POSE?", s.alert = true;
	write(dark ? "poses-dark.svg" : "poses-light.svg", poses::panel(), dark, accent, rackDir,
		[&](Painter& p, const Theme& theme) { poses::paint(p, theme, s); });
}

static void reelMockup(bool dark, int accent, const std::string& rackDir) {
	reel::ViewState s;
	s.filled[0] = s.filled[1] = s.filled[2] = s.filled[5] = true;
	s.playing[1] = true;
	s.selected = 1, s.seconds = 12.4f, s.start = 0.12f, s.end = 0.86f, s.head = 0.47f, s.loop = true;
	s.line = "VCF: 3 TARGETS";
	for (int k = 0; k < 3; k++) {
		std::vector<Vec2f> c;
		float v = 0.3f + 0.2f * k;
		for (float x = 0; x <= 1.f; x += 0.02f) {
			c.push_back({x, v});
			v = std::clamp(v + 0.08f * std::sin(x * (9 + 4 * k) + k), 0.05f, 0.95f);
			c.push_back({x, v});
		}
		s.curves.push_back(c);
	}
	if (dark)
		s.line = "REC 0:07.3 OF 5:00.0", s.alert = true, s.head = NAN;
	write(dark ? "reel-dark.svg" : "reel-light.svg", reel::panel(), dark, accent, rackDir,
		[&](Painter& p, const Theme& theme) { reel::paint(p, theme, s); });
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
	stageMockup(false, 0, rack);
	stageMockup(true, 2, rack);
	posesMockup(false, 1, rack);
	posesMockup(true, 3, rack);
	reelMockup(false, 2, rack);
	reelMockup(true, 0, rack);
	for (bool dark : {false, true})
		write(dark ? "crew-dark.svg" : "crew-light.svg", crew::panel(), dark, 0, rack,
			[](Painter& p, const Theme& theme) { p.label({p.width / 2, p.height / 2}, "CREW A", 8, theme.field); });
}

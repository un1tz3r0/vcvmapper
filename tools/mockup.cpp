// Panel mockups as SVG, drawn by the same layout and screen code as the plugin, so the look can be checked without Rack.
// Usage: mockup [rack-dir]   writes pin-light.svg and pin-dark.svg; rack-dir (Rack's source or SDK) supplies its
// jacks, screws and label font.
#include <cmath>
#include <fstream>
#include "pin/Layout.hpp"
#include "pin/View.hpp"
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

int main(int argc, char** argv) {
	const std::string rack = argc > 1 ? argv[1] : "";
	pinMockup("pin-light.svg", false, 0, rack);
	pinMockup("pin-dark.svg", true, 2, rack);
}

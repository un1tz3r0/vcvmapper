#pragma once
#include <rack.hpp>
#include <array>

namespace puppeteer {
using namespace rack;

/** A screen theme from the orbit colour sheet: the trace colour and its contrasting highlight. */
struct Accent {
	const char* name;
	NVGcolor trace, highlight;
};

inline const std::array<Accent, 5> ACCENTS = {{
	{"Cyan", nvgRGB(0x4f, 0xd8, 0xf0), nvgRGB(0xff, 0xc4, 0x3d)},
	{"Green", nvgRGB(0x6c, 0xe0, 0x8a), nvgRGB(0xff, 0xc4, 0x3d)},
	{"Amber", nvgRGB(0xf5, 0xa6, 0x3c), nvgRGB(0x8f, 0xe8, 0xff)},
	{"Lavender", nvgRGB(0xa9, 0x8c, 0xf0), nvgRGB(0xff, 0xc4, 0x3d)},
	{"White", nvgRGB(0xd8, 0xdc, 0xe4), nvgRGB(0xff, 0xc4, 0x3d)},
}};

inline std::vector<std::string> accentNames() {
	std::vector<std::string> names;
	for (const Accent& a : ACCENTS)
		names.push_back(a.name);
	return names;
}

/** Panel colours, following Rack's "prefer dark panels" setting. */
struct Palette {
	NVGcolor face, edge, ink, quiet, island, islandInk, bezel;
};

inline Palette palette() {
	if (settings::preferDarkPanels)
		return {nvgRGB(0x26, 0x27, 0x2b), nvgRGB(0x10, 0x10, 0x12), nvgRGB(0xe4, 0xe4, 0xe6), nvgRGB(0x8e, 0x8f, 0x95),
			nvgRGB(0x15, 0x16, 0x19), nvgRGB(0xe4, 0xe4, 0xe6), nvgRGB(0x0b, 0x0b, 0x0d)};
	return {nvgRGB(0xec, 0xeb, 0xe8), nvgRGB(0xc4, 0xc3, 0xbf), nvgRGB(0x2e, 0x2e, 0x31), nvgRGB(0x75, 0x75, 0x7a),
		nvgRGB(0x2a, 0x2b, 0x30), nvgRGB(0xf0, 0xf0, 0xf0), nvgRGB(0x1c, 0x1d, 0x21)};
}

inline std::shared_ptr<window::Font> labelFont() {
	return APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
}

inline std::shared_ptr<window::Font> monoFont() {
	return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
}

} // namespace puppeteer

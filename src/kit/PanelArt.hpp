#pragma once
#include "../plugin.hpp"
#include "Panel.hpp"

namespace kit {

// A panel's background, screen bezel, wires, output boxes and labels, painted from its description so they can never
// drift from the controls. Follows Rack's dark-panel setting and is cached in a framebuffer.
struct PanelArt : widget::FramebufferWidget {
	kit::Panel panel;
	bool dark = settings::preferDarkPanels;

	explicit PanelArt(kit::Panel panel);
	void step() override;
	void paint(NVGcontext* vg) const;
};

// Gives a module widget its panel: the art (as the widget's panel), the screws and every control, each bound to its
// module's param, light or port.
void addPanel(ModuleWidget* widget, const kit::Panel& panel);

} // namespace kit

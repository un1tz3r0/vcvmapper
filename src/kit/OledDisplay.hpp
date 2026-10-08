#pragma once
#include "../plugin.hpp"
#include "Painter.hpp"

namespace kit {

// Black glass whose scene is painted on Rack's light layer, so it stays lit when the room is dimmed.
struct OledDisplay : widget::Widget {
	kit::Theme theme;
	float cornerRadius = 2.5f;

	void draw(const DrawArgs& args) override;
	void drawLayer(const DrawArgs& args, int layer) override;
	virtual void paintScene(kit::Painter& painter) = 0;
};

} // namespace kit

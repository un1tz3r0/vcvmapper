#pragma once
#include "Binding.hpp"

namespace puppeteer {

/** Outlines a binding's targets on the rack, and brackets the groups they sit in, while the binding is being edited.
One per rack, made on first use. UI thread. */
struct Overlay : widget::TransparentWidget {
	std::vector<Target> targets;
	NVGcolor color;
	int idle = 0;

	/** Call every frame while `b` is being edited; the overlay hides a couple of frames after the calls stop. */
	static void show(const Binding& b);

	void step() override;
	void drawLayer(const DrawArgs& args, int layer) override;
};

} // namespace puppeteer

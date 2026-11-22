#include "plugin.hpp"
#include "core/Resolve.hpp"
#include "kit/PanelArt.hpp"
#include "groups/Layout.hpp"

using namespace puppeteer;

/** Closes the group a Troupe opened. Without one the group runs to the first gap or the next Troupe. */
struct Wing : Module, Marker {
	Wing() {
		config(0, 0, 0, 0);
	}
	Role role() const override {
		return Role::End;
	}
	std::string label() const override {
		return "";
	}
};

struct WingWidget : ModuleWidget {
	WingWidget(Wing* module) {
		setModule(module);
		kit::addPanel(this, wing::panel());
	}
};

Model* modelWing = createModel<Wing, WingWidget>("Wing");

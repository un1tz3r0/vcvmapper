#include "plugin.hpp"
#include "kit/OledDisplay.hpp"
#include "kit/PanelArt.hpp"
#include "stage/Layout.hpp"

using namespace crew;

/** Eight more jacks for the Stage to its left. Stage reads and writes them itself, so this has nothing to process;
it only works out which of Stage's crews it is, for its screen. */
struct Crew : Module {
	std::atomic<int> place{-1}; // 0 for A, next to Stage; -1 when no Stage is in reach

	Crew() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < JACKS; i++) {
			configInput(CV_INPUT + i, string::f("CV %d", i + 1));
			configOutput(CV_OUTPUT + i, string::f("Target value of the first binding on CV %d", i + 1));
		}
	}

	void process(const ProcessArgs& args) override {
		if (args.frame % 512)
			return;
		int n = 0;
		Module* x = leftExpander.module;
		while (n < CREWS - 1 && x && x->model == modelCrew) {
			x = x->leftExpander.module;
			n++;
		}
		place = x && x->model == modelStage ? n : -1;
	}
};

struct CrewScreen : kit::OledDisplay {
	Crew* module = nullptr;

	void paintScene(kit::Painter& p) override {
		int n = module ? module->place.load() : 0;
		std::string text = n < 0 ? "NO STAGE" : std::string("CREW ") + char('A' + n);
		p.label({p.width / 2, p.height / 2}, text, 8, n < 0 ? theme.guide : theme.field);
	}
};

struct CrewWidget : ModuleWidget {
	CrewWidget(Crew* module) {
		setModule(module);
		const kit::Panel panel = crew::panel();
		kit::addPanel(this, panel);
		const kit::Panel::Rect& glass = panel.screen;
		auto* screen = createWidget<CrewScreen>(mm2px(Vec(glass.min.x, glass.min.y)));
		screen->box.size = mm2px(Vec(glass.max.x - glass.min.x, glass.max.y - glass.min.y));
		screen->module = module;
		addChild(screen);
	}
};

Model* modelCrew = createModel<Crew, CrewWidget>("Crew");

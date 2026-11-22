#include "plugin.hpp"
#include "core/BindingUi.hpp"
#include "core/Overlay.hpp"
#include "core/Resolve.hpp"
#include "kit/Nvg.hpp"
#include "kit/OledDisplay.hpp"
#include "kit/PanelArt.hpp"
#include "groups/Layout.hpp"
#include "groups/View.hpp"

using namespace puppeteer;
using namespace troupe;

struct Troupe : Module, BindingHost, Marker {
	enum Learn { OFF, REPLACE, ADD };

	Binding bindings[MACROS];
	std::string name; // UI thread
	int accent = 0;
	std::atomic<int> selected{0}, learning{OFF};
	dsp::BooleanTrigger learnTrigger;

	Troupe() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(LEARN_PARAM, "Learn the selected macro");
		for (int i = 0; i < MACROS; i++)
			configParam(MACRO_PARAM + i, -1.f, 1.f, 0.f, string::f("Macro %d", i + 1), "%", 0.f, 100.f);
		configLight(LEARN_LIGHT, "Learning");
		for (Binding& b : bindings)
			b.locked = true;
	}

	Binding* bindingAt(int index) override {
		return index >= 0 && index < MACROS ? &bindings[index] : nullptr;
	}
	Role role() const override {
		return Role::Header;
	}
	std::string label() const override {
		return name;
	}

	void process(const ProcessArgs& args) override {
		if (learnTrigger.process(params[LEARN_PARAM].getValue() > 0.f))
			learning = learning ? OFF : REPLACE;
		bool blink = learning && std::fmod(args.frame * (double) args.sampleTime, 0.5) < 0.25;
		lights[LEARN_LIGHT].setBrightnessSmooth(blink ? 1.f : 0.f, args.sampleTime);
		// A macro at ±100% is ±5 V: the full travel either side of the hand.
		for (int i = 0; i < MACROS; i++)
			bindings[i].process(args, 5.f * params[MACRO_PARAM + i].getValue());
	}

	void processBypass(const ProcessArgs& args) override {
		Module::processBypass(args);
		for (Binding& b : bindings)
			b.bypass(args);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (Binding& b : bindings) {
			b.settings = Settings();
			b.selector = Selector();
			b.changed();
		}
		selected = 0;
		learning = OFF;
	}

	json_t* dataToJson() override {
		json_t* bindingsJ = json_array();
		for (const Binding& b : bindings)
			json_array_append_new(bindingsJ, b.toJson());
		return json_pack("{s:s, s:i, s:i, s:o}", "name", name.c_str(), "accent", accent, "selected", selected.load(), "bindings", bindingsJ);
	}

	void dataFromJson(json_t* rootJ) override {
		if (const char* n = json_string_value(json_object_get(rootJ, "name")))
			name = cleanName(n);
		if (json_t* j = json_object_get(rootJ, "accent"))
			accent = clamp((int) json_integer_value(j), 0, (int) std::size(kit::Theme::ACCENTS) - 1);
		if (json_t* j = json_object_get(rootJ, "selected"))
			selected = clamp((int) json_integer_value(j), 0, MACROS - 1);
		json_t* bindingsJ = json_object_get(rootJ, "bindings");
		for (int i = 0; i < MACROS; i++)
			if (json_t* j = json_array_get(bindingsJ, i))
				bindings[i].fromJson(j, this);
	}
};


struct TroupeScreen : kit::OledDisplay {
	Troupe* module = nullptr;
	MenuWatch* watch = nullptr;
	int members = 0, frames = 0;
	double refused = -INFINITY; // when learn last refused a target outside the group
	ViewState state;

	void step() override {
		theme = kit::Theme::accent(module ? module->accent : 0);
		state = ViewState();
		if (module) {
			if (frames++ % 30 == 0) {
				Snapshot s = Snapshot::take(getAncestorOfType<ModuleWidget>());
				members = s.owner < 0 ? 0 : (int) std::count(s.layout.group.begin(), s.layout.group.end(), s.layout.group[s.owner]);
			}
			state.name = module->name;
			state.members = members;
			state.selected = module->selected;
			for (int i = 0; i < MACROS; i++) {
				const Binding& b = module->bindings[i];
				state.macros[i].knob = module->params[MACRO_PARAM + i].getValue();
				if (!b.selector.empty())
					state.macros[i].label = b.problem() == Binding::Problem::None ? summary(b) : "NO MATCH";
			}
			if (module->learning) {
				state.note = system::getTime() - refused < 1.5 ? "NOT IN GROUP" : "TOUCH A KNOB";
				state.alert = true;
			}
		}
		OledDisplay::step();
	}

	void paintScene(kit::Painter& painter) override {
		paint(painter, theme, state);
	}

	/** A click on a macro's row selects it; one on the name renames the group. */
	void onButton(const ButtonEvent& e) override {
		if (!module || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		int i = hit(e.pos.y);
		if (i >= 0)
			module->selected = i;
		else {
			ui::Menu* menu = createMenu();
			appendNameEntry(menu, &module->name, "Voice 1");
		}
		e.consume(this);
	}
};


struct TroupeWidget : ModuleWidget {
	Learner learner;
	MenuWatch menu;
	TroupeScreen* screen;

	TroupeWidget(Troupe* module) {
		setModule(module);
		const kit::Panel panel = troupe::panel();
		kit::addPanel(this, panel);

		const kit::Panel::Rect& glass = panel.screen;
		screen = createWidget<TroupeScreen>(mm2px(Vec(glass.min.x, glass.min.y)));
		screen->box.size = mm2px(Vec(glass.max.x - glass.min.x, glass.max.y - glass.min.y));
		screen->module = module;
		screen->watch = &menu;
		addChild(screen);
	}

	void step() override {
		ModuleWidget::step();
		Troupe* m = getModule<Troupe>();
		if (!m)
			return;
		NVGcolor color = kit::nvg(kit::Theme::accent(m->accent).field);
		for (Binding& b : m->bindings) {
			b.color = color;
			b.update(this);
		}
		// Touching a macro selects it, so LEARN then a macro then a target binds that macro.
		app::ParamWidget* touched = APP->scene->rack->getTouchedParam();
		if (touched && touched->module == m && touched->paramId >= MACRO_PARAM)
			m->selected = touched->paramId - MACRO_PARAM;
		int sel = m->selected;
		if (menu.open() || m->learning)
			Overlay::show(m->bindings[sel]);

		Target t;
		if (learner.step(this, m->learning, t)) {
			Snapshot s = Snapshot::take(this);
			if (!s.layout.admits(Scope::Group, "", s.owner, s.find(t.moduleId)))
				screen->refused = system::getTime();
			else {
				learnInto(m->bindings[sel], m, sel, t, m->learning == Troupe::ADD);
				m->learning = Troupe::OFF;
			}
		}
	}

	void appendContextMenu(Menu* menu) override {
		Troupe* m = getModule<Troupe>();
		int sel = m->selected;
		this->menu.opened(menu);
		menu->addChild(new MenuSeparator);
		appendNameEntry(menu, &m->name, "Voice 1");
		menu->addChild(createMenuItem("Select the group", "", [=] {
			Snapshot s = Snapshot::take(this);
			if (s.owner < 0)
				return;
			app::RackWidget* rack = APP->scene->rack;
			rack->deselectAll();
			for (int i = 0; i < (int) s.modules.size(); i++)
				if (s.layout.group[i] == s.layout.group[s.owner])
					rack->select(s.modules[i]);
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(string::f("Macro %d, within this group", sel + 1)));
		appendBindingMenu(menu, m, sel, [=](bool add) {
			m->selected = sel;
			m->learning = add ? Troupe::ADD : Troupe::REPLACE;
		}, true);
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Screen colour", std::vector<std::string>(std::begin(kit::Theme::ACCENTS), std::end(kit::Theme::ACCENTS)), &m->accent));
	}
};


Model* modelTroupe = createModel<Troupe, TroupeWidget>("Troupe");

#include "plugin.hpp"
#include "core/BindingUi.hpp"
#include "kit/Nvg.hpp"
#include "kit/OledDisplay.hpp"
#include "kit/PanelArt.hpp"
#include "strings/Layout.hpp"
#include "strings/View.hpp"

using namespace puppeteer;
using namespace strings;

struct Strings : Module, BindingHost {
	enum Learn { OFF, REPLACE, ADD };

	Binding bindings[CHANNELS];
	int accent = 0;
	int cursor = 0; // the setting the edit page points at
	std::atomic<int> selected{0}, learning{OFF};
	std::atomic<bool> editing{false};
	dsp::BooleanTrigger learnTrigger, editTrigger;

	Strings() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(LEARN_PARAM, "Learn the selected channel");
		configButton(EDIT_PARAM, "Edit the selected channel");
		// Endless encoders: only their movement is read.
		configParam(ITEM_PARAM, -INFINITY, INFINITY, 0.f, "Channel, or setting when editing");
		configParam(VALUE_PARAM, -INFINITY, INFINITY, 0.f, "Depth, or the setting's value when editing");
		for (int i = 0; i < CHANNELS; i++) {
			configInput(CV_INPUT + i, string::f("Channel %d control", i + 1));
			configOutput(CV_OUTPUT + i, string::f("Channel %d target value", i + 1));
		}
		configLight(LEARN_LIGHT, "Learning");
		configLight(EDIT_LIGHT, "Editing");
	}

	Binding* bindingAt(int index) override {
		return index >= 0 && index < CHANNELS ? &bindings[index] : nullptr;
	}

	void process(const ProcessArgs& args) override {
		if (learnTrigger.process(params[LEARN_PARAM].getValue() > 0.f))
			learning = learning ? OFF : REPLACE;
		if (editTrigger.process(params[EDIT_PARAM].getValue() > 0.f))
			editing = !editing;
		bool blink = learning && std::fmod(args.frame * (double) args.sampleTime, 0.5) < 0.25;
		lights[LEARN_LIGHT].setBrightnessSmooth(blink ? 1.f : 0.f, args.sampleTime);
		lights[EDIT_LIGHT].setBrightness(editing ? 1.f : 0.f);
		for (int i = 0; i < CHANNELS; i++)
			bindings[i].process(args, inputs[CV_INPUT + i], outputs[CV_OUTPUT + i]);
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
		editing = false;
	}

	json_t* dataToJson() override {
		json_t* bindingsJ = json_array();
		for (const Binding& b : bindings)
			json_array_append_new(bindingsJ, b.toJson());
		return json_pack("{s:i, s:i, s:o}", "accent", accent, "selected", selected.load(), "bindings", bindingsJ);
	}

	void dataFromJson(json_t* rootJ) override {
		if (json_t* j = json_object_get(rootJ, "accent"))
			accent = clamp((int) json_integer_value(j), 0, (int) std::size(kit::Theme::ACCENTS) - 1);
		if (json_t* j = json_object_get(rootJ, "selected"))
			selected = clamp((int) json_integer_value(j), 0, CHANNELS - 1);
		json_t* bindingsJ = json_object_get(rootJ, "bindings");
		for (int i = 0; i < CHANNELS; i++)
			if (json_t* j = json_array_get(bindingsJ, i))
				bindings[i].fromJson(j, this);
	}
};


struct StringsScreen : kit::OledDisplay {
	Strings* module = nullptr;
	ViewState state;

	static Row row(const Binding& b, int learning) {
		static const char* PROBLEMS[] = {"", "", "BAD PATTERN", "NO MATCH", "TARGET MISSING", "IN USE BY OTHER MAPPER"};
		Row r;
		Binding::Problem problem = b.problem();
		const Plan* p = b.plan();
		const Lane* l = p && p->count ? &p->lanes[0] : nullptr;
		if (l) {
			float x = l->position.load();
			TargetInfo info = describe(l->target, x);
			r.label = info.module.empty() ? "?" : info.module + " " + info.name;
			r.value = x;
			r.tag = l->target.kind == Kind::Light ? "LIGHT" : l->momentary ? "BTN"
				: b.settings.mode == Mode::Modulate ? "MOD" : b.settings.mode == Mode::Overwrite ? "SET" : "PICK";
			if (p->count > 1)
				r.tag += string::f(" x%d", p->count);
			if (l->lease && b.settings.mode == Mode::Modulate && !l->momentary)
				r.base = l->lease.slot->base.load();
		}
		else if (problem != Binding::Problem::Empty)
			r.label = b.selector.pattern.empty() ? "?" : b.selector.pattern;
		r.note = PROBLEMS[int(problem)];
		if (learning) {
			r.note = learning == Strings::ADD ? "TOUCH ANOTHER TARGET" : "TOUCH A KNOB OR LIGHT";
			r.alert = true;
		}
		return r;
	}

	void step() override {
		theme = kit::Theme::accent(module ? module->accent : 0);
		state = ViewState();
		if (module) {
			state.selected = module->selected;
			state.editing = module->editing;
			state.cursor = module->cursor;
			for (int i = 0; i < CHANNELS; i++)
				state.rows[i] = row(module->bindings[i], i == state.selected ? module->learning.load() : 0);
			const Settings& s = module->bindings[state.selected].settings;
			if (state.editing) {
				for (const Field& f : fields::ALL)
					state.items.push_back({f.name, f.show(s)});
				state.left = fields::ALL[state.cursor].name;
				state.right = fields::ALL[state.cursor].show(s);
			}
			else {
				state.left = string::f("CH %d", state.selected + 1);
				state.right = "DEPTH " + fields::ALL[0].show(s);
			}
		}
		OledDisplay::step();
	}

	void paintScene(kit::Painter& painter) override {
		paint(painter, theme, state);
	}

	/** A click selects a channel's row, or a setting when editing; a right click on a row then opens its menu. */
	void onButton(const ButtonEvent& e) override {
		if (!module || e.action != GLFW_PRESS)
			return;
		bool editing = module->editing;
		int i = Geometry(box.size.x, box.size.y, fields::COUNT).hit(e.pos.y, editing, editing ? fields::COUNT : CHANNELS);
		if (i < 0)
			return;
		if (editing)
			module->cursor = i;
		else
			module->selected = i;
		if (e.button == GLFW_MOUSE_BUTTON_LEFT)
			e.consume(this);
	}
};


struct StringsWidget : ModuleWidget {
	static constexpr float CLICKS = 100.f; // per encoder turn
	Learner learner;
	float seen[2] = {NAN, NAN};

	StringsWidget(Strings* module) {
		setModule(module);
		const kit::Panel panel = strings::panel();
		kit::addPanel(this, panel);

		const kit::Panel::Rect& glass = panel.screen;
		auto* screen = createWidget<StringsScreen>(mm2px(Vec(glass.min.x, glass.min.y)));
		screen->box.size = mm2px(Vec(glass.max.x - glass.min.x, glass.max.y - glass.min.y));
		screen->module = module;
		addChild(screen);
	}

	/** Whole clicks the encoder has turned since last asked. */
	int clicks(Strings* m, int i) {
		float v = m->params[ITEM_PARAM + i].getValue();
		// A jump is a reset or a preset, not a hand.
		if (std::isnan(seen[i]) || std::fabs(v - seen[i]) > 0.5f)
			seen[i] = v;
		int n = int((v - seen[i]) * CLICKS);
		seen[i] += n / CLICKS;
		return n;
	}

	void step() override {
		ModuleWidget::step();
		Strings* m = getModule<Strings>();
		if (!m)
			return;
		NVGcolor color = kit::nvg(kit::Theme::accent(m->accent).field);
		for (Binding& b : m->bindings) {
			b.color = color;
			b.update(this);
		}

		int sel = m->selected;
		int item = clicks(m, 0), value = clicks(m, 1);
		if (item && m->editing)
			m->cursor = clamp(m->cursor + item, 0, fields::COUNT - 1);
		else if (item)
			m->selected = sel = clamp(sel + item, 0, CHANNELS - 1);
		if (value) {
			fields::ALL[m->editing ? m->cursor : 0].turn(m->bindings[sel].settings, value);
			m->bindings[sel].changed();
		}

		Target t;
		if (learner.step(this, m->learning, t)) {
			bool add = m->learning == Strings::ADD;
			learnInto(m->bindings[sel], m, sel, t, add);
			// Like MIDI-Map: carry on down the channels while they are empty.
			if (!add && sel + 1 < CHANNELS && m->bindings[sel + 1].selector.empty())
				m->selected = sel + 1;
			else
				m->learning = Strings::OFF;
		}
	}

	void appendContextMenu(Menu* menu) override {
		Strings* m = getModule<Strings>();
		int sel = m->selected;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(string::f("Channel %d", sel + 1)));
		appendBindingMenu(menu, m, sel, [=](bool add) {
			m->selected = sel;
			m->learning = add ? Strings::ADD : Strings::REPLACE;
		}, true);
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Screen colour", std::vector<std::string>(std::begin(kit::Theme::ACCENTS), std::end(kit::Theme::ACCENTS)), &m->accent));
	}
};


Model* modelStrings = createModel<Strings, StringsWidget>("Strings");

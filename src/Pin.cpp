#include "plugin.hpp"
#include "core/BindingUi.hpp"
#include "kit/Nvg.hpp"
#include "kit/OledDisplay.hpp"
#include "kit/PanelArt.hpp"
#include "pin/Layout.hpp"
#include "pin/View.hpp"

using namespace puppeteer;
using namespace pin;

struct Pin : Module, BindingHost {
	enum Learn { OFF, REPLACE, ADD };

	Binding binding;
	int accent = 0;
	std::atomic<int> learning{OFF};
	dsp::BooleanTrigger learnTrigger;

	Pin() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(LEARN_PARAM, "Learn");
		configParam(DEPTH_PARAM, -1.f, 1.f, 1.f, "Depth", "%", 0.f, 100.f);
		configInput(CV_INPUT, "Control");
		configOutput(CV_OUTPUT, "Target value");
		configLight(LEARN_LIGHT, "Learning");
	}

	Binding* bindingAt(int index) override {
		return index == 0 ? &this->binding : nullptr;
	}

	void process(const ProcessArgs& args) override {
		if (learnTrigger.process(params[LEARN_PARAM].getValue() > 0.f))
			learning = learning ? OFF : REPLACE;
		bool blink = learning && std::fmod(args.frame * (double) args.sampleTime, 0.5) < 0.25;
		lights[LEARN_LIGHT].setBrightnessSmooth(blink ? 1.f : 0.f, args.sampleTime);
		binding.process(args, inputs[CV_INPUT], outputs[CV_OUTPUT], params[DEPTH_PARAM].getValue());
	}

	void processBypass(const ProcessArgs& args) override {
		Module::processBypass(args);
		binding.bypass(args);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		binding.settings = Settings();
		binding.selector = Selector();
		binding.changed();
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "accent", json_integer(accent));
		json_object_set_new(rootJ, "binding", binding.toJson());
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		if (json_t* j = json_object_get(rootJ, "accent"))
			accent = clamp((int) json_integer_value(j), 0, (int) std::size(kit::Theme::ACCENTS) - 1);
		json_t* bindingJ = json_object_get(rootJ, "binding");
		if (bindingJ)
			binding.fromJson(bindingJ, this);
		else {
			bindingJ = legacy(rootJ);
			binding.fromJson(bindingJ, this);
			json_decref(bindingJ);
		}
	}

	/** Pin 2.0 kept one target and its settings at the top level. */
	static json_t* legacy(json_t* rootJ) {
		json_t* targetJ = json_object_get(rootJ, "target");
		bool overwrite = json_integer_value(json_object_get(rootJ, "mode")) == 1;
		bool bipolar = !json_is_false(json_object_get(rootJ, "bipolar"));
		json_t* targetsJ = json_array();
		json_t* basesJ = json_array();
		if (Target t = Target::fromJson(targetJ)) {
			json_array_append(targetsJ, targetJ);
			json_t* baseJ = json_object_get(rootJ, "base");
			if (t.kind == Kind::Param && baseJ && json_number_value(baseJ) >= 0.0)
				json_array_append_new(basesJ, json_pack("[I, i, f]", (json_int_t) t.moduleId, t.index, json_number_value(baseJ)));
		}
		return json_pack("{s:{s:s, s:b}, s:{s:o}, s:o}",
			"settings", "mode", overwrite ? "overwrite" : "modulate", "bipolar", bipolar,
			"selector", "targets", targetsJ,
			"bases", basesJ);
	}
};


struct PinScreen : kit::OledDisplay {
	Pin* module = nullptr;
	TargetInfo info;
	std::array<float, 64> trace{};
	size_t head = 0;

	const Lane* first() const {
		const Plan* p = module ? module->binding.plan() : nullptr;
		return p && p->count ? &p->lanes[0] : nullptr;
	}

	void step() override {
		theme = kit::Theme::accent(module ? module->accent : 0);
		if (const Lane* l = first()) {
			float x = l->position.load();
			info = describe(l->target, x);
			trace[head++ % trace.size()] = std::isnan(x) ? 0.f : x;
		}
		else
			info = {};
		OledDisplay::step();
	}

	ViewState state() const {
		ViewState s;
		const Lane* l = first();
		const Binding* b = module ? &module->binding : nullptr;
		if (!module)
			s.message = {"PIN"};
		else if (module->learning)
			s.message = module->learning == Pin::ADD ? std::vector<std::string>{"TOUCH", "ANOTHER", "TARGET"}
				: std::vector<std::string>{"TOUCH A", "KNOB OR", "CLICK A", "LIGHT"}, s.alert = true;
		else if (b->selector.empty())
			s.message = {"PRESS", "LEARN"};
		else if (!b->error.empty())
			s.message = {"BAD", "PATTERN"};
		else if (!l)
			s.message = {"NO", "MATCH"};
		else if (info.module.empty())
			s.message = {"TARGET", "MISSING"};
		else if (l->lease && !l->lease.slot->bound)
			s.message = {"IN USE", "BY OTHER", "MAPPER"};
		if (!s.message.empty())
			return s;

		int count = b->plan()->count;
		const Settings& set = b->settings;
		s.module = info.module;
		s.name = info.name;
		s.value = info.value;
		if (l->target.kind == Kind::Light)
			s.tag = "LIGHT 0..10";
		else if (l->momentary)
			s.tag = "BUTTON";
		else
			s.tag = std::string(set.mode == Mode::Modulate ? "MOD " : set.mode == Mode::Overwrite ? "SET " : "PICK ") + (set.shape.bipolar ? "-5..5" : "0..10");
		if (count > 1)
			s.tag += string::f(" x%d", count);
		for (size_t i = 0; i < trace.size(); i++)
			s.trace.push_back(trace[(head + i) % trace.size()]);
		if (l->lease && set.mode == Mode::Modulate && !l->momentary)
			s.base = l->lease.slot->base.load();
		return s;
	}

	void paintScene(kit::Painter& painter) override {
		paint(painter, theme, state());
	}
};


struct PinWidget : ModuleWidget {
	Learner learner;

	PinWidget(Pin* module) {
		setModule(module);
		const kit::Panel panel = pin::panel();
		kit::addPanel(this, panel);

		const kit::Panel::Rect& glass = panel.screen;
		auto* screen = createWidget<PinScreen>(mm2px(Vec(glass.min.x, glass.min.y)));
		screen->box.size = mm2px(Vec(glass.max.x - glass.min.x, glass.max.y - glass.min.y));
		screen->module = module;
		addChild(screen);
	}

	void step() override {
		ModuleWidget::step();
		Pin* m = getModule<Pin>();
		if (!m)
			return;
		m->binding.color = kit::nvg(kit::Theme::accent(m->accent).field);
		m->binding.update(this);

		Target t;
		if (learner.step(this, m->learning, t)) {
			learnInto(m->binding, m, 0, t, m->learning == Pin::ADD);
			m->learning = Pin::OFF;
		}
	}

	void appendContextMenu(Menu* menu) override {
		Pin* m = getModule<Pin>();
		menu->addChild(new MenuSeparator);
		appendBindingMenu(menu, m, 0, [=](bool add) { m->learning = add ? Pin::ADD : Pin::REPLACE; }, false);
		menu->addChild(createIndexPtrSubmenuItem("Screen colour", std::vector<std::string>(std::begin(kit::Theme::ACCENTS), std::end(kit::Theme::ACCENTS)), &m->accent));
	}
};


Model* modelPin = createModel<Pin, PinWidget>("Pin");

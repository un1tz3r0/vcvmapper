#include "plugin.hpp"
#include "core/Binding.hpp"
#include "kit/Menus.hpp"
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
	bool wasLearning = false;
	bool mouseWas = false;
	Target candidate;

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

		bool learning = m->learning;
		if (learning && !wasLearning) {
			APP->scene->rack->setTouchedParam(NULL);
			// The click that pressed LEARN must not count as the click on a light.
			mouseWas = true;
		}
		wasLearning = learning;
		if (learning)
			learn(m);
	}

	/** A touched knob or switch wins; otherwise a click near a light binds that light. */
	void learn(Pin* m) {
		app::ParamWidget* pw = APP->scene->rack->getTouchedParam();
		if (pw && pw->module && pw->module != m) {
			APP->scene->rack->setTouchedParam(NULL);
			Target t;
			t.kind = Kind::Param;
			t.moduleId = pw->module->id;
			t.index = pw->paramId;
			finish(m, t);
			return;
		}
		bool down = glfwGetMouseButton(APP->window->win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
		if (down && !mouseWas)
			candidate = nearestLight(APP->scene->rack->getMousePos(), this);
		if (!down && mouseWas && candidate)
			finish(m, candidate);
		mouseWas = down;
	}

	void finish(Pin* m, Target t) {
		refresh(t, this);
		bool add = m->learning == Pin::ADD;
		m->binding.edit(m, 0, add ? "learn another Pin target" : "learn Pin target", [&](Selector& s) {
			if (!add)
				s = Selector();
			if (std::none_of(s.targets.begin(), s.targets.end(), [&](const Target& u) { return u.key() == t.key(); }))
				s.targets.push_back(t);
		}, t);
		m->learning = Pin::OFF;
		candidate = {};
	}

	static std::string label(const Target& t) {
		TargetInfo info = describe(t, 0.f);
		return info.module.empty() ? "Missing module" : info.module + ": " + info.name;
	}

	/** The clause that matches this target on every module of its model. */
	static std::string everyInstance(const Target& t) {
		return t.plugin + "/" + t.model + ":" + (t.kind == Kind::Light ? "light:#" : "#") + std::to_string(t.index);
	}

	void appendTargets(Menu* menu, Pin* m) {
		Binding& b = m->binding;
		const Plan* p = b.plan();
		for (int i = 0; p && i < p->count; i++) {
			Target t = p->lanes[i].target;
			bool learned = std::any_of(b.selector.targets.begin(), b.selector.targets.end(), [&](const Target& u) { return u.key() == t.key(); });
			menu->addChild(createSubmenuItem(string::f("%d. %s", i + 1, label(t).c_str()), "", [=](Menu* sub) {
				auto without = [=](Selector& s) {
					s.targets.erase(std::remove_if(s.targets.begin(), s.targets.end(), [&](const Target& u) { return u.key() == t.key(); }), s.targets.end());
				};
				if (!learned) {
					sub->addChild(createMenuItem("Leave out", "", [=] {
						m->binding.edit(m, 0, "leave out Pin target", [&](Selector& s) { s.excluded.push_back(t); });
					}));
					return;
				}
				sub->addChild(createMenuItem("Remove", "", [=] { m->binding.edit(m, 0, "remove Pin target", without); }));
				std::string model = describe(t, 0.f).module;
				if (!model.empty())
					sub->addChild(createMenuItem("On every " + model, "", [=] {
						m->binding.edit(m, 0, "widen Pin target", [&](Selector& s) {
							without(s);
							s.pattern += (s.pattern.empty() ? "" : ", ") + everyInstance(t);
						});
					}));
			}));
		}
		if (!b.selector.excluded.empty())
			menu->addChild(createMenuItem(string::f("Bring back %d left out", (int) b.selector.excluded.size()), "", [=] {
				m->binding.edit(m, 0, "bring back Pin targets", [](Selector& s) { s.excluded.clear(); });
			}));
	}

	void appendPattern(Menu* menu, Pin* m) {
		auto* entry = new kit::TextEntry;
		entry->text = m->binding.selector.pattern;
		entry->placeholder = "Fundamental/VCO:Freq*";
		entry->commit = [=](const std::string& text) {
			m->binding.edit(m, 0, "edit Pin pattern", [&](Selector& s) { s.pattern = text; });
		};
		menu->addChild(entry);
		menu->addChild(createMenuLabel("plugin/model:param, comma between several"));
		menu->addChild(createMenuLabel("* and ? match any text; #3 or #0-7 are ids"));
		menu->addChild(createMenuLabel("light:name or light:#N reads lights"));
		if (!m->binding.error.empty())
			menu->addChild(createMenuLabel(m->binding.error));
	}

	void appendShape(Menu* menu, Pin* m) {
		Shape* sh = &m->binding.settings.shape;
		auto set = [=](auto apply) { return [=](float v) { apply(v); m->binding.changed(); }; };
		menu->addChild(createBoolMenuItem("Invert", "", [=] { return sh->invert; }, [=](bool v) { sh->invert = v; m->binding.changed(); }));
		menu->addChild(kit::createSlider("Curve", "", -1.f, 1.f, 0.f, 2, [=] { return sh->curve; }, set([=](float v) { sh->curve = v; })));
		menu->addChild(kit::createSlider("Knob from", "%", 0.f, 100.f, 0.f, 0, [=] { return sh->lo * 100.f; }, set([=](float v) { sh->lo = v / 100.f; })));
		menu->addChild(kit::createSlider("Knob to", "%", 0.f, 100.f, 100.f, 0, [=] { return sh->hi * 100.f; }, set([=](float v) { sh->hi = v / 100.f; })));
		std::vector<int> steps = {0, 2, 3, 4, 5, 6, 8, 12, 16, 24, 32};
		menu->addChild(createSubmenuItem("Steps", sh->steps ? std::to_string(sh->steps) : "Off", [=](Menu* sub) {
			for (int n : steps)
				sub->addChild(createCheckMenuItem(n ? std::to_string(n) : "Off", "", [=] { return sh->steps == n; }, [=] { sh->steps = n; m->binding.changed(); }));
		}));
	}

	void appendTiming(Menu* menu, Pin* m) {
		Settings* s = &m->binding.settings;
		menu->addChild(kit::createSlider("Slew", " ms", 0.f, 2000.f, 0.f, 1, [=] { return s->slew; }, [=](float v) { s->slew = v; m->binding.changed(); }, 3.f));
		menu->addChild(createSubmenuItem("Write rate", s->autoRate ? "Auto" : "", [=](Menu* sub) {
			sub->addChild(createBoolMenuItem("Every sample for audio-rate CV", "", [=] { return s->autoRate; }, [=](bool v) { s->autoRate = v; m->binding.changed(); }));
			sub->addChild(new MenuSeparator);
			for (int n : {1, 4, 8, 16, 32, 64, 128})
				sub->addChild(createCheckMenuItem(n == 1 ? "Every sample" : string::f("Every %d samples", n), "", [=] { return s->interval == n; }, [=] { s->interval = n; m->binding.changed(); }));
		}));
		menu->addChild(createSubmenuItem("Button hold", string::f("%g ms", s->hold), [=](Menu* sub) {
			for (float ms : {0.f, 0.5f, 1.f, 2.f, 5.f, 10.f, 20.f, 50.f})
				sub->addChild(createCheckMenuItem(string::f("%g ms", ms), "", [=] { return s->hold == ms; }, [=] { s->hold = ms; m->binding.changed(); }));
		}));
	}

	void appendContextMenu(Menu* menu) override {
		Pin* m = getModule<Pin>();
		Binding& b = m->binding;
		Settings* s = &b.settings;
		const Plan* p = b.plan();
		int count = p ? p->count : 0;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(count == 0 ? "Not bound" : count == 1 ? label(p->lanes[0].target) : string::f("%d targets", count)));
		menu->addChild(createMenuItem("Learn", "", [=] { m->learning = Pin::REPLACE; }));
		menu->addChild(createMenuItem("Learn another", "", [=] { m->learning = Pin::ADD; }, b.selector.empty()));
		menu->addChild(createSubmenuItem("Targets", std::to_string(count), [=](Menu* sub) { appendTargets(sub, m); }));
		menu->addChild(createSubmenuItem("Pattern", b.selector.pattern.empty() ? "" : "On", [=](Menu* sub) { appendPattern(sub, m); }));
		menu->addChild(createMenuItem("Unbind all", "", [=] {
			m->binding.edit(m, 0, "unbind Pin", [](Selector& s) { s = Selector(); });
		}, b.selector.empty()));

		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Hand and CV", {"Modulate around the knob", "Overwrite the knob", "Pick up the knob"},
			[=] { return (size_t) s->mode; },
			[=](size_t i) { s->mode = Mode(i); m->binding.changed(); }));
		menu->addChild(createIndexSubmenuItem("CV range", {"-5 V to +5 V", "0 V to 10 V"},
			[=] { return s->shape.bipolar ? 0 : 1; },
			[=](size_t i) { s->shape.bipolar = i == 0; m->binding.changed(); }));
		menu->addChild(createSubmenuItem("Shape", "", [=](Menu* sub) { appendShape(sub, m); }));
		appendTiming(menu, m);
		menu->addChild(createIndexPtrSubmenuItem("Screen colour", std::vector<std::string>(std::begin(kit::Theme::ACCENTS), std::end(kit::Theme::ACCENTS)), &m->accent));
	}
};


Model* modelPin = createModel<Pin, PinWidget>("Pin");

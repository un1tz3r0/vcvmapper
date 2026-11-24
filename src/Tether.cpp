#include "plugin.hpp"
#include "core/BindingUi.hpp"
#include "core/Overlay.hpp"
#include "core/Resolve.hpp"
#include "kit/Nvg.hpp"
#include "kit/OledDisplay.hpp"
#include "kit/PanelArt.hpp"
#include "tether/Layout.hpp"
#include "tether/View.hpp"

using namespace puppeteer;
using namespace tether;

/** Targets whatever module its cord reaches: the modules at the far end of THRU's cables, or of IN's cable when THRU
is unpatched. CV drives them, or IN when CV is unpatched. */
struct Tether : Module, BindingHost {
	enum Learn { OFF, REPLACE, ADD };

	Binding binding;
	int accent = 0;
	std::atomic<int> learning{OFF};
	dsp::BooleanTrigger learnTrigger;

	Tether() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(LEARN_PARAM, "Learn the control to drive on whatever the cord reaches");
		configParam(DEPTH_PARAM, -1.f, 1.f, 1.f, "Depth", "%", 0.f, 100.f);
		configInput(IN_INPUT, "Signal, passed to THRU; also the control when CV is unpatched");
		configInput(CV_INPUT, "Control");
		configOutput(THRU_OUTPUT, "IN, through to the target");
		configBypass(IN_INPUT, THRU_OUTPUT);
		configLight(LEARN_LIGHT, "Learning");
		binding.corded = true;
	}

	Binding* bindingAt(int index) override {
		return index == 0 ? &binding : nullptr;
	}

	void process(const ProcessArgs& args) override {
		if (learnTrigger.process(params[LEARN_PARAM].getValue() > 0.f))
			learning = learning ? OFF : REPLACE;
		bool blink = learning && std::fmod(args.frame * (double) args.sampleTime, 0.5) < 0.25;
		lights[LEARN_LIGHT].setBrightnessSmooth(blink ? 1.f : 0.f, args.sampleTime);

		Input& in = inputs[IN_INPUT];
		Output& thru = outputs[THRU_OUTPUT];
		thru.setChannels(in.getChannels());
		thru.writeVoltages(in.getVoltages());
		Input& cv = inputs[CV_INPUT].isConnected() ? inputs[CV_INPUT] : in;
		binding.process(args, cv, params[DEPTH_PARAM].getValue());
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
		learning = OFF;
	}

	json_t* dataToJson() override {
		return json_pack("{s:i, s:o}", "accent", accent, "binding", binding.toJson());
	}

	void dataFromJson(json_t* rootJ) override {
		if (json_t* j = json_object_get(rootJ, "accent"))
			accent = clamp((int) json_integer_value(j), 0, (int) std::size(kit::Theme::ACCENTS) - 1);
		if (json_t* j = json_object_get(rootJ, "binding"))
			binding.fromJson(j, this);
	}
};


struct TetherScreen : kit::OledDisplay {
	Tether* module = nullptr;
	ViewState state;

	static ViewState view(const Tether* m) {
		ViewState s;
		if (!m)
			return {"TETHER", "", "", false};
		const Binding& b = m->binding;
		const Plan* p = b.plan();
		const Lane* l = p && p->count ? &p->lanes[0] : nullptr;
		if (m->learning)
			return {"TOUCH A", "KNOB", "ANY MODULE", true};
		if (b.selector.empty())
			return {"PRESS", "LEARN", "", false};
		if (!b.error.empty())
			return {"BAD", "PATH", "", true};
		if (b.cabled.empty())
			return {"PATCH", "THRU OR IN", "", false};
		if (!l) {
			size_t at = b.missing.find(" has no ");
			return at == std::string::npos ? ViewState{"NO", "MATCH", "", true}
				: ViewState{b.missing.substr(0, at), "NO " + b.missing.substr(at + 8), "MOVE THE CORD", true};
		}
		float x = l->position.load();
		TargetInfo info = describe(l->target, x);
		s.top = info.module;
		s.bottom = info.name;
		s.value = x;
		if (l->lease && b.settings.mode == Mode::Modulate && !l->momentary)
			s.base = l->lease.slot->base.load();
		s.tag = l->target.kind == Kind::Light ? "LIGHT" : l->momentary ? "BUTTON" : info.value;
		if (p->count > 1)
			s.tag += string::f(" x%d", p->count);
		if (l->lease && !l->lease.slot->bound)
			s.tag = "IN USE BY OTHER MAPPER", s.alert = true;
		return s;
	}

	void step() override {
		theme = kit::Theme::accent(module ? module->accent : 0);
		state = view(module);
		OledDisplay::step();
	}

	void paintScene(kit::Painter& painter) override {
		paint(painter, theme, state);
	}
};


struct TetherWidget : ModuleWidget {
	Learner learner;
	MenuWatch menu;

	TetherWidget(Tether* module) {
		setModule(module);
		const kit::Panel panel = tether::panel();
		kit::addPanel(this, panel);

		const kit::Panel::Rect& glass = panel.screen;
		auto* screen = createWidget<TetherScreen>(mm2px(Vec(glass.min.x, glass.min.y)));
		screen->box.size = mm2px(Vec(glass.max.x - glass.min.x, glass.max.y - glass.min.y));
		screen->module = module;
		addChild(screen);
	}

	/** The modules at the far end of THRU's cables, in the order they were patched, else of IN's cable. Polled rather
	than left to onPortChange, which says nothing when a cable is stacked onto an output that already has one. */
	std::vector<int64_t> cabled(Module* self) {
		std::vector<int64_t> ids;
		auto far = [&](app::PortWidget* port, bool output) {
			for (app::CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
				engine::Cable* c = cw->getCable();
				Module* m = output ? c->inputModule : c->outputModule;
				if (m && m != self && std::find(ids.begin(), ids.end(), m->id) == ids.end())
					ids.push_back(m->id);
			}
		};
		far(getOutput(THRU_OUTPUT), true);
		if (ids.empty())
			far(getInput(IN_INPUT), false);
		return ids;
	}

	void step() override {
		ModuleWidget::step();
		Tether* m = getModule<Tether>();
		if (!m)
			return;
		Binding& b = m->binding;
		std::vector<int64_t> ids = cabled(m);
		if (ids != b.cabled) {
			b.cabled = std::move(ids);
			b.changed();
		}
		b.color = kit::nvg(kit::Theme::accent(m->accent).field);
		b.update(this);
		if (menu.open())
			Overlay::show(b);

		Target t;
		if (learner.step(this, m->learning, t)) {
			Module* target = APP->engine->getModule(t.moduleId);
			t.name = target ? itemName(target, t.kind == Kind::Light, t.index) : "";
			bool add = m->learning == Tether::ADD;
			b.edit(m, 0, add ? "learn another target" : "learn target", [&](Selector& s) {
				if (!add)
					s = Selector();
				s.grips.push_back(t);
			}, t);
			m->learning = Tether::OFF;
		}
	}

	void appendContextMenu(Menu* menu) override {
		Tether* m = getModule<Tether>();
		this->menu.opened(menu);
		menu->addChild(new MenuSeparator);
		appendBindingMenu(menu, m, 0, [=](bool add) { m->learning = add ? Tether::ADD : Tether::REPLACE; }, false);
		menu->addChild(createIndexPtrSubmenuItem("Screen colour", std::vector<std::string>(std::begin(kit::Theme::ACCENTS), std::end(kit::Theme::ACCENTS)), &m->accent));
	}
};


Model* modelTether = createModel<Tether, TetherWidget>("Tether");

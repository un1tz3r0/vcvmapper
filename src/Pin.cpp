#include "plugin.hpp"
#include "core/Target.hpp"
#include "ui/Oled.hpp"
#include "ui/Panel.hpp"
#include <atomic>

using namespace puppeteer;

struct Pin : Module {
	enum ParamId { LEARN_PARAM, DEPTH_PARAM, PARAMS_LEN };
	enum InputId { CV_INPUT, INPUTS_LEN };
	enum OutputId { CV_OUTPUT, OUTPUTS_LEN };
	enum LightId { LEARN_LIGHT, LIGHTS_LEN };
	enum Mode { MODULATE, OVERWRITE };

	// UI thread
	Target target;
	bool pasted = false;

	// Read by the audio thread. The engine owns the handle and clears it when the target is deleted.
	ParamHandle handle;
	std::atomic<Kind> kind{Kind::None};
	std::atomic<int64_t> lightModuleId{-1};
	std::atomic<int> lightId{0};
	std::atomic<bool> learning{false};
	std::atomic<uint32_t> generation{0};
	std::atomic<float> startBase{-1.f};
	int mode = MODULATE;
	bool bipolar = true;
	int accent = 0;

	// Audio thread, all in scaled 0..1 units of the target param
	uint32_t seen = 0;
	float base = -1.f;     // where the hand left the knob
	float written = -1.f;  // our last write, to tell it apart from a hand's
	float value = 0.f;     // current target reading, also shown on the screen
	bool driving = false;
	dsp::ClockDivider divider;
	dsp::BooleanTrigger learnTrigger;

	Pin() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(LEARN_PARAM, "Learn");
		configParam(DEPTH_PARAM, -1.f, 1.f, 1.f, "Depth", "%", 0.f, 100.f);
		configInput(CV_INPUT, "Control");
		configOutput(CV_OUTPUT, "Target value");
		configLight(LEARN_LIGHT, "Learning");
		handle.text = "Pin";
		handle.color = ACCENTS[0].trace;
		APP->engine->addParamHandle(&handle);
		divider.setDivision(32);
	}

	~Pin() {
		APP->engine->removeParamHandle(&handle);
	}

	void process(const ProcessArgs& args) override {
		if (learnTrigger.process(params[LEARN_PARAM].getValue() > 0.f))
			learning = !learning;
		bool blink = learning && std::fmod(args.frame * (double) args.sampleTime, 0.5) < 0.25;
		lights[LEARN_LIGHT].setBrightnessSmooth(blink ? 1.f : 0.f, args.sampleTime);

		if (seen != generation) {
			seen = generation;
			base = startBase;
			written = -1.f;
			driving = false;
		}

		Kind k = kind;
		if (k == Kind::Param && processParam())
			outputs[CV_OUTPUT].setVoltage(bipolar ? 10.f * value - 5.f : 10.f * value);
		else if (k == Kind::Light && processLight())
			outputs[CV_OUTPUT].setVoltage(10.f * value);
		else {
			value = 0.f;
			outputs[CV_OUTPUT].setVoltage(0.f);
		}
	}

	/** False when the param can't be reached: deleted, held by another mapper, or unbounded. */
	bool processParam() {
		Module* m = handle.module;
		ParamQuantity* pq = m && handle.paramId < (int) m->paramQuantities.size() ? m->paramQuantities[handle.paramId] : nullptr;
		if (!pq || !pq->isBounded()) {
			driving = false;
			return false;
		}
		value = pq->getScaledValue();

		if (!inputs[CV_INPUT].isConnected()) {
			// Cable pulled: hand the knob back where the hand left it.
			if (driving && mode == MODULATE)
				value = write(m, pq, base);
			driving = false;
			base = value;
			return true;
		}
		if (!driving) {
			driving = true;
			written = -1.f;
			if (base < 0.f)
				base = value;
		}
		if (!divider.process())
			return true;
		// Anything that moved the knob since our last write was a hand (or MIDI, or a preset): follow it.
		if (written >= 0.f && std::fabs(value - written) > 1e-4f)
			base = clamp(base + value - written, 0.f, 1.f);
		float home = mode == MODULATE ? base : bipolar ? 0.5f : 0.f;
		float cv = inputs[CV_INPUT].getVoltage() / 10.f;
		value = write(m, pq, clamp(home + params[DEPTH_PARAM].getValue() * cv, 0.f, 1.f));
		return true;
	}

	float write(Module* m, ParamQuantity* pq, float scaled) {
		float v = pq->fromScaled(scaled);
		if (pq->snapEnabled)
			v = std::round(v);
		APP->engine->setParamValue(m, handle.paramId, v);
		return written = pq->toScaled(v);
	}

	bool processLight() {
		// Looked up every sample: a pointer kept across process() calls could outlive a deleted module.
		Module* m = APP->engine->getModule_NoLock(lightModuleId);
		int id = lightId;
		if (!m || id >= (int) m->lights.size())
			return false;
		value = clamp(m->lights[id].getBrightness(), 0.f, 1.f);
		return true;
	}

	/** Sets the target's fields for both threads. The param handle is claimed separately. */
	void assign(const Target& t, float base = -1.f) {
		target = t;
		lightModuleId = t.kind == Kind::Light ? t.moduleId : -1;
		lightId = t.index;
		startBase = base;
		kind = t.kind;
		generation++;
	}

	/** Claims the param handle, never taking it from another mapper. UI thread. */
	void acquire() {
		bool want = target.kind == Kind::Param;
		if (want ? held() : handle.moduleId < 0)
			return;
		if (want && APP->engine->getParamHandle(target.moduleId, target.index))
			return;
		APP->engine->updateParamHandle(&handle, want ? target.moduleId : -1, want ? target.index : 0, false);
	}

	bool held() const {
		return target.kind != Kind::Param || (handle.moduleId == target.moduleId && handle.paramId == target.index);
	}

	void setTarget(const Target& t, float base = -1.f) {
		assign(t, base);
		acquire();
	}

	void bind(const Target& t);

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "mode", json_integer(mode));
		json_object_set_new(rootJ, "bipolar", json_boolean(bipolar));
		json_object_set_new(rootJ, "accent", json_integer(accent));
		json_object_set_new(rootJ, "base", json_real(base));
		if (target)
			json_object_set_new(rootJ, "target", target.toJson());
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		if (json_t* j = json_object_get(rootJ, "mode"))
			mode = clamp((int) json_integer_value(j), 0, 1);
		if (json_t* j = json_object_get(rootJ, "bipolar"))
			bipolar = json_boolean_value(j);
		if (json_t* j = json_object_get(rootJ, "accent"))
			accent = clamp((int) json_integer_value(j), 0, (int) ACCENTS.size() - 1);
		json_t* baseJ = json_object_get(rootJ, "base");
		assign(Target::fromJson(json_object_get(rootJ, "target")), baseJ ? json_number_value(baseJ) : -1.f);

		// Rack strips module ids on paste, duplicate and selection import, so a missing id means
		// the stored target may belong to the original. The widget remaps it once positions are final.
		pasted = id < 0;
		if (!pasted && target.kind == Kind::Param)
			APP->engine->updateParamHandle_NoLock(&handle, target.moduleId, target.index, false);
	}
};

/** Undo and redo for learn and unbind. */
struct BindAction : history::ModuleAction {
	Target before, after;

	BindAction(int64_t id, Target before, Target after) : before(std::move(before)), after(std::move(after)) {
		moduleId = id;
		name = "bind Pin";
	}

	void apply(const Target& t) {
		if (auto* m = dynamic_cast<Pin*>(APP->engine->getModule(moduleId)))
			m->setTarget(t);
	}

	void undo() override {
		apply(before);
	}

	void redo() override {
		apply(after);
	}
};

void Pin::bind(const Target& t) {
	APP->history->push(new BindAction(id, target, t));
	setTarget(t);
}


struct PinScreen : OledDisplay {
	Pin* module = nullptr;
	TargetInfo info;
	std::array<float, 64> trace{};
	size_t head = 0;

	void step() override {
		if (module) {
			info = describe(module->target, module->value);
			trace[head++ % trace.size()] = module->value;
		}
		OledDisplay::step();
	}

	/** Centred message lines when there is nothing to plot. */
	std::vector<std::string> message() const {
		if (!module)
			return {"PIN"};
		if (module->learning)
			return {"TOUCH A", "KNOB OR", "CLICK A", "LIGHT"};
		if (!module->target)
			return {"PRESS", "LEARN"};
		if (info.module.empty())
			return {"TARGET", "MISSING"};
		if (!module->held())
			return {"IN USE", "BY OTHER", "MAPPER"};
		return {};
	}

	void drawScreen(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		const Accent& a = ACCENTS[module ? module->accent : 0];
		const float w = box.size.x - 2 * PAD, h = box.size.y;

		std::vector<std::string> lines = message();
		if (!lines.empty()) {
			nvgFontSize(vg, 8.f);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			float y = h / 2 - 5.f * (lines.size() - 1);
			for (const std::string& line : lines) {
				glowText(vg, box.size.x / 2, y, line, module && module->learning ? a.highlight : a.trace);
				y += 10.f;
			}
			return;
		}

		nvgFontSize(vg, 8.f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		glowText(vg, PAD, 10.f, fit(vg, info.module, w), a.trace);
		glowText(vg, PAD, 19.f, fit(vg, info.name, w), a.highlight);

		const float y0 = 26.f, y1 = h - 22.f;
		auto Y = [&](float v) { return y1 - v * (y1 - y0); };

		nvgBeginPath(vg);
		nvgRect(vg, PAD, y0, w, y1 - y0);
		nvgStrokeColor(vg, nvgTransRGBAf(a.trace, 0.18f));
		nvgStrokeWidth(vg, 0.5f);
		nvgStroke(vg);

		bool showBase = module->target.kind == Kind::Param && module->mode == Pin::MODULATE && module->driving;
		if (showBase && module->base >= 0.f) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, PAD, Y(module->base));
			nvgLineTo(vg, PAD + w, Y(module->base));
			glowStroke(vg, nvgTransRGBAf(a.highlight, 0.7f), 0.6f);
		}

		nvgBeginPath(vg);
		for (size_t i = 0; i < trace.size(); i++) {
			float x = PAD + w * i / (trace.size() - 1);
			float y = Y(trace[(head + i) % trace.size()]);
			if (i == 0)
				nvgMoveTo(vg, x, y);
			else
				nvgLineTo(vg, x, y);
		}
		glowStroke(vg, a.trace, 1.f);

		glowText(vg, PAD, h - 11.f, fit(vg, info.value, w), a.trace);

		nvgFontSize(vg, 6.5f);
		std::string tag = module->target.kind == Kind::Light ? "LIGHT 0..10"
			: std::string(module->mode == Pin::MODULATE ? "MOD " : "SET ") + (module->bipolar ? "-5..5" : "0..10");
		glowText(vg, PAD, h - 3.5f, fit(vg, tag, w), nvgTransRGBAf(a.trace, 0.6f));
	}
};


struct PinWidget : ModuleWidget {
	bool wasLearning = false;
	bool mouseWas = false;
	Target candidate;
	int frame = 0;

	PinWidget(Pin* module) {
		setModule(module);
		auto* panel = new PanelWidget(4, "pin");
		panel->label(Vec(10.16, 55.f), "LEARN")
			->label(Vec(10.16, 70.f), "DEPTH")
			->label(Vec(10.16, 84.5f), "IN")
			->island(Vec(1.6f, 96.5f), Vec(17.12f, 20.5f))
			->label(Vec(10.16, 101.5f), "OUT", true);
		setPanel(panel);

		addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		auto* screen = createWidget<PinScreen>(mm2px(Vec(1.6f, 12.f)));
		screen->box.size = mm2px(Vec(17.12f, 38.f));
		screen->module = module;
		addChild(screen);

		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(10.16, 61.f)), module, Pin::LEARN_PARAM, Pin::LEARN_LIGHT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(10.16, 75.5f)), module, Pin::DEPTH_PARAM));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(10.16, 90.5f)), module, Pin::CV_INPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(10.16, 109.f)), module, Pin::CV_OUTPUT));
	}

	void step() override {
		ModuleWidget::step();
		Pin* m = getModule<Pin>();
		if (!m)
			return;

		if (m->pasted) {
			m->pasted = false;
			Target t = m->target;
			remap(t, this);
			m->setTarget(t, m->startBase);
		}

		bool learning = m->learning;
		if (learning && !wasLearning) {
			APP->scene->rack->setTouchedParam(NULL);
			// The click that pressed LEARN must not count as the click on a light.
			mouseWas = true;
		}
		wasLearning = learning;
		if (learning)
			learn(m);

		// Keep paste-remap data current, and pick the param up if another mapper lets go of it.
		if (++frame % 60 == 0) {
			refresh(m->target, this);
			m->acquire();
		}
		m->handle.color = ACCENTS[m->accent].trace;
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
		m->bind(t);
		m->learning = false;
		candidate = {};
	}

	void appendContextMenu(Menu* menu) override {
		Pin* m = getModule<Pin>();
		TargetInfo info = describe(m->target, m->value);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(info.module.empty() ? "Not bound" : info.module + ": " + info.name));
		menu->addChild(createMenuItem("Learn", "", [=] { m->learning = true; }));
		menu->addChild(createMenuItem("Unbind", "", [=] { m->bind(Target()); }, !m->target));
		menu->addChild(createIndexPtrSubmenuItem("Hand and CV", {"Modulate around the knob", "Overwrite the knob"}, &m->mode));
		menu->addChild(createIndexSubmenuItem("CV range", {"-5 V to +5 V", "0 V to 10 V"},
			[=] { return m->bipolar ? 0 : 1; },
			[=](size_t i) { m->bipolar = i == 0; }));
		menu->addChild(createIndexPtrSubmenuItem("Screen colour", accentNames(), &m->accent));
	}
};


Model* modelPin = createModel<Pin, PinWidget>("Pin");

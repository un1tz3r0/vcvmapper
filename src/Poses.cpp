#include "plugin.hpp"
#include "core/BindingUi.hpp"
#include "core/Overlay.hpp"
#include "kit/Nvg.hpp"
#include "kit/OledDisplay.hpp"
#include "kit/PanelArt.hpp"
#include "poses/Layout.hpp"
#include "poses/View.hpp"

using namespace puppeteer;
using namespace poses;

/** Each pose's value for each lane of one plan, NAN where the pose doesn't hold that control. Built on the UI
thread and handed to the audio thread whole. */
struct Table {
	uint64_t serial = 0;
	bool filled[POSES] = {};
	float values[POSES][MAX_LANES];
};

/** The capture set's controls as they are, with the model and name of each so a pose survives its module being
replaced by another of the same model. */
using Pose = std::vector<std::pair<Target, float>>;

/** Fills in what a target's model and control are called now, for matching it by name later. */
static Target described(Target t) {
	Module* m = APP->engine->getModule_NoLock(t.moduleId);
	if (!m || !m->model)
		return t;
	t.plugin = m->model->plugin ? m->model->plugin->slug : "";
	t.model = m->model->slug;
	if (t.index < (int) m->paramQuantities.size())
		t.name = m->paramQuantities[t.index]->name;
	return t;
}

static json_t* poseToJson(const Pose& pose) {
	json_t* j = json_array();
	for (const auto& [t, v] : pose)
		json_array_append_new(j, json_pack("{s:o, s:f}", "target", t.toJson(), "value", v));
	return j;
}

static Pose poseFromJson(json_t* j) {
	Pose pose;
	size_t i;
	json_t* e;
	json_array_foreach(j, i, e)
		if (Target t = Target::fromJson(json_object_get(e, "target")))
			pose.push_back({t, clamp((float) json_number_value(json_object_get(e, "value")), 0.f, 1.f)});
	return pose;
}

/** Captures the controls its set reaches as poses, recalls them with a glide, and crossfades between two of them,
either of which can be muted to stand for the controls as the hand left them. */
struct Poses : Module, BindingHost {
	enum Hold { IDLE, GLIDE, FADE };

	Binding binding;
	Pose poses[POSES]; // UI thread
	bool dirty = true;
	uint64_t posted = 0;
	int accent = 0;
	std::atomic<int> selected{0}, a{-1}, b{-1}, capture{-1}, clicked{-1};
	std::atomic<bool> muteA{false}, muteB{false}, armed{false}, learning{false};
	std::atomic<Table*> fresh{nullptr}, spent{nullptr};

	// Audio thread
	Table* table = nullptr;
	Hold hold = IDLE, begin = IDLE;
	int target = 0;
	float from[MAX_LANES], hand[MAX_LANES], left[MAX_LANES];
	float elapsed = 0.f, still = 0.f, seen[5];
	bool handKept = false, ending = false, aWas = false, bWas = false, aUsed = false, bUsed = false;
	int lastSelect = -1;
	dsp::BooleanTrigger buttons[BUTTONS], captureButton, learnButton;
	dsp::SchmittTrigger recallTrigger, captureTrigger;

	Poses() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < BUTTONS; i++)
			configButton(POSE_PARAM + i, string::f("Pose %d: recall, or capture after CAPTURE, or set an end while holding A or B", i + 1));
		configButton(CAPTURE_PARAM, "Capture into the next pose pressed");
		configButton(A_PARAM, "A: hold and press a pose to set it, tap to mute");
		configButton(B_PARAM, "B: hold and press a pose to set it, tap to mute");
		configButton(LEARN_PARAM, "Learn controls into the capture set");
		configParam(FADE_PARAM, 0.f, 1.f, 0.f, "Crossfade", "%", 0.f, 100.f);
		configParam(GLIDE_PARAM, 0.f, 1.f, 0.5f, "Glide", " s", 1000.f, 0.01f, -0.01f);
		configInput(FADE_INPUT, "Crossfade, 0 to 10 V added to the knob");
		configInput(SELECT_INPUT, "Pose select, 0 to 10 V over the 16 poses");
		configInput(RECALL_INPUT, "Recall the selected pose");
		configInput(CAPTURE_INPUT, "Capture into the selected pose");
		configLight(CAPTURE_LIGHT, "Capture armed");
		configLight(LEARN_LIGHT, "Learning");
		std::fill(std::begin(seen), std::end(seen), NAN);
		std::fill(std::begin(left), std::end(left), NAN);
	}

	~Poses() {
		delete fresh.load();
		delete spent.load();
		delete table;
	}

	Binding* bindingAt(int index) override {
		return index == 0 ? &binding : nullptr;
	}

	float glide() {
		return 0.01f * (std::pow(1000.f, params[GLIDE_PARAM].getValue()) - 1.f);
	}

	void recall(int k) {
		selected = k;
		if (!table || !table->filled[k])
			return;
		target = k;
		hold = begin = GLIDE;
		elapsed = 0.f;
		handKept = false;
	}

	void press(int k, bool aDown, bool bDown) {
		if (armed) {
			armed = false;
			selected = k;
			capture = k;
		}
		else if (aDown || bDown) {
			if (aDown)
				a = k, aUsed = true;
			if (bDown)
				b = k, bUsed = true;
		}
		else
			recall(k);
	}

	float value(const Plan& p, int i) {
		const Lane& l = p.lanes[i];
		const float pos = l.position.load(std::memory_order_relaxed);
		if (!table || table->serial != p.serial)
			return NAN;
		if (begin == GLIDE)
			from[i] = pos;
		if (begin == FADE && (!handKept || std::isnan(left[i]) || std::fabs(pos - left[i]) > 1e-3f))
			hand[i] = pos; // the hand moved it since the fader let go, so that's where a muted end now stands
		if (ending)
			left[i] = pos;
		if (hold == GLIDE) {
			const float to = table->values[target][i], u = std::min(1.f, elapsed / std::max(glide(), 1e-6f));
			if (std::isnan(to) || std::isnan(from[i]))
				return to;
			return l.snap ? (u < 0.5f ? from[i] : to) : from[i] + (to - from[i]) * u;
		}
		if (hold == FADE) {
			float va = muteA || a < 0 ? NAN : table->values[a][i], vb = muteB || b < 0 ? NAN : table->values[b][i];
			if (std::isnan(va) && std::isnan(vb))
				return NAN;
			va = std::isnan(va) ? hand[i] : va;
			vb = std::isnan(vb) ? hand[i] : vb;
			const float x = seen[0];
			return l.snap ? (x < 0.5f ? va : vb) : va + (vb - va) * x;
		}
		return NAN;
	}

	void process(const ProcessArgs& args) override {
		if (Table* t = fresh.load(std::memory_order_acquire)) {
			spent.store(table, std::memory_order_relaxed);
			table = t;
			fresh.store(nullptr, std::memory_order_release);
			hold = IDLE;
			handKept = false;
		}
		if (learnButton.process(params[LEARN_PARAM].getValue() > 0.f))
			learning = !learning;
		if (captureButton.process(params[CAPTURE_PARAM].getValue() > 0.f))
			armed = !armed;
		if (captureTrigger.process(inputs[CAPTURE_INPUT].getVoltage(), 0.1f, 1.f))
			capture = selected.load();

		// A or B held while a pose is pressed picks that end; tapped alone, it mutes.
		const bool aDown = params[A_PARAM].getValue() > 0.f, bDown = params[B_PARAM].getValue() > 0.f;
		auto end = [](bool down, bool& was, bool& used, std::atomic<bool>& mute) {
			if (down && !was)
				used = false;
			if (!down && was && !used)
				mute = !mute;
			was = down;
		};
		end(aDown, aWas, aUsed, muteA);
		end(bDown, bWas, bUsed, muteB);
		for (int i = 0; i < BUTTONS; i++)
			if (buttons[i].process(params[POSE_PARAM + i].getValue() > 0.f))
				press(i, aDown, bDown);
		if (int k = clicked.exchange(-1); k >= 0)
			press(k, aDown, bDown);

		if (inputs[SELECT_INPUT].isConnected()) {
			int k = clamp(int(inputs[SELECT_INPUT].getVoltage() * POSES / 10.f), 0, POSES - 1);
			if (k != lastSelect) {
				if (lastSelect >= 0 && !inputs[RECALL_INPUT].isConnected())
					recall(k);
				else
					selected = k;
				lastSelect = k;
			}
		}
		if (recallTrigger.process(inputs[RECALL_INPUT].getVoltage(), 0.1f, 1.f))
			recall(selected);

		// The fader takes hold when it, an end or a mute changes, and lets go once it has been still a while.
		const float now[5] = {clamp(params[FADE_PARAM].getValue() + inputs[FADE_INPUT].getVoltage() / 10.f, 0.f, 1.f),
			float(a), float(b), float(muteA), float(muteB)};
		bool moved = false;
		for (int i = 0; i < 5; i++) {
			if (!std::isnan(seen[i]) && std::fabs(now[i] - seen[i]) > 1e-3f)
				moved = true;
			if (std::isnan(seen[i]) || moved)
				seen[i] = now[i];
		}
		if (moved && !(muteA && muteB)) {
			if (hold != FADE)
				begin = FADE;
			hold = FADE;
			still = 0.f;
		}
		const float wait = std::max(glide(), 0.1f);
		if (hold == FADE && (still += args.sampleTime) > wait)
			ending = true;
		if (hold == GLIDE)
			elapsed += args.sampleTime;

		binding.place(args, [&](const Plan& p, int i) { return value(p, i); });

		begin = IDLE;
		if (ending)
			ending = false, handKept = true, hold = IDLE;
		if (hold == GLIDE && elapsed > glide() + 0.1f)
			hold = IDLE;

		const bool blink = std::fmod(args.frame * (double) args.sampleTime, 0.5) < 0.25;
		for (int i = 0; i < BUTTONS; i++)
			lights[POSE_LIGHT + i].setBrightness(i == selected ? 1.f : table && table->filled[i] ? 0.15f : 0.f);
		lights[CAPTURE_LIGHT].setBrightness(armed && blink);
		lights[LEARN_LIGHT].setBrightness(learning && blink);
		lights[A_LIGHT].setBrightness(muteA ? 0.f : a < 0 ? 0.15f : 1.f);
		lights[B_LIGHT].setBrightness(muteB ? 0.f : b < 0 ? 0.15f : 1.f);
	}

	void processBypass(const ProcessArgs& args) override {
		Module::processBypass(args);
		binding.bypass(args);
	}

	/** UI thread, every frame: captures what the audio thread asked for, and hands it a fresh table whenever the
	poses or the plan change. */
	void tend() override {
		if (int k = capture.exchange(-1); k >= 0)
			setPose(k, take(), "capture pose");
		const Plan* p = binding.plan();
		const uint64_t serial = p ? p->serial : 0;
		if ((!dirty && serial == posted) || fresh.load(std::memory_order_acquire))
			return;
		delete spent.exchange(nullptr);
		Table* t = new Table;
		t->serial = serial;
		for (int k = 0; k < POSES; k++) {
			t->filled[k] = !poses[k].empty();
			std::fill(std::begin(t->values[k]), std::end(t->values[k]), NAN);
			for (int i = 0; p && i < p->count; i++)
				t->values[k][i] = find(poses[k], p->lanes[i].target);
		}
		fresh.store(t, std::memory_order_release);
		posted = serial;
		dirty = false;
	}

	/** By module and control, else by name on a module of the same model that replaced the one captured. */
	static float find(const Pose& pose, const Target& lane) {
		for (const auto& [t, v] : pose)
			if (t.key() == lane.key())
				return v;
		Target d = described(lane);
		for (const auto& [t, v] : pose)
			if (!d.name.empty() && t.model == d.model && t.plugin == d.plugin && t.name == d.name && !APP->engine->getModule_NoLock(t.moduleId))
				return v;
		return NAN;
	}

	Pose take() const {
		Pose pose;
		const Plan* p = binding.plan();
		for (int i = 0; p && i < p->count; i++) {
			const Lane& l = p->lanes[i];
			float x = l.position.load(std::memory_order_relaxed);
			if (l.target.kind == Kind::Param && !l.momentary && !std::isnan(x))
				pose.push_back({described(l.target), x});
		}
		return pose;
	}

	void setPose(int k, Pose pose, const std::string& name);

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		binding.settings = Settings();
		binding.selector = Selector();
		binding.changed();
		for (Pose& p : poses)
			p.clear();
		dirty = true;
		selected = 0, a = -1, b = -1;
		muteA = muteB = armed = learning = false;
	}

	json_t* dataToJson() override {
		json_t* posesJ = json_array();
		for (const Pose& p : poses)
			json_array_append_new(posesJ, poseToJson(p));
		return json_pack("{s:i, s:i, s:i, s:i, s:b, s:b, s:o, s:o}", "accent", accent, "selected", selected.load(),
			"a", a.load(), "b", b.load(), "muteA", muteA.load(), "muteB", muteB.load(), "binding", binding.toJson(), "poses", posesJ);
	}

	void dataFromJson(json_t* rootJ) override {
		auto integer = [&](const char* key, int lo, int hi, int fallback) {
			json_t* j = json_object_get(rootJ, key);
			return j ? clamp((int) json_integer_value(j), lo, hi) : fallback;
		};
		accent = integer("accent", 0, (int) std::size(kit::Theme::ACCENTS) - 1, 0);
		selected = integer("selected", 0, POSES - 1, 0);
		a = integer("a", -1, POSES - 1, -1);
		b = integer("b", -1, POSES - 1, -1);
		muteA = json_is_true(json_object_get(rootJ, "muteA"));
		muteB = json_is_true(json_object_get(rootJ, "muteB"));
		json_t* posesJ = json_object_get(rootJ, "poses");
		for (int k = 0; k < POSES; k++)
			poses[k] = poseFromJson(json_array_get(posesJ, k));
		dirty = true;
		if (json_t* j = json_object_get(rootJ, "binding"))
			binding.fromJson(j, this);
	}
};

struct PoseAction : history::ModuleAction {
	int pose;
	json_t *before, *after;
	PoseAction(int64_t id, int pose, const std::string& name, json_t* before, json_t* after) : pose(pose), before(before), after(after) {
		moduleId = id;
		this->name = name;
	}
	~PoseAction() {
		json_decref(before);
		json_decref(after);
	}
	void apply(json_t* j) {
		if (auto* m = dynamic_cast<Poses*>(APP->engine->getModule(moduleId))) {
			m->poses[pose] = poseFromJson(j);
			m->dirty = true;
		}
	}
	void undo() override {
		apply(before);
	}
	void redo() override {
		apply(after);
	}
};

void Poses::setPose(int k, Pose pose, const std::string& name) {
	json_t* before = poseToJson(poses[k]);
	poses[k] = std::move(pose);
	dirty = true;
	if (APP->history)
		APP->history->push(new PoseAction(id, k, name, before, poseToJson(poses[k])));
	else
		json_decref(before);
}


struct PosesScreen : kit::OledDisplay {
	Poses* module = nullptr;
	ViewState state;

	void step() override {
		theme = kit::Theme::accent(module ? module->accent : 0);
		state = ViewState();
		if (module) {
			Poses* m = module;
			for (int k = 0; k < POSES; k++)
				state.counts[k] = (int) m->poses[k].size();
			state.selected = m->selected;
			state.a = m->a;
			state.b = m->b;
			state.muteA = m->muteA;
			state.muteB = m->muteB;
			state.fade = clamp(m->params[FADE_PARAM].getValue() + m->inputs[FADE_INPUT].getVoltage() / 10.f, 0.f, 1.f);
			state.alert = m->learning || m->armed;
			state.line = m->learning ? "TOUCH KNOBS, LEARN ENDS" : m->armed ? "CAPTURE INTO WHICH POSE?"
				: m->binding.selector.empty() ? "PRESS LEARN" : summary(m->binding);
		}
		else {
			state.counts[0] = 12, state.counts[1] = 12, state.counts[4] = 9;
			state.a = 0, state.b = 1, state.fade = 0.35f;
			state.line = "12 TARGETS";
		}
		OledDisplay::step();
	}

	void paintScene(kit::Painter& painter) override {
		paint(painter, theme, state);
	}

	void onButton(const ButtonEvent& e) override {
		if (!module || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		if (int k = hit(e.pos.x, e.pos.y, box.size.x); k >= 0)
			module->clicked = k;
		e.consume(this);
	}
};


struct PosesWidget : ModuleWidget {
	Learner learner;
	MenuWatch menu;

	PosesWidget(Poses* module) {
		setModule(module);
		const kit::Panel panel = poses::panel();
		kit::addPanel(this, panel);
		const kit::Panel::Rect& glass = panel.screen;
		auto* screen = createWidget<PosesScreen>(mm2px(Vec(glass.min.x, glass.min.y)));
		screen->box.size = mm2px(Vec(glass.max.x - glass.min.x, glass.max.y - glass.min.y));
		screen->module = module;
		addChild(screen);
	}

	void step() override {
		ModuleWidget::step();
		Poses* m = getModule<Poses>();
		if (!m)
			return;
		m->binding.color = kit::nvg(kit::Theme::accent(m->accent).field);
		m->binding.update(this);
		m->tend();
		if (menu.open() || m->learning)
			Overlay::show(m->binding);
		// Learning stays on, so a whole voice can be touched in one go.
		Target t;
		if (learner.step(this, m->learning, t))
			learnInto(m->binding, m, 0, t, true);
	}

	void appendContextMenu(Menu* menu) override {
		Poses* m = getModule<Poses>();
		this->menu.opened(menu);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Capture set"));
		appendBindingMenu(menu, m, 0, [=](bool) { m->learning = true; }, false);
		menu->addChild(new MenuSeparator);
		menu->addChild(createSubmenuItem("Clear a pose", "", [=](Menu* sub) {
			for (int k = 0; k < POSES; k++)
				if (!m->poses[k].empty())
					sub->addChild(createMenuItem(string::f("Pose %d", k + 1), string::f("%d", (int) m->poses[k].size()),
						[=] { m->setPose(k, {}, "clear pose"); }));
		}));
		menu->addChild(createIndexPtrSubmenuItem("Screen colour", std::vector<std::string>(std::begin(kit::Theme::ACCENTS), std::end(kit::Theme::ACCENTS)), &m->accent));
	}
};


Model* modelPoses = createModel<Poses, PosesWidget>("Poses");

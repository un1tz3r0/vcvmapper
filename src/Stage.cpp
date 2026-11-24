#include "plugin.hpp"
#include "core/BindingUi.hpp"
#include "core/Overlay.hpp"
#include "core/Resolve.hpp"
#include "kit/Nvg.hpp"
#include "kit/OledDisplay.hpp"
#include "kit/PanelArt.hpp"
#include "stage/Layout.hpp"
#include "stage/View.hpp"
#include <cstring>

using namespace puppeteer;
using namespace stage;

static std::string sourceName(int s, bool longName = false) {
	if (s < 0)
		return longName ? "None" : "--";
	if (s >= MACRO_SOURCE)
		return string::f(longName ? "Macro %d" : "M%d", s - MACRO_SOURCE + 1);
	if (s < JACKS)
		return string::f(longName ? "Input %d" : "IN%d", s + 1);
	return string::f(longName ? "Crew %c input %d" : "%c%d", 'A' + s / JACKS - 1, s % JACKS + 1);
}

/** The edit page's items: the path, then every setting. */
constexpr int ITEMS = fields::COUNT + 1;

/** A rolling record of a binding's source and its first target, for the edit page's graph. */
struct History {
	static constexpr int SIZE = 160, EVERY = 256; // about 0.85 s at 48 kHz
	std::atomic<float> cv[SIZE], target[SIZE];
	std::atomic<int> head{0};

	void push(float volts, float position) {
		int h = head.load(std::memory_order_relaxed);
		cv[h].store(volts / 5.f, std::memory_order_relaxed);
		target[h].store(position, std::memory_order_relaxed);
		head.store((h + 1) % SIZE, std::memory_order_release);
	}
	void read(std::vector<float>& volts, std::vector<float>& position) const {
		int h = head.load(std::memory_order_acquire);
		volts.clear();
		position.clear();
		for (int i = 0; i < SIZE; i++) {
			volts.push_back(cv[(h + i) % SIZE].load(std::memory_order_relaxed));
			position.push_back(target[(h + i) % SIZE].load(std::memory_order_relaxed));
		}
	}
	void clear() {
		for (int i = 0; i < SIZE; i++)
			cv[i] = target[i] = NAN;
	}
};

struct Stage : Module, BindingHost {
	enum Learn { OFF, REPLACE, ADD };

	Binding bindings[BINDINGS];
	History history[BINDINGS];
	std::atomic<int> sources[BINDINGS];
	std::atomic<float> macros[MACROS];
	std::atomic<int> page{BINDINGS_PAGE}, learning{OFF};
	int cursor[PAGES] = {}; // per page; the bindings page's is the selected binding, the edit page's its item
	int accent = 0;
	int tick = 0;
	Input* ins[PORTS] = {}; // audio thread: this block's jacks, by source
	Output* outs[PORTS] = {};

	Stage() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < SOFT; i++) {
			configButton(PAGE_PARAM + i, std::string("Page: ") + PAGE_NAMES[i]);
			configButton(ACTION_PARAM + i, string::f("Action %d, as labelled on the screen", i + 1));
		}
		for (int i = 0; i < KNOBS; i++)
			configParam(KNOB_PARAM + i, -INFINITY, INFINITY, 0.f, string::f("Encoder %d, as labelled on the screen", i + 1));
		const char* keys[] = {"Up", "Down", "Left", "Right", "Select", "Back", "Shift (latches)"};
		for (int k = 0; k < KEYS; k++)
			configButton(KEY_PARAM + k, keys[k]);
		for (int i = 0; i < JACKS; i++) {
			configInput(CV_INPUT + i, string::f("CV %d", i + 1));
			configOutput(CV_OUTPUT + i, string::f("Target value of the first binding on CV %d", i + 1));
		}
		reset();
	}

	void reset() {
		for (int i = 0; i < BINDINGS; i++) {
			bindings[i].settings = Settings();
			bindings[i].selector = Selector();
			bindings[i].changed();
			sources[i] = i < JACKS ? i : NO_SOURCE;
			history[i].clear();
		}
		for (auto& m : macros)
			m = 0.f;
		std::fill(std::begin(cursor), std::end(cursor), 0);
		page = BINDINGS_PAGE;
		learning = OFF;
	}

	Binding* bindingAt(int index) override {
		return index >= 0 && index < BINDINGS ? &bindings[index] : nullptr;
	}

	/** The source's voltage on its first channel, NAN when it has none. */
	float sourceVolts(int s) {
		if (s >= MACRO_SOURCE)
			return 5.f * macros[s - MACRO_SOURCE].load();
		return s >= 0 && ins[s] && ins[s]->isConnected() ? ins[s]->getVoltage() : NAN;
	}

	/** Gathers the jacks of Stage and of the Crews touching it in a chain to its right. A Crew does nothing in its
	own process(), and cables only move voltages between module steps, so its ports are safe to use here. */
	void gather() {
		std::fill(std::begin(ins), std::end(ins), nullptr);
		std::fill(std::begin(outs), std::end(outs), nullptr);
		for (int j = 0; j < JACKS; j++) {
			ins[j] = &inputs[CV_INPUT + j];
			outs[j] = &outputs[CV_OUTPUT + j];
		}
		Module* x = rightExpander.module;
		for (int c = 1; c <= crew::CREWS && x && x->model == modelCrew; c++, x = x->rightExpander.module)
			for (int j = 0; j < JACKS; j++) {
				ins[c * JACKS + j] = &x->inputs[crew::CV_INPUT + j];
				outs[c * JACKS + j] = &x->outputs[crew::CV_OUTPUT + j];
			}
	}

	void process(const ProcessArgs& args) override {
		gather();
		// Each jack's output reads back the first binding it feeds.
		bool claimed[PORTS] = {};
		for (int i = 0; i < BINDINGS; i++) {
			int s = sources[i].load(std::memory_order_relaxed);
			Binding& b = bindings[i];
			if (s >= MACRO_SOURCE)
				b.process(args, 5.f * macros[s - MACRO_SOURCE].load(std::memory_order_relaxed));
			else if (s < 0 || !ins[s])
				b.bypass(args);
			else if (!claimed[s]) {
				claimed[s] = true;
				b.process(args, *ins[s], *outs[s]);
			}
			else
				b.process(args, *ins[s], 1.f);
		}
		for (int j = 0; j < PORTS; j++)
			if (outs[j] && !claimed[j]) {
				outs[j]->setChannels(1);
				outs[j]->setVoltage(0.f);
			}

		if (++tick >= History::EVERY) {
			tick = 0;
			for (int i = 0; i < BINDINGS; i++) {
				int s = sources[i].load(std::memory_order_relaxed);
				const Plan* p = bindings[i].plan();
				if (s >= 0 || (p && p->count))
					history[i].push(sourceVolts(s), p && p->count ? p->lanes[0].position.load(std::memory_order_relaxed) : NAN);
			}
		}

		bool blink = learning && std::fmod(args.frame * (double) args.sampleTime, 0.5) < 0.25;
		for (int i = 0; i < SOFT; i++)
			lights[PAGE_LIGHT + i].setBrightness(page == i);
		lights[ACTION_LIGHT].setBrightnessSmooth(blink ? 1.f : 0.f, args.sampleTime);
		for (int k = 0; k < KEYS; k++)
			if (k != SHIFT)
				lights[KEY_LIGHT + k].setBrightness(params[KEY_PARAM + k].getValue());
	}

	void processBypass(const ProcessArgs& args) override {
		Module::processBypass(args);
		for (Binding& b : bindings)
			b.bypass(args);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		reset();
	}

	json_t* dataToJson() override {
		json_t* bindingsJ = json_array();
		for (int i = 0; i < BINDINGS; i++)
			json_array_append_new(bindingsJ, json_pack("{s:i, s:o}", "source", sources[i].load(), "binding", bindings[i].toJson()));
		json_t* macrosJ = json_array();
		for (auto& m : macros)
			json_array_append_new(macrosJ, json_real(m.load()));
		json_t* cursorJ = json_array();
		for (int c : cursor)
			json_array_append_new(cursorJ, json_integer(c));
		return json_pack("{s:i, s:i, s:o, s:o, s:o}", "accent", accent, "page", page.load(), "cursor", cursorJ, "macros", macrosJ,
			"bindings", bindingsJ);
	}

	void dataFromJson(json_t* rootJ) override {
		if (json_t* j = json_object_get(rootJ, "accent"))
			accent = clamp((int) json_integer_value(j), 0, (int) std::size(kit::Theme::ACCENTS) - 1);
		if (json_t* j = json_object_get(rootJ, "page"))
			page = clamp((int) json_integer_value(j), 0, PAGES - 1);
		for (int i = 0; i < PAGES; i++)
			if (json_t* j = json_array_get(json_object_get(rootJ, "cursor"), i))
				cursor[i] = std::max(0, (int) json_integer_value(j));
		for (int i = 0; i < MACROS; i++)
			if (json_t* j = json_array_get(json_object_get(rootJ, "macros"), i))
				macros[i] = clamp((float) json_number_value(j), -1.f, 1.f);
		json_t* bindingsJ = json_object_get(rootJ, "bindings");
		for (int i = 0; i < BINDINGS; i++)
			if (json_t* j = json_array_get(bindingsJ, i)) {
				sources[i] = clamp((int) json_integer_value(json_object_get(j, "source")), NO_SOURCE, SOURCES - 1);
				if (json_t* bJ = json_object_get(j, "binding"))
					bindings[i].fromJson(bJ, this);
			}
	}
};


struct StageWidget;

/** The screen takes keyboard focus on a click and hands keys to the widget. */
struct StageScreen : kit::OledDisplay {
	StageWidget* owner = nullptr;
	ViewState state;

	void step() override;
	void paintScene(kit::Painter& painter) override {
		paint(painter, theme, state);
	}
	void onButton(const ButtonEvent& e) override;
	void onSelectKey(const SelectKeyEvent& e) override;
};


struct StageWidget : ModuleWidget {
	static constexpr float CLICKS = 100.f; // per encoder turn
	Learner learner;
	MenuWatch menu;
	StageScreen* screen;
	float seen[KNOBS];
	bool held[PARAMS_LEN] = {};
	bool shiftLatch = false;
	int frames = 0;
	Layout layout; // the rack's groups for the groups page, refreshed while it shows; no widget pointers kept
	std::vector<int> groupMembers, groupTargets;

	StageWidget(Stage* module) {
		setModule(module);
		std::fill(std::begin(seen), std::end(seen), NAN);
		const kit::Panel panel = stage::panel();
		kit::addPanel(this, panel);
		const kit::Panel::Rect& glass = panel.screen;
		screen = createWidget<StageScreen>(mm2px(Vec(glass.min.x, glass.min.y)));
		screen->box.size = mm2px(Vec(glass.max.x - glass.min.x, glass.max.y - glass.min.y));
		screen->owner = this;
		addChild(screen);
	}

	Stage* stage() {
		return getModule<Stage>();
	}
	int selected() {
		return clamp(stage()->cursor[BINDINGS_PAGE], 0, BINDINGS - 1);
	}
	Binding& binding() {
		return stage()->bindings[selected()];
	}
	int rows(int page) {
		return page == BINDINGS_PAGE ? BINDINGS : page == EDIT_PAGE ? ITEMS : page == MACROS_PAGE ? MACROS : (int) layout.groups.size();
	}
	void moveCursor(int by) {
		Stage* m = stage();
		int p = m->page;
		m->cursor[p] = clamp(m->cursor[p] + by, 0, std::max(0, rows(p) - 1));
	}
	void setPage(int p) {
		stage()->page = (p + PAGES) % PAGES;
		APP->event->setSelectedWidget(screen);
	}
	void turnSource(int by) {
		std::atomic<int>& s = stage()->sources[selected()];
		s = clamp(s + by, NO_SOURCE, SOURCES - 1);
	}
	void turnField(const char* name, int by) {
		for (const Field& f : fields::ALL)
			if (!std::strcmp(f.name, name)) {
				f.turn(binding().settings, by);
				binding().changed();
			}
	}
	void turnMacro(int i, int by, bool fine) {
		std::atomic<float>& m = stage()->macros[i];
		m = clamp(std::round(m * 100.f + by * (fine ? 1 : 2)) / 100.f, -1.f, 1.f);
	}
	void learn(bool add) {
		Stage* m = stage();
		m->learning = m->learning ? Stage::OFF : add ? Stage::ADD : Stage::REPLACE;
	}
	void clear() {
		Stage* m = stage();
		int i = selected();
		m->bindings[i].edit(m, i, "clear binding", [](Selector& s) { s = Selector(); });
		m->bindings[i].settings = Settings();
		m->bindings[i].changed();
	}
	void openPath() {
		ui::Menu* menu = createMenu();
		this->menu.opened(menu);
		appendPathMenu(menu, stage(), selected());
	}
	void selectGroup() {
		Stage* m = stage();
		int g = m->cursor[GROUPS_PAGE];
		if (g >= (int) layout.groups.size())
			return;
		Snapshot fresh = Snapshot::take(this);
		app::RackWidget* rack = APP->scene->rack;
		rack->deselectAll();
		// Group indices only hold for the snapshot that numbered them; match the fresh one by position.
		const Layout::Group& want = layout.groups[g];
		for (int i = 0; i < (int) fresh.modules.size(); i++) {
			const Layout::Group& in = fresh.layout.groups[fresh.layout.group[i]];
			if (in.row == want.row && in.x0 == want.x0)
				rack->select(fresh.modules[i]);
		}
	}

	/** The soft buttons down the left, by page. */
	struct Action {
		const char* label;
		void (StageWidget::*run)();
	};
	void learnOne() { learn(false); }
	void learnMore() { learn(true); }
	void toEdit() { setPage(EDIT_PAGE); }
	void zero() { stage()->macros[clamp(stage()->cursor[MACROS_PAGE], 0, MACROS - 1)] = 0.f; }
	void zeroAll() { for (auto& m : stage()->macros) m = 0.f; }
	const Action* actions(int page) {
		static const Action ACTIONS[PAGES][SOFT] = {
			{{"LEARN", &StageWidget::learnOne}, {"LEARN+", &StageWidget::learnMore}, {"CLEAR", &StageWidget::clear}, {"EDIT", &StageWidget::toEdit}},
			{{"LEARN", &StageWidget::learnOne}, {"LEARN+", &StageWidget::learnMore}, {"PATH", &StageWidget::openPath}, {"CLEAR", &StageWidget::clear}},
			{{"ZERO", &StageWidget::zero}, {"ZERO ALL", &StageWidget::zeroAll}, {"", nullptr}, {"", nullptr}},
			{{"SELECT", &StageWidget::selectGroup}, {"", nullptr}, {"", nullptr}, {"", nullptr}},
		};
		return ACTIONS[page];
	}
	void action(int i) {
		if (auto run = actions(stage()->page)[i].run)
			(this->*run)();
	}

	/** The encoders by page: a name and what a turn does. Knobs 4-7 are beside the screen. */
	static constexpr const char* QUICK[SOFT] = {"KNOB FROM", "KNOB TO", "CURVE", "SPREAD"};
	KnobLabel knob(int k) {
		Stage* m = stage();
		const Settings& s = binding().settings;
		auto field = [&](const char* name) -> KnobLabel {
			for (const Field& f : fields::ALL)
				if (!std::strcmp(f.name, name))
					return {name, f.show(s)};
			return {};
		};
		switch (m->page) {
			case MACROS_PAGE: return {string::f("M%d", k + 1), string::f("%+.0f%%", m->macros[k] * 100.f)};
			case GROUPS_PAGE: return k == 0 ? KnobLabel{"ROW", ""} : KnobLabel{};
			default:
				if (k >= SOFT)
					return field(QUICK[k - SOFT]);
				if (m->page == BINDINGS_PAGE) {
					const KnobLabel b[] = {{"ROW", ""}, {"SOURCE", sourceName(m->sources[selected()])}, field("DEPTH"), {"MODE", field("HAND+CV").value}};
					return b[k];
				}
				const KnobLabel e[] = {{"ITEM", ""}, {"VALUE", ""}, {"BINDING", std::to_string(selected() + 1)}, {"SOURCE", sourceName(m->sources[selected()])}};
				return e[k];
		}
	}
	void turn(int k, int by, bool shift) {
		Stage* m = stage();
		int big = shift ? 8 : 1;
		switch (m->page) {
			case MACROS_PAGE: turnMacro(k, by, shift); return;
			case GROUPS_PAGE: if (k == 0) moveCursor(by * big); return;
			default:
				if (k >= SOFT)
					return turnField(QUICK[k - SOFT], by * big);
				if (m->page == BINDINGS_PAGE) {
					if (k == 0) moveCursor(by * big);
					if (k == 1) turnSource(by);
					if (k == 2) turnField("DEPTH", by * big);
					if (k == 3) turnField("HAND+CV", by);
					return;
				}
				if (k == 0) moveCursor(by * big);
				if (k == 1) value(by * big);
				if (k == 2) m->cursor[BINDINGS_PAGE] = clamp(selected() + by * big, 0, BINDINGS - 1);
				if (k == 3) turnSource(by);
		}
	}
	/** The edit page's value: the path opens its menu, a setting turns. */
	void value(int by) {
		int item = stage()->cursor[EDIT_PAGE];
		if (item > 0 && item < ITEMS) {
			fields::ALL[item - 1].turn(binding().settings, by);
			binding().changed();
		}
	}

	/** One keypress or d-pad press. */
	void key(Key k, bool shift) {
		Stage* m = stage();
		int big = shift ? 8 : 1;
		switch (k) {
			case UP: moveCursor(-big); break;
			case DOWN: moveCursor(big); break;
			case LEFT:
			case RIGHT: {
				int by = k == LEFT ? -1 : 1;
				if (m->page == BINDINGS_PAGE) turnSource(by);
				else if (m->page == EDIT_PAGE) value(by * big);
				else if (m->page == MACROS_PAGE) turnMacro(m->cursor[MACROS_PAGE], by * 5, shift);
				else moveCursor(by);
				break;
			}
			case SELECT:
				if (m->page == BINDINGS_PAGE) setPage(EDIT_PAGE);
				else if (m->page == EDIT_PAGE && m->cursor[EDIT_PAGE] == 0) openPath();
				else if (m->page == GROUPS_PAGE) selectGroup();
				break;
			case BACK:
				if (m->learning) m->learning = Stage::OFF;
				else setPage(BINDINGS_PAGE);
				break;
			default: break;
		}
	}

	/** Whole clicks encoder `k` has turned since last asked. */
	int clicks(int k) {
		float v = stage()->params[KNOB_PARAM + k].getValue();
		if (std::isnan(seen[k]) || std::fabs(v - seen[k]) > 0.5f)
			seen[k] = v;
		int n = int((v - seen[k]) * CLICKS);
		seen[k] += n / CLICKS;
		return n;
	}
	bool pressed(int param) {
		bool down = stage()->params[param].getValue() > 0.f;
		bool edge = down && !held[param];
		held[param] = down;
		return edge;
	}

	ViewState view() {
		Stage* m = stage();
		ViewState v;
		v.page = m->page;
		v.focused = APP->event->selectedWidget == screen;
		v.shift = shiftLatch;
		v.cursor = m->cursor[v.page];
		for (int i = 0; i < SOFT; i++)
			v.actions[i] = actions(v.page)[i].label;
		if (m->learning)
			v.actions[0] = "STOP";
		for (int k = 0; k < KNOBS; k++)
			v.knobs[k] = knob(k);

		if (v.page == BINDINGS_PAGE)
			for (int i = 0; i < BINDINGS; i++)
				v.rows.push_back(bindingRow(i));
		else if (v.page == EDIT_PAGE) {
			Binding& b = binding();
			Row r = bindingRow(selected());
			v.heading = string::f("%d %s ", selected() + 1, r.source.c_str()) + (r.note.empty() ? r.label : r.note);
			v.items.push_back({"PATH", b.selector.pattern.empty() ? "SELECT TO ADD" : b.selector.pattern});
			for (const Field& f : fields::ALL)
				v.items.push_back({f.name, f.show(b.settings)});
			m->history[selected()].read(v.source, v.result);
		}
		else if (v.page == MACROS_PAGE)
			for (int i = 0; i < MACROS; i++) {
				Row r;
				r.index = std::to_string(i + 1);
				r.source = string::f("M%d", i + 1);
				int users = 0;
				for (int j = 0; j < BINDINGS; j++)
					users += m->sources[j] == MACRO_SOURCE + i && !m->bindings[j].selector.empty();
				r.label = users == 1 ? "1 BINDING" : string::f("%d BINDINGS", users);
				r.dim = !users;
				r.value = 0.5f + 0.5f * m->macros[i];
				r.base = 0.5f;
				v.rows.push_back(r);
			}
		else
			for (int g = 0; g < (int) layout.groups.size(); g++) {
				const Layout::Group& group = layout.groups[g];
				Row r;
				r.index = std::to_string(g + 1);
				r.source = std::to_string(groupMembers[g]);
				r.label = group.name.empty() ? string::f("row %d, %d HP", group.row + 1, group.x1 - group.x0) : group.name;
				r.dim = group.name.empty();
				r.note = groupTargets[g] ? string::f("%d TARGETS", groupTargets[g]) : "";
				v.rows.push_back(r);
			}
		return v;
	}

	Row bindingRow(int i) {
		static const char* PROBLEMS[] = {"", "", "BAD PATH", "NO MATCH", "MISSING", "IN USE"};
		Stage* m = stage();
		const Binding& b = m->bindings[i];
		Row r;
		r.index = std::to_string(i + 1);
		r.source = sourceName(m->sources[i]);
		Binding::Problem problem = b.problem();
		const Plan* p = b.plan();
		if (p && p->count) {
			const Lane& l = p->lanes[0];
			r.label = summary(b);
			r.value = l.position.load();
			if (l.lease && b.settings.mode == Mode::Modulate && !l.momentary)
				r.base = l.lease.slot->base.load();
		}
		else if (problem == Binding::Problem::Empty)
			r.dim = true;
		r.note = PROBLEMS[int(problem)];
		if (int was = b.shifted(); was >= 0 && problem == Binding::Problem::None)
			r.note = string::f("WAS %d", was), r.alert = true;
		if (m->learning && i == selected())
			r.note = m->learning == Stage::ADD ? "TOUCH ANOTHER" : "TOUCH A KNOB", r.alert = true;
		return r;
	}

	void step() override {
		ModuleWidget::step();
		Stage* m = stage();
		if (!m)
			return;
		NVGcolor color = kit::nvg(kit::Theme::accent(m->accent).field);
		for (Binding& b : m->bindings) {
			b.color = color;
			b.update(this);
		}
		if (m->page == GROUPS_PAGE && frames++ % 30 == 0)
			takeSnapshot();

		if (pressed(KEY_PARAM + SHIFT))
			shiftLatch = !shiftLatch;
		m->lights[KEY_LIGHT + SHIFT].setBrightness(shiftLatch);
		for (int i = 0; i < SOFT; i++) {
			if (pressed(PAGE_PARAM + i))
				setPage(i);
			if (pressed(ACTION_PARAM + i))
				action(i);
		}
		for (int k = UP; k < SHIFT; k++)
			if (pressed(KEY_PARAM + k))
				key(Key(k), shiftLatch);
		for (int k = 0; k < KNOBS; k++)
			if (int n = clicks(k))
				turn(k, n, shiftLatch);

		int sel = selected();
		if (m->page == EDIT_PAGE || m->learning || menu.open())
			Overlay::show(m->bindings[sel]);
		Target t;
		if (learner.step(this, m->learning, t)) {
			bool add = m->learning == Stage::ADD;
			learnInto(m->bindings[sel], m, sel, t, add);
			// Like Marionette: carry on down the bindings while they are empty.
			if (!add && m->page == BINDINGS_PAGE && sel + 1 < BINDINGS && m->bindings[sel + 1].selector.empty())
				m->cursor[BINDINGS_PAGE] = sel + 1;
			else
				m->learning = Stage::OFF;
		}
	}

	void takeSnapshot() {
		Snapshot snapshot = Snapshot::take(this);
		layout = snapshot.layout;
		const Layout& l = layout;
		groupMembers.assign(l.groups.size(), 0);
		groupTargets.assign(l.groups.size(), 0);
		for (int g : l.group)
			groupMembers[g]++;
		for (const Binding& b : stage()->bindings)
			if (const Plan* p = b.plan())
				for (int i = 0; i < p->count; i++)
					if (int at = snapshot.find(p->lanes[i].target.moduleId); at >= 0)
						groupTargets[l.group[at]]++;
		stage()->cursor[GROUPS_PAGE] = clamp(stage()->cursor[GROUPS_PAGE], 0, std::max(0, (int) l.groups.size() - 1));
	}

	void appendContextMenu(Menu* menu) override {
		Stage* m = stage();
		int sel = selected();
		this->menu.opened(menu);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(string::f("Binding %d", sel + 1)));
		menu->addChild(createIndexSubmenuItem("Source", [] {
			std::vector<std::string> names;
			for (int s = NO_SOURCE; s < SOURCES; s++)
				names.push_back(sourceName(s, true));
			return names;
		}(), [=] { return (size_t) (m->sources[sel] + 1); }, [=](size_t i) { m->sources[sel] = int(i) - 1; }));
		appendBindingMenu(menu, m, sel, [=](bool add) {
			m->cursor[BINDINGS_PAGE] = sel;
			m->learning = add ? Stage::ADD : Stage::REPLACE;
		}, true);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Click the screen, then arrows, Enter, Esc, Backspace, Tab and Shift"));
		menu->addChild(createIndexPtrSubmenuItem("Screen colour", std::vector<std::string>(std::begin(kit::Theme::ACCENTS), std::end(kit::Theme::ACCENTS)), &m->accent));
	}
};


void StageScreen::step() {
	Stage* m = owner ? owner->stage() : nullptr;
	theme = kit::Theme::accent(m ? m->accent : 0);
	if (m)
		state = owner->view();
	else {
		state = ViewState();
		for (int i = 0; i < SOFT; i++)
			state.actions[i] = i ? "" : "LEARN";
	}
	OledDisplay::step();
}

void StageScreen::onButton(const ButtonEvent& e) {
	Stage* m = owner ? owner->stage() : nullptr;
	if (!m || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
		return;
	// Consuming the click is what selects the screen for keys.
	int page = m->page;
	if (page != EDIT_PAGE) {
		int n = owner->rows(page);
		if (int i = hit(e.pos.y, box.size.y, m->cursor[page], n); i >= 0)
			m->cursor[page] = i;
	}
	e.consume(this);
}

void StageScreen::onSelectKey(const SelectKeyEvent& e) {
	Stage* m = owner ? owner->stage() : nullptr;
	if (!m || (e.action != GLFW_PRESS && e.action != GLFW_REPEAT) || (e.mods & RACK_MOD_CTRL))
		return;
	bool shift = (e.mods & GLFW_MOD_SHIFT) || owner->shiftLatch;
	switch (e.key) {
		case GLFW_KEY_UP: owner->key(UP, shift); break;
		case GLFW_KEY_DOWN: owner->key(DOWN, shift); break;
		case GLFW_KEY_LEFT: owner->key(LEFT, shift); break;
		case GLFW_KEY_RIGHT: owner->key(RIGHT, shift); break;
		case GLFW_KEY_ENTER:
		case GLFW_KEY_KP_ENTER: owner->key(SELECT, shift); break;
		case GLFW_KEY_ESCAPE: owner->key(BACK, shift); break;
		case GLFW_KEY_BACKSPACE:
		case GLFW_KEY_DELETE: if (m->page <= EDIT_PAGE) owner->clear(); break;
		case GLFW_KEY_TAB: owner->setPage(m->page + (shift ? -1 : 1)); break;
		default: return;
	}
	e.consume(this);
}


Model* modelStage = createModel<Stage, StageWidget>("Stage");

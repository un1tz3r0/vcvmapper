#include "plugin.hpp"
#include "core/BindingUi.hpp"
#include "core/Overlay.hpp"
#include "kit/Nvg.hpp"
#include "kit/OledDisplay.hpp"
#include "kit/PanelArt.hpp"
#include "reel/Layout.hpp"
#include "reel/View.hpp"
#include <cstring>
#include <map>
#include <memory>

using namespace puppeteer;
using namespace reel;

/** A change to one control: milliseconds into the clip, and where it went in 0..65535 of its travel. */
struct Event {
	uint32_t tick;
	uint16_t value;
};
struct Track {
	Target target; // with model and name, so a clip survives its module being replaced by one of the same model
	std::vector<Event> events;
};
/** A recording, never changed once made, so the audio thread can play it while the UI thread replaces it. */
struct Clip {
	uint32_t length = 0; // ms
	std::vector<Track> tracks;
};

/** The value at `tick`, with `cursor` left on the last event at or before it, so steady playback is a walk. */
static float valueAt(const Track& t, uint32_t tick, int& cursor) {
	const std::vector<Event>& e = t.events;
	if (cursor < 0 || cursor >= (int) e.size() || e[cursor].tick > tick)
		cursor = int(std::upper_bound(e.begin(), e.end(), tick, [](uint32_t x, const Event& v) { return x < v.tick; }) - e.begin()) - 1;
	while (cursor + 1 < (int) e.size() && e[cursor + 1].tick <= tick)
		cursor++;
	return e[std::max(cursor, 0)].value / 65535.f;
}

static json_t* clipToJson(const Clip& c) {
	json_t* tracksJ = json_array();
	for (const Track& t : c.tracks) {
		std::vector<uint8_t> bytes(t.events.size() * 6);
		for (size_t i = 0; i < t.events.size(); i++) {
			std::memcpy(&bytes[6 * i], &t.events[i].tick, 4);
			std::memcpy(&bytes[6 * i + 4], &t.events[i].value, 2);
		}
		json_array_append_new(tracksJ, json_pack("{s:o, s:s}", "target", t.target.toJson(), "events", string::toBase64(bytes).c_str()));
	}
	return json_pack("{s:I, s:o}", "length", (json_int_t) c.length, "tracks", tracksJ);
}

static std::shared_ptr<const Clip> clipFromJson(json_t* j) {
	if (!json_is_object(j))
		return nullptr;
	auto c = std::make_shared<Clip>();
	c->length = (uint32_t) json_integer_value(json_object_get(j, "length"));
	size_t i;
	json_t* t;
	json_array_foreach(json_object_get(j, "tracks"), i, t) {
		Track track{Target::fromJson(json_object_get(t, "target")), {}};
		const char* s = json_string_value(json_object_get(t, "events"));
		std::vector<uint8_t> bytes = string::fromBase64(s ? s : "");
		for (size_t k = 0; k + 6 <= bytes.size(); k += 6) {
			Event e;
			std::memcpy(&e.tick, &bytes[k], 4);
			std::memcpy(&e.value, &bytes[k + 4], 2);
			track.events.push_back(e);
		}
		std::stable_sort(track.events.begin(), track.events.end(), [](const Event& a, const Event& b) { return a.tick < b.tick; });
		if (track.target && !track.events.empty())
			c->tracks.push_back(std::move(track));
	}
	return c->tracks.empty() ? nullptr : c;
}

/** Each clip's track for each lane of one plan. Built on the UI thread and handed to the audio thread whole. */
struct Deck {
	uint64_t serial = 0;
	std::shared_ptr<const Clip> clips[CLIPS];
	float seconds[CLIPS] = {};
	const Track* tracks[CLIPS][MAX_LANES] = {};
};

/** What recording tells the UI thread: a change on a lane, or the start (lane START, tick the clip) or end
(lane END, tick the length) of a take. Single producer, single consumer. */
struct Note {
	enum { START = -1, END = -2 };
	int lane;
	uint32_t tick;
	float value;
	uint64_t serial;
};
template <class T, size_t N>
struct Queue {
	T items[N];
	std::atomic<size_t> head{0}, tail{0};
	bool push(const T& x) {
		size_t t = tail.load(std::memory_order_relaxed);
		if (t - head.load(std::memory_order_acquire) == N)
			return false;
		items[t % N] = x;
		tail.store(t + 1, std::memory_order_release);
		return true;
	}
	bool pop(T& x) {
		size_t h = head.load(std::memory_order_relaxed);
		if (h == tail.load(std::memory_order_acquire))
			return false;
		x = items[h % N];
		head.store(h + 1, std::memory_order_release);
		return true;
	}
};

/** A clip's crop, as fractions of its length, and how it plays. Set from either thread. */
struct Take {
	std::atomic<float> start{0.f}, end{1.f}, speed{1.f};
	std::atomic<bool> loop{false};
	void reset() {
		start = 0.f, end = 1.f, speed = 1.f, loop = false;
	}
};

/** Records the controls its set reaches as clips of changes over time, and plays them back over the hand. */
struct Reel : Module, BindingHost {
	enum State { IDLE, ARMED, RECORDING };
	static constexpr float TURN = 0.25f; // of a clip per encoder turn

	Binding binding;
	std::shared_ptr<const Clip> clips[CLIPS]; // UI thread
	Take takes[CLIPS];
	int accent = 0;
	bool dirty = true;
	uint64_t posted = 0;
	std::atomic<int> selected{0}, cap{300}, state{IDLE};
	std::atomic<bool> gates{false}, learning{false}, overflowed{false};
	std::atomic<float> recorded{0.f}, heads[CLIPS];
	std::atomic<bool> playing[CLIPS];
	std::atomic<Deck*> fresh{nullptr}, spent{nullptr};
	Queue<Note, 1 << 14> notes;

	// UI thread: the take coming in
	std::shared_ptr<Clip> taking;
	int takingInto = 0;
	uint64_t takingSerial = 0;
	std::map<int, size_t> takingLanes;

	// Audio thread
	Deck* deck = nullptr;
	double head[CLIPS] = {}, now = 0.;
	bool run[CLIPS] = {}, gateWas[CLIPS] = {};
	int64_t order[CLIPS] = {}, starts = 0;
	int cursor[CLIPS][MAX_LANES];
	int scrubbed = -1, into = 0;
	float scrubStill = 0.f, encoders[3] = {NAN, NAN, NAN};
	float baseline[MAX_LANES], last[MAX_LANES];
	bool touched[MAX_LANES] = {}, arming = false, starting = false, sample = false;
	uint32_t tick = 0;
	dsp::BooleanTrigger clipButtons[CLIPS], armButton, loopButton, learnButton;
	dsp::SchmittTrigger playTriggers[CLIPS], recTrigger;
	dsp::PulseGenerator endPulses[CLIPS];

	Reel() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < CLIPS; i++) {
			configButton(CLIP_PARAM + i, string::f("Clip %d: select and play, or stop", i + 1));
			configInput(PLAY_INPUT + i, string::f("Play clip %d", i + 1));
			configOutput(END_OUTPUT + i, string::f("Clip %d ended or looped", i + 1));
			heads[i] = NAN;
			playing[i] = false;
		}
		configButton(ARM_PARAM, "Arm: the next movement records into the selected clip; again to stop");
		configButton(LOOP_PARAM, "Loop the selected clip");
		configButton(LEARN_PARAM, "Learn controls into the capture set");
		configParam(START_PARAM, -INFINITY, INFINITY, 0.f, "Crop start of the selected clip");
		configParam(END_PARAM, -INFINITY, INFINITY, 0.f, "Crop end of the selected clip");
		configParam(SCRUB_PARAM, -INFINITY, INFINITY, 0.f, "Scrub the selected clip");
		configParam(RATE_PARAM, -2.f, 2.f, 0.f, "Rate", "x", 2.f);
		configInput(REC_INPUT, "Start or stop recording");
		configInput(RATE_INPUT, "Rate, 1 V/octave");
		configOutput(PLAYING_OUTPUT, "Any clip playing");
		configLight(ARM_LIGHT, "Armed, or recording");
		configLight(LOOP_LIGHT, "Selected clip loops");
		configLight(LEARN_LIGHT, "Learning");
		std::fill(&cursor[0][0], &cursor[0][0] + CLIPS * MAX_LANES, -1);
	}

	~Reel() {
		delete fresh.load();
		delete spent.load();
		delete deck;
	}

	Binding* bindingAt(int index) override {
		return index == 0 ? &binding : nullptr;
	}

	bool filled(int k) const {
		return deck && deck->clips[k];
	}

	void play(int k) {
		if (!filled(k))
			return;
		run[k] = true;
		head[k] = takes[k].start * deck->seconds[k];
		order[k] = ++starts;
	}

	void press(int k) {
		if (run[k] && k == selected)
			run[k] = false;
		else {
			selected = k;
			play(k);
		}
	}

	void begin(bool capture) {
		state = RECORDING;
		into = selected;
		now = 0.;
		tick = 0;
		sample = starting = true;
		arming = capture;
		std::fill(std::begin(touched), std::end(touched), false);
	}

	void finish() {
		if (state == RECORDING && !starting)
			emit(Note::END, tick, 0.f, 0);
		state = IDLE;
		starting = false;
	}

	void emit(int lane, uint32_t at, float value, uint64_t serial) {
		if (!notes.push({lane, at, value, serial}))
			overflowed = true;
	}

	float value(const Plan& p, int i) {
		const Lane& l = p.lanes[i];
		const float pos = l.position.load(std::memory_order_relaxed);
		if (starting) {
			starting = false;
			overflowed = false;
			emit(Note::START, into, 0.f, p.serial);
		}
		if (arming)
			baseline[i] = pos, touched[i] = false;
		if (state == ARMED && !std::isnan(pos) && std::fabs(pos - baseline[i]) > 1e-3f) {
			begin(false);
			starting = false;
			emit(Note::START, into, 0.f, p.serial);
		}
		// Only changes are kept: a lane's first one also keeps where it was before, at the clip's start.
		if (state == RECORDING && sample && !std::isnan(pos) && !std::isnan(baseline[i])) {
			if (!touched[i]) {
				if (std::fabs(pos - baseline[i]) > 1e-3f) {
					touched[i] = true;
					emit(i, 0, baseline[i], p.serial);
					emit(i, tick, pos, p.serial);
					last[i] = pos;
				}
			}
			else if (std::fabs(pos - last[i]) > 1e-3f) {
				emit(i, tick, pos, p.serial);
				last[i] = pos;
			}
		}
		// The clip started last wins a lane two clips share.
		if (!deck || deck->serial != p.serial)
			return NAN;
		float v = NAN;
		int64_t newest = -1;
		for (int c = 0; c < CLIPS; c++) {
			const Track* t = deck->tracks[c][i];
			if (t && (run[c] || c == scrubbed) && order[c] > newest) {
				v = valueAt(*t, uint32_t(head[c] * 1000.), cursor[c][i]);
				newest = order[c];
			}
		}
		return v;
	}

	void process(const ProcessArgs& args) override {
		const float dt = args.sampleTime;
		if (Deck* d = fresh.load(std::memory_order_acquire)) {
			spent.store(deck, std::memory_order_relaxed);
			deck = d;
			fresh.store(nullptr, std::memory_order_release);
			std::fill(&cursor[0][0], &cursor[0][0] + CLIPS * MAX_LANES, -1);
			for (int k = 0; k < CLIPS; k++)
				run[k] = run[k] && filled(k);
		}
		if (learnButton.process(params[LEARN_PARAM].getValue() > 0.f))
			learning = !learning;
		const int sel = selected;
		if (loopButton.process(params[LOOP_PARAM].getValue() > 0.f))
			takes[sel].loop = !takes[sel].loop;
		if (armButton.process(params[ARM_PARAM].getValue() > 0.f)) {
			if (state == IDLE)
				state = ARMED, arming = true;
			else
				finish();
		}
		if (recTrigger.process(inputs[REC_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (state == RECORDING)
				finish();
			else
				begin(true);
		}

		for (int k = 0; k < CLIPS; k++) {
			if (clipButtons[k].process(params[CLIP_PARAM + k].getValue() > 0.f))
				press(k);
			Input& in = inputs[PLAY_INPUT + k];
			if (gates) {
				bool high = in.getVoltage() >= 1.f;
				if (high && !gateWas[k])
					play(k);
				if (!high && gateWas[k])
					run[k] = false;
				gateWas[k] = high;
			}
			else if (playTriggers[k].process(in.getVoltage(), 0.1f, 1.f))
				play(k);
		}

		// Encoders: crop start, crop end and scrub, each a quarter of the clip per turn.
		for (int e = 0; e < 3; e++) {
			const float v = params[START_PARAM + e].getValue();
			float d = std::isnan(encoders[e]) || std::fabs(v - encoders[e]) > 1.f ? 0.f : v - encoders[e];
			encoders[e] = v;
			if (d == 0.f || !filled(sel))
				continue;
			Take& t = takes[sel];
			if (e == 0)
				t.start = clamp(t.start + d * TURN, 0.f, t.end - 1e-3f);
			else if (e == 1)
				t.end = clamp(t.end + d * TURN, t.start + 1e-3f, 1.f);
			else {
				const float len = deck->seconds[sel];
				head[sel] = clamp(float(head[sel]) + d * TURN * len, t.start * len, t.end * len);
				if (!run[sel])
					scrubbed = sel, scrubStill = 0.f, order[sel] = ++starts;
			}
		}
		if (scrubbed >= 0 && (run[scrubbed] || (scrubStill += dt) > 0.5f))
			scrubbed = -1;

		const float rate = std::pow(2.f, clamp(params[RATE_PARAM].getValue() + inputs[RATE_INPUT].getVoltage(), -4.f, 4.f));
		for (int k = 0; k < CLIPS; k++) {
			if (!run[k])
				continue;
			const Take& t = takes[k];
			const double len = deck->seconds[k], s = t.start * len, e = t.end * len;
			head[k] += dt * rate * t.speed;
			if (head[k] >= e) {
				endPulses[k].trigger(1e-3f);
				if (t.loop && e > s)
					head[k] = s + std::fmod(head[k] - s, e - s);
				else
					run[k] = false;
			}
		}

		if (state == RECORDING) {
			now += dt;
			uint32_t t = uint32_t(now * 1000.);
			sample = sample || t != tick;
			tick = t;
		}
		binding.place(args, [&](const Plan& p, int i) { return value(p, i); });
		arming = sample = false;
		if (state == RECORDING) {
			recorded = float(now);
			if (cap > 0 && now >= cap)
				finish();
		}

		const bool blink = std::fmod(args.frame * (double) args.sampleTime, 0.5) < 0.25;
		bool any = false;
		for (int k = 0; k < CLIPS; k++) {
			any |= run[k];
			playing[k] = run[k];
			heads[k] = run[k] || k == scrubbed ? float(head[k] / std::max(1e-3f, deck ? deck->seconds[k] : 0.f)) : NAN;
			outputs[END_OUTPUT + k].setVoltage(endPulses[k].process(dt) ? 10.f : 0.f);
			lights[CLIP_LIGHT + k].setBrightness(run[k] ? 1.f : k == sel ? 0.4f : filled(k) ? 0.1f : 0.f);
		}
		outputs[PLAYING_OUTPUT].setVoltage(any ? 10.f : 0.f);
		lights[ARM_LIGHT].setBrightness(state == RECORDING || (state == ARMED && blink));
		lights[LOOP_LIGHT].setBrightness(takes[sel].loop);
		lights[LEARN_LIGHT].setBrightness(learning && blink);
	}

	void processBypass(const ProcessArgs& args) override {
		Module::processBypass(args);
		binding.bypass(args);
	}

	/** UI thread, every frame: gathers the take coming in, and hands the audio thread a fresh deck whenever the
	clips or the plan change. */
	void tend() override {
		Note n;
		const Plan* p = binding.plan();
		while (notes.pop(n)) {
			if (n.lane == Note::START) {
				taking = std::make_shared<Clip>();
				takingInto = clamp((int) n.tick, 0, CLIPS - 1);
				takingSerial = n.serial;
				takingLanes.clear();
			}
			else if (n.lane == Note::END) {
				if (taking && !taking->tracks.empty()) {
					taking->length = n.tick;
					clips[takingInto] = taking;
					takes[takingInto].reset();
					dirty = true;
				}
				taking.reset();
			}
			else if (taking && p && p->serial == n.serial && n.serial == takingSerial && n.lane < p->count) {
				auto [it, added] = takingLanes.try_emplace(n.lane, taking->tracks.size());
				if (added)
					taking->tracks.push_back({described(p->lanes[n.lane].target), {}});
				taking->tracks[it->second].events.push_back({n.tick, uint16_t(clamp(n.value, 0.f, 1.f) * 65535.f + 0.5f)});
			}
		}

		const uint64_t serial = p ? p->serial : 0;
		if ((!dirty && serial == posted) || fresh.load(std::memory_order_acquire))
			return;
		delete spent.exchange(nullptr);
		Deck* d = new Deck;
		d->serial = serial;
		for (int k = 0; k < CLIPS; k++) {
			const Clip* c = (d->clips[k] = clips[k]).get();
			if (!c)
				continue;
			d->seconds[k] = c->length / 1000.f;
			for (int i = 0; p && i < p->count; i++) {
				int j = puppeteer::recorded((int) c->tracks.size(), [&](int t) -> const Target& { return c->tracks[t].target; }, p->lanes[i].target);
				d->tracks[k][i] = j < 0 ? nullptr : &c->tracks[j];
			}
		}
		fresh.store(d, std::memory_order_release);
		posted = serial;
		dirty = false;
	}

	void clear(int k) {
		clips[k] = nullptr;
		takes[k].reset();
		dirty = true;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		binding.settings = Settings();
		binding.selector = Selector();
		binding.changed();
		for (int k = 0; k < CLIPS; k++)
			clear(k);
		selected = 0, cap = 300, state = IDLE;
		gates = learning = false;
	}

	json_t* dataToJson() override {
		json_t* clipsJ = json_array();
		for (int k = 0; k < CLIPS; k++) {
			const Take& t = takes[k];
			json_t* j = clips[k] ? clipToJson(*clips[k]) : json_object();
			json_object_set_new(j, "start", json_real(t.start));
			json_object_set_new(j, "end", json_real(t.end));
			json_object_set_new(j, "speed", json_real(t.speed));
			json_object_set_new(j, "loop", json_boolean(t.loop));
			json_array_append_new(clipsJ, j);
		}
		return json_pack("{s:i, s:i, s:i, s:b, s:o, s:o}", "accent", accent, "selected", selected.load(), "cap", cap.load(),
			"gates", gates.load(), "binding", binding.toJson(), "clips", clipsJ);
	}

	void dataFromJson(json_t* rootJ) override {
		if (json_t* j = json_object_get(rootJ, "accent"))
			accent = clamp((int) json_integer_value(j), 0, (int) std::size(kit::Theme::ACCENTS) - 1);
		if (json_t* j = json_object_get(rootJ, "selected"))
			selected = clamp((int) json_integer_value(j), 0, CLIPS - 1);
		if (json_t* j = json_object_get(rootJ, "cap"))
			cap = std::max(0, (int) json_integer_value(j));
		gates = json_is_true(json_object_get(rootJ, "gates"));
		json_t* clipsJ = json_object_get(rootJ, "clips");
		for (int k = 0; k < CLIPS; k++) {
			json_t* j = json_array_get(clipsJ, k);
			clips[k] = clipFromJson(j);
			Take& t = takes[k];
			t.reset();
			auto number = [&](const char* key, float lo, float hi, std::atomic<float>& out) {
				if (json_t* n = json_object_get(j, key))
					out = clamp((float) json_number_value(n), lo, hi);
			};
			number("start", 0.f, 1.f, t.start);
			number("end", 0.f, 1.f, t.end);
			number("speed", 0.0625f, 16.f, t.speed);
			t.loop = json_is_true(json_object_get(j, "loop"));
			if (t.end <= t.start)
				t.start = 0.f, t.end = 1.f;
		}
		dirty = true;
		if (json_t* j = json_object_get(rootJ, "binding"))
			binding.fromJson(j, this);
	}
};


struct ReelScreen : kit::OledDisplay {
	Reel* module = nullptr;
	ViewState state;
	std::shared_ptr<const Clip> drawn; // held, so a new clip can't reuse its address
	std::vector<std::vector<kit::Vec2f>> curves;

	/** Each track as steps across the clip, thinned to a few hundred points. */
	void trace(const std::shared_ptr<const Clip>& c) {
		drawn = c;
		curves.clear();
		for (size_t i = 0; c && c->length && i < std::min<size_t>(c->tracks.size(), 16); i++) {
			const std::vector<Event>& e = c->tracks[i].events;
			const size_t step = std::max<size_t>(1, e.size() / 200);
			std::vector<kit::Vec2f> pts;
			for (size_t k = 0; k < e.size(); k += step) {
				const float x = std::min(1.f, e[k].tick / float(c->length)), y = e[k].value / 65535.f;
				if (!pts.empty())
					pts.push_back({x, pts.back().y});
				pts.push_back({x, y});
			}
			pts.push_back({1.f, pts.back().y});
			curves.push_back(std::move(pts));
		}
	}

	void step() override {
		theme = kit::Theme::accent(module ? module->accent : 0);
		state = ViewState();
		if (module) {
			Reel* m = module;
			const int k = m->selected;
			const std::shared_ptr<const Clip>& c = m->clips[k];
			if (c != drawn)
				trace(c);
			for (int i = 0; i < CLIPS; i++) {
				state.filled[i] = m->clips[i] != nullptr;
				state.playing[i] = m->playing[i];
			}
			state.selected = k;
			state.curves = curves;
			state.seconds = c ? c->length / 1000.f : 0.f;
			state.start = m->takes[k].start;
			state.end = m->takes[k].end;
			state.loop = m->takes[k].loop;
			state.head = m->heads[k];
			const int s = m->state;
			state.alert = m->learning || s != Reel::IDLE || m->overflowed;
			state.line = m->learning ? "TOUCH KNOBS, LEARN ENDS"
				: s == Reel::ARMED ? string::f("ARMED: MOVE A KNOB FOR CLIP %d", k + 1)
				: s == Reel::RECORDING ? "REC " + clock(m->recorded) + (m->cap > 0 ? " OF " + clock((float) m->cap) : "")
				: m->overflowed ? "TOO MANY CHANGES, SOME LOST"
				: m->binding.selector.empty() ? "PRESS LEARN" : summary(m->binding);
		}
		else {
			state.filled[0] = state.filled[1] = state.filled[3] = true;
			state.playing[1] = true;
			state.selected = 1;
			state.line = "3 TARGETS";
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
			module->selected = k;
		e.consume(this);
	}
};


struct ReelWidget : ModuleWidget {
	Learner learner;
	MenuWatch menu;

	ReelWidget(Reel* module) {
		setModule(module);
		const kit::Panel panel = reel::panel();
		kit::addPanel(this, panel);
		const kit::Panel::Rect& glass = panel.screen;
		auto* screen = createWidget<ReelScreen>(mm2px(Vec(glass.min.x, glass.min.y)));
		screen->box.size = mm2px(Vec(glass.max.x - glass.min.x, glass.max.y - glass.min.y));
		screen->module = module;
		addChild(screen);
	}

	void step() override {
		ModuleWidget::step();
		Reel* m = getModule<Reel>();
		if (!m)
			return;
		m->binding.color = kit::nvg(kit::Theme::accent(m->accent).field);
		m->binding.update(this);
		m->tend();
		if (menu.open() || m->learning)
			Overlay::show(m->binding);
		Target t;
		if (learner.step(this, m->learning, t))
			learnInto(m->binding, m, 0, t, true);
	}

	void appendContextMenu(Menu* menu) override {
		Reel* m = getModule<Reel>();
		const int k = m->selected;
		this->menu.opened(menu);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Capture set"));
		appendBindingMenu(menu, m, 0, [=](bool) { m->learning = true; }, false);
		menu->addChild(new MenuSeparator);
		const std::vector<std::string> caps = {"1 minute", "5 minutes", "30 minutes", "None"};
		const int seconds[] = {60, 300, 1800, 0};
		menu->addChild(createSubmenuItem("Recording length cap", "", [=](Menu* sub) {
			for (int i = 0; i < 4; i++)
				sub->addChild(createCheckMenuItem(caps[i], "", [=] { return m->cap == seconds[i]; }, [=] { m->cap = seconds[i]; }));
		}));
		menu->addChild(createBoolMenuItem("PLAY jacks are gates", "", [=] { return m->gates.load(); }, [=](bool g) { m->gates = g; }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(string::f("Clip %d", k + 1)));
		const float speeds[] = {0.25f, 0.5f, 1.f, 2.f, 4.f};
		menu->addChild(createSubmenuItem("Speed", string::f("%gx", m->takes[k].speed.load()), [=](Menu* sub) {
			for (float s : speeds)
				sub->addChild(createCheckMenuItem(string::f("%gx", s), "", [=] { return m->takes[k].speed == s; }, [=] { m->takes[k].speed = s; }));
		}));
		menu->addChild(createMenuItem("Reset the crop", "", [=] { m->takes[k].start = 0.f, m->takes[k].end = 1.f; }, !m->clips[k]));
		menu->addChild(createMenuItem("Clear", "", [=] { m->clear(k); }, !m->clips[k]));
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Screen colour", std::vector<std::string>(std::begin(kit::Theme::ACCENTS), std::end(kit::Theme::ACCENTS)), &m->accent));
	}
};


Model* modelReel = createModel<Reel, ReelWidget>("Reel");

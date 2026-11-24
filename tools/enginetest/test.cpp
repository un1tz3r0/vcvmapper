// Runs the modules against Rack's real engine, headless: no widgets, so update() gets no owner and patterns don't
// resolve, but everything process() and the slots do is real.
#include "../../src/plugin.hpp"
#include "core/Binding.hpp"
#include "pin/Layout.hpp"
#include "marionette/Layout.hpp"
#include "groups/Layout.hpp"
#include "tether/Layout.hpp"
#include "stage/Layout.hpp"
#include "poses/Layout.hpp"
#include <cstdio>

using namespace puppeteer;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static bool near(float a, float b, float tol = 2e-3f) { return std::fabs(a - b) <= tol; }

struct Dummy : Module {
	Dummy() {
		config(4, 0, 0, 1);
		configParam(0, 0.f, 10.f, 5.f, "Freq");
		configParam(1, 0.f, 10.f, 5.f, "Level");
		configSwitch(2, 0.f, 2.f, 1.f, "Mode", {"A", "B", "C"});
		configButton(3, "Go");
		configLight(0, "Gate");
	}
};

rack::engine::Engine* makeEngine();
static engine::Engine* E;
static void run(int frames = 256) { E->stepBlock(frames); }
static float scaled(Module* m, int p) { return m->paramQuantities[p]->getScaledValue(); }
static Binding& binding(Module* m, int i = 0) { return *dynamic_cast<BindingHost*>(m)->bindingAt(i); }

static Target param(Module* m, int p) {
	Target t; t.kind = Kind::Param; t.moduleId = m->id; t.index = p; return t;
}
static json_t* bindingJ(std::vector<Target> targets, const char* mode = "modulate", double depth = 1.0) {
	Selector s; s.targets = targets;
	return json_pack("{s:{s:s, s:f}, s:o}", "settings", "mode", mode, "depth", depth, "selector", s.toJson());
}
static Module* add(Model* model, int64_t id, json_t* dataJ) {
	Module* m = model->createModule();
	m->id = id;
	m->dataFromJson(dataJ);
	json_decref(dataJ);
	for (Output& o : m->outputs) o.channels = 1; // as if cables were plugged in
	E->addModule(m);
	return m;
}
static Module* makePin(int64_t id, json_t* bJ) { return add(modelPin, id, json_pack("{s:o}", "binding", bJ)); }
static void cv(Module* m, std::vector<float> volts, int input = 0) {
	Input& in = m->inputs[input];
	in.channels = volts.size(); // Port::setChannels ignores a port with no cable
	for (size_t i = 0; i < volts.size(); i++) in.setVoltage(volts[i], i);
}
static void unplug(Module* m, int input = 0) { m->inputs[input].channels = 0; }
static void uiSteps(Module* m, int bindings = 1, int n = 3) {
	for (int i = 0; i < n; i++) { for (int b = 0; b < bindings; b++) binding(m, b).update(nullptr); run(64); }
}
static void remove(Module* m) { E->removeModule(m); delete m; }
static Dummy* dummy(int64_t id) { auto* d = new Dummy; d->id = id; E->addModule(d); return d; }

static void modulateAndHand() {
	Dummy* d = dummy(1);
	Module* p = makePin(10, bindingJ({param(d, 0)}));
	cv(p, {5.f}); run();
	CHECK(near(scaled(d, 0), 1.f), "+5 V modulates to the top, got %f", scaled(d, 0));
	cv(p, {0.f}); run();
	CHECK(near(scaled(d, 0), 0.5f), "0 V sits at the hand position, got %f", scaled(d, 0));
	cv(p, {-2.5f}); run();
	CHECK(near(scaled(d, 0), 0.25f), "-2.5 V, got %f", scaled(d, 0));
	cv(p, {0.f}); run();
	d->paramQuantities[0]->setScaledValue(0.7f); // a hand
	run();
	cv(p, {1.f}); run();
	CHECK(near(scaled(d, 0), 0.8f), "the hand moved the centre to 0.7, got %f", scaled(d, 0));
	p->params[pin::DEPTH_PARAM].setValue(-0.5f); run();
	CHECK(near(scaled(d, 0), 0.65f), "depth -50%%, got %f", scaled(d, 0));
	p->params[pin::DEPTH_PARAM].setValue(1.f);
	unplug(p); run();
	CHECK(near(scaled(d, 0), 0.7f), "unplugging returns to the hand, got %f", scaled(d, 0));
	remove(p); remove(d);
}

static void manyToOne(int threads) {
	settings::threadCount = threads;
	Dummy* d = dummy(2);
	Module* a = makePin(20, bindingJ({param(d, 0)}));
	Module* b = makePin(21, bindingJ({param(d, 0)}));
	cv(a, {1.f}); cv(b, {2.f});
	for (int i = 0; i < 50; i++) run();
	CHECK(near(scaled(d, 0), 0.8f), "[%d threads] offsets add with no drift, got %f", threads, scaled(d, 0));
	unplug(a); run();
	CHECK(near(scaled(d, 0), 0.7f), "[%d threads] one left, got %f", threads, scaled(d, 0));
	unplug(b); run();
	CHECK(near(scaled(d, 0), 0.5f), "[%d threads] both gone, back to the hand, got %f", threads, scaled(d, 0));
	remove(a);
	cv(b, {-1.f}); run();
	CHECK(near(scaled(d, 0), 0.4f), "[%d threads] the slot survives one binding's deletion, got %f", threads, scaled(d, 0));
	remove(b); remove(d);
	settings::threadCount = 1;
}

static void overwriteAndPickUp() {
	Dummy* d = dummy(3);
	Module* p = makePin(30, bindingJ({param(d, 0)}, "overwrite"));
	cv(p, {5.f}); run();
	CHECK(near(scaled(d, 0), 1.f), "overwrite +5 V, got %f", scaled(d, 0));
	d->paramQuantities[0]->setScaledValue(0.2f); run();
	CHECK(near(scaled(d, 0), 1.f), "overwrite ignores the hand, got %f", scaled(d, 0));
	unplug(p); run();
	CHECK(near(scaled(d, 0), 1.f), "overwrite leaves the knob where CV put it, got %f", scaled(d, 0));
	remove(p);

	d->paramQuantities[0]->setScaledValue(0.5f);
	p = makePin(31, bindingJ({param(d, 0)}, "pickup"));
	cv(p, {-5.f}); run();
	CHECK(near(scaled(d, 0), 0.5f), "pick-up waits for the CV to reach the knob, got %f", scaled(d, 0));
	for (float v = -5.f; v <= 1.f; v += 0.05f) { cv(p, {v}); run(32); }
	CHECK(near(scaled(d, 0), 0.6f, 0.01f), "then follows it, got %f", scaled(d, 0));
	d->paramQuantities[0]->setScaledValue(0.9f); run();
	cv(p, {2.f}); run();
	CHECK(near(scaled(d, 0), 0.9f), "the hand drops the pick-up, got %f", scaled(d, 0));
	remove(p); remove(d);
}

static void lightsAndPoly() {
	Dummy* d = dummy(4);
	d->lights[0].setBrightness(0.3f);
	Target light; light.kind = Kind::Light; light.moduleId = d->id; light.index = 0;
	Module* p = makePin(40, bindingJ({param(d, 0), param(d, 1), light}));
	cv(p, {1.f, -1.f}); run();
	Output& out = p->outputs[pin::CV_OUTPUT];
	CHECK(out.getChannels() == 3, "one output channel per target, got %d", out.getChannels());
	CHECK(near(scaled(d, 0), 0.6f) && near(scaled(d, 1), 0.4f), "poly channels drive their own targets, got %f %f", scaled(d, 0), scaled(d, 1));
	CHECK(near(out.getVoltage(2), 3.f), "light brightness out, got %f", out.getVoltage(2));
	cv(p, {2.f}); run();
	CHECK(near(scaled(d, 0), 0.7f) && near(scaled(d, 1), 0.7f), "mono drives every target, got %f %f", scaled(d, 0), scaled(d, 1));
	CHECK(near(out.getVoltage(0), 2.f), "param value out, got %f", out.getVoltage(0));
	remove(p); remove(d);
}

static void otherMappers() {
	Dummy* d = dummy(5);
	engine::ParamHandle foreign;
	E->addParamHandle(&foreign);
	E->updateParamHandle(&foreign, d->id, 0, true);
	Module* p = makePin(50, bindingJ({param(d, 0)}));
	cv(p, {5.f}); run();
	CHECK(near(scaled(d, 0), 0.5f), "never writes a param another mapper holds, got %f", scaled(d, 0));
	E->updateParamHandle(&foreign, -1, 0, true);
	for (int i = 0; i < 70; i++) binding(p).update(nullptr);
	run();
	CHECK(near(scaled(d, 0), 1.f), "picks it up once they let go, got %f", scaled(d, 0));
	// Unmap from the knob's menu
	E->updateParamHandle(E->getParamHandle(d->id, 0), -1, 0, true);
	uiSteps(p);
	CHECK(binding(p).selector.targets.empty(), "Unmap drops the target from the binding");
	CHECK(!E->getParamHandle(d->id, 0), "and the slot is gone");
	E->removeParamHandle(&foreign);
	remove(p); remove(d);
}

static void audioRate() {
	Dummy* d = dummy(6);
	Module* p = makePin(60, bindingJ({param(d, 0)}));
	auto changes = [&](float hz) {
		int n = 0; float last = -1;
		for (int i = 0; i < 9600; i++) {
			cv(p, {2.f * std::sin(2.f * float(M_PI) * hz * i / 48000.f)});
			run(1);
			float v = scaled(d, 0);
			if (i >= 4800 && v != last) n++;
			last = v;
		}
		return n;
	};
	int slow = changes(2.f), fast = changes(300.f);
	CHECK(slow <= 4800 / 32 + 2, "a 2 Hz LFO writes every 32 samples, got %d writes", slow);
	CHECK(fast > 4000, "a 300 Hz CV writes every sample, got %d writes", fast);
	remove(p); remove(d);
}

static void buttons() {
	Dummy* d = dummy(7);
	Module* p = makePin(70, bindingJ({param(d, 3)}));
	// Without widgets nothing can tell the binding its target is a button, so drive a slot by hand.
	remove(p);
	Slot s(d->id, 3);
	E->addParamHandle(&s.handle);
	E->updateParamHandle(&s.handle, d->id, 3, false);
	Cell& c = s.cells[0];
	c.state = Cell::EDGE;
	s.used = 1; // as registry::acquire would
	int64_t frame = 0;
	auto tick = [&] { s.tick(frame++); };
	tick();
	CHECK(d->params[3].getValue() == 0.f, "arming writes nothing");
	c.value = 1.f; c.stamp = frame; tick();
	CHECK(d->params[3].getValue() == 1.f, "a press writes once");
	d->params[3].setValue(0.f); tick(); tick();
	CHECK(d->params[3].getValue() == 0.f, "the hand is free between edges");
	c.value = 0.f; c.stamp = frame; d->params[3].setValue(1.f); tick();
	CHECK(d->params[3].getValue() == 0.f, "a release writes once");
	E->removeParamHandle(&s.handle);
	remove(d);

	Press press;
	float dt = 1.f / 48000.f;
	int down = 0;
	bool on = press.process(5.f, 1e-3f, dt);
	for (int i = 0; i < 200; i++) down += press.process(0.f, 1e-3f, dt);
	CHECK(on && down >= 46 && down <= 49, "a one-sample trigger holds for 1 ms, got %d samples", down + 1);
}

static void saveAndLoad() {
	Dummy* d = dummy(8);
	Module* p = makePin(80, bindingJ({param(d, 0)}));
	d->paramQuantities[0]->setScaledValue(0.3f);
	cv(p, {2.f}); run();
	CHECK(near(scaled(d, 0), 0.5f), "modulated before saving, got %f", scaled(d, 0));
	json_t* saved = p->dataToJson();
	remove(p); run();
	CHECK(near(scaled(d, 0), 0.3f), "deleting Pin hands the knob back, got %f", scaled(d, 0));
	d->paramQuantities[0]->setScaledValue(0.5f); // as the patch would have saved it
	p = add(modelPin, 81, saved);
	cv(p, {2.f}); run();
	CHECK(near(scaled(d, 0), 0.5f), "a reloaded patch keeps modulating around the saved hand position, got %f", scaled(d, 0));
	unplug(p); run();
	CHECK(near(scaled(d, 0), 0.3f), "and returns to it, got %f", scaled(d, 0));
	remove(p);

	// Pin 2.0's format
	d->paramQuantities[0]->setScaledValue(0.5f);
	Target t = param(d, 0);
	p = add(modelPin, 82, json_pack("{s:i, s:b, s:i, s:f, s:o}", "mode", 1, "bipolar", 0, "accent", 2, "base", 0.5, "target", t.toJson()));
	cv(p, {10.f}); run();
	json_t* now = p->dataToJson();
	CHECK(near(scaled(d, 0), 1.f) && json_integer_value(json_object_get(now, "accent")) == 2, "Pin 2.0 patches load as overwrite 0..10 V, got %f", scaled(d, 0));
	json_decref(now);
	remove(p); remove(d);
}

static void bypassAndPresets() {
	Dummy* d = dummy(9);
	Module* p = makePin(90, bindingJ({param(d, 0)}));
	cv(p, {2.f}); run();
	E->bypassModule(p, true); run();
	CHECK(near(scaled(d, 0), 0.5f), "bypass takes the modulation off, got %f", scaled(d, 0));
	E->bypassModule(p, false); run();
	CHECK(near(scaled(d, 0), 0.7f), "and un-bypass puts it back, got %f", scaled(d, 0));
	// A preset onto a running module: the engine lock is held, so the widget-side update rebuilds.
	json_t* dataJ = json_pack("{s:o}", "binding", bindingJ({param(d, 1)}));
	p->dataFromJson(dataJ); // as Engine::moduleFromJson would, on a module that is already running
	json_decref(dataJ);
	uiSteps(p);
	run();
	CHECK(near(scaled(d, 0), 0.5f) && near(scaled(d, 1), 0.7f), "a preset moves the binding to its target, got %f %f", scaled(d, 0), scaled(d, 1));
	remove(p); remove(d);
}

static void marionette8() {
	using namespace marionette;
	Dummy* d = dummy(11);
	Dummy* e = dummy(12);
	json_t* bindingsJ = json_array();
	json_array_append_new(bindingsJ, bindingJ({param(d, 0)}));
	json_array_append_new(bindingsJ, bindingJ({param(d, 0)}, "modulate", -0.5)); // a second say in the same knob
	json_array_append_new(bindingsJ, bindingJ({param(d, 1), param(e, 1)}, "overwrite"));
	Module* s = add(modelMarionette, 100, json_pack("{s:i, s:o}", "selected", 2, "bindings", bindingsJ));
	cv(s, {2.f}, CV_INPUT + 0);
	cv(s, {2.f}, CV_INPUT + 1);
	cv(s, {-5.f, 5.f}, CV_INPUT + 2);
	run();
	CHECK(near(scaled(d, 0), 0.6f), "two channels on one knob add, each with its own depth, got %f", scaled(d, 0));
	CHECK(near(scaled(d, 1), 0.f) && near(scaled(e, 1), 1.f), "a poly channel fans out, got %f %f", scaled(d, 1), scaled(e, 1));
	CHECK(s->outputs[CV_OUTPUT + 2].getChannels() == 2 && near(s->outputs[CV_OUTPUT + 2].getVoltage(1), 5.f), "and reads back per target");
	CHECK(s->outputs[CV_OUTPUT + 5].getVoltage() == 0.f, "empty channels are silent");

	// What the VALUE encoder does on the edit page
	Binding& b = binding(s, 0);
	fields::ALL[0].turn(b.settings, -25); // depth 100% -> 50%
	b.changed();
	uiSteps(s, CHANNELS);
	CHECK(near(scaled(d, 0), 0.5f), "editing depth from the screen takes effect, got %f", scaled(d, 0));

	json_t* saved = s->dataToJson();
	remove(s); run();
	CHECK(near(scaled(d, 0), 0.5f), "deleting Marionette hands the knobs back, got %f", scaled(d, 0));
	s = add(modelMarionette, 101, saved);
	cv(s, {2.f}, CV_INPUT + 0);
	run();
	CHECK(near(scaled(d, 0), 0.6f) && binding(s, 0).settings.depth == 0.5f, "channels and their settings reload, got %f", scaled(d, 0));
	E->bypassModule(s, true); run();
	CHECK(near(scaled(d, 0), 0.5f), "bypass lets every channel go, got %f", scaled(d, 0));
	remove(s); remove(d); remove(e);
}

static void wideAndSpread() {
	std::vector<Dummy*> ds;
	std::vector<Target> ts;
	for (int i = 0; i < 10; i++) {
		ds.push_back(dummy(100 + i));
		ts.push_back(param(ds.back(), 0));
		ts.push_back(param(ds.back(), 1));
	}
	Module* p = makePin(60, bindingJ(ts, "overwrite"));
	cv(p, {0.f, 5.f}); run();
	CHECK(binding(p).plan()->count == 20, "more targets than poly channels, got %d", binding(p).plan()->count);
	CHECK(near(scaled(ds[8], 0), 0.5f) && near(scaled(ds[8], 1), 1.f) && near(scaled(ds[9], 1), 1.f),
		"lanes past the channels wrap round them, got %f %f", scaled(ds[8], 0), scaled(ds[8], 1));
	CHECK(p->outputs[pin::CV_OUTPUT].getChannels() == 16, "the output reads back the first 16");
	remove(p);

	json_t* bJ = bindingJ({param(ds[0], 0), param(ds[1], 0), param(ds[2], 0)}, "overwrite");
	json_object_set_new(json_object_get(bJ, "settings"), "spread", json_real(0.4));
	p = makePin(61, bJ);
	cv(p, {0.f}); run();
	CHECK(near(scaled(ds[0], 0), 0.3f) && near(scaled(ds[1], 0), 0.5f) && near(scaled(ds[2], 0), 0.7f),
		"spread fans the targets out, got %f %f %f", scaled(ds[0], 0), scaled(ds[1], 0), scaled(ds[2], 0));
	remove(p);
	for (Dummy* d : ds) remove(d);
}

static void troupeMacros() {
	Dummy* d = dummy(200);
	json_t* bindingsJ = json_pack("[o, o]", bindingJ({param(d, 0)}), bindingJ({param(d, 1)}, "overwrite"));
	Module* t = add(modelTroupe, 201, json_pack("{s:s, s:o}", "name", "Voice: 1*", "bindings", bindingsJ));
	run();
	CHECK(near(scaled(d, 0), 0.5f) && near(scaled(d, 1), 0.5f), "macros at zero leave the knobs alone, got %f %f", scaled(d, 0), scaled(d, 1));
	t->params[troupe::MACRO_PARAM].setValue(0.5f);
	t->params[troupe::MACRO_PARAM + 1].setValue(-1.f);
	run();
	CHECK(near(scaled(d, 0), 0.75f), "macro 1 at +50%% modulates by a quarter of the travel, got %f", scaled(d, 0));
	CHECK(near(scaled(d, 1), 0.f), "macro 2 overwrites, got %f", scaled(d, 1));
	json_t* j = t->dataToJson();
	CHECK(std::string(json_string_value(json_object_get(j, "name"))) == "Voice 1", "the name loses pattern syntax");
	CHECK(binding(t, 0).locked && binding(t, 3).locked, "macros are locked to the group");
	json_decref(j);
	t->params[troupe::MACRO_PARAM].setValue(0.f);
	run();
	CHECK(near(scaled(d, 0), 0.5f), "and hand the knob back, got %f", scaled(d, 0));
	Module* w = add(modelWing, 202, json_object());
	Module* g = add(modelTag, 203, json_pack("{s:s}", "name", "lead"));
	run();
	remove(t); remove(w); remove(g); remove(d);
}

struct Other : Module {
	Other() {
		config(2, 0, 0, 0);
		configParam(0, 0.f, 1.f, 0.5f, "Resonance");
		configParam(1, 0.f, 1.f, 0.5f, "FREQ");
	}
};

static void tetherCords() {
	static plugin::Model a, b;
	a.slug = "A";
	b.slug = "B";
	b.name = "Other";
	Dummy* same = dummy(300);
	same->model = &a;
	auto* other = new Other;
	other->id = 301;
	other->model = &b;
	E->addModule(other);
	Dummy* learned = dummy(302); // where Freq was learned; no cord reaches it
	learned->model = &a;
	Selector sel;
	Target g = param(learned, 0);
	g.model = "A";
	g.name = "Freq";
	sel.grips = {g};
	Module* t = add(modelTether, 303, json_pack("{s:{s:{s:s}, s:o}}", "binding", "settings", "mode", "overwrite", "selector", sel.toJson()));
	auto cord = [&](std::vector<int64_t> ids) { binding(t).cabled = ids; binding(t).changed(); uiSteps(t); };

	cord({});
	CHECK(!binding(t).plan() || binding(t).plan()->count == 0, "no cord, no target");
	cord({same->id});
	cv(t, {1.f, -2.f}, tether::IN_INPUT); run();
	CHECK(near(scaled(same, 0), 0.6f), "same model by id, driven by IN while CV is unpatched, got %f", scaled(same, 0));
	CHECK(t->outputs[tether::THRU_OUTPUT].getChannels() == 2 && near(t->outputs[tether::THRU_OUTPUT].getVoltage(1), -2.f), "THRU copies IN");
	cv(t, {-5.f}, tether::CV_INPUT); run();
	CHECK(near(scaled(same, 0), 0.f), "CV wins over IN, got %f", scaled(same, 0));
	cord({other->id, same->id});
	cv(t, {0.f}, tether::CV_INPUT); run();
	CHECK(binding(t).plan()->count == 2 && near(scaled(other, 1), 0.5f) && near(scaled(other, 0), 0.5f) && near(scaled(same, 0), 0.5f),
		"another model by name, one lane per cord, got %f %f", scaled(other, 1), scaled(same, 0));
	cv(t, {5.f}, tether::CV_INPUT); run();
	CHECK(near(scaled(other, 1), 1.f) && near(scaled(other, 0), 0.5f), "the name match is the one driven, got %f", scaled(other, 1));

	g.name = "Cutoff";
	binding(t).selector.grips = {g};
	cord({other->id});
	CHECK(binding(t).plan()->count == 0 && binding(t).missing == "Other has no Cutoff", "a miss says why: '%s'", binding(t).missing.c_str());
	unplug(t, tether::CV_INPUT); unplug(t, tether::IN_INPUT);
	remove(t); remove(same); remove(other); remove(learned);
}

static void posesFade() {
	Dummy* d = dummy(500);
	Module* m = add(modelPoses, 501, json_pack("{s:o}", "binding", bindingJ({param(d, 0), param(d, 1), param(d, 2), param(d, 3)})));
	auto tend = [&] { for (int i = 0; i < 3; i++) { binding(m).update(nullptr); dynamic_cast<BindingHost*>(m)->tend(); run(64); } };
	auto tap = [&](int id) { m->params[id].setValue(1.f); run(64); m->params[id].setValue(0.f); run(64); };
	auto set = [&](float x, float y, float sw) {
		d->paramQuantities[0]->setScaledValue(x); d->paramQuantities[1]->setScaledValue(y); d->paramQuantities[2]->setValue(sw); run();
	};
	auto check = [&](float x, float y, float sw, const char* what) {
		CHECK(near(scaled(d, 0), x) && near(scaled(d, 1), y) && d->params[2].getValue() == sw, "%s: want %g %g %g, got %f %f %g",
			what, x, y, sw, scaled(d, 0), scaled(d, 1), d->params[2].getValue());
	};
	tend();
	set(0.2f, 0.8f, 0.f);
	tap(poses::CAPTURE_PARAM); tap(poses::POSE_PARAM + 0); tend();
	set(0.6f, 0.4f, 2.f);
	tap(poses::CAPTURE_PARAM); tap(poses::POSE_PARAM + 1); tend();
	json_t* j = m->dataToJson();
	CHECK(json_array_size(json_array_get(json_object_get(j, "poses"), 1)) == 4, "captured into pose 2");
	json_decref(j);

	// Holding an end while pressing a pose picks it, without muting it on release.
	m->params[poses::A_PARAM].setValue(1.f); run(64); tap(poses::POSE_PARAM + 0); m->params[poses::A_PARAM].setValue(0.f); run(64);
	m->params[poses::B_PARAM].setValue(1.f); run(64); tap(poses::POSE_PARAM + 1); m->params[poses::B_PARAM].setValue(0.f); run();
	check(0.2f, 0.8f, 0.f, "the fader at A");
	m->params[poses::FADE_PARAM].setValue(0.25f); run();
	check(0.3f, 0.7f, 0.f, "a quarter of the way to B, the switch still on A's side");
	cv(m, {5.f}, poses::FADE_INPUT); run();
	check(0.5f, 0.5f, 2.f, "CV adds to the fader");
	cv(m, {0.f}, poses::FADE_INPUT); unplug(m, poses::FADE_INPUT); run(int(E->getSampleRate()));

	// Muted, A stands for the controls as the fader found them, or as the hand has left them since.
	d->paramQuantities[0]->setScaledValue(0.9f); run();
	tap(poses::A_PARAM);
	m->params[poses::FADE_PARAM].setValue(0.f); run();
	check(0.9f, 0.4f, 2.f, "muted A is the hand's new 0.9 and the 0.4 from before the fader took hold");
	m->params[poses::FADE_PARAM].setValue(1.f); run();
	check(0.6f, 0.4f, 2.f, "and B is still B");
	run(int(E->getSampleRate()));

	// Recall glides over GLIDE, then lets go.
	m->params[poses::GLIDE_PARAM].setValue(std::log(101.f) / std::log(1000.f)); // one second
	tap(poses::POSE_PARAM + 0);
	run(int(E->getSampleRate() / 2) - 128);
	CHECK(near(scaled(d, 0), 0.4f, 0.02f), "halfway through a one second glide from 0.6 to 0.2, got %f", scaled(d, 0));
	run(int(E->getSampleRate()));
	check(0.2f, 0.8f, 0.f, "recalled");
	d->paramQuantities[0]->setScaledValue(0.5f); run();
	CHECK(near(scaled(d, 0), 0.5f), "and let go, got %f", scaled(d, 0));
	remove(m); remove(d);
}

static void stagePool() {
	Dummy* d = dummy(400);
	json_t* bindingsJ = json_array();
	auto entry = [&](int source, json_t* bJ) { json_array_append_new(bindingsJ, json_pack("{s:i, s:o}", "source", source, "binding", bJ)); };
	entry(2, bindingJ({param(d, 0)}));               // IN3
	entry(2, bindingJ({param(d, 1)}, "overwrite"));  // IN3 again, no readback
	entry(stage::MACRO_SOURCE + 4, bindingJ({param(d, 2)}, "overwrite")); // M5
	entry(-1, bindingJ({param(d, 3)}));              // no source
	Module* s = add(modelStage, 401, json_pack("{s:o, s:[f, f, f, f, f]}", "bindings", bindingsJ, "macros", 0.0, 0.0, 0.0, 0.0, -1.0));
	cv(s, {2.5f}, stage::CV_INPUT + 2); run();
	CHECK(near(scaled(d, 0), 0.75f) && near(scaled(d, 1), 0.75f), "two bindings share IN3, got %f %f", scaled(d, 0), scaled(d, 1));
	CHECK(near(s->outputs[stage::CV_OUTPUT + 2].getVoltage(), 2.5f), "OUT3 reads back the first, got %f", s->outputs[stage::CV_OUTPUT + 2].getVoltage());
	CHECK(near(d->params[2].getValue(), 0.f), "M5 at -100%% overwrites the switch to its bottom, got %f", d->params[2].getValue());
	json_t* j = s->dataToJson();
	CHECK(json_integer_value(json_object_get(json_array_get(json_object_get(j, "bindings"), 2), "source")) == stage::MACRO_SOURCE + 4, "sources save");
	CHECK(json_integer_value(json_object_get(json_array_get(json_object_get(j, "bindings"), 4), "source")) == 4, "unused bindings default to their own jack");
	CHECK(json_number_value(json_array_get(json_object_get(j, "macros"), 4)) == -1.0, "macros save");
	json_decref(j);
	unplug(s, stage::CV_INPUT + 2); run();
	CHECK(near(scaled(d, 0), 0.5f), "unplugging hands IN3's knob back, got %f", scaled(d, 0));

	// Crews chain to the right: B's third input is source 8 * 2 + 2.
	Module* a = add(modelCrew, 402, json_object());
	Module* b = add(modelCrew, 403, json_object());
	s->rightExpander.moduleId = a->id;
	a->leftExpander.moduleId = s->id;
	a->rightExpander.moduleId = b->id;
	b->leftExpander.moduleId = a->id;
	binding(s, 0).selector = Selector();
	binding(s, 0).changed();
	binding(s, 5).fromJson(bindingJ({param(d, 0)}), s);
	json_t* sJ = s->dataToJson();
	json_integer_set(json_object_get(json_array_get(json_object_get(sJ, "bindings"), 5), "source"), 18);
	s->dataFromJson(sJ);
	json_decref(sJ);
	uiSteps(s, 8);
	cv(b, {-5.f}, crew::CV_INPUT + 2);
	for (int i = 0; i < 4; i++) run(512);
	CHECK(near(scaled(d, 0), 0.f), "Crew B's third input drives its binding, got %f", scaled(d, 0));
	CHECK(near(b->outputs[crew::CV_OUTPUT + 2].getVoltage(), -5.f), "and its output reads it back, got %f", b->outputs[crew::CV_OUTPUT + 2].getVoltage());
	b->leftExpander.moduleId = -1;
	s->rightExpander.moduleId = -1;
	run();
	CHECK(near(scaled(d, 0), 0.5f), "unchaining the Crew lets go, got %f", scaled(d, 0));
	unplug(b, crew::CV_INPUT + 2);
	remove(a); remove(b);
	remove(s); remove(d);
}

static void fieldsRoundTrip() {
	Settings s;
	for (const Field& f : fields::ALL) {
		std::string before = f.show(s);
		f.turn(s, 3);
		f.turn(s, -100);
		f.turn(s, 100);
		f.turn(s, -100);
		CHECK(!f.show(s).empty() && !before.empty(), "%s shows a value", f.name);
	}
	CHECK(s.depth == -1.f && s.shape.steps == 0 && s.interval == 1 && s.slew == 0.f && !s.autoRate && s.mode == Mode::Modulate, "fields clamp at their ends");
	fields::ALL[10].turn(s, 4);
	CHECK(s.interval == 32, "write rate steps through its list, got %d", s.interval);
}

int main() {
	setvbuf(stdout, nullptr, _IONBF, 0);
	E = makeEngine();
	modulateAndHand();
	manyToOne(1);
	manyToOne(4);
	overwriteAndPickUp();
	lightsAndPoly();
	otherMappers();
	audioRate();
	buttons();
	saveAndLoad();
	bypassAndPresets();
	marionette8();
	wideAndSpread();
	troupeMacros();
	tetherCords();
	stagePool();
	posesFade();
	fieldsRoundTrip();
	std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
	return failures != 0;
}

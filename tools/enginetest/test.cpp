// Runs the modules against Rack's real engine, headless: no widgets, so update() gets no owner and patterns don't
// resolve, but everything process() and the slots do is real.
#include "../../src/plugin.hpp"
#include "core/Binding.hpp"
#include "pin/Layout.hpp"
#include "strings/Layout.hpp"
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

static void strings8() {
	using namespace strings;
	Dummy* d = dummy(11);
	Dummy* e = dummy(12);
	json_t* bindingsJ = json_array();
	json_array_append_new(bindingsJ, bindingJ({param(d, 0)}));
	json_array_append_new(bindingsJ, bindingJ({param(d, 0)}, "modulate", -0.5)); // a second say in the same knob
	json_array_append_new(bindingsJ, bindingJ({param(d, 1), param(e, 1)}, "overwrite"));
	Module* s = add(modelStrings, 100, json_pack("{s:i, s:o}", "selected", 2, "bindings", bindingsJ));
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
	CHECK(near(scaled(d, 0), 0.5f), "deleting Strings hands the knobs back, got %f", scaled(d, 0));
	s = add(modelStrings, 101, saved);
	cv(s, {2.f}, CV_INPUT + 0);
	run();
	CHECK(near(scaled(d, 0), 0.6f) && binding(s, 0).settings.depth == 0.5f, "channels and their settings reload, got %f", scaled(d, 0));
	E->bypassModule(s, true); run();
	CHECK(near(scaled(d, 0), 0.5f), "bypass lets every channel go, got %f", scaled(d, 0));
	remove(s); remove(d); remove(e);
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
	strings8();
	fieldsRoundTrip();
	std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
	return failures != 0;
}

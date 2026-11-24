#include "Binding.hpp"
#include "Resolve.hpp"
#include <algorithm>

namespace puppeteer {

static const char* MODES[] = {"modulate", "overwrite", "pickup"};
static const char* TWINS[] = {"none", "row", "column", "all"};

json_t* Settings::toJson() const {
	return json_pack("{s:s, s:f, s:b, s:f, s:f, s:f, s:b, s:i, s:f, s:i, s:b, s:f, s:s, s:f}",
		"mode", MODES[int(mode)], "depth", depth, "bipolar", shape.bipolar, "lo", shape.lo, "hi", shape.hi, "curve", shape.curve,
		"invert", shape.invert, "steps", shape.steps, "slew", slew, "interval", interval, "autoRate", autoRate,
		"hold", hold, "twins", TWINS[int(twins)], "spread", spread);
}

void Settings::fromJson(json_t* rootJ) {
	*this = Settings();
	auto number = [&](const char* key, auto& out, double lo, double hi) {
		if (json_t* j = json_object_get(rootJ, key))
			out = std::clamp(json_number_value(j), lo, hi);
	};
	auto flag = [&](const char* key, bool& out) {
		if (json_t* j = json_object_get(rootJ, key))
			out = json_boolean_value(j);
	};
	if (const char* m = json_string_value(json_object_get(rootJ, "mode")))
		for (int i = 0; i < 3; i++)
			if (std::string(m) == MODES[i])
				mode = Mode(i);
	if (const char* t = json_string_value(json_object_get(rootJ, "twins")))
		for (int i = 0; i < 4; i++)
			if (std::string(t) == TWINS[i])
				twins = Twins(i);
	number("spread", spread, -1, 1);
	number("depth", depth, -1, 1);
	flag("bipolar", shape.bipolar);
	number("lo", shape.lo, 0, 1);
	number("hi", shape.hi, 0, 1);
	number("curve", shape.curve, -1, 1);
	flag("invert", shape.invert);
	number("steps", shape.steps, 0, 128);
	number("slew", slew, 0, 10000);
	number("interval", interval, 1, 4096);
	flag("autoRate", autoRate);
	number("hold", hold, 0, 1000);
}

json_t* Selector::toJson() const {
	json_t* targetsJ = json_array();
	for (const Target& t : targets)
		json_array_append_new(targetsJ, t.toJson());
	json_t* gripsJ = json_array();
	for (const Target& t : grips)
		json_array_append_new(gripsJ, t.toJson());
	json_t* excludedJ = json_array();
	for (const Target& t : excluded)
		json_array_append_new(excludedJ, t.toJson());
	return json_pack("{s:o, s:o, s:s, s:o}", "targets", targetsJ, "grips", gripsJ, "pattern", pattern.c_str(), "excluded", excludedJ);
}

Selector Selector::fromJson(json_t* rootJ) {
	Selector s;
	auto list = [&](const char* key, std::vector<Target>& out) {
		size_t i;
		json_t* j;
		json_array_foreach(json_object_get(rootJ, key), i, j)
			if (Target t = Target::fromJson(j))
				out.push_back(t);
	};
	list("targets", s.targets);
	list("grips", s.grips);
	list("excluded", s.excluded);
	if (const char* p = json_string_value(json_object_get(rootJ, "pattern")))
		s.pattern = p;
	return s;
}


float Lane::read(int64_t block) {
	// A module pointer is good for the rest of the block: deleting a module takes the engine's exclusive lock,
	// which waits for the block to finish.
	if (block != this->block) {
		this->block = block;
		module = APP->engine->getModule_NoLock(target.moduleId);
	}
	if (!module)
		return NAN;
	if (target.kind == Kind::Light)
		return target.index < (int) module->lights.size() ? clamp(module->lights[target.index].getBrightness(), 0.f, 1.f) : NAN;
	ParamQuantity* pq = target.index < (int) module->paramQuantities.size() ? module->paramQuantities[target.index] : nullptr;
	return pq && pq->isBounded() ? pq->getScaledValue() : NAN;
}

void Lane::carry(const Lane& from) {
	lag = from.lag;
	rate = from.rate;
	press = from.press;
	sent = from.sent;
	live = from.live;
}


Binding::~Binding() {
	// The module is out of the engine by now, so nothing else can be reading these.
	delete next.load();
	delete current.load();
	delete retired;
	for (auto& entry : leases)
		registry::release(entry.second);
	for (Lease& lease : doomed)
		registry::release(lease);
}

Plan* Binding::swap(int64_t frame) {
	Plan* n = next.load(std::memory_order_acquire);
	Plan* old = current.load(std::memory_order_relaxed);
	if (!n)
		return old;
	for (int i = 0; old && i < old->count; i++) {
		Lane& from = old->lanes[i];
		Lane* to = std::find_if(n->lanes, n->lanes + n->count, [&](const Lane& l) { return l.target.key() == from.target.key(); });
		if (to != n->lanes + n->count) {
			// A target that turned out to be a button starts over as one; its cell is re-armed on the next frame.
			if (to->momentary == from.momentary)
				to->carry(from);
		}
		else if (from.lease) {
			// Dropped: let go now rather than when the UI frees the lease.
			(*from.lease).state = Cell::IDLE;
			from.lease.slot->tick(frame);
		}
	}
	current.store(n, std::memory_order_release);
	retired = old;
	next.store(nullptr, std::memory_order_release);
	return n;
}

void Binding::run(const Module::ProcessArgs& args, const float* volts, int channels, Output* out, float depth) {
	Plan* p = swap(args.frame);
	int n = p ? p->count : 0;
	if (out) {
		out->setChannels(clamp(n, 1, PORT_MAX_CHANNELS));
		if (!n)
			out->setVoltage(0.f);
	}
	int64_t block = n ? APP->engine->getBlock() : 0;
	for (int i = 0; i < n; i++) {
		Lane& l = p->lanes[i];
		float x = l.read(block);
		l.position.store(x, std::memory_order_relaxed);
		if (out && i < PORT_MAX_CHANNELS)
			out->setVoltage(std::isnan(x) ? 0.f : l.target.kind == Kind::Light ? 10.f * x : p->settings.shape.reverse(x), i);
		if (l.lease)
			drive(l, *p, channels ? volts[i % channels] : NAN, depth, args);
	}
}

void Binding::bypass(const Module::ProcessArgs& args) {
	if (Plan* p = swap(args.frame))
		for (int i = 0; i < p->count; i++)
			if (p->lanes[i].lease)
				drive(p->lanes[i], *p, NAN, 0.f, args);
}

void Binding::drive(Lane& l, const Plan& p, float volts, float depth, const Module::ProcessArgs& args) {
	const Settings& s = p.settings;
	Cell& c = *l.lease;
	if (l.momentary) {
		// Buttons stay EDGE cells for good, so the slot writes only on a press or release and the hand is free in
		// between. The first frame only arms the cell, so the slot can't mistake an old stamp for a press.
		float down = l.press.process(std::isnan(volts) ? 0.f : volts, s.hold * 1e-3f, args.sampleTime) ? 1.f : 0.f;
		if (!l.live) {
			l.live = true;
			l.sent = 0.f;
			c.state.store(Cell::EDGE, std::memory_order_relaxed);
		}
		else if (down != l.sent) {
			l.sent = down;
			c.value.store(down, std::memory_order_relaxed);
			c.stamp.store(args.frame, std::memory_order_relaxed);
		}
	}
	else if (std::isnan(volts)) {
		if (l.live) {
			l.live = false;
			l.lag = {};
			l.rate = {};
			l.sent = NAN;
			c.state.store(Cell::IDLE, std::memory_order_relaxed);
		}
	}
	else {
		float x = l.lag.process(s.shape.forward(volts), s.slew, args.sampleTime);
		float y = p.rest + depth * s.depth * (x - p.rest);
		float value = (s.mode == Mode::Modulate ? y - p.rest : y) + l.offset;
		if (value != l.sent) {
			l.sent = value;
			c.stamp.store(args.frame, std::memory_order_relaxed);
		}
		c.value.store(value, std::memory_order_relaxed);
		c.interval.store(s.autoRate && l.rate.process(volts, args.sampleTime) ? 1 : s.interval, std::memory_order_relaxed);
		c.state.store(s.mode == Mode::Modulate ? Cell::MODULATE : s.mode == Mode::Overwrite ? Cell::ABSOLUTE : Cell::PICKUP,
			std::memory_order_relaxed);
		l.live = true;
	}
	l.lease.slot->tick(args.frame);
}


Binding::Problem Binding::problem() const {
	const Plan* p = plan();
	if (selector.empty())
		return Problem::Empty;
	if (!error.empty())
		return Problem::BadPattern;
	if (!p || !p->count)
		return Problem::NoMatch;
	const Lane& l = p->lanes[0];
	if (!widgetOf(l.target.moduleId))
		return Problem::Missing;
	return l.lease && !l.lease.slot->bound ? Problem::InUse : Problem::None;
}

void Binding::acknowledge() {
	const Plan* p = plan();
	known = p ? p->count : -1;
}

int Binding::shifted() const {
	const Plan* p = plan();
	return p && known >= 0 && p->count != known ? known : -1;
}

void Binding::collect() {
	if (!posting || next.load(std::memory_order_acquire))
		return;
	delete retired;
	retired = nullptr;
	for (Lease& lease : doomed)
		registry::release(lease);
	doomed.clear();
	posting = false;
}

std::vector<std::pair<Target, bool>> Binding::resolve(app::ModuleWidget* owner) {
	std::vector<std::pair<Target, bool>> out;
	auto add = [&](const Target& t) {
		auto same = [&](const Target& u) { return u.key() == t.key(); };
		if (out.size() < MAX_LANES && std::none_of(out.begin(), out.end(), [&](auto& r) { return same(r.first); })
			&& std::none_of(selector.excluded.begin(), selector.excluded.end(), same))
			out.push_back({t, isMomentary(t)});
	};
	std::vector<Clause> clauses = Clause::parseAll(selector.pattern, error);
	// Twins and patterns need the rack's widgets, which a loading patch doesn't have yet: they follow on the first
	// update. Plain learned targets and grips skip the rack altogether.
	bool rack = owner && (locked || settings.twins != Twins::None || !clauses.empty());
	Snapshot snapshot = rack ? Snapshot::take(owner) : Snapshot();
	auto inGroup = [&](const Target& t) {
		return !locked || snapshot.layout.admits(Scope::Group, "", snapshot.owner, snapshot.find(t.moduleId));
	};
	for (const Target& t : selector.targets) {
		if (!rack)
			add(t);
		else
			for (const Target& u : twins(t, locked ? Twins::None : settings.twins, snapshot))
				if (inGroup(u))
					add(u);
	}
	missing.clear();
	for (const Target& g : selector.grips)
		for (int64_t id : cabled) {
			Target t = grip(g, APP->engine->getModule_NoLock(id), missing);
			if (t && (!owner || t.moduleId != owner->module->id))
				add(t);
		}
	for (const Clause& c : clauses)
		for (const Target& t : match(c, snapshot, locked, cabled))
			add(t);
	return out;
}

Plan* Binding::build(const std::vector<std::pair<Target, bool>>& resolved) {
	Plan* p = new Plan;
	p->settings = settings;
	p->rest = settings.shape.forward(0.f);
	std::map<Key, Lease> kept;
	int n = (int) resolved.size();
	for (const auto& r : resolved) {
		Lane& l = p->lanes[p->count];
		l.offset = n > 1 && !r.second ? settings.spread * (float(p->count) / (n - 1) - 0.5f) : 0.f;
		p->count++;
		l.target = r.first;
		l.momentary = r.second;
		if (!writes || l.target.kind != Kind::Param)
			continue;
		Key key = l.target.key();
		auto it = leases.find(key);
		Lease lease = it != leases.end() ? it->second : registry::acquire(l.target.moduleId, l.target.index, this, retakes.count(key));
		if (it != leases.end())
			leases.erase(it);
		auto seed = seeds.find(key);
		if (lease && seed != seeds.end() && std::isnan(lease.slot->base.load()))
			lease.slot->base = seed->second;
		if (lease)
			kept[key] = lease;
		l.lease = lease;
	}
	for (auto& entry : leases)
		doomed.push_back(entry.second);
	leases = std::move(kept);
	retakes.clear();
	unmapped.clear();
	return p;
}

void Binding::update(app::ModuleWidget* owner) {
	collect();
	if (pasted) {
		pasted = false;
		seeds.clear();
		for (Target& t : selector.targets)
			remap(t, owner);
		dirty = true;
	}

	// Unmap on the knob, or another mapper learning it, takes the target out of this binding for good.
	for (auto& entry : leases) {
		Slot* s = entry.second.slot;
		registry::check(s);
		if (!s->owner)
			s->owner = this;
		if (s->owner == this)
			s->handle.color = color;
		if (!s->takenAway || !unmapped.insert(entry.first).second)
			continue;
		Target t;
		t.kind = Kind::Param;
		t.moduleId = s->moduleId;
		t.index = s->paramId;
		auto& ts = selector.targets;
		auto learned = std::find_if(ts.begin(), ts.end(), [&](const Target& u) { return u.key() == t.key(); });
		if (learned != ts.end())
			ts.erase(learned);
		else if (std::none_of(selector.excluded.begin(), selector.excluded.end(), [&](const Target& u) { return u.key() == t.key(); }))
			selector.excluded.push_back(t);
		changed();
	}

	if (posting || (!dirty && ++frame % 30))
		return;
	for (Target& t : selector.targets)
		refresh(t, owner);
	std::vector<std::pair<Target, bool>> resolved = resolve(owner);
	const Plan* p = plan();
	bool same = p && p->count == (int) resolved.size()
		&& std::equal(resolved.begin(), resolved.end(), p->lanes, [](const auto& r, const Lane& l) {
			return r.first.key() == l.target.key() && r.second == l.momentary;
		});
	if (same && !dirty)
		return;
	dirty = false;
	if (known < 0)
		known = (int) resolved.size();
	next.store(build(resolved), std::memory_order_release);
	posting = true;
	seeds.clear(); // they were for the patch as loaded
}

void Binding::edit(Module* module, int index, const std::string& name, const std::function<void(Selector&)>& f, const Target& retake) {
	struct Action : history::ModuleAction {
		int index;
		json_t *before, *after;
		Action(int64_t id, int index, std::string name, json_t* before, json_t* after) : index(index), before(before), after(after) {
			moduleId = id;
			this->name = std::move(name);
		}
		~Action() {
			json_decref(before);
			json_decref(after);
		}
		void apply(json_t* j) {
			if (auto* host = dynamic_cast<BindingHost*>(APP->engine->getModule(moduleId)))
				if (Binding* b = host->bindingAt(index)) {
					b->selector = Selector::fromJson(j);
					b->changed();
				}
		}
		void undo() override {
			apply(before);
		}
		void redo() override {
			apply(after);
		}
	};
	json_t* before = selector.toJson();
	f(selector);
	if (retake) {
		// Learning a target undoes an earlier unmap of it.
		auto& ex = selector.excluded;
		ex.erase(std::remove_if(ex.begin(), ex.end(), [&](const Target& t) { return t.key() == retake.key(); }), ex.end());
		retakes.insert(retake.key());
	}
	APP->history->push(new Action(module->id, index, name, before, selector.toJson()));
	changed();
}

json_t* Binding::toJson() const {
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "settings", settings.toJson());
	json_object_set_new(rootJ, "selector", selector.toJson());
	json_t* basesJ = json_array();
	for (const auto& entry : leases) {
		float b = entry.second.slot->base.load();
		if (!std::isnan(b))
			json_array_append_new(basesJ, json_pack("[I, i, f]", (json_int_t) std::get<1>(entry.first), std::get<2>(entry.first), b));
	}
	json_object_set_new(rootJ, "bases", basesJ);
	const Plan* p = plan();
	json_object_set_new(rootJ, "known", json_integer(known >= 0 ? known : p ? p->count : 0));
	return rootJ;
}

void Binding::fromJson(json_t* rootJ, Module* module) {
	settings.fromJson(json_object_get(rootJ, "settings"));
	selector = Selector::fromJson(json_object_get(rootJ, "selector"));
	json_t* knownJ = json_object_get(rootJ, "known");
	known = knownJ ? (int) json_integer_value(knownJ) : -1;
	seeds.clear();
	size_t i;
	json_t* baseJ;
	json_array_foreach(json_object_get(rootJ, "bases"), i, baseJ) {
		json_int_t id = -1;
		int param = 0;
		double b = 0;
		if (!json_unpack(baseJ, "[I, i, F]", &id, &param, &b))
			seeds[{int(Kind::Param), id, param}] = b;
	}
	dirty = true;

	// Rack strips module ids on paste and duplicate, so the targets may belong to the originals. The widget
	// remaps them once positions are final.
	if (module->id < 0) {
		pasted = true;
		return;
	}
	// A preset loaded onto a running module arrives with the engine locked, and claiming a handle locks too.
	// The widget rebuilds instead.
	if (APP->engine->getModule_NoLock(module->id) == module)
		return;
	// Patch load or undo: the module isn't running yet, so the plan goes straight in. Patterns need widgets to
	// resolve, so they follow on the widget's first update.
	delete current.exchange(build(resolve(nullptr)));
}

} // namespace puppeteer

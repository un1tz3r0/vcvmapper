#include "Binding.hpp"
#include "Pattern.hpp"
#include <algorithm>

namespace puppeteer {

static const char* MODES[] = {"modulate", "overwrite", "pickup"};

json_t* Settings::toJson() const {
	return json_pack("{s:s, s:b, s:f, s:f, s:f, s:b, s:i, s:f, s:i, s:b, s:f}",
		"mode", MODES[int(mode)], "bipolar", shape.bipolar, "lo", shape.lo, "hi", shape.hi, "curve", shape.curve,
		"invert", shape.invert, "steps", shape.steps, "slew", slew, "interval", interval, "autoRate", autoRate,
		"hold", hold);
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
	json_t* excludedJ = json_array();
	for (const Target& t : excluded)
		json_array_append_new(excludedJ, t.toJson());
	return json_pack("{s:o, s:s, s:o}", "targets", targetsJ, "pattern", pattern.c_str(), "excluded", excludedJ);
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

void Binding::process(const Module::ProcessArgs& args, Input& in, Output& out, float depth) {
	Plan* p = swap(args.frame);
	int n = p ? p->count : 0;
	out.setChannels(std::max(n, 1));
	if (!n) {
		out.setVoltage(0.f);
		return;
	}
	bool connected = in.isConnected();
	int64_t block = APP->engine->getBlock();
	for (int i = 0; i < n; i++) {
		Lane& l = p->lanes[i];
		float x = l.read(block);
		l.position.store(x, std::memory_order_relaxed);
		out.setVoltage(std::isnan(x) ? 0.f : l.target.kind == Kind::Light ? 10.f * x : p->settings.shape.reverse(x), i);
		if (l.lease)
			drive(l, *p, connected ? in.getPolyVoltage(i) : NAN, depth, args);
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
		float y = p.rest + depth * (x - p.rest);
		float value = s.mode == Mode::Modulate ? y - p.rest : y;
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

static bool matchModule(const Clause& c, plugin::Model* model) {
	plugin::Plugin* p = model->plugin;
	return (glob(c.plugin, p->slug) || glob(c.plugin, p->name) || glob(c.plugin, p->brand))
		&& (glob(c.model, model->slug) || glob(c.model, model->name));
}

std::vector<std::pair<Target, bool>> Binding::resolve(app::ModuleWidget* owner) {
	std::vector<std::pair<Target, bool>> out;
	auto add = [&](const Target& t) {
		auto same = [&](const Target& u) { return u.key() == t.key(); };
		if (out.size() < PORT_MAX_CHANNELS && std::none_of(out.begin(), out.end(), [&](auto& r) { return same(r.first); })
			&& std::none_of(selector.excluded.begin(), selector.excluded.end(), same))
			out.push_back({t, isMomentary(t)});
	};
	for (const Target& t : selector.targets)
		add(t);

	std::vector<Clause> clauses = Clause::parseAll(selector.pattern, error);
	if (!owner || clauses.empty())
		return out;
	std::vector<app::ModuleWidget*> modules = APP->scene->rack->getModules();
	auto row = [](app::ModuleWidget* mw) { return std::round(mw->box.pos.y / RACK_GRID_HEIGHT); };
	std::sort(modules.begin(), modules.end(), [&](app::ModuleWidget* a, app::ModuleWidget* b) {
		return row(a) != row(b) ? row(a) < row(b) : a->box.pos.x < b->box.pos.x;
	});
	for (const Clause& c : clauses) {
		for (app::ModuleWidget* mw : modules) {
			Module* m = mw->module;
			if (mw == owner || !m || !matchModule(c, mw->model))
				continue;
			Target t;
			t.kind = c.light ? Kind::Light : Kind::Param;
			t.moduleId = m->id;
			int count = c.light ? (int) m->lights.size() : (int) m->paramQuantities.size();
			for (t.index = 0; t.index < count; t.index++) {
				std::string name;
				if (c.light)
					name = t.index < (int) m->lightInfos.size() && m->lightInfos[t.index] ? m->lightInfos[t.index]->name : "";
				else
					name = m->paramQuantities[t.index]->name;
				if (c.first >= 0 ? c.matchesId(t.index) : !name.empty() && glob(c.name, name))
					add(t);
			}
		}
	}
	return out;
}

Plan* Binding::build(const std::vector<std::pair<Target, bool>>& resolved) {
	Plan* p = new Plan;
	p->settings = settings;
	p->rest = settings.shape.forward(0.f);
	std::map<Key, Lease> kept;
	for (const auto& r : resolved) {
		Lane& l = p->lanes[p->count++];
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
		dirty = true;
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
	return rootJ;
}

void Binding::fromJson(json_t* rootJ, Module* module) {
	settings.fromJson(json_object_get(rootJ, "settings"));
	selector = Selector::fromJson(json_object_get(rootJ, "selector"));
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

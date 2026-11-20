#include "BindingUi.hpp"
#include "Resolve.hpp"
#include "../kit/Menus.hpp"
#include <map>

namespace puppeteer {

static bool same(const Target& a, const Target& b) {
	return a.key() == b.key();
}

static void drop(std::vector<Target>& from, const Target& t) {
	from.erase(std::remove_if(from.begin(), from.end(), [&](const Target& u) { return same(u, t); }), from.end());
}

bool Learner::step(app::ModuleWidget* self, bool learning, Target& out) {
	app::RackWidget* rack = APP->scene->rack;
	if (learning && !was) {
		rack->setTouchedParam(NULL);
		// The click that started learning must not count as the click on a light.
		mouseWas = true;
		candidate = {};
	}
	was = learning;
	if (!learning)
		return false;

	// A touched knob or switch wins; otherwise a click near a light picks that light.
	app::ParamWidget* pw = rack->getTouchedParam();
	bool down = glfwGetMouseButton(APP->window->win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	bool picked = false;
	if (pw && pw->module && pw->module != self->module) {
		rack->setTouchedParam(NULL);
		out = Target();
		out.kind = Kind::Param;
		out.moduleId = pw->module->id;
		out.index = pw->paramId;
		picked = true;
	}
	else if (down && !mouseWas)
		candidate = nearestLight(rack->getMousePos(), self);
	else if (!down && mouseWas && candidate) {
		out = candidate;
		picked = true;
	}
	mouseWas = down;
	if (picked) {
		candidate = {};
		refresh(out, self);
	}
	return picked;
}

void learnInto(Binding& b, Module* module, int index, Target t, bool add) {
	b.edit(module, index, add ? "learn another target" : "learn target", [&](Selector& s) {
		if (!add)
			s = Selector();
		if (std::none_of(s.targets.begin(), s.targets.end(), [&](const Target& u) { return same(u, t); }))
			s.targets.push_back(t);
	}, t);
}

static std::string label(const Target& t) {
	TargetInfo info = describe(t, 0.f);
	return info.module.empty() ? "Missing module" : info.module + ": " + info.name;
}

std::string summary(const Binding& b) {
	const Plan* p = b.plan();
	int count = p ? p->count : 0;
	return count == 0 ? "Not bound" : count == 1 ? label(p->lanes[0].target) : string::f("%d targets", count);
}

/** The clause that matches this target on every module of its model. */
static std::string everyInstance(const Target& t) {
	return t.plugin + "/" + t.model + ":" + (t.kind == Kind::Light ? "light:#" : "#") + std::to_string(t.index);
}

void appendBindingMenu(ui::Menu* menu, Module* m, int index, std::function<void(bool add)> learn, bool depth) {
	Binding* b = dynamic_cast<BindingHost*>(m)->bindingAt(index);
	Settings* s = &b->settings;
	Shape* sh = &s->shape;
	auto edit = [=](std::string name, std::function<void(Selector&)> f) {
		return [=] { b->edit(m, index, name, f); };
	};
	auto set = [=](auto apply) {
		return [=](float v) {
			apply(v);
			b->changed();
		};
	};
	const Plan* p = b->plan();

	menu->addChild(createMenuLabel(summary(*b)));
	int was = b->shifted();
	if (was >= 0)
		menu->addChild(createMenuItem(string::f("Accept %d targets, was %d", p ? p->count : 0, was), "", [=] { b->acknowledge(); }));
	menu->addChild(createMenuItem("Learn", "", [=] { learn(false); }));
	menu->addChild(createMenuItem("Learn another", "", [=] { learn(true); }, b->selector.empty()));

	menu->addChild(createSubmenuItem("Targets", std::to_string(p ? p->count : 0), [=](ui::Menu* sub) {
		const Plan* p = b->plan();
		for (int i = 0; p && i < p->count; i++) {
			Target t = p->lanes[i].target;
			bool learned = std::any_of(b->selector.targets.begin(), b->selector.targets.end(), [&](const Target& u) { return same(u, t); });
			sub->addChild(createSubmenuItem(string::f("%d. %s", i + 1, label(t).c_str()), "", [=](ui::Menu* item) {
				if (!learned) {
					item->addChild(createMenuItem("Leave out", "", edit("leave out target", [=](Selector& s) { s.excluded.push_back(t); })));
					return;
				}
				item->addChild(createMenuItem("Remove", "", edit("remove target", [=](Selector& s) { drop(s.targets, t); })));
				std::string model = describe(t, 0.f).module;
				if (!model.empty())
					item->addChild(createMenuItem("On every " + model, "", edit("widen target", [=](Selector& s) {
						drop(s.targets, t);
						s.pattern += (s.pattern.empty() ? "" : ", ") + everyInstance(t);
					})));
			}));
		}
		if (!b->selector.excluded.empty())
			sub->addChild(createMenuItem(string::f("Bring back %d left out", (int) b->selector.excluded.size()), "",
				edit("bring back targets", [](Selector& s) { s.excluded.clear(); })));
	}));

	menu->addChild(createSubmenuItem("Path", b->selector.pattern.empty() ? "" : "On", [=](ui::Menu* sub) { appendPathMenu(sub, m, index); }));
	menu->addChild(createIndexSubmenuItem("Twins of learned targets",
		{"Off", "In the other groups on its row", "Above and below it", "In every group in the rack"},
		[=] { return (size_t) s->twins; },
		[=](size_t i) { s->twins = Twins(i); b->changed(); }));
	menu->addChild(createMenuItem("Unbind all", "", edit("unbind", [](Selector& s) { s = Selector(); }), b->selector.empty()));

	menu->addChild(new ui::MenuSeparator);
	if (depth)
		menu->addChild(kit::createSlider("Depth", "%", -100.f, 100.f, 100.f, 0, [=] { return s->depth * 100.f; }, set([=](float v) { s->depth = v / 100.f; })));
	menu->addChild(createIndexSubmenuItem("Hand and CV", {"Modulate around the knob", "Overwrite the knob", "Pick up the knob"},
		[=] { return (size_t) s->mode; },
		[=](size_t i) { s->mode = Mode(i); b->changed(); }));
	menu->addChild(createIndexSubmenuItem("CV range", {"-5 V to +5 V", "0 V to 10 V"},
		[=] { return s->shape.bipolar ? 0 : 1; },
		[=](size_t i) { s->shape.bipolar = i == 0; b->changed(); }));
	menu->addChild(createSubmenuItem("Shape", "", [=](ui::Menu* sub) {
		sub->addChild(createBoolMenuItem("Invert", "", [=] { return sh->invert; }, [=](bool v) { sh->invert = v; b->changed(); }));
		sub->addChild(kit::createSlider("Curve", "", -1.f, 1.f, 0.f, 2, [=] { return sh->curve; }, set([=](float v) { sh->curve = v; })));
		sub->addChild(kit::createSlider("Knob from", "%", 0.f, 100.f, 0.f, 0, [=] { return sh->lo * 100.f; }, set([=](float v) { sh->lo = v / 100.f; })));
		sub->addChild(kit::createSlider("Knob to", "%", 0.f, 100.f, 100.f, 0, [=] { return sh->hi * 100.f; }, set([=](float v) { sh->hi = v / 100.f; })));
		sub->addChild(createSubmenuItem("Steps", sh->steps ? std::to_string(sh->steps) : "Off", [=](ui::Menu* steps) {
			for (int n : fields::STEPS)
				steps->addChild(createCheckMenuItem(n ? std::to_string(n) : "Off", "", [=] { return sh->steps == n; }, [=] { sh->steps = n; b->changed(); }));
		}));
	}));
	menu->addChild(kit::createSlider("Spread", "%", -100.f, 100.f, 0.f, 0, [=] { return s->spread * 100.f; }, set([=](float v) { s->spread = v / 100.f; })));
	menu->addChild(kit::createSlider("Slew", " ms", 0.f, 2000.f, 0.f, 1, [=] { return s->slew; }, set([=](float v) { s->slew = v; }), 3.f));
	menu->addChild(createSubmenuItem("Write rate", s->autoRate ? "Auto" : "", [=](ui::Menu* sub) {
		sub->addChild(createBoolMenuItem("Every sample for audio-rate CV", "", [=] { return s->autoRate; }, [=](bool v) { s->autoRate = v; b->changed(); }));
		sub->addChild(new ui::MenuSeparator);
		for (int n : fields::INTERVALS)
			sub->addChild(createCheckMenuItem(n == 1 ? "Every sample" : string::f("Every %d samples", n), "", [=] { return s->interval == n; }, [=] { s->interval = n; b->changed(); }));
	}));
	menu->addChild(createSubmenuItem("Button hold", string::f("%g ms", s->hold), [=](ui::Menu* sub) {
		for (float ms : fields::HOLDS)
			sub->addChild(createCheckMenuItem(string::f("%g ms", ms), "", [=] { return s->hold == ms; }, [=] { s->hold = ms; b->changed(); }));
	}));
}


/** A text field in a menu, for a glob or a whole pattern. */
static kit::TextEntry* entry(const std::string& text, const std::string& placeholder, std::function<void(const std::string&)> commit) {
	auto* e = new kit::TextEntry;
	e->text = text;
	e->placeholder = placeholder;
	e->commit = std::move(commit);
	return e;
}

/** Plain names make readable paths; anything a glob or the parser would misread, or that names two items, is #N. */
static bool plainName(const std::string& name) {
	return !name.empty() && name.find_first_of(",:*?#$@\"") == std::string::npos && name.find("light:") != 0;
}

static void appendClauseMenu(ui::Menu* menu, Binding* b, Module* m, int index, int which) {
	std::string error;
	std::vector<Clause> clauses = Clause::parseAll(b->selector.pattern, error);
	if (which >= (int) clauses.size())
		return;
	const Clause c = clauses[which];
	auto change = [=](std::string name, std::function<void(Clause&)> f) {
		return [=] {
			b->edit(m, index, name, [&](Selector& s) {
				std::string e;
				std::vector<Clause> cs = Clause::parseAll(s.pattern, e);
				if (which < (int) cs.size()) {
					f(cs[which]);
					s.pattern = Clause::join(cs);
				}
			});
		};
	};
	auto typed = [=](std::string name, std::function<void(Clause&, const std::string&)> f) {
		return [=](const std::string& text) { change(name, [=](Clause& k) { f(k, trim(text)); })(); };
	};
	menu->addChild(createMenuItem("Remove this path", "", [=] {
		b->edit(m, index, "remove path", [&](Selector& s) {
			std::string e;
			std::vector<Clause> cs = Clause::parseAll(s.pattern, e);
			if (which < (int) cs.size())
				cs.erase(cs.begin() + which);
			s.pattern = Clause::join(cs);
		});
	}));
	if (!c.tag.empty())
		return;

	Snapshot snap = Snapshot::take(widgetOf(m->id));
	auto count = [=](Clause k) { return std::to_string(match(k, snap).size()); };
	menu->addChild(new ui::MenuSeparator);

	menu->addChild(createSubmenuItem("Where", c.scopeText(), [=](ui::Menu* sub) {
		static const char* WHERE[] = {"Anywhere", "On this module's row", "Above and below this module", "In this module's group"};
		for (int i = 0; i < 4; i++) {
			Clause k = c;
			k.scope = Scope(i);
			sub->addChild(createCheckMenuItem(WHERE[i], count(k), [=] { return c.scope == Scope(i); },
				change("change path scope", [=](Clause& x) { x.scope = Scope(i); })));
		}
		sub->addChild(createMenuLabel("In groups named"));
		sub->addChild(entry(c.scope == Scope::Named ? c.groups : "", "Voice*", typed("change path scope", [](Clause& x, const std::string& t) {
			if (t.empty() || !Clause::parseScope("\"" + t + "\"", x))
				x.scope = Scope::All;
		})));
	}));

	// Plugins and modules present, with how many instances the clause would reach through each.
	std::map<std::string, std::string> plugins, models; // slug -> name
	for (app::ModuleWidget* mw : snap.modules) {
		plugins[mw->model->plugin->slug] = mw->model->plugin->name;
		if (glob(c.plugin, mw->model->plugin->slug) || glob(c.plugin, mw->model->plugin->name))
			models[mw->model->plugin->slug + "/" + mw->model->slug] = mw->model->name;
	}
	auto instances = [=](Clause k) {
		int n = 0;
		for (int i = 0; i < (int) snap.modules.size(); i++)
			n += i != snap.owner && snap.layout.admits(k.scope, k.groups, snap.owner, i) && matchModule(k, snap.modules[i]->model);
		return std::to_string(n);
	};
	menu->addChild(createSubmenuItem("Plugin", c.plugin, [=](ui::Menu* sub) {
		sub->addChild(createCheckMenuItem("Any", "", [=] { return c.plugin == "*"; }, change("change path plugin", [](Clause& x) { x.plugin = "*"; })));
		for (const auto& entry : plugins) {
			std::string slug = entry.first, name = entry.second;
			Clause k = c;
			k.plugin = slug;
			sub->addChild(createCheckMenuItem(name, instances(k), [=] { return c.plugin == slug; },
				change("change path plugin", [=](Clause& x) { x.plugin = slug; })));
		}
		sub->addChild(entry(c.plugin, "Fundamental", typed("change path plugin", [](Clause& x, const std::string& t) { x.plugin = t.empty() ? "*" : t; })));
	}));
	menu->addChild(createSubmenuItem("Module", c.model, [=](ui::Menu* sub) {
		for (const auto& entry : models) {
			std::string slug = entry.first.substr(entry.first.find('/') + 1), name = entry.second;
			Clause k = c;
			k.model = slug;
			sub->addChild(createCheckMenuItem(name, instances(k), [=] { return c.model == slug; },
				change("change path module", [=](Clause& x) { x.model = slug; })));
		}
		sub->addChild(entry(c.model, "VCO*", typed("change path module", [](Clause& x, const std::string& t) {
			if (!t.empty())
				x.model = t;
		})));
	}));

	menu->addChild(createSubmenuItem(c.light ? "Light" : "Control", c.item(), [=](ui::Menu* sub) {
		sub->addChild(createCheckMenuItem("Knobs and switches", "", [=] { return !c.light; }, change("change path item", [](Clause& x) { x.light = false; })));
		sub->addChild(createCheckMenuItem("Lights", "", [=] { return c.light; }, change("change path item", [](Clause& x) { x.light = true; })));
		sub->addChild(new ui::MenuSeparator);
		// Every item id on the modules the clause reaches, by name; an id named differently, or twice, is ambiguous.
		struct Item {
			std::string name;
			int modules = 0;
			bool ambiguous = false;
		};
		std::map<int, Item> items;
		for (int i = 0; i < (int) snap.modules.size(); i++) {
			Module* mod = snap.modules[i]->module;
			if (i == snap.owner || !snap.layout.admits(c.scope, c.groups, snap.owner, i) || !matchModule(c, snap.modules[i]->model))
				continue;
			int n = c.light ? (int) mod->lights.size() : (int) mod->paramQuantities.size();
			std::map<std::string, int> seen;
			for (int id = 0; id < n; id++)
				seen[itemName(mod, c.light, id)]++;
			for (int id = 0; id < n; id++) {
				std::string name = itemName(mod, c.light, id);
				Item& it = items[id];
				it.ambiguous |= (it.modules && it.name != name) || seen[name] > 1;
				it.name = name;
				it.modules++;
			}
		}
		constexpr int MAX_ITEMS = 64;
		int shown = 0;
		for (const auto& entry : items) {
			int id = entry.first;
			Item it = entry.second;
			if (shown++ == MAX_ITEMS) {
				sub->addChild(createMenuLabel(string::f("%d more: type #N below", (int) items.size() - MAX_ITEMS)));
				break;
			}
			bool byName = !it.ambiguous && plainName(it.name);
			sub->addChild(createCheckMenuItem(string::f("#%d %s", id, it.name.c_str()), std::to_string(it.modules),
				[=] { return c.first >= 0 ? c.matchesId(id) && c.first == c.last : byName && c.name == it.name; },
				change("change path item", [=](Clause& x) {
					x.first = x.last = byName ? -1 : id;
					x.name = byName ? it.name : "";
				})));
		}
		sub->addChild(entry(c.item(), "Freq* or #3 or #0-7", typed("change path item", [](Clause& x, const std::string& t) {
			Clause parsed;
			std::string e;
			if (Clause::parse("m:" + t, parsed, e)) {
				x.light = parsed.light;
				x.name = parsed.name;
				x.first = parsed.first;
				x.last = parsed.last;
			}
		})));
	}));
}

void appendPathMenu(ui::Menu* menu, Module* m, int index) {
	Binding* b = dynamic_cast<BindingHost*>(m)->bindingAt(index);
	menu->addChild(entry(b->selector.pattern, "Fundamental/VCO@row:Freq*", [=](const std::string& text) {
		b->edit(m, index, "edit path", [&](Selector& s) { s.pattern = text; });
	}));
	if (!b->error.empty())
		menu->addChild(createMenuLabel(b->error));

	std::string error;
	std::vector<Clause> clauses = Clause::parseAll(b->selector.pattern, error);
	for (int i = 0; i < (int) clauses.size(); i++)
		menu->addChild(createSubmenuItem(clauses[i].str(), "", [=](ui::Menu* sub) { appendClauseMenu(sub, b, m, index, i); }));

	// A new clause starts from a module in the rack and one of its controls.
	menu->addChild(createSubmenuItem("Add a path", "", [=](ui::Menu* sub) {
		Snapshot snap = Snapshot::take(widgetOf(m->id));
		std::map<std::string, std::pair<plugin::Model*, int>> models;
		for (int i = 0; i < (int) snap.modules.size(); i++)
			if (i != snap.owner) {
				plugin::Model* model = snap.modules[i]->model;
				auto& entry = models[model->plugin->name + " " + model->name];
				entry.first = model;
				entry.second++;
			}
		for (const auto& row : models) {
			const std::string& label = row.first;
			plugin::Model* model = row.second.first;
			int instances = row.second.second;
			Module* sample = nullptr;
			for (app::ModuleWidget* mw : snap.modules)
				if (mw->model == model && mw->module != m)
					sample = mw->module;
			sub->addChild(createSubmenuItem(label, std::to_string(instances), [=](ui::Menu* items) {
				for (int id = 0; sample && id < (int) sample->paramQuantities.size(); id++) {
					std::string name = itemName(sample, false, id);
					Clause k;
					k.plugin = model->plugin->slug;
					k.model = model->slug;
					if (plainName(name) && std::count_if(sample->paramQuantities.begin(), sample->paramQuantities.end(),
						[&](ParamQuantity* pq) { return pq->name == name; }) == 1)
						k.name = name;
					else
						k.first = k.last = id;
					items->addChild(createMenuItem(string::f("#%d %s", id, name.c_str()), "", [=] {
						b->edit(m, index, "add path", [&](Selector& s) {
							s.pattern += (trim(s.pattern).empty() ? "" : ", ") + k.str();
						});
					}));
				}
			}));
		}
	}));

	menu->addChild(new ui::MenuSeparator);
	menu->addChild(createMenuLabel("plugin/model@where:control, comma between several"));
	menu->addChild(createMenuLabel("* and ? match any text; #3 or #0-7 are ids"));
	menu->addChild(createMenuLabel("where: row, column, group, or a group name"));
	menu->addChild(createMenuLabel("light:name or light:#N reads lights"));
}

} // namespace puppeteer

#include "BindingUi.hpp"
#include "../kit/Menus.hpp"

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

	menu->addChild(createSubmenuItem("Pattern", b->selector.pattern.empty() ? "" : "On", [=](ui::Menu* sub) {
		auto* entry = new kit::TextEntry;
		entry->text = b->selector.pattern;
		entry->placeholder = "Fundamental/VCO:Freq*";
		entry->commit = [=](const std::string& text) {
			b->edit(m, index, "edit pattern", [&](Selector& s) { s.pattern = text; });
		};
		sub->addChild(entry);
		sub->addChild(createMenuLabel("plugin/model:param, comma between several"));
		sub->addChild(createMenuLabel("* and ? match any text; #3 or #0-7 are ids"));
		sub->addChild(createMenuLabel("light:name or light:#N reads lights"));
		if (!b->error.empty())
			sub->addChild(createMenuLabel(b->error));
	}));
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

} // namespace puppeteer

#include "Target.hpp"
#include <functional>

namespace puppeteer {

json_t* Target::toJson() const {
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "kind", json_string(kind == Kind::Param ? "param" : "light"));
	json_object_set_new(rootJ, "moduleId", json_integer(moduleId));
	json_object_set_new(rootJ, "index", json_integer(index));
	json_object_set_new(rootJ, "plugin", json_string(plugin.c_str()));
	json_object_set_new(rootJ, "model", json_string(model.c_str()));
	json_object_set_new(rootJ, "offset", json_pack("[f, f]", offset.x, offset.y));
	return rootJ;
}

Target Target::fromJson(json_t* rootJ) {
	Target t;
	if (!rootJ)
		return t;
	auto str = [&](const char* key) {
		const char* s = json_string_value(json_object_get(rootJ, key));
		return std::string(s ? s : "");
	};
	std::string kind = str("kind");
	t.kind = kind == "param" ? Kind::Param : kind == "light" ? Kind::Light : Kind::None;
	t.moduleId = json_integer_value(json_object_get(rootJ, "moduleId"));
	t.index = json_integer_value(json_object_get(rootJ, "index"));
	t.plugin = str("plugin");
	t.model = str("model");
	double x = 0, y = 0;
	json_unpack(json_object_get(rootJ, "offset"), "[F, F]", &x, &y);
	t.offset = Vec(x, y);
	if (t.moduleId < 0)
		t.kind = Kind::None;
	return t;
}

app::ModuleWidget* widgetOf(int64_t moduleId) {
	return moduleId >= 0 && APP->scene ? APP->scene->rack->getModule(moduleId) : nullptr;
}

static Vec cellOf(app::ModuleWidget* mw) {
	return mw->box.pos.minus(RACK_OFFSET).div(RACK_GRID_SIZE).round();
}

static bool sameModel(app::ModuleWidget* mw, const Target& t) {
	return mw->model && mw->model->slug == t.model && mw->model->plugin->slug == t.plugin;
}

bool refresh(Target& t, app::ModuleWidget* owner) {
	app::ModuleWidget* mw = widgetOf(t.moduleId);
	if (!t || !mw || !mw->module)
		return false;
	t.plugin = mw->model->plugin->slug;
	t.model = mw->model->slug;
	t.offset = cellOf(mw).minus(cellOf(owner));
	return true;
}

bool remap(Target& t, app::ModuleWidget* owner) {
	if (!t)
		return false;
	Vec want = cellOf(owner).plus(t.offset);
	for (app::ModuleWidget* mw : APP->scene->rack->getModules()) {
		if (mw != owner && mw->module && sameModel(mw, t) && cellOf(mw).equals(want)) {
			t.moduleId = mw->module->id;
			return true;
		}
	}
	return false;
}

bool isMomentary(const Target& t) {
	app::ModuleWidget* mw = t.kind == Kind::Param ? widgetOf(t.moduleId) : nullptr;
	auto* sw = mw ? dynamic_cast<app::Switch*>(mw->getParam(t.index)) : nullptr;
	return sw && sw->momentary;
}

static void eachLight(widget::Widget* w, const std::function<void(app::ModuleLightWidget*)>& f) {
	for (widget::Widget* child : w->children) {
		if (auto* lw = dynamic_cast<app::ModuleLightWidget*>(child))
			if (lw->module)
				f(lw);
		eachLight(child, f);
	}
}

Target nearestLight(Vec pos, app::ModuleWidget* owner, float radius) {
	Target best;
	float bestDistance = radius;
	app::RackWidget* rack = APP->scene->rack;
	for (app::ModuleWidget* mw : rack->getModules()) {
		if (mw == owner || !mw->module)
			continue;
		eachLight(mw, [&](app::ModuleLightWidget* lw) {
			float d = lw->getRelativeOffset(lw->box.size.div(2), rack).minus(pos).norm();
			if (d < bestDistance) {
				bestDistance = d;
				best.kind = Kind::Light;
				best.moduleId = mw->module->id;
				best.index = lw->firstLightId;
			}
		});
	}
	return best;
}

TargetInfo describe(const Target& t, float scaled) {
	app::ModuleWidget* mw = widgetOf(t.moduleId);
	if (!t || !mw || !mw->module)
		return {};
	engine::Module* m = mw->module;
	TargetInfo info{mw->model->name, "", ""};
	if (t.kind == Kind::Param && t.index < (int) m->paramQuantities.size()) {
		engine::ParamQuantity* pq = m->paramQuantities[t.index];
		info.name = pq->getLabel();
		info.value = string::trim(pq->getDisplayValueString() + " " + pq->getUnit());
	}
	else if (t.kind == Kind::Light && t.index < (int) m->lights.size()) {
		engine::LightInfo* li = t.index < (int) m->lightInfos.size() ? m->lightInfos[t.index] : nullptr;
		info.name = li ? li->getName() : string::f("Light %d", t.index + 1);
		info.value = string::f("%d%%", (int) std::round(scaled * 100.f));
	}
	return info;
}

} // namespace puppeteer

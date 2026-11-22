#include "Resolve.hpp"

namespace puppeteer {

static Vec cell(app::ModuleWidget* mw) {
	return mw->box.pos.minus(RACK_OFFSET).div(RACK_GRID_SIZE).round();
}

Snapshot Snapshot::take(app::ModuleWidget* owner) {
	Snapshot s;
	for (app::ModuleWidget* mw : APP->scene->rack->getModules())
		if (mw->module)
			s.modules.push_back(mw);
	std::sort(s.modules.begin(), s.modules.end(), [](app::ModuleWidget* a, app::ModuleWidget* b) {
		Vec ca = cell(a), cb = cell(b);
		return ca.y != cb.y ? ca.y < cb.y : ca.x < cb.x;
	});
	std::vector<Box> boxes;
	for (app::ModuleWidget* mw : s.modules) {
		Vec c = cell(mw);
		int width = (int) std::round(mw->box.size.x / RACK_GRID_WIDTH);
		Box b{mw->module->id, int(c.y), int(c.x), int(c.x) + width, mw->model->plugin->slug + "/" + mw->model->slug, Role::Plain, ""};
		if (auto* marker = dynamic_cast<Marker*>(mw->module)) {
			b.role = marker->role();
			b.name = marker->label();
		}
		boxes.push_back(std::move(b));
	}
	s.layout = Layout::of(std::move(boxes));
	s.owner = owner && owner->module ? s.find(owner->module->id) : -1;
	return s;
}

math::Rect Snapshot::span(int g) const {
	const Layout::Group& group = layout.groups[g];
	return math::Rect(RACK_OFFSET.plus(Vec(group.x0, group.row).mult(RACK_GRID_SIZE)),
		Vec(group.x1 - group.x0, 1).mult(RACK_GRID_SIZE));
}

bool matchModule(const Clause& c, plugin::Model* model) {
	plugin::Plugin* p = model->plugin;
	return (glob(c.plugin, p->slug) || glob(c.plugin, p->name) || glob(c.plugin, p->brand))
		&& (glob(c.model, model->slug) || glob(c.model, model->name));
}

std::string itemName(Module* m, bool light, int index) {
	if (light)
		return index < (int) m->lightInfos.size() && m->lightInfos[index] ? m->lightInfos[index]->name : "";
	return index < (int) m->paramQuantities.size() ? m->paramQuantities[index]->name : "";
}

std::vector<Target> match(Clause c, const Snapshot& s, bool locked, const std::vector<int64_t>& cabled) {
	std::vector<Target> out;
	if (locked)
		c.scope = Scope::Group;
	bool cable = c.scope == Scope::Cable;
	std::vector<int> order;
	if (cable) {
		for (int64_t id : cabled)
			if (int i = s.find(id); i >= 0)
				order.push_back(i);
	}
	else
		for (int i = 0; i < int(s.modules.size()); i++)
			order.push_back(i);
	std::vector<int> tagged = c.tag.empty() ? std::vector<int>() : s.layout.tagged(c.tag);
	for (int i : order) {
		Module* m = s.modules[i]->module;
		bool named = c.tag.empty() ? matchModule(c, s.modules[i]->model) : std::count(tagged.begin(), tagged.end(), i);
		if (i == s.owner || !named || !(cable || s.layout.admits(c.scope, c.groups, s.owner, i)))
			continue;
		Target t;
		t.kind = c.light ? Kind::Light : Kind::Param;
		t.moduleId = m->id;
		int count = c.light ? (int) m->lights.size() : (int) m->paramQuantities.size();
		for (t.index = 0; t.index < count; t.index++)
			if (c.matchesItem(t.index, itemName(m, c.light, t.index)))
				out.push_back(t);
	}
	return out;
}

std::vector<Target> twins(const Target& t, Twins scope, const Snapshot& s) {
	static const Scope SCOPES[] = {Scope::All, Scope::Row, Scope::Column, Scope::All};
	int anchor = s.find(t.moduleId);
	if (scope == Twins::None || anchor < 0)
		return {t};
	std::vector<Target> out;
	for (int i : s.layout.twins(anchor, SCOPES[int(scope)])) {
		if (i == s.owner)
			continue;
		Target u = t;
		u.moduleId = s.layout.boxes[i].id;
		out.push_back(u);
	}
	return out;
}

Target grip(const Target& g, Module* m, std::string& missing) {
	if (!m)
		return {};
	Target t = g;
	t.moduleId = m->id;
	t.plugin = m->model && m->model->plugin ? m->model->plugin->slug : "";
	t.model = m->model ? m->model->slug : "";
	bool light = g.kind == Kind::Light;
	int count = light ? (int) m->lights.size() : (int) m->paramQuantities.size();
	if (t.plugin == g.plugin && t.model == g.model && g.index < count)
		return t;
	for (t.index = 0; t.index < count && !g.name.empty(); t.index++)
		if (string::lowercase(itemName(m, light, t.index)) == string::lowercase(g.name))
			return t;
	missing = (m->model ? m->model->name : "Its module") + " has no " + (g.name.empty() ? "#" + std::to_string(g.index) : g.name);
	return {};
}

} // namespace puppeteer

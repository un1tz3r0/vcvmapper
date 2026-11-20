#include "Overlay.hpp"
#include "Resolve.hpp"
#include <set>

namespace puppeteer {

void Overlay::show(const Binding& b) {
	app::RackWidget* rack = APP->scene->rack;
	Overlay* o = nullptr;
	for (widget::Widget* w : rack->children)
		if ((o = dynamic_cast<Overlay*>(w)))
			break;
	if (!o) {
		o = new Overlay;
		rack->addChild(o);
	}
	o->targets.clear();
	if (const Plan* p = b.plan())
		for (int i = 0; i < p->count; i++)
			o->targets.push_back(p->lanes[i].target);
	o->color = b.color;
	o->idle = 0;
	o->widget::Widget::show();
}

void Overlay::step() {
	box = math::Rect(Vec(), parent->box.size);
	if (isVisible() && ++idle > 2)
		hide();
}

static widget::Widget* lightWidget(widget::Widget* w, int lightId) {
	for (widget::Widget* child : w->children) {
		if (auto* lw = dynamic_cast<app::ModuleLightWidget*>(child))
			if (lw->firstLightId == lightId)
				return lw;
		if (widget::Widget* found = lightWidget(child, lightId))
			return found;
	}
	return nullptr;
}

void Overlay::drawLayer(const DrawArgs& args, int layer) {
	if (layer != 3)
		return;
	NVGcontext* vg = args.vg;
	Snapshot s = Snapshot::take(nullptr);
	std::set<int> groups;
	for (const Target& t : targets) {
		int i = s.find(t.moduleId);
		if (i < 0)
			continue;
		groups.insert(s.layout.group[i]);
		app::ModuleWidget* mw = s.modules[i];
		widget::Widget* w = t.kind == Kind::Light ? lightWidget(mw, t.index) : mw->getParam(t.index);
		if (!w)
			continue;
		math::Rect r(w->getRelativeOffset(Vec(), parent), w->box.size);
		r = r.grow(Vec(3, 3));
		nvgBeginPath(vg);
		nvgRoundedRect(vg, RECT_ARGS(r), 3);
		nvgFillColor(vg, nvgTransRGBA(color, 40));
		nvgFill(vg);
		nvgStrokeWidth(vg, 2);
		nvgStrokeColor(vg, color);
		nvgStroke(vg);
	}
	// A bracket over each group in play: a bar along its top with ticks down at both ends.
	for (int g : groups) {
		math::Rect r = s.span(g);
		float y = r.pos.y + 3, x0 = r.pos.x + 2, x1 = r.pos.x + r.size.x - 2;
		nvgBeginPath(vg);
		nvgMoveTo(vg, x0, y + 12);
		nvgLineTo(vg, x0, y);
		nvgLineTo(vg, x1, y);
		nvgLineTo(vg, x1, y + 12);
		nvgStrokeWidth(vg, 3);
		nvgStrokeColor(vg, nvgTransRGBA(color, 180));
		nvgStroke(vg);
	}
}

} // namespace puppeteer

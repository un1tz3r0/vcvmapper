#pragma once
#include "Groups.hpp"
#include "Settings.hpp"
#include "Target.hpp"

// Turns clauses and learned targets into targets in the rack. UI thread.
namespace puppeteer {

/** The rack's modules in lane order, by row then left to right, with their groups. */
struct Snapshot {
	std::vector<app::ModuleWidget*> modules;
	Layout layout;
	int owner = -1; // the owning module's index, the anchor of typed clauses

	static Snapshot take(app::ModuleWidget* owner);
	int find(int64_t moduleId) const {
		return layout.find(moduleId);
	}
	/** Rack coordinates of group `g`. */
	math::Rect span(int g) const;
};

bool matchModule(const Clause& c, plugin::Model* model);
/** The param's or light's name on `m`, empty when it has none. */
std::string itemName(Module* m, bool light, int index);
/** What the clause matches, in rack order, skipping the owner. */
std::vector<Target> match(const Clause& c, const Snapshot& s);
/** `t` and its twins, in rack order; just `t` when its module isn't in the snapshot. */
std::vector<Target> twins(const Target& t, Twins scope, const Snapshot& s);

} // namespace puppeteer

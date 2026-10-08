#pragma once
#include <rack.hpp>

namespace puppeteer {
using namespace rack;

enum class Kind { None, Param, Light };

/** What a binding points at. Identity is the module id; the model slugs and the grid offset
from the owning module are kept so a pasted copy can find its pasted twin. */
struct Target {
	Kind kind = Kind::None;
	int64_t moduleId = -1;
	int index = 0;
	std::string plugin, model;
	math::Vec offset;

	explicit operator bool() const {
		return kind != Kind::None;
	}

	json_t* toJson() const;
	static Target fromJson(json_t* rootJ);
};

// UI thread only.

app::ModuleWidget* widgetOf(int64_t moduleId);
/** Updates the cached slugs and offset. False when the target module is not in the rack. */
bool refresh(Target& t, app::ModuleWidget* owner);
/** After a paste: rebinds to a module of the same model at the same offset from the pasted owner, if there is one. */
bool remap(Target& t, app::ModuleWidget* owner);
/** The light nearest `pos` (rack coordinates) within `radius`, skipping `owner`. */
Target nearestLight(math::Vec pos, app::ModuleWidget* owner, float radius = 8.f);

struct TargetInfo {
	std::string module, name, value;
};
/** Display strings for the screen and menus; empty when the target is missing. */
TargetInfo describe(const Target& t, float scaled);

} // namespace puppeteer

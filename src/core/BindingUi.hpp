#pragma once
#include "Binding.hpp"

// What every module's widget does with a binding: learn by touch, and the context menu. UI thread.
namespace puppeteer {

/** Watches for the next knob touched or light clicked on another module. */
struct Learner {
	bool was = false, mouseWas = false;
	Target candidate;

	/** Call every frame. True once, with `out` set, when a target was picked while `learning`. */
	bool step(app::ModuleWidget* self, bool learning, Target& out);
};

/** Makes `t` the binding's only learned target, or one more of them, with undo. */
void learnInto(Binding& b, Module* module, int index, Target t, bool add);

/** "VCO: Frequency", "3 targets" or "Not bound". */
std::string summary(const Binding& b);

/** Targets, pattern and settings for binding `index` of `module`. `learn(add)` starts the module's learn mode. */
void appendBindingMenu(ui::Menu* menu, Module* module, int index, std::function<void(bool add)> learn, bool depth);

} // namespace puppeteer

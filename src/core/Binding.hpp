#pragma once
#include "Settings.hpp"
#include "Slot.hpp"
#include "Target.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <set>

namespace puppeteer {

constexpr int MAX_LANES = 64;

/** What a binding points at: learned targets in learn order, each with its twins, then whatever the pattern matches
from left to right and top to bottom, minus the targets someone unmapped from the knob. At most MAX_LANES. */
struct Selector {
	std::vector<Target> targets;
	std::string pattern;
	std::vector<Target> excluded;

	bool empty() const {
		return targets.empty() && pattern.empty();
	}
	json_t* toJson() const;
	static Selector fromJson(json_t* rootJ);
};

/** One resolved target. Lane i follows input channel i, wrapping round the channels there are, and the first 16
lanes read back on the output's channels. */
struct Lane {
	Target target;
	bool momentary = false;
	float offset = 0.f; // this lane's share of the spread
	Lease lease; // writers only; borrowed from the binding, which owns every lease

	// Audio thread
	Lag lag;
	RateDetector rate;
	Press press;
	float sent = NAN;
	bool live = false;
	int64_t block = -1;
	Module* module = nullptr;

	std::atomic<float> position{NAN}; // the target's value in 0..1, NAN when unreachable; for display

	float read(int64_t block);
	void carry(const Lane& from);
};

/** Everything process() needs, built on the UI thread and handed over whole, so the audio thread never sees a
half-edited binding. */
struct Plan {
	Settings settings;
	float rest = 0.f; // where 0 V puts the knob
	int count = 0;
	Lane lanes[MAX_LANES];
};

struct Binding {
	// UI thread. Call changed() after editing either; it also takes whatever the edit matches as known.
	Settings settings;
	Selector selector;
	bool writes = true; // claims params; off for a binding that only reads them
	bool locked = false; // keeps every target to the owner's group, so a cloned group drives its own copies
	NVGcolor color = nvgRGB(0x52, 0xdb, 0xff);
	std::string error;  // why the pattern doesn't parse

	~Binding();

	/** Why the binding isn't driving anything, for screens to word as they like. */
	enum class Problem { None, Empty, BadPattern, NoMatch, Missing, InUse };
	Problem problem() const;
	/** How many targets the binding had when last edited or loaded, when that differs from now; else -1. Moving
	modules can change what a scope or twins match, so screens flag it until acknowledge() or an edit. */
	int shifted() const;
	void acknowledge();

	// Audio thread
	void process(const Module::ProcessArgs& args, Input& in, Output& out, float depth = 1.f) {
		run(args, in.getVoltages(), in.getChannels(), &out, depth);
	}
	/** Driven by one voltage that isn't a jack, such as a macro knob. */
	void process(const Module::ProcessArgs& args, float volts) {
		run(args, &volts, 1, nullptr, 1.f);
	}
	void bypass(const Module::ProcessArgs& args);

	// UI thread
	void update(app::ModuleWidget* owner);
	void changed() {
		dirty = true;
		known = -1;
	}
	/** Selector edits through here get undo. `retake` re-claims a param that was unmapped from under us. */
	void edit(Module* module, int index, const std::string& name, const std::function<void(Selector&)>& f, const Target& retake = {});
	/** The plan process() is running. Stays valid until the next update(). */
	const Plan* plan() const {
		return current.load(std::memory_order_acquire);
	}
	json_t* toJson() const;
	void fromJson(json_t* rootJ, Module* module);

private:
	using Key = std::tuple<int, int64_t, int>;

	std::atomic<Plan*> current{nullptr}, next{nullptr};
	Plan* retired = nullptr; // set by the audio thread before it clears `next`
	bool posting = false, dirty = true, pasted = false;
	int frame = 0;
	int known = -1; // the target count the user has seen, -1 to take the next one
	std::map<Key, Lease> leases;
	std::vector<Lease> doomed; // released once the plan that still mentions them is retired
	std::set<Key> retakes;
	std::set<Key> unmapped; // taken away and already dropped from the selector
	std::map<Key, float> seeds; // hand positions from the patch, for slots that aren't running yet

	Plan* swap(int64_t frame);
	void run(const Module::ProcessArgs& args, const float* volts, int channels, Output* out, float depth);
	void drive(Lane& lane, const Plan& plan, float volts, float depth, const Module::ProcessArgs& args);
	void collect();
	std::vector<std::pair<Target, bool>> resolve(app::ModuleWidget* owner);
	Plan* build(const std::vector<std::pair<Target, bool>>& resolved);
};

/** A module with bindings, so undo can find them by module id. */
struct BindingHost {
	virtual ~BindingHost() = default;
	virtual Binding* bindingAt(int index) = 0;
};

} // namespace puppeteer

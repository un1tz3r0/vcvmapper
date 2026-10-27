#pragma once
#include <rack.hpp>
#include <atomic>

namespace puppeteer {
using namespace rack;

/** One binding's say in a param. Its binding writes it in process(); whichever process() ticks the slot reads it. */
struct Cell {
	enum State : uint8_t {
		FREE,     // not leased
		IDLE,     // leased, CV unplugged
		MODULATE, // value is an offset around the hand's position
		ABSOLUTE, // value is where the knob goes; the newest stamp wins among several
		PICKUP,   // ABSOLUTE once the CV has caught up with the knob, until the hand moves it again
		EDGE,     // a button: value is written once per change of stamp, and the hand is free in between
	};
	std::atomic<uint8_t> state{FREE};
	std::atomic<float> value{0.f};
	std::atomic<int64_t> stamp{0}; // engine frame of the value's last change
	std::atomic<int> interval{32}; // frames between writes this cell asks for

	// Ticker only
	uint8_t was = FREE;
	bool engaged = false;
	float previous = NAN;
	int64_t applied = 0;
};

/** Puppeteer's single handle on one param, shared by every binding that writes it. Its tick() combines the
bindings' cells with the hand once per frame, in whichever process() calls it first, so several bindings never
mistake each other's writes for the hand. All fields but the cells, base and the ticker's are UI thread only. */
struct Slot {
	static constexpr int CELLS = 16;
	static constexpr float EPSILON = 1e-4f; // in 0..1 of the knob's travel
	static constexpr float CATCH = 0.02f;   // how close CV must come to the knob for pick-up

	const int64_t moduleId;
	const int paramId;
	engine::ParamHandle handle;
	Cell cells[CELLS];
	std::atomic<int> used{0};     // cells ever leased, so ticks scan only those
	std::atomic<float> base{NAN}; // the hand's position while bindings drive the param, else NAN

	int users = 0;
	const void* owner = nullptr; // the binding whose colour the knob shows
	bool bound = false;          // holds the engine's handle for the param
	bool takenAway = false;      // was bound until Unmap or another mapper cleared it
	int retry = 0;

	Slot(int64_t moduleId, int paramId) : moduleId(moduleId), paramId(paramId) {}

	/** Audio thread. Cheap to call from every lane every frame; only the first call in a frame does anything. */
	void tick(int64_t frame);
	/** Takes any modulation back off the knob. UI thread, once nothing can tick the slot any more. */
	void finish() {
		stop(quantity());
	}

private:
	std::atomic<int64_t> ticked{-1};
	float written = NAN; // our last write, to tell it apart from the hand's
	bool running = false;
	int countdown = 0;

	struct Range {
		Cell *b, *e;
		Cell* begin() const {
			return b;
		}
		Cell* end() const {
			return e;
		}
	};
	Range leased();
	ParamQuantity* quantity() const;
	void update(ParamQuantity* pq);
	void press(ParamQuantity* pq);
	void stop(ParamQuantity* pq);
	void write(ParamQuantity* pq, float x);
};

/** A binding's claim on one cell of a slot. */
struct Lease {
	Slot* slot = nullptr;
	int cell = -1;

	explicit operator bool() const {
		return slot;
	}
	Cell& operator*() const {
		return slot->cells[cell];
	}
};

// Shares slots between bindings. UI thread, never with the engine lock held: creating and freeing a slot add
// and remove its handle, which lock. A slot is freed with its last lease, and a lease outlives every plan that
// mentions it, so no process() can be using a slot when it goes.
namespace registry {

/** A cell in the param's slot, claiming the engine's handle unless another mapper holds it. `retake` is for an
explicit learn: it tries again on a param that was unmapped from under us. Empty when all cells are taken. */
Lease acquire(int64_t moduleId, int paramId, const void* owner, bool retake);
void release(Lease& lease);
/** Notices Unmap and other mappers, and picks the param up when they let go. */
void check(Slot* slot);

} // namespace registry
} // namespace puppeteer

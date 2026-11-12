#include "Slot.hpp"
#include <climits>
#include <map>

namespace puppeteer {

Slot::Range Slot::leased() {
	return {cells, cells + used.load(std::memory_order_acquire)};
}

ParamQuantity* Slot::quantity() const {
	// The engine nulls handle.module under its exclusive lock, so it is safe to follow for the rest of the block.
	Module* m = handle.module;
	ParamQuantity* pq = m && paramId < (int) m->paramQuantities.size() ? m->paramQuantities[paramId] : nullptr;
	return pq && pq->isBounded() ? pq : nullptr;
}

void Slot::tick(int64_t frame) {
	if (ticked.exchange(frame, std::memory_order_acq_rel) == frame)
		return;

	bool active = false, edge = false;
	int interval = INT_MAX;
	for (Cell& c : leased()) {
		uint8_t s = c.state.load(std::memory_order_relaxed);
		if (s != c.was) {
			c.was = s;
			c.engaged = false;
			c.previous = NAN;
			c.applied = c.stamp.load(std::memory_order_relaxed);
		}
		if (s == Cell::EDGE)
			edge |= c.stamp.load(std::memory_order_relaxed) != c.applied;
		else if (s >= Cell::MODULATE) {
			active = true;
			interval = std::min(interval, c.interval.load(std::memory_order_relaxed));
		}
	}

	ParamQuantity* pq = quantity();
	if (!pq || !active || edge) {
		stop(pq);
		if (pq && edge)
			press(pq);
		return;
	}
	if (!running) {
		running = true;
		written = NAN;
		countdown = 0;
		if (std::isnan(base.load(std::memory_order_relaxed)))
			base = pq->getScaledValue();
	}
	if (countdown-- > 0)
		return;
	countdown = interval - 1;
	update(pq);
}

void Slot::update(ParamQuantity* pq) {
	float v = pq->getScaledValue();
	float b = base.load(std::memory_order_relaxed);
	// Anything that moved the knob since our last write was a hand (or MIDI, or a preset): follow it.
	bool hand = !std::isnan(written) && std::fabs(v - written) > EPSILON;
	if (hand)
		b = clamp(b + v - written, 0.f, 1.f);

	float offset = 0.f, absolute = NAN;
	int64_t newest = INT64_MIN;
	for (Cell& c : leased()) {
		float x = c.value.load(std::memory_order_relaxed);
		switch (c.was) {
			case Cell::MODULATE:
				offset += x;
				break;
			case Cell::PICKUP:
				if (hand)
					c.engaged = false;
				else if (!c.engaged)
					c.engaged = std::fabs(x - v) < CATCH || (c.previous - v) * (x - v) < 0.f;
				c.previous = x;
				if (!c.engaged)
					break;
				[[fallthrough]];
			case Cell::ABSOLUTE: {
				int64_t t = c.stamp.load(std::memory_order_relaxed);
				if (t >= newest) {
					newest = t;
					absolute = x;
				}
				break;
			}
			default:
				break;
		}
	}
	// A CV that sets the knob also sets where modulation centres, and where the knob stays when it lets go.
	if (!std::isnan(absolute))
		b = absolute;
	base = b;
	write(pq, clamp(b + offset, 0.f, 1.f));
}

void Slot::press(ParamQuantity* pq) {
	for (Cell& c : leased()) {
		int64_t t = c.stamp.load(std::memory_order_relaxed);
		if (c.was == Cell::EDGE && t != c.applied) {
			c.applied = t;
			write(pq, c.value.load(std::memory_order_relaxed));
		}
	}
	written = NAN;
}

void Slot::stop(ParamQuantity* pq) {
	if (!running)
		return;
	running = false;
	float b = base.load(std::memory_order_relaxed);
	if (pq && !std::isnan(written) && !std::isnan(b)) {
		// Take the modulation back off, keeping whatever the hand did since our last write.
		float v = pq->getScaledValue();
		if (std::fabs(v - written) > EPSILON)
			b = clamp(b + v - written, 0.f, 1.f);
		if (std::fabs(b - v) > EPSILON)
			write(pq, b);
	}
	base = NAN;
	written = NAN;
}

void Slot::write(ParamQuantity* pq, float x) {
	float v = pq->fromScaled(x);
	if (pq->snapEnabled)
		v = std::round(v);
	APP->engine->setParamValue(handle.module, paramId, v);
	written = pq->toScaled(v);
}


namespace registry {

static std::map<std::pair<int64_t, int>, Slot*> slots;

static void claim(Slot* s) {
	if (!APP->engine->getParamHandle(s->moduleId, s->paramId))
		APP->engine->updateParamHandle(&s->handle, s->moduleId, s->paramId, false);
	s->bound = s->handle.moduleId == s->moduleId;
	s->retry = 60;
}

Lease acquire(int64_t moduleId, int paramId, const void* owner, bool retake) {
	Slot*& s = slots[{moduleId, paramId}];
	if (!s) {
		s = new Slot(moduleId, paramId);
		s->handle.text = "Puppeteer";
		s->handle.color = nvgRGB(0x52, 0xdb, 0xff);
		APP->engine->addParamHandle(&s->handle);
		claim(s);
	}
	else if (retake && s->takenAway) {
		s->takenAway = false;
		claim(s);
	}
	for (int i = 0; i < Slot::CELLS; i++) {
		uint8_t free = Cell::FREE;
		if (s->cells[i].state.compare_exchange_strong(free, Cell::IDLE)) {
			if (s->used.load() <= i)
				s->used.store(i + 1, std::memory_order_release);
			s->users++;
			if (!s->owner)
				s->owner = owner;
			return {s, i};
		}
	}
	return {};
}

void release(Lease& lease) {
	if (!lease)
		return;
	Slot* s = lease.slot;
	(*lease).state = Cell::FREE;
	lease = {};
	if (--s->users > 0) {
		s->owner = nullptr; // the next binding to update takes over the colour
		return;
	}
	s->finish();
	APP->engine->removeParamHandle(&s->handle);
	slots.erase({s->moduleId, s->paramId});
	delete s;
}

void check(Slot* s) {
	bool bound = s->handle.moduleId == s->moduleId && s->handle.paramId == s->paramId;
	if (s->bound && !bound)
		s->takenAway = true;
	s->bound = bound;
	if (!bound && !s->takenAway && --s->retry <= 0)
		claim(s);
}

} // namespace registry
} // namespace puppeteer

// Rack's internal headers, without rack.hpp's guard on internal functions.
#include <context.hpp>
#include <engine/Engine.hpp>

rack::engine::Engine* makeEngine() {
	rack::contextSet(new rack::Context);
	auto* e = APP->engine = new rack::engine::Engine;
	e->setSampleRate(48000.f);
	return e;
}

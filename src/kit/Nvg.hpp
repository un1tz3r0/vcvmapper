#pragma once
#include "../plugin.hpp"
#include "Painter.hpp"

namespace kit {

inline NVGcolor nvg(kit::Rgba c, float alpha = 1) { return nvgRGBAf(c.r, c.g, c.b, c.a * alpha); }

} // namespace kit

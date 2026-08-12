#pragma once

#include "palette.hpp"

inline constexpr Color with_alpha(Color c, float a) {
    return {c.r, c.g, c.b, a};
}

inline constexpr Color lerp_color(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t};
}

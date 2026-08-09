
#include "../../src/render/palette.hpp"

#include <cassert>
#include <cmath>

void test_palette() {
    Color a{0.0f, 0.2f, 0.4f, 1.0f};
    Color b{1.0f, 0.6f, 0.8f, 0.0f};

    Color at0 = lerp_color(a, b, 0.0f);
    assert(at0.r == a.r && at0.g == a.g && at0.b == a.b && at0.a == a.a);

    Color at1 = lerp_color(a, b, 1.0f);
    assert(at1.r == b.r && at1.g == b.g && at1.b == b.b && at1.a == b.a);

    Color mid = lerp_color(a, b, 0.5f);
    assert(std::fabs(mid.r - (a.r + b.r) / 2.0f) < 1e-6f);
    assert(std::fabs(mid.g - (a.g + b.g) / 2.0f) < 1e-6f);
    assert(std::fabs(mid.b - (a.b + b.b) / 2.0f) < 1e-6f);
    assert(std::fabs(mid.a - (a.a + b.a) / 2.0f) < 1e-6f);

    Color c = with_alpha(a, 0.75f);
    assert(c.r == a.r && c.g == a.g && c.b == a.b && c.a == 0.75f);
}

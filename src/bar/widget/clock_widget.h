#pragma once

#include "render/texture.h"
#include "bar/widget/widget_capsule.h"

#include <cstdint>

namespace bar_detail {

void update_clock(Texture &clock_texture);

void draw_clock_pill(Node *root, float height, int32_t surface_width,
                     const Texture &clock_texture, const float tint[4],
                     const float pill_bg[4]);

}

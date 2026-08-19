#pragma once

#include <cstdint>

#include "bar/widget/widget_capsule.h"

#include "render/texture.h"

namespace bar_detail {

void update_clock(Texture &clock_texture);

void draw_clock_pill(Node *root, float height, int32_t surface_width,
                     const Texture &clock_texture, const float tint[4],
                     const float pill_bg[4]);

}

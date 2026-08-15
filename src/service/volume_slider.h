#pragma once

#include "render/rect.h"
#include "service/pipewire.h"

#include <string>

struct DraggedSlider {
    std::string tag;
    Rect rect;
};

uint32_t volume_slider_resolve_tag_id(const PipewireState &pw,
                                      const std::string &tag);

void volume_slider_apply_drag(PipewireState &pw, const DraggedSlider &drag,
                              double px);

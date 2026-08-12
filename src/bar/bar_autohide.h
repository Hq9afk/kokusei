#pragma once

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <cstdint>
#include <wayland-client.h>
#include <wayland-egl.h>

struct AutoHideState {
    bool hidden = false;
    bool collapsed = false;
    float opacity = 1.0f;

    bool enabled = false;
};

namespace bar_detail {

void bar_autohide_set_surface_geometry(
    zwlr_layer_surface_v1 *layer_surface, wl_surface *surface,
    wl_egl_window *egl_window, int32_t width, int32_t height_px,
    int32_t margin_top, int32_t margin_right, int32_t margin_left,
    int32_t exclusive_zone, int32_t output_scale);

}

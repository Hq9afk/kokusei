#pragma once

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <cstdint>
#include <wayland-client.h>
#include <wayland-egl.h>

struct AutoHideState {
    bool hidden = false;
    float height_px = 0.0f;
};

namespace bar_detail {
inline void bar_autohide_apply_surface_state(zwlr_layer_surface_v1 *layer_surface,
                                             wl_surface *surface,
                                             wl_egl_window *egl_window,
                                             int32_t width, int32_t height_px,
                                             int32_t margin_top,
                                             int32_t output_scale) {
    zwlr_layer_surface_v1_set_size(layer_surface, 0, height_px);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface,
                                             height_px + margin_top);
    wl_surface_commit(surface);
    if (egl_window)
        wl_egl_window_resize(egl_window, width * output_scale,
                             height_px * output_scale, 0, 0);
}
}

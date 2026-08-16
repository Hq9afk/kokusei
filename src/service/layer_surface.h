#pragma once

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <EGL/egl.h>
#include <cstdint>
#include <wayland-client.h>
#include <wayland-egl.h>

struct LayerSurfaceConfig {
    uint32_t layer;
    const char *name_space;
    uint32_t anchor = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t margin_top = 0;
    int32_t margin_right = 0;
    int32_t margin_bottom = 0;
    int32_t margin_left = 0;
    int32_t exclusive_zone = -1;
    bool empty_input_region = false;
};

zwlr_layer_surface_v1 *
layer_surface_create(wl_surface *&out_surface, wl_compositor *compositor,
                     zwlr_layer_shell_v1 *layer_shell,
                     const LayerSurfaceConfig &cfg,
                     const zwlr_layer_surface_v1_listener *listener,
                     void *listener_data, wl_output *output = nullptr);

void destroy_layer_surface(EGLDisplay display, wl_surface *&surface,
                           zwlr_layer_surface_v1 *&layer_surface,
                           wl_egl_window *&egl_window, EGLSurface &egl_surface);

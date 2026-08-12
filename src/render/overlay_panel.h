#pragma once

#include "../wayland/frame_clock.h"
#include "../wayland/layer_surface.h"
#include "../wayland/output_scale.h"
#include "animation.h"

#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>

constexpr float kOverlayFadeMs = 220.0f;
constexpr uint64_t kOverlayFadeOwner = 1;
constexpr uint64_t kPanelHeightAnimOwner = 2;

struct OverlayPanelBase {
    const char *name_space = nullptr;
    wl_compositor *compositor = nullptr;
    wl_surface *surface = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLDisplay egl_display = nullptr;
    EGLContext egl_context = nullptr;
    bool configured = false, open = false;
    int32_t width = 0, height = 0;
    OutputScale output_scale;
    FrameClock frame_clock;
    AnimationManager animations;
    float opacity = 0.0f;
};

extern const zwlr_layer_surface_v1_listener overlay_panel_listener;

void overlay_panel_update_input_region(OverlayPanelBase &base);

bool overlay_panel_create_surface(OverlayPanelBase &base,
                                  wl_compositor *compositor,
                                  zwlr_layer_shell_v1 *layer_shell,
                                  const char *name_space,
                                  wl_output *output = nullptr);

bool overlay_panel_init_egl(OverlayPanelBase &base, EGLDisplay display,
                            EGLConfig config, EGLContext context);

void overlay_panel_request_frame(OverlayPanelBase &base);

void overlay_panel_toggle(OverlayPanelBase &base);

template <typename OnOpen, typename OnClose>
inline bool panel_lock_toggle(OverlayPanelBase &base, float &locked_center_x,
                              float pill_center_x, OnOpen on_open,
                              OnClose on_close) {
    bool was_open = base.open;
    overlay_panel_toggle(base);
    if (was_open) {
        locked_center_x = -1.0f;
        on_close();
    } else {
        if (pill_center_x >= 0.0f)
            locked_center_x = pill_center_x;
        on_open();
    }
    return !was_open;
}

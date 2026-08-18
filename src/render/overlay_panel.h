#pragma once

#include "core/log.h"
#include "render/animation.h"
#include "service/frame_clock.h"
#include "service/layer_surface.h"
#include "service/output_scale.h"

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

void overlay_panel_destroy_surface(OverlayPanelBase &base);

// Lazily (re)creates base's surface if it isn't already there (either never
// created, or destroyed by overlay_panel_toggle's close path), waiting for
// the compositor's configure before running init_egl. No-op returning true
// if the surface already exists.
template <typename CreateSurface, typename InitEgl>
inline bool overlay_panel_ensure(OverlayPanelBase &base, wl_display *display,
                                 CreateSurface create_surface,
                                 InitEgl init_egl) {
    if (base.layer_surface)
        return true;
    if (!create_surface())
        return false;
    while (!base.configured)
        wl_display_dispatch(display);
    return init_egl();
}

template <typename CreateSurface, typename InitEgl>
inline wl_output *
overlay_panel_retarget(OverlayPanelBase &base, wl_display *display,
                       wl_output *previous_output, wl_output *target_output,
                       const char *target_name, CreateSurface create_surface,
                       InitEgl init_egl) {
    klog("panel: %s retargeting from output=%p to '%s'",
         base.name_space ? base.name_space : "?",
         static_cast<void *>(previous_output), target_name);
    overlay_panel_destroy_surface(base);
    base.configured = false;
    base.open = false;
    base.opacity = 0.0f;

    auto bind_to = [&](wl_output *out) -> bool {
        if (!create_surface(out))
            return false;
        while (!base.configured)
            wl_display_dispatch(display);
        return init_egl();
    };

    if (bind_to(target_output))
        return target_output;
    if (previous_output && bind_to(previous_output))
        return previous_output;
    klog("panel: %s retarget fallback also failed",
         base.name_space ? base.name_space : "?");
    return nullptr;
}

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

#pragma once

#include "service/frame_clock.h"
#include "service/output_scale.h"
#include "render/animation.h"
#include "xdg-shell-client-protocol.h"

#include <EGL/egl.h>
#include <functional>
#include <wayland-client.h>
#include <wayland-egl.h>

struct ToplevelWindowBase {
    wl_compositor *compositor = nullptr;
    wl_surface *surface = nullptr;
    xdg_surface *shell_surface = nullptr;
    xdg_toplevel *toplevel = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLDisplay egl_display = nullptr;
    EGLContext egl_context = nullptr;
    bool configured = false, open = false;
    int32_t width = 0, height = 0;
    int32_t pending_width = 0, pending_height = 0;
    int32_t last_toplevel_width = 0, last_toplevel_height = 0;
    OutputScale output_scale;
    FrameClock frame_clock;
    AnimationManager animations;
    float opacity = 0.0f;
    // Invoked when the WM sends xdg_toplevel.close (titlebar close button,
    // Alt+F4). The module wires this to its own toggle-closed path so a
    // WM-initiated close fades out the same way an Escape-triggered one does.
    std::function<void()> on_close_request;
};

bool toplevel_window_create_surface(ToplevelWindowBase &base,
                                    wl_compositor *compositor,
                                    xdg_wm_base *wm_base, const char *title,
                                    const char *app_id, int32_t default_width,
                                    int32_t default_height);

bool toplevel_window_init_egl(ToplevelWindowBase &base, EGLDisplay display,
                              EGLConfig config, EGLContext context);

void toplevel_window_request_frame(ToplevelWindowBase &base);

void toplevel_window_destroy_surface(ToplevelWindowBase &base);

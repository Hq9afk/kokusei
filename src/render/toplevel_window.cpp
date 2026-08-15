#include "render/toplevel_window.h"

namespace {

void xdg_surface_configure(void *data, xdg_surface *surface,
                           uint32_t serial) {
    auto *base = static_cast<ToplevelWindowBase *>(data);
    xdg_surface_ack_configure(surface, serial);

    int32_t width = base->last_toplevel_width > 0 ? base->last_toplevel_width
                                                   : base->pending_width;
    int32_t height = base->last_toplevel_height > 0
                          ? base->last_toplevel_height
                          : base->pending_height;
    base->width = width;
    base->height = height;
    int32_t scale = base->output_scale.scale;
    if (base->egl_window)
        wl_egl_window_resize(base->egl_window, width * scale, height * scale,
                             0, 0);
    base->configured = true;
}

const xdg_surface_listener xdg_surface_listener_impl = {
    .configure = xdg_surface_configure,
};

void xdg_toplevel_configure(void *data, xdg_toplevel *, int32_t width,
                            int32_t height, wl_array *) {
    auto *base = static_cast<ToplevelWindowBase *>(data);
    base->last_toplevel_width = width;
    base->last_toplevel_height = height;
}

void xdg_toplevel_close(void *data, xdg_toplevel *) {
    auto *base = static_cast<ToplevelWindowBase *>(data);
    if (base->on_close_request)
        base->on_close_request();
}

void xdg_toplevel_configure_bounds(void *, xdg_toplevel *, int32_t, int32_t) {
}

void xdg_toplevel_wm_capabilities(void *, xdg_toplevel *, wl_array *) {}

const xdg_toplevel_listener xdg_toplevel_listener_impl = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
    .configure_bounds = xdg_toplevel_configure_bounds,
    .wm_capabilities = xdg_toplevel_wm_capabilities,
};

} // namespace

bool toplevel_window_create_surface(ToplevelWindowBase &base,
                                    wl_compositor *compositor,
                                    xdg_wm_base *wm_base, const char *title,
                                    const char *app_id, int32_t default_width,
                                    int32_t default_height) {
    base.compositor = compositor;
    base.pending_width = default_width;
    base.pending_height = default_height;
    base.last_toplevel_width = 0;
    base.last_toplevel_height = 0;

    base.surface = wl_compositor_create_surface(compositor);
    base.shell_surface = xdg_wm_base_get_xdg_surface(wm_base, base.surface);
    if (!base.shell_surface)
        return false;
    xdg_surface_add_listener(base.shell_surface, &xdg_surface_listener_impl,
                             &base);

    base.toplevel = xdg_surface_get_toplevel(base.shell_surface);
    if (!base.toplevel) {
        xdg_surface_destroy(base.shell_surface);
        base.shell_surface = nullptr;
        return false;
    }
    xdg_toplevel_add_listener(base.toplevel, &xdg_toplevel_listener_impl,
                              &base);
    xdg_toplevel_set_title(base.toplevel, title);
    xdg_toplevel_set_app_id(base.toplevel, app_id);

    base.output_scale.on_change = [&base](int32_t scale) {
        if (base.egl_window)
            wl_egl_window_resize(base.egl_window, base.width * scale,
                                 base.height * scale, 0, 0);
        if (base.frame_clock.surface)
            request_frame(base.frame_clock);
    };
    output_scale_watch(base.output_scale, base.surface);
    wl_surface_commit(base.surface);
    return true;
}

bool toplevel_window_init_egl(ToplevelWindowBase &base, EGLDisplay display,
                              EGLConfig config, EGLContext context) {
    base.egl_display = display;
    base.egl_context = context;
    int32_t scale = base.output_scale.scale;
    base.egl_window = wl_egl_window_create(base.surface, base.width * scale,
                                           base.height * scale);
    base.egl_surface = eglCreateWindowSurface(
        display, config, reinterpret_cast<EGLNativeWindowType>(base.egl_window),
        nullptr);
    if (base.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!eglMakeCurrent(display, base.egl_surface, base.egl_surface, context))
        return false;
    base.frame_clock.surface = base.surface;
    return true;
}

void toplevel_window_request_frame(ToplevelWindowBase &base) {
    if (base.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(base.frame_clock);
}

void toplevel_window_destroy_surface(ToplevelWindowBase &base) {
    if (base.egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(base.egl_display, base.egl_surface);
        base.egl_surface = EGL_NO_SURFACE;
    }
    if (base.egl_window) {
        wl_egl_window_destroy(base.egl_window);
        base.egl_window = nullptr;
    }
    if (base.toplevel) {
        xdg_toplevel_destroy(base.toplevel);
        base.toplevel = nullptr;
    }
    if (base.shell_surface) {
        xdg_surface_destroy(base.shell_surface);
        base.shell_surface = nullptr;
    }
    if (base.surface) {
        wl_surface_destroy(base.surface);
        base.surface = nullptr;
    }
    if (base.frame_clock.callback) {
        wl_callback_destroy(base.frame_clock.callback);
        base.frame_clock.callback = nullptr;
    }
    base.frame_clock.redraw_requested = false;
    base.frame_clock.surface = nullptr;
    base.configured = false;
}

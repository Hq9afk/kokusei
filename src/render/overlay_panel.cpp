#include "overlay_panel.h"

#include "../core/log.h"

namespace {

void overlay_panel_configure(void *data, zwlr_layer_surface_v1 *layer_surface,
                             uint32_t serial, uint32_t width, uint32_t height) {
    auto *base = static_cast<OverlayPanelBase *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    base->width = static_cast<int32_t>(width);
    base->height = static_cast<int32_t>(height);
    int32_t scale = base->output_scale.scale;
    if (base->egl_window)
        wl_egl_window_resize(base->egl_window, base->width * scale,
                             base->height * scale, 0, 0);
    base->configured = true;
}

void overlay_panel_closed(void *, zwlr_layer_surface_v1 *) {}

} // namespace

const zwlr_layer_surface_v1_listener overlay_panel_listener = {
    .configure = overlay_panel_configure,
    .closed = overlay_panel_closed,
};

void overlay_panel_update_input_region(OverlayPanelBase &base) {
    if (base.open) {
        wl_surface_set_input_region(base.surface, nullptr);
        return;
    }
    wl_region *empty_region = wl_compositor_create_region(base.compositor);
    wl_surface_set_input_region(base.surface, empty_region);
    wl_region_destroy(empty_region);
}

bool overlay_panel_create_surface(OverlayPanelBase &base,
                                  wl_compositor *compositor,
                                  zwlr_layer_shell_v1 *layer_shell,
                                  const char *name_space, wl_output *output) {
    base.compositor = compositor;
    base.name_space = name_space;
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        .name_space = name_space,
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
    };
    base.layer_surface =
        layer_surface_create(base.surface, compositor, layer_shell, cfg,
                             &overlay_panel_listener, &base, output);
    if (!base.layer_surface)
        return false;

    base.output_scale.on_change = [&base](int32_t scale) {
        if (base.egl_window)
            wl_egl_window_resize(base.egl_window, base.width * scale,
                                 base.height * scale, 0, 0);
        if (base.frame_clock.surface)
            request_frame(base.frame_clock);
    };
    output_scale_watch(base.output_scale, base.surface);
    overlay_panel_update_input_region(base);
    wl_surface_commit(base.surface);
    return true;
}

bool overlay_panel_init_egl(OverlayPanelBase &base, EGLDisplay display,
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

void overlay_panel_request_frame(OverlayPanelBase &base) {
    if (base.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(base.frame_clock);
}

void overlay_panel_destroy_surface(OverlayPanelBase &base) {
    if (base.egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(base.egl_display, base.egl_surface);
        base.egl_surface = EGL_NO_SURFACE;
    }
    if (base.egl_window) {
        wl_egl_window_destroy(base.egl_window);
        base.egl_window = nullptr;
    }
    if (base.layer_surface) {
        zwlr_layer_surface_v1_destroy(base.layer_surface);
        base.layer_surface = nullptr;
    }
    if (base.surface) {
        wl_surface_destroy(base.surface);
        base.surface = nullptr;
    }
}

void overlay_panel_toggle(OverlayPanelBase &base) {
    if (!base.layer_surface || base.egl_surface == EGL_NO_SURFACE)
        return;

    bool opening = !base.open;
    if (opening) {
        base.open = true;
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            base.layer_surface,
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
        overlay_panel_update_input_region(base);
        wl_surface_commit(base.surface);
        klog("panel: %s acquired exclusive keyboard interactivity",
             base.name_space ? base.name_space : "?");
    }

    base.animations.animate(
        base.opacity, opening ? 1.0f : 0.0f, kOverlayFadeMs,
        Easing::EaseOutCubic, [&base](float v) { base.opacity = v; },
        [&base, opening] {
            if (opening)
                return;
            base.open = false;
            zwlr_layer_surface_v1_set_keyboard_interactivity(
                base.layer_surface,
                ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
            overlay_panel_update_input_region(base);
            wl_surface_commit(base.surface);
            klog("panel: %s released exclusive keyboard interactivity",
                 base.name_space ? base.name_space : "?");
        },
        kOverlayFadeOwner);
    overlay_panel_request_frame(base);
}

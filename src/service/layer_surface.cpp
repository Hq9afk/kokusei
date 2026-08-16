#include "service/layer_surface.h"

zwlr_layer_surface_v1 *
layer_surface_create(wl_surface *&out_surface, wl_compositor *compositor,
                     zwlr_layer_shell_v1 *layer_shell,
                     const LayerSurfaceConfig &cfg,
                     const zwlr_layer_surface_v1_listener *listener,
                     void *listener_data, wl_output *output) {
    out_surface = wl_compositor_create_surface(compositor);
    zwlr_layer_surface_v1 *layer_surface =
        zwlr_layer_shell_v1_get_layer_surface(layer_shell, out_surface, output,
                                              cfg.layer, cfg.name_space);
    if (!layer_surface)
        return nullptr;

    if (cfg.anchor)
        zwlr_layer_surface_v1_set_anchor(layer_surface, cfg.anchor);
    if (cfg.width || cfg.height)
        zwlr_layer_surface_v1_set_size(layer_surface, cfg.width, cfg.height);
    if (cfg.margin_top || cfg.margin_right || cfg.margin_bottom ||
        cfg.margin_left)
        zwlr_layer_surface_v1_set_margin(layer_surface, cfg.margin_top,
                                         cfg.margin_right, cfg.margin_bottom,
                                         cfg.margin_left);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, cfg.exclusive_zone);

    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_add_listener(layer_surface, listener, listener_data);

    if (cfg.empty_input_region) {
        wl_region *empty_region = wl_compositor_create_region(compositor);
        wl_surface_set_input_region(out_surface, empty_region);
        wl_region_destroy(empty_region);
    }

    return layer_surface;
}

void destroy_layer_surface(EGLDisplay display, wl_surface *&surface,
                           zwlr_layer_surface_v1 *&layer_surface,
                           wl_egl_window *&egl_window,
                           EGLSurface &egl_surface) {
    if (egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(display, egl_surface);
        egl_surface = EGL_NO_SURFACE;
    }
    if (egl_window) {
        wl_egl_window_destroy(egl_window);
        egl_window = nullptr;
    }
    if (layer_surface) {
        zwlr_layer_surface_v1_destroy(layer_surface);
        layer_surface = nullptr;
    }
    if (surface) {
        wl_surface_destroy(surface);
        surface = nullptr;
    }
}

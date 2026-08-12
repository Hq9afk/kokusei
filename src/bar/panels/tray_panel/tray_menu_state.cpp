#include "tray_menu_state.h"

#include "../../../wayland/layer_surface.h"
#include "tray_menu.h"

bool tray_menu_create_surface(TrayMenuState &state, wl_compositor *compositor,
                              zwlr_layer_shell_v1 *layer_shell,
                              wl_output *output) {
    state.base.compositor = compositor;
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        .name_space = "kokusei-tray-menu",
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
        .width = static_cast<int32_t>(kPanelWidth),
        .height = static_cast<int32_t>(kPanelHeaderHeight),
        .empty_input_region = true,
    };
    state.base.layer_surface =
        layer_surface_create(state.base.surface, compositor, layer_shell, cfg,
                             &overlay_panel_listener, &state.base, output);
    if (!state.base.layer_surface)
        return false;

    state.base.output_scale.on_change = [&state](int32_t scale) {
        if (state.base.egl_window)
            wl_egl_window_resize(state.base.egl_window,
                                 state.base.width * scale,
                                 state.base.height * scale, 0, 0);
        if (state.base.frame_clock.surface)
            request_frame(state.base.frame_clock);
    };
    output_scale_watch(state.base.output_scale, state.base.surface);
    wl_surface_commit(state.base.surface);
    return true;
}

bool tray_menu_init_egl(TrayMenuState &state, Renderer &renderer,
                        TrayState &tray, EGLDisplay display, EGLConfig config,
                        EGLContext context) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state, &tray] {
        tray_menu_paint(state, tray);
    };
    return true;
}

void tray_menu_request_frame(TrayMenuState &state) {
    overlay_panel_request_frame(state.base);
}

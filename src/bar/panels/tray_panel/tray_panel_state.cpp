#include "tray_panel_state.h"

#include "../../../render/image.h"
#include "../../../wayland/layer_surface.h"
#include "tray_panel.h"

const Texture *tray_panel_detail_item_icon_texture(TrayPanelState &state,
                                                   const TrayItem &item) {
    std::string path = tray_item_icon_path(item);
    if (path.empty())
        return nullptr;
    auto it = state.icon_cache.find(path);
    if (it == state.icon_cache.end())
        it = state.icon_cache.emplace(path, load_image_texture(path)).first;
    return it->second.id ? &it->second : nullptr;
}

bool tray_panel_create_surface(TrayPanelState &state, wl_compositor *compositor,
                               zwlr_layer_shell_v1 *layer_shell,
                               wl_output *output) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-tray-panel", output);
}

bool tray_panel_init_egl(TrayPanelState &state, Renderer &renderer,
                         TrayState &tray, EGLDisplay display, EGLConfig config,
                         EGLContext context) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state, &tray] {
        tray_panel_paint(state, tray, state.pending_pill_center_x,
                         state.pending_bar_height,
                         state.pending_bar_top_margin);
    };
    return true;
}

void tray_panel_request_frame(TrayPanelState &state, float pill_center_x,
                              float bar_height, float bar_top_margin) {
    state.pending_pill_center_x = pill_center_x;
    state.pending_bar_height = bar_height;
    state.pending_bar_top_margin = bar_top_margin;
    overlay_panel_request_frame(state.base);
}

void tray_panel_toggle(TrayPanelState &state, float pill_center_x) {
    panel_lock_toggle(
        state.base, state.locked_center_x, pill_center_x,
        [&state] { state.visible_height = -1.0f; },
        [&state] {
            state.base.animations.animate(
                state.visible_height, 0.0f, kOverlayFadeMs,
                Easing::EaseOutCubic,
                [&state](float v) { state.visible_height = v; }, {},
                kPanelHeightAnimOwner);
        });
}

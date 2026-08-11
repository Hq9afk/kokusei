#pragma once

#include "../../../core/log.hpp"
#include "../../../dbus/tray/tray_service.hpp"
#include "../../../render/icon.hpp"
#include "../../../render/icons.hpp"
#include "../../../render/image.hpp"
#include "../../../render/node.hpp"
#include "../../../render/overlay_panel.hpp"
#include "../../../render/palette.hpp"
#include "../../../render/panel_chrome.hpp"
#include "../../../render/rect.hpp"
#include "../../../render/renderer.hpp"
#include "../../../render/scene.hpp"
#include "../../../render/text.hpp"
#include "../../../render/texture.hpp"
#include "../../../render/texture_cache.hpp"
#include "../../../wayland/layer_surface.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

constexpr float kTrayCellSize = 40.0f;
constexpr int kTrayColumns = 4;
constexpr float kTrayGridGap = 4.0f;
constexpr float kTrayPanelWidth = kTrayColumns * kTrayCellSize +
                                  (kTrayColumns - 1) * kTrayGridGap +
                                  2.0f * kPanelPadding;

struct TrayPanelState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    TextureCache tcache;
    std::unordered_map<std::string, Texture> icon_cache;

    Rect panel_rect;
    std::vector<PanelClickRegion> click_regions;
    float locked_center_x = -1.0f;
    float visible_height = -1.0f;

    float pending_pill_center_x = 0.0f;
    float pending_bar_height = 0.0f;
    float pending_bar_top_margin = 0.0f;
};

namespace tray_panel_detail {

inline const Texture *item_icon_texture(TrayPanelState &state,
                                        const TrayItem &item) {
    std::string path = tray_item_icon_path(item);
    if (path.empty())
        return nullptr;
    auto it = state.icon_cache.find(path);
    if (it == state.icon_cache.end())
        it = state.icon_cache.emplace(path, load_image_texture(path)).first;
    return it->second.id ? &it->second : nullptr;
}

}

inline bool tray_panel_create_surface(TrayPanelState &state,
                                      wl_compositor *compositor,
                                      zwlr_layer_shell_v1 *layer_shell,
                                      wl_output *output = nullptr) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-tray-panel", output);
}

inline void tray_panel_paint(TrayPanelState &state, TrayState &tray,
                             float pill_center_x, float bar_height,
                             float bar_top_margin);

inline bool tray_panel_init_egl(TrayPanelState &state, Renderer &renderer,
                                TrayState &tray, EGLDisplay display,
                                EGLConfig config, EGLContext context) {
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

inline void tray_panel_request_frame(TrayPanelState &state, float pill_center_x,
                                     float bar_height, float bar_top_margin) {
    state.pending_pill_center_x = pill_center_x;
    state.pending_bar_height = bar_height;
    state.pending_bar_top_margin = bar_top_margin;
    overlay_panel_request_frame(state.base);
}

inline void tray_panel_toggle(TrayPanelState &state,
                              float pill_center_x = -1.0f) {
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

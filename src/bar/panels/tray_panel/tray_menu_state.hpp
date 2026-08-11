#pragma once

#include "../../../dbus/tray/tray_service.hpp"
#include "../../../render/overlay_panel.hpp"
#include "../../../render/panel_chrome.hpp"
#include "../../../render/rect.hpp"
#include "../../../render/renderer.hpp"
#include "../../../render/scene.hpp"
#include "../../../render/texture_cache.hpp"
#include "../../../wayland/layer_surface.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <cstdint>
#include <string>
#include <vector>

constexpr float kTrayMenuRowHeight = 32.0f;

struct TrayMenuState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    TextureCache tcache;

    Rect panel_rect;
    std::vector<PanelClickRegion> click_regions;

    std::string item_key;
    std::vector<int32_t> menu_path;
};

namespace tray_menu_detail {

inline std::vector<MenuEntry> *current_menu_level(TrayState &tray,
                                                  TrayMenuState &state) {
    auto it = tray.menu_cache.find(state.item_key);
    if (it == tray.menu_cache.end())
        return nullptr;
    std::vector<MenuEntry> *level = &it->second;
    for (int32_t id : state.menu_path) {
        MenuEntry *found = nullptr;
        for (MenuEntry &entry : *level) {
            if (entry.id == id) {
                found = &entry;
                break;
            }
        }
        if (!found)
            return level;
        level = &found->children;
    }
    return level;
}

} // namespace tray_menu_detail

inline bool tray_menu_create_surface(TrayMenuState &state,
                                     wl_compositor *compositor,
                                     zwlr_layer_shell_v1 *layer_shell,
                                     wl_output *output = nullptr) {
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

inline void tray_menu_paint(TrayMenuState &state, TrayState &tray);

inline bool tray_menu_init_egl(TrayMenuState &state, Renderer &renderer,
                               TrayState &tray, EGLDisplay display,
                               EGLConfig config, EGLContext context) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state, &tray] {
        tray_menu_paint(state, tray);
    };
    return true;
}

inline void tray_menu_request_frame(TrayMenuState &state) {
    overlay_panel_request_frame(state.base);
}

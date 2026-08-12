#pragma once

#include "../../../dbus/tray/tray_service.h"
#include "../../../render/overlay_panel.h"
#include "../../../render/panel_chrome.h"
#include "../../../render/rect.h"
#include "../../../render/renderer.h"
#include "../../../render/scene.h"
#include "../../../render/texture_cache.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <EGL/egl.h>
#include <unordered_map>
#include <wayland-client.h>
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

// Shared with tray_panel.cpp's paint function.
const Texture *tray_panel_detail_item_icon_texture(TrayPanelState &state,
                                                   const TrayItem &item);

bool tray_panel_create_surface(TrayPanelState &state, wl_compositor *compositor,
                               zwlr_layer_shell_v1 *layer_shell,
                               wl_output *output = nullptr);

bool tray_panel_init_egl(TrayPanelState &state, Renderer &renderer,
                         TrayState &tray, EGLDisplay display, EGLConfig config,
                         EGLContext context);

void tray_panel_request_frame(TrayPanelState &state, float pill_center_x,
                              float bar_height, float bar_top_margin);

void tray_panel_toggle(TrayPanelState &state, float pill_center_x = -1.0f);

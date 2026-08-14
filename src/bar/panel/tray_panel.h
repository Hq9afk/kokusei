#pragma once

#include "service/tray_service.h"
#include "render/overlay_panel.h"
#include "render/panel_chrome.h"
#include "render/rect.h"
#include "render/renderer.h"
#include "render/scene.h"
#include "render/texture_cache.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <EGL/egl.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <wayland-client.h>

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

bool tray_menu_create_surface(TrayMenuState &state, wl_compositor *compositor,
                              zwlr_layer_shell_v1 *layer_shell,
                              wl_output *output = nullptr);

bool tray_menu_init_egl(TrayMenuState &state, Renderer &renderer,
                        TrayState &tray, EGLDisplay display, EGLConfig config,
                        EGLContext context);

void tray_menu_request_frame(TrayMenuState &state);

void tray_menu_paint(TrayMenuState &state, TrayState &tray);

void tray_menu_close(TrayMenuState &state);

void tray_menu_open(TrayMenuState &state, TrayState &tray, const TrayItem &item,
                    const Rect &anchor_cell, int32_t screen_width);

void tray_menu_handle_click(TrayMenuState &state, TrayState &tray, double px,
                            double py);

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

void tray_panel_paint(TrayPanelState &state, TrayState &tray,
                      float pill_center_x, float bar_height,
                      float bar_top_margin);

void tray_panel_handle_click(TrayPanelState &state, TrayState &tray,
                             TrayMenuState &menu, double px, double py,
                             uint32_t button);

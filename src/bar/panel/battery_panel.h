#pragma once

#include <EGL/egl.h>
#include <string>
#include <vector>
#include <wayland-client.h>

#include "render/overlay_panel.h"
#include "render/panel_chrome.h"
#include "render/rect.h"
#include "render/renderer.h"
#include "render/scene.h"
#include "render/texture_cache.h"

#include "service/keyboard.h"
#include "service/upower_service.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

constexpr float kBatteryEmptyStateHeight = 72.0f;
constexpr float kBatteryTextRowHeight = 18.0f;
constexpr float kBatteryPanelBarHeight = 6.0f;
constexpr float kBatteryPanelBarRadius = 3.0f;
constexpr float kBatteryBarTopGap = 12.0f;
constexpr float kBatteryDeviceRowBottomPad = 14.0f;
constexpr float kBatteryDeviceRowHeight =
    kBatteryTextRowHeight + kBatteryBarTopGap + kBatteryPanelBarHeight +
    kBatteryDeviceRowBottomPad;

struct BatteryPanelState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    TextureCache tcache;

    Rect panel_rect;
    std::vector<PanelClickRegion> click_regions;
    float locked_center_x = -1.0f;
    float visible_height = -1.0f;
    float scroll_offset = 0.0f;
    float visible_content_height = 0.0f;

    float pending_pill_center_x = 0.0f;
    float pending_bar_height = 0.0f;
    float pending_bar_top_margin = 0.0f;
};

namespace battery_panel_detail {

enum class RowKind {
    EmptyPluggedIn,
    EmptyNoBattery,
    Device,
    Spacer,
};

struct PanelRow {
    RowKind kind;
    float height;
    const UpowerDeviceEntry *entry = nullptr;
};

bool is_battery_type(uint32_t type);

std::string format_time(int seconds);

std::vector<PanelRow> build_rows(const UpowerState &u);

float content_height(const std::vector<PanelRow> &rows);

float panel_height(const std::vector<PanelRow> &rows);

} // namespace battery_panel_detail

bool battery_panel_create_surface(BatteryPanelState &state,
                                  wl_compositor *compositor,
                                  zwlr_layer_shell_v1 *layer_shell,
                                  wl_output *output = nullptr);

bool battery_panel_init_egl(BatteryPanelState &state, Renderer &renderer,
                            UpowerState &u, EGLDisplay display,
                            EGLConfig config, EGLContext context);

void battery_panel_request_frame(BatteryPanelState &state, float pill_center_x,
                                 float bar_height, float bar_top_margin);

void battery_panel_toggle(BatteryPanelState &state,
                          float pill_center_x = -1.0f);

void battery_panel_handle_scroll(BatteryPanelState &state, const UpowerState &u,
                                 double dy);

void battery_panel_handle_click(BatteryPanelState &state, double px, double py);

void battery_panel_handle_key_event(BatteryPanelState &state,
                                    const KeyEvent &event);

void battery_panel_paint(BatteryPanelState &state, const UpowerState &u,
                         float pill_center_x, float bar_height,
                         float bar_top_margin);

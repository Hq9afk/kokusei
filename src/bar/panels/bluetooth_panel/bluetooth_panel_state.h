#pragma once

#include "../../../dbus/bluetooth/bluetooth_service.h"
#include "../../../render/overlay_panel.h"
#include "../../../render/panel_chrome.h"
#include "../../../render/rect.h"
#include "../../../render/renderer.h"
#include "../../../render/scene.h"
#include "../../../render/texture_cache.h"
#include "../../../wayland/keyboard.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <EGL/egl.h>
#include <string>
#include <vector>
#include <wayland-client.h>

constexpr float kBtEmptyStateHeight = 72.0f;
constexpr float kBtSectionGapSmall = 20.0f;
constexpr float kBtSectionGapLarge = 24.0f;

struct BluetoothPanelState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    TextureCache tcache;

    std::string sub_mode, sub_device_path;

    Rect panel_rect;
    Rect sub_rect;
    std::vector<PanelClickRegion> click_regions;
    float locked_center_x = -1.0f;
    float visible_height = -1.0f;
    float scroll_offset = 0.0f;
    float visible_content_height = 0.0f;

    float pending_pill_center_x = 0.0f;
    float pending_bar_height = 0.0f;
    float pending_bar_top_margin = 0.0f;
};

namespace bluetooth_panel_detail {

// Shared with bluetooth_panel.cpp's paint function.

enum class RowKind {
    NoAdapter,
    Off,
    Empty,
    SectionConnected,
    SectionPaired,
    SectionNearby,
    Device,
    Spacer,
};

struct PanelRow {
    RowKind kind;
    float height;
    const BluetoothDeviceInfo *info = nullptr;
};

std::vector<PanelRow> build_rows(const BluetoothState &bt);

float content_height(const std::vector<PanelRow> &rows);

float panel_height(const std::vector<PanelRow> &rows);

}

bool bluetooth_panel_create_surface(BluetoothPanelState &state,
                                    wl_compositor *compositor,
                                    zwlr_layer_shell_v1 *layer_shell,
                                    wl_output *output = nullptr);

bool bluetooth_panel_init_egl(BluetoothPanelState &state, Renderer &renderer,
                              BluetoothState &bt, EGLDisplay display,
                              EGLConfig config, EGLContext context);

void bluetooth_panel_request_frame(BluetoothPanelState &state,
                                   float pill_center_x, float bar_height,
                                   float bar_top_margin);

void bluetooth_panel_toggle(BluetoothPanelState &state, BluetoothState &bt,
                            float pill_center_x = -1.0f);

void bluetooth_panel_handle_scroll(BluetoothPanelState &state,
                                   BluetoothState &bt, double dy);

void bluetooth_panel_open_sub(BluetoothPanelState &state,
                              const std::string &mode,
                              const std::string &device_path);

void bluetooth_panel_close_sub(BluetoothPanelState &state);

void bluetooth_panel_handle_key_event(BluetoothPanelState &state,
                                      BluetoothState &bt,
                                      const KeyEvent &event);

void bluetooth_panel_handle_click(BluetoothPanelState &state,
                                  BluetoothState &bt, double px, double py);

#pragma once

#include "../../../dbus/bluetooth/bluetooth_service.hpp"
#include "../../../render/icon.hpp"
#include "../../../render/icons.hpp"
#include "../../../render/node.hpp"
#include "../../../render/overlay_panel.hpp"
#include "../../../render/palette.hpp"
#include "../../../render/panel_chrome.hpp"
#include "../../../render/panel_scroll.hpp"
#include "../../../render/rect.hpp"
#include "../../../render/renderer.hpp"
#include "../../../render/scene.hpp"
#include "../../../render/text.hpp"
#include "../../../render/texture.hpp"
#include "../../../render/texture_cache.hpp"
#include "../../../wayland/keyboard.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

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

using panel_chrome_detail::cached_icon;
using panel_chrome_detail::cached_text;
using panel_chrome_detail::cached_text_clipped;

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

inline std::vector<PanelRow> build_rows(const BluetoothState &bt) {
    std::vector<PanelRow> rows;

    if (!bt.adapter_present) {
        rows.push_back({RowKind::NoAdapter, kBtEmptyStateHeight});
        rows.push_back({RowKind::Spacer, kPanelTrailingSpacerHeight});
        return rows;
    }
    if (!bt.powered) {
        rows.push_back({RowKind::Off, kBtEmptyStateHeight});
        rows.push_back({RowKind::Spacer, kPanelTrailingSpacerHeight});
        return rows;
    }

    std::vector<const BluetoothDeviceInfo *> connected, paired, nearby;
    for (const BluetoothDeviceInfo &d : bt.devices) {
        if (bluetooth_detail::is_connected_bucket(d))
            connected.push_back(&d);
        else if (bluetooth_detail::is_paired_bucket(d))
            paired.push_back(&d);
        else if (bt.scanning)
            nearby.push_back(&d);
    }

    if (connected.empty() && paired.empty() && nearby.empty()) {
        rows.push_back({RowKind::Empty, kBtEmptyStateHeight});
    } else {
        if (!connected.empty()) {
            rows.push_back({RowKind::SectionConnected, kBtSectionGapSmall});
            for (const BluetoothDeviceInfo *d : connected)
                rows.push_back({RowKind::Device, kPanelDeviceRowHeight, d});
        }
        if (!paired.empty()) {
            rows.push_back({RowKind::SectionPaired, kBtSectionGapLarge});
            for (const BluetoothDeviceInfo *d : paired)
                rows.push_back({RowKind::Device, kPanelDeviceRowHeight, d});
        }
        if (!nearby.empty()) {
            rows.push_back({RowKind::SectionNearby, kBtSectionGapLarge});
            for (const BluetoothDeviceInfo *d : nearby)
                rows.push_back({RowKind::Device, kPanelDeviceRowHeight, d});
        }
    }

    rows.push_back({RowKind::Spacer, kPanelTrailingSpacerHeight});
    return rows;
}

inline float content_height(const std::vector<PanelRow> &rows) {
    float h = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i > 0)
            h += kPanelListSpacing;
        h += rows[i].height;
    }
    return h;
}

inline float panel_height(const std::vector<PanelRow> &rows) {
    float h = kPanelPadding + kPanelHeaderHeight + kPanelHeaderDividerGap +
              1.0f + kPanelContentGap + content_height(rows) + kPanelPadding;
    return std::min(kPanelMaxHeight, h);
}

}

inline bool bluetooth_panel_create_surface(BluetoothPanelState &state,
                                           wl_compositor *compositor,
                                           zwlr_layer_shell_v1 *layer_shell) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-bluetooth-panel");
}

inline void bluetooth_panel_paint(BluetoothPanelState &state,
                                  BluetoothState &bt, float pill_center_x,
                                  float bar_height, float bar_top_margin);

inline bool bluetooth_panel_init_egl(BluetoothPanelState &state,
                                     Renderer &renderer, BluetoothState &bt,
                                     EGLDisplay display, EGLConfig config,
                                     EGLContext context) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state, &bt] {
        bluetooth_panel_paint(state, bt, state.pending_pill_center_x,
                              state.pending_bar_height,
                              state.pending_bar_top_margin);
    };
    return true;
}

inline void bluetooth_panel_request_frame(BluetoothPanelState &state,
                                          float pill_center_x, float bar_height,
                                          float bar_top_margin) {
    state.pending_pill_center_x = pill_center_x;
    state.pending_bar_height = bar_height;
    state.pending_bar_top_margin = bar_top_margin;
    overlay_panel_request_frame(state.base);
}

inline void bluetooth_panel_toggle(BluetoothPanelState &state,
                                   BluetoothState &bt,
                                   float pill_center_x = -1.0f) {
    state.sub_mode.clear();
    state.sub_device_path.clear();
    panel_lock_toggle(
        state.base, state.locked_center_x, pill_center_x,
        [&state, &bt] {
            state.visible_height = -1.0f;
            bluetooth_start_discovery(bt);
        },
        [&state, &bt] {
            bluetooth_stop_discovery(bt);
            state.scroll_offset = 0.0f;
            state.base.animations.animate(
                state.visible_height, 0.0f, kOverlayFadeMs,
                Easing::EaseOutCubic,
                [&state](float v) { state.visible_height = v; }, {},
                kPanelHeightAnimOwner);
        });
}

inline void bluetooth_panel_handle_scroll(BluetoothPanelState &state,
                                          BluetoothState &bt, double dy) {
    state.scroll_offset =
        panel_clamp_scroll(state.scroll_offset, static_cast<float>(dy),
                           bluetooth_panel_detail::content_height(
                               bluetooth_panel_detail::build_rows(bt)),
                           state.visible_content_height);
}

inline void bluetooth_panel_open_sub(BluetoothPanelState &state,
                                     const std::string &mode,
                                     const std::string &device_path) {
    state.sub_mode = mode;
    state.sub_device_path = device_path;
}
inline void bluetooth_panel_close_sub(BluetoothPanelState &state) {
    state.sub_mode.clear();
    state.sub_device_path.clear();
}

inline void bluetooth_panel_handle_key_event(BluetoothPanelState &state,
                                             BluetoothState &bt,
                                             const KeyEvent &event) {
    if (event.kind != KeyKind::Escape)
        return;
    if (!state.sub_mode.empty())
        bluetooth_panel_close_sub(state);
    else
        bluetooth_panel_toggle(state, bt);
}

inline void bluetooth_panel_handle_click(BluetoothPanelState &state,
                                         BluetoothState &bt, double px,
                                         double py) {
    auto hit = [](const Rect &r, double x, double y) {
        return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y &&
               y < r.y + r.h;
    };

    for (const PanelClickRegion &region : state.click_regions) {
        if (!hit(region.rect, px, py))
            continue;
        switch (region.kind) {
        case PanelClickKind::Close:
            bluetooth_panel_toggle(state, bt);
            return;
        case PanelClickKind::HeaderToggle:
            bluetooth_set_powered(bt, !bt.powered);
            return;
        case PanelClickKind::RowConnect: {
            const BluetoothDeviceInfo *info = nullptr;
            for (const BluetoothDeviceInfo &d : bt.devices)
                if (d.path == region.tag)
                    info = &d;
            if (!info)
                return;
            if (info->connected) {
                bluetooth_panel_open_sub(state, "disconnect", region.tag);
            } else if (info->paired || info->trusted) {
                bluetooth_connect(bt, region.tag);
            } else {
                bluetooth_pair(bt, region.tag);
            }
            return;
        }
        case PanelClickKind::RowForget:
            bluetooth_panel_open_sub(state, "forget", region.tag);
            return;
        case PanelClickKind::SubClose:
        case PanelClickKind::SubCancel:
            bluetooth_panel_close_sub(state);
            return;
        case PanelClickKind::SubConfirm:
            if (state.sub_mode == "disconnect") {
                bluetooth_disconnect(bt, state.sub_device_path);
            } else if (state.sub_mode == "forget") {
                bluetooth_forget(bt, state.sub_device_path);
            }
            bluetooth_panel_close_sub(state);
            return;
        case PanelClickKind::HeaderAction:
        case PanelClickKind::ErrorClose:
            return;
        }
    }

    bool inside_main = hit(state.panel_rect, px, py);
    bool inside_sub = hit(state.sub_rect, px, py);
    if (!inside_main && !inside_sub)
        bluetooth_panel_toggle(state, bt);
}

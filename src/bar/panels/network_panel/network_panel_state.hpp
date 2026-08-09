#pragma once

#include "../../../core/log.hpp"
#include "../../../dbus/network/network_service.hpp"
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
#include "../../../wayland/layer_surface.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

constexpr float kNetErrorBannerHeight = 48.0f;
constexpr float kNetworkEthernetBannerHeight = 40.0f;
constexpr float kNetworkUnavailableStateHeight = 72.0f;
constexpr float kNetworkScanningStateHeight = 48.0f;
constexpr float kNetworkSectionGapSmall = 20.0f;
constexpr float kNetworkSectionGapLarge = 24.0f;
constexpr float kNetworkConnectDisabledAlpha = 0.4f;
constexpr float kNetworkCaptiveBg[4] = {1.0f, 0.76f, 0.03f, 0.15f};
constexpr float kNetworkCaptiveFg[4] = {1.0f, 0.7569f, 0.0275f, 1.0f};

struct NetworkPanelState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    TextureCache tcache;

    std::string sub_mode, sub_ssid;
    std::string password_input;

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

namespace network_panel_detail {

using panel_chrome_detail::cached_icon;
using panel_chrome_detail::cached_text;
using panel_chrome_detail::cached_text_clipped;

inline const char *signal_icon(int pct) {
    if (pct > 75)
        return icon::wifi;
    if (pct > 50)
        return icon::wifi2;
    if (pct > 25)
        return icon::wifi1;
    return icon::wifi0;
}
inline int signal_band(int pct) {
    if (pct > 75)
        return 3;
    if (pct > 50)
        return 2;
    if (pct > 25)
        return 1;
    return 0;
}

inline std::vector<const NetworkInfo *>
sorted_by_signal(const std::vector<const NetworkInfo *> &in) {
    std::vector<const NetworkInfo *> out = in;
    std::sort(out.begin(), out.end(),
              [](const NetworkInfo *a, const NetworkInfo *b) {
                  int ba = signal_band(a->signal), bb = signal_band(b->signal);
                  if (ba != bb)
                      return ba > bb;
                  return a->signal > b->signal;
              });
    return out;
}

enum class RowKind {
    Error,
    Ethernet,
    NoAdapter,
    Disabled,
    Scanning,
    SectionConnected,
    SectionKnown,
    SectionAvailable,
    Device,
    Spacer,
};

struct PanelRow {
    RowKind kind;
    float height;
    const NetworkInfo *info = nullptr;
};

inline std::vector<PanelRow> build_rows(const NetworkState &net) {
    std::vector<PanelRow> rows;

    if (!net.last_error.empty())
        rows.push_back({RowKind::Error, kNetErrorBannerHeight});
    if (net.ethernet_connected)
        rows.push_back({RowKind::Ethernet, kNetworkEthernetBannerHeight});
    if (!net.wifi_available)
        rows.push_back({RowKind::NoAdapter, kNetworkUnavailableStateHeight});
    else if (!net.wifi_enabled)
        rows.push_back({RowKind::Disabled, kNetworkUnavailableStateHeight});
    if (net.wifi_available && net.wifi_enabled &&
        network_visible_count(net) == 0)
        rows.push_back({RowKind::Scanning, kNetworkScanningStateHeight});

    std::vector<const NetworkInfo *> connected, saved, available;
    for (const auto &[ssid, info] : net.networks) {
        if (info.connected)
            connected.push_back(&info);
        else if (info.existing && info.in_range)
            saved.push_back(&info);
        else if (!info.existing)
            available.push_back(&info);
    }

    if (!connected.empty()) {
        rows.push_back({RowKind::SectionConnected, kNetworkSectionGapSmall});
        for (const NetworkInfo *info : sorted_by_signal(connected))
            rows.push_back({RowKind::Device, kPanelDeviceRowHeight, info});
    }
    if (!saved.empty()) {
        rows.push_back({RowKind::SectionKnown, kNetworkSectionGapLarge});
        for (const NetworkInfo *info : sorted_by_signal(saved))
            rows.push_back({RowKind::Device, kPanelDeviceRowHeight, info});
    }
    if (!available.empty()) {
        rows.push_back({RowKind::SectionAvailable, kNetworkSectionGapLarge});
        for (const NetworkInfo *info : sorted_by_signal(available))
            rows.push_back({RowKind::Device, kPanelDeviceRowHeight, info});
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

inline float panel_chrome_top_offset() {
    return kPanelPadding + kPanelHeaderHeight + kPanelHeaderDividerGap + 1.0f +
           kPanelContentGap;
}

inline float panel_height(float content_h) {
    float h = panel_chrome_top_offset() + content_h + kPanelPadding;
    return std::min(kPanelMaxHeight, h);
}

inline float sub_panel_height(const std::string &mode) {
    float middle =
        mode == "password" ? kPanelSubPanelRowHeight : kPanelSubLabelHeight;
    float inner = kPanelDialogSpacerHeight + middle + kPanelRowGap +
                  kPanelSubPanelRowHeight + kPanelRowGap;
    return inner + 2.0f * kPanelPadding;
}

} // namespace network_panel_detail

inline bool network_panel_create_surface(NetworkPanelState &state,
                                         wl_compositor *compositor,
                                         zwlr_layer_shell_v1 *layer_shell) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-network-panel");
}

inline void network_panel_paint(NetworkPanelState &state, NetworkState &net,
                                float pill_center_x, float bar_height,
                                float bar_top_margin);

inline bool network_panel_init_egl(NetworkPanelState &state, Renderer &renderer,
                                   NetworkState &net, EGLDisplay display,
                                   EGLConfig config, EGLContext context) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state, &net] {
        network_panel_paint(state, net, state.pending_pill_center_x,
                            state.pending_bar_height,
                            state.pending_bar_top_margin);
    };
    return true;
}

inline void network_panel_request_frame(NetworkPanelState &state,
                                        float pill_center_x, float bar_height,
                                        float bar_top_margin) {
    state.pending_pill_center_x = pill_center_x;
    state.pending_bar_height = bar_height;
    state.pending_bar_top_margin = bar_top_margin;
    overlay_panel_request_frame(state.base);
}

inline void network_panel_toggle(NetworkPanelState &state,
                                 float pill_center_x = -1.0f) {
    state.sub_mode.clear();
    state.sub_ssid.clear();
    state.password_input.clear();
    panel_lock_toggle(
        state.base, state.locked_center_x, pill_center_x,
        [&state] { state.visible_height = -1.0f; },
        [&state] {
            state.scroll_offset = 0.0f;
            state.base.animations.animate(
                state.visible_height, 0.0f, kOverlayFadeMs,
                Easing::EaseOutCubic,
                [&state](float v) { state.visible_height = v; }, {},
                kPanelHeightAnimOwner);
        });
}

inline void network_panel_handle_scroll(NetworkPanelState &state,
                                        NetworkState &net, double dy) {
    state.scroll_offset =
        panel_clamp_scroll(state.scroll_offset, static_cast<float>(dy),
                           network_panel_detail::content_height(
                               network_panel_detail::build_rows(net)),
                           state.visible_content_height);
}

inline void network_panel_open_sub(NetworkPanelState &state,
                                   const std::string &mode,
                                   const std::string &ssid) {
    state.sub_mode = mode;
    state.sub_ssid = ssid;
    state.password_input.clear();
}
inline void network_panel_close_sub(NetworkPanelState &state) {
    state.sub_mode.clear();
    state.sub_ssid.clear();
    state.password_input.clear();
}

inline void network_panel_handle_key_event(NetworkPanelState &state,
                                           NetworkState &net,
                                           const KeyEvent &event) {
    switch (event.kind) {
    case KeyKind::Text:
        if (state.sub_mode == "password")
            state.password_input += event.text;
        break;
    case KeyKind::Backspace:
        if (state.sub_mode != "password")
            break;
        while (!state.password_input.empty() &&
               (static_cast<unsigned char>(state.password_input.back()) &
                0xC0) == 0x80)
            state.password_input.pop_back();
        if (!state.password_input.empty())
            state.password_input.pop_back();
        break;
    case KeyKind::Enter:
        if (state.sub_mode == "password" && state.password_input.size() >= 8) {
            network_connect(net, state.sub_ssid, state.password_input);
            network_panel_close_sub(state);
        }
        break;
    case KeyKind::Escape:
        if (!state.sub_mode.empty())
            network_panel_close_sub(state);
        else
            network_panel_toggle(state);
        break;
    default:
        break;
    }
}

inline void network_panel_handle_click(NetworkPanelState &state,
                                       NetworkState &net, double px,
                                       double py) {
    auto hit = [](const Rect &r, double x, double y) {
        return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y &&
               y < r.y + r.h;
    };

    for (const PanelClickRegion &region : state.click_regions) {
        if (!hit(region.rect, px, py))
            continue;
        switch (region.kind) {
        case PanelClickKind::HeaderAction:
            network_scan(net);
            return;
        case PanelClickKind::HeaderToggle:
            network_set_wifi_enabled(net, !net.wifi_enabled);
            return;
        case PanelClickKind::Close:
            network_panel_toggle(state);
            return;
        case PanelClickKind::ErrorClose:
            net.last_error.clear();
            return;
        case PanelClickKind::RowConnect: {
            auto it = net.networks.find(region.tag);
            if (it == net.networks.end())
                return;
            const NetworkInfo &info = it->second;
            if (info.connected) {
                network_panel_open_sub(state, "disconnect", region.tag);
            } else if (info.existing || info.security.empty() ||
                       info.security == "--") {
                network_connect(net, region.tag, "");
            } else {
                network_panel_open_sub(state, "password", region.tag);
            }
            return;
        }
        case PanelClickKind::RowForget:
            network_panel_open_sub(state, "forget", region.tag);
            return;
        case PanelClickKind::SubClose:
        case PanelClickKind::SubCancel:
            network_panel_close_sub(state);
            return;
        case PanelClickKind::SubConfirm:
            if (state.sub_mode == "password") {
                if (state.password_input.size() >= 8) {
                    network_connect(net, state.sub_ssid, state.password_input);
                    network_panel_close_sub(state);
                }
            } else if (state.sub_mode == "disconnect") {
                network_disconnect(net, state.sub_ssid);
                network_panel_close_sub(state);
            } else if (state.sub_mode == "forget") {
                network_forget(net, state.sub_ssid);
                network_panel_close_sub(state);
            }
            return;
        }
    }

    bool inside_main = hit(state.panel_rect, px, py);
    bool inside_sub = hit(state.sub_rect, px, py);
    if (!inside_main && !inside_sub)
        network_panel_toggle(state);
}

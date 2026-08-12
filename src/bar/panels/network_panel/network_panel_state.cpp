#include "network_panel_state.h"

#include "../../../render/icons.h"
#include "../../../render/panel_scroll.h"
#include "../../../wayland/layer_surface.h"
#include "network_panel.h"

#include <algorithm>

namespace network_panel_detail {

const char *signal_icon(int pct) {
    if (pct > 75)
        return icon::wifi;
    if (pct > 50)
        return icon::wifi2;
    if (pct > 25)
        return icon::wifi1;
    return icon::wifi0;
}

namespace {

int signal_band(int pct) {
    if (pct > 75)
        return 3;
    if (pct > 50)
        return 2;
    if (pct > 25)
        return 1;
    return 0;
}

std::vector<const NetworkInfo *>
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

float panel_chrome_top_offset() {
    return kPanelPadding + kPanelHeaderHeight + kPanelHeaderDividerGap + 1.0f +
           kPanelContentGap;
}

} // namespace

std::vector<PanelRow> build_rows(const NetworkState &net) {
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

float content_height(const std::vector<PanelRow> &rows) {
    float h = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i > 0)
            h += kPanelListSpacing;
        h += rows[i].height;
    }
    return h;
}

float panel_height(float content_h) {
    float h = panel_chrome_top_offset() + content_h + kPanelPadding;
    return std::min(kPanelMaxHeight, h);
}

float sub_panel_height(const std::string &mode) {
    float middle =
        mode == "password" ? kPanelSubPanelRowHeight : kPanelSubLabelHeight;
    float inner = kPanelDialogSpacerHeight + middle + kPanelRowGap +
                  kPanelSubPanelRowHeight + kPanelRowGap;
    return inner + 2.0f * kPanelPadding;
}

} // namespace network_panel_detail

bool network_panel_create_surface(NetworkPanelState &state,
                                  wl_compositor *compositor,
                                  zwlr_layer_shell_v1 *layer_shell,
                                  wl_output *output) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-network-panel", output);
}

bool network_panel_init_egl(NetworkPanelState &state, Renderer &renderer,
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

void network_panel_request_frame(NetworkPanelState &state, float pill_center_x,
                                 float bar_height, float bar_top_margin) {
    state.pending_pill_center_x = pill_center_x;
    state.pending_bar_height = bar_height;
    state.pending_bar_top_margin = bar_top_margin;
    overlay_panel_request_frame(state.base);
}

void network_panel_toggle(NetworkPanelState &state, float pill_center_x) {
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

void network_panel_handle_scroll(NetworkPanelState &state, NetworkState &net,
                                 double dy) {
    state.scroll_offset = panel_clamp_scroll(
        state.scroll_offset, static_cast<float>(dy),
        network_panel_detail::content_height(network_panel_detail::build_rows(net)),
        state.visible_content_height);
}

void network_panel_open_sub(NetworkPanelState &state, const std::string &mode,
                            const std::string &ssid) {
    state.sub_mode = mode;
    state.sub_ssid = ssid;
    state.password_input.clear();
}
void network_panel_close_sub(NetworkPanelState &state) {
    state.sub_mode.clear();
    state.sub_ssid.clear();
    state.password_input.clear();
}

void network_panel_handle_key_event(NetworkPanelState &state, NetworkState &net,
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

void network_panel_handle_click(NetworkPanelState &state, NetworkState &net,
                                double px, double py) {
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

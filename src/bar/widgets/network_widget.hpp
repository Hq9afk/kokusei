#pragma once

#include "../../dbus/network/network_service.hpp"
#include "../../render/icon.hpp"
#include "../../render/icons.hpp"
#include "widget_capsule.hpp"

#include <string>

namespace bar_detail {
inline const char *wifi_icon_glyph(const NetworkState &n) {
    if (n.connectivity == "portal")
        return icon::lock;
    if (n.ethernet_connected)
        return icon::router;
    if (!n.connected_ssid().empty()) {
        int sig = n.connected_signal();
        if (sig > 75)
            return icon::wifi;
        if (sig > 50)
            return icon::wifi2;
        if (sig > 25)
            return icon::wifi1;
        return icon::wifi0;
    }
    if (n.ethernet_available && !n.wifi_available)
        return icon::router;
    return icon::wifi_off;
}
inline std::string wifi_label(const NetworkState &n) {
    if (n.connectivity == "portal")
        return "Sign in";
    std::string label = n.ethernet_connected ? "Ethernet" : n.connected_ssid();
    return label.empty() ? "Wi-Fi" : label;
}

inline Pill wifi_pill(WaylandState &state) {
    const char *glyph = wifi_icon_glyph(state.network);
    if (glyph != state.wifi_icon_glyph_cached) {
        state.wifi_icon_texture = make_icon_texture(glyph);
        state.wifi_icon_glyph_cached = glyph;
    }
    return Pill{PillId::Wifi, &state.wifi_icon_texture,
                wifi_label(state.network), nullptr, [&state] {
                    close_other_overlays(state, PillId::Wifi);
                    if (!state.network_panel.base.open) {
                        update_pill_expand(state.capsule, state.animations,
                                           PillId::Wifi, true, true);
                        bar_paint(state);
                    }
                    network_panel_toggle(
                        state.network_panel,
                        pill_center_x(state.capsule, PillId::Wifi));
                    if (state.network_panel.base.open)
                        network_scan(state.network);
                }};
}
}

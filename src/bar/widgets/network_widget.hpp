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

inline Pill wifi_pill(MonitorOutput &mon) {
    const char *glyph = wifi_icon_glyph(mon.app->network);
    if (glyph != mon.wifi_icon_glyph_cached) {
        mon.wifi_icon_texture = make_icon_texture(glyph);
        mon.wifi_icon_glyph_cached = glyph;
    }
    return Pill{PillId::Wifi, &mon.wifi_icon_texture,
                wifi_label(mon.app->network), nullptr, [&mon] {
                    close_other_overlays(mon, PillId::Wifi);
                    if (!mon.network_panel.base.open) {
                        update_pill_expand(mon.capsule, mon.animations,
                                           PillId::Wifi, true, true);
                        bar_paint(mon);
                    }
                    network_panel_toggle(
                        mon.network_panel,
                        pill_center_x(mon.capsule, PillId::Wifi));
                    if (mon.network_panel.base.open)
                        network_scan(mon.app->network);
                }};
}
}

#include <string>

#include "bar/widget/network_widget.h"

#include "modules/bar.h"

#include "render/icon.h"
#include "render/icons.h"

#include "service/network_service.h"

namespace {

const char *wifi_icon_glyph(const NetworkState &n) {
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

std::string wifi_label(const NetworkState &n) {
    if (n.connectivity == "portal")
        return "Sign in";
    std::string label = n.ethernet_connected ? "Ethernet" : n.connected_ssid();
    return label.empty() ? "Wi-Fi" : label;
}

} // namespace

namespace bar_detail {

Pill wifi_pill(MonitorOutput &mon) {
    BarPerMonitorState &bs = bar_state(mon);
    const char *glyph = wifi_icon_glyph(mon.app->network);
    if (glyph != bs.wifi_icon_glyph_cached) {
        bs.wifi_icon_texture = make_icon_texture(glyph);
        bs.wifi_icon_glyph_cached = glyph;
    }
    return Pill{PillId::Wifi, &bs.wifi_icon_texture,
                wifi_label(mon.app->network), nullptr, [&mon, &bs] {
                    close_other_overlays(mon, PillId::Wifi);
                    if (!bs.network_panel.base.open) {
                        update_pill_expand(bs.capsule, mon.animations,
                                           PillId::Wifi, true, true);
                        bar_paint(mon);
                        overlay_panel_ensure(
                            bs.network_panel.base, mon.app->display,
                            [&] {
                                return network_panel_create_surface(
                                    bs.network_panel, mon.app->compositor,
                                    mon.app->layer_shell, mon.output.wl);
                            },
                            [&] {
                                return network_panel_init_egl(
                                    bs.network_panel, mon.app->renderer,
                                    mon.app->network, mon.app->egl_display,
                                    mon.app->egl_config, mon.app->egl_context);
                            });
                        bar_detail::rest_egl_current(*mon.app);
                    }
                    network_panel_toggle(
                        bs.network_panel,
                        pill_center_x(bs.capsule, PillId::Wifi));
                    if (bs.network_panel.base.open)
                        network_scan(mon.app->network);
                }};
}

} // namespace bar_detail

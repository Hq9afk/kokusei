#pragma once

#include "../../dbus/bluetooth/bluetooth_service.hpp"
#include "../../render/icon.hpp"
#include "../../render/icons.hpp"
#include "widget_capsule.hpp"

#include <string>

namespace bar_detail {
inline const char *bluetooth_icon_glyph(const BluetoothState &b) {
    if (!b.adapter_present || !b.powered)
        return icon::bluetooth_off;
    for (const BluetoothDeviceInfo &d : b.devices)
        if (d.connected)
            return icon::bluetooth_connected;
    return icon::bluetooth_on;
}
inline std::string bluetooth_label(const BluetoothState &b) {
    if (!b.adapter_present)
        return "Unavailable";
    if (!b.powered)
        return "Disconnected";
    for (const BluetoothDeviceInfo &d : b.devices)
        if (d.connected)
            return d.name.empty() ? d.address : d.name;
    return "Idle";
}

inline Pill bluetooth_pill(MonitorOutput &mon) {
    const char *glyph = bluetooth_icon_glyph(mon.app->bluetooth);
    if (glyph != mon.bluetooth_icon_glyph_cached) {
        mon.bluetooth_icon_texture = make_icon_texture(glyph);
        mon.bluetooth_icon_glyph_cached = glyph;
    }
    return Pill{PillId::Bluetooth, &mon.bluetooth_icon_texture,
                bluetooth_label(mon.app->bluetooth), nullptr, [&mon] {
                    close_other_overlays(mon, PillId::Bluetooth);
                    if (!mon.bluetooth_panel.base.open) {
                        update_pill_expand(mon.capsule, mon.animations,
                                           PillId::Bluetooth, true, true);
                        bar_paint(mon);
                    }
                    bluetooth_panel_toggle(
                        mon.bluetooth_panel, mon.app->bluetooth,
                        pill_center_x(mon.capsule, PillId::Bluetooth));
                }};
}
}

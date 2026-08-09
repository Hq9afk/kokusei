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

inline Pill bluetooth_pill(WaylandState &state) {
    const char *glyph = bluetooth_icon_glyph(state.bluetooth);
    if (glyph != state.bluetooth_icon_glyph_cached) {
        state.bluetooth_icon_texture = make_icon_texture(glyph);
        state.bluetooth_icon_glyph_cached = glyph;
    }
    return Pill{PillId::Bluetooth, &state.bluetooth_icon_texture,
                bluetooth_label(state.bluetooth), nullptr, [&state] {
                    close_other_overlays(state, PillId::Bluetooth);
                    bluetooth_panel_toggle(
                        state.bluetooth_panel, state.bluetooth,
                        pill_center_x(state.capsule, PillId::Bluetooth));
                }};
}
} // namespace bar_detail

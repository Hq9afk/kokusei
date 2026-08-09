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
                    // A click can land before the pill's own hover-expand
                    // tween has settled (no sustained hover required to
                    // click), so pill_center_x below would otherwise read a
                    // still-collapsing (or fully collapsed) center - snap
                    // pill_expand_t to its target instantly, then force a
                    // real repaint so pill_expanded_center_x is recomputed
                    // from that now-settled value before we read it (the
                    // snap alone does not update pill_expanded_center_x,
                    // only the next draw_pills() call does). Same fix as
                    // the IPC-triggered open path in kokusei.cpp.
                    if (!state.bluetooth_panel.base.open) {
                        update_pill_expand(state.capsule, state.animations,
                                           PillId::Bluetooth, true, true);
                        bar_paint(state);
                    }
                    bluetooth_panel_toggle(
                        state.bluetooth_panel, state.bluetooth,
                        pill_center_x(state.capsule, PillId::Bluetooth));
                }};
}
} // namespace bar_detail

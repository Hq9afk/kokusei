#include "bar/widget/bluetooth_widget.h"

#include "bar/bar.h"

#include "render/icon.h"
#include "render/icons.h"
#include "service/bluetooth_service.h"

#include <string>

namespace {

const char *bluetooth_icon_glyph(const BluetoothState &b) {
    if (!b.adapter_present || !b.powered)
        return icon::bluetooth_off;
    for (const BluetoothDeviceInfo &d : b.devices)
        if (d.connected)
            return icon::bluetooth_connected;
    return icon::bluetooth_on;
}

std::string bluetooth_label(const BluetoothState &b) {
    if (!b.adapter_present)
        return "Unavailable";
    if (!b.powered)
        return "Disconnected";
    for (const BluetoothDeviceInfo &d : b.devices)
        if (d.connected)
            return d.name.empty() ? d.address : d.name;
    return "Idle";
}

} // namespace

namespace bar_detail {

Pill bluetooth_pill(MonitorOutput &mon) {
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

} // namespace bar_detail

#pragma once

#include "../../render/icon.hpp"
#include "../../render/icons.hpp"
#include "../panels/tray_panel/tray_panel.hpp"
#include "widget_capsule.hpp"

inline void init_stub_widgets(MonitorOutput &mon) {
    mon.power_texture = make_icon_texture(icon::power);
    mon.tray_texture = make_icon_texture(icon::tray);
    mon.cpu_texture = make_icon_texture(icon::cpu);
    mon.control_center_texture = make_icon_texture(icon::control_center);
}

namespace bar_detail {
inline Pill tray_pill(MonitorOutput &mon) {
    return Pill{PillId::Tray, &mon.tray_texture, "Tray", nullptr, [&mon] {
                    close_other_overlays(mon, PillId::Tray);
                    tray_menu_close(mon.tray_menu);
                    if (!mon.tray_panel.base.open) {
                        update_pill_expand(mon.capsule, mon.animations,
                                           PillId::Tray, true, true);
                        bar_paint(mon);
                    }
                    tray_panel_toggle(mon.tray_panel,
                                      pill_center_x(mon.capsule, PillId::Tray));
                }};
}

inline Pill cpu_pill(MonitorOutput &mon) {
    return Pill{PillId::Cpu, &mon.cpu_texture, "CPU"};
}

inline Pill control_center_pill(MonitorOutput &mon) {
    return Pill{PillId::ControlCenter, &mon.control_center_texture,
                "Control Center"};
}
} // namespace bar_detail

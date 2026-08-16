#include "bar/widget/control_center_widget.h"
#include "bar/panel/tray_panel.h"
#include "modules/bar.h"

#include "render/icon.h"
#include "render/icons.h"

namespace bar_detail {

Pill tray_pill(MonitorOutput &mon) {
    BarPerMonitorState &bs = bar_state(mon);
    return Pill{PillId::Tray, &bs.tray_texture, "Tray", nullptr, [&mon, &bs] {
                    close_other_overlays(mon, PillId::Tray);
                    tray_menu_close(bs.tray_menu);
                    if (!bs.tray_panel.base.open) {
                        update_pill_expand(bs.capsule, mon.animations,
                                           PillId::Tray, true, true);
                        bar_paint(mon);
                    }
                    tray_panel_toggle(bs.tray_panel,
                                      pill_center_x(bs.capsule, PillId::Tray));
                }};
}

Pill cpu_pill(MonitorOutput &mon) {
    return Pill{PillId::Cpu, &bar_state(mon).cpu_texture, "CPU"};
}

Pill control_center_pill(MonitorOutput &mon) {
    BarPerMonitorState &bs = bar_state(mon);
    return Pill{PillId::ControlCenter, &bs.control_center_texture,
                "Control Center", nullptr, [&mon, &bs] {
                    close_other_overlays(mon, PillId::ControlCenter);
                    update_pill_expand(bs.capsule, mon.animations,
                                       PillId::ControlCenter, true, true);
                    bar_paint(mon);
                    if (Module *cc =
                            find_overlay_by_name(*mon.app, "controlcenter"))
                        cc->toggle_from_widget(*mon.app);
                }};
}

} // namespace bar_detail

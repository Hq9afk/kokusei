#include "bar/widget/control_center_widget.h"
#include "bar/bar.h"
#include "bar/panel/tray_panel.h"

#include "render/icon.h"
#include "render/icons.h"

void init_stub_widgets(MonitorOutput &mon) {
    mon.starward_texture = make_icon_texture(icon::power);
    mon.tray_texture = make_icon_texture(icon::tray);
    mon.cpu_texture = make_icon_texture(icon::cpu);
    mon.control_center_texture = make_icon_texture(icon::control_center);
}

namespace bar_detail {

Pill tray_pill(MonitorOutput &mon) {
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

Pill cpu_pill(MonitorOutput &mon) {
    return Pill{PillId::Cpu, &mon.cpu_texture, "CPU"};
}

Pill control_center_pill(MonitorOutput &mon) {
    return Pill{PillId::ControlCenter, &mon.control_center_texture,
                "Control Center", nullptr, [&mon] {
                    close_other_overlays(mon, PillId::ControlCenter);
                    update_pill_expand(mon.capsule, mon.animations,
                                       PillId::ControlCenter, true, true);
                    bar_paint(mon);
                    ControlCenterState &cc = mon.app->controlcenter;
                    if (!cc.base.open && mon.output.wl != cc.bound_output)
                        controlcenter_retarget(
                            cc, mon.app->compositor, mon.app->layer_shell,
                            mon.app->display, mon.app->renderer, *mon.app,
                            mon.app->egl_display, mon.app->egl_config,
                            mon.app->egl_context, mon.output.wl,
                            mon.output.name.c_str());
                    controlcenter_toggle(cc, true);
                }};
}

} // namespace bar_detail

#include "app/user_info.h"

#include "bar/panel/system_monitor_panel.h"
#include "bar/panel/tray_panel.h"
#include "bar/widget/control_center_widget.h"

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
                        overlay_panel_ensure(
                            bs.tray_panel.base, mon.app->display,
                            [&] {
                                return tray_panel_create_surface(
                                    bs.tray_panel, mon.app->compositor,
                                    mon.app->layer_shell, mon.output.wl);
                            },
                            [&] {
                                return tray_panel_init_egl(
                                    bs.tray_panel, mon.app->renderer,
                                    mon.app->tray, mon.app->egl_display,
                                    mon.app->egl_config, mon.app->egl_context);
                            });
                        bar_detail::rest_egl_current(*mon.app);
                    }
                    tray_panel_toggle(bs.tray_panel,
                                      pill_center_x(bs.capsule, PillId::Tray));
                }};
}

Pill cpu_pill(MonitorOutput &mon) {
    BarPerMonitorState &bs = bar_state(mon);
    return Pill{
        PillId::Cpu, &bs.cpu_texture, "CPU", nullptr, [&mon, &bs] {
            close_other_overlays(mon, PillId::Cpu);
            if (!bs.system_monitor_panel.base.open) {
                update_pill_expand(bs.capsule, mon.animations, PillId::Cpu,
                                   true, true);
                bar_paint(mon);
                overlay_panel_ensure(
                    bs.system_monitor_panel.base, mon.app->display,
                    [&] {
                        return system_monitor_panel_create_surface(
                            bs.system_monitor_panel, mon.app->compositor,
                            mon.app->layer_shell, mon.output.wl);
                    },
                    [&] {
                        return system_monitor_panel_init_egl(
                            bs.system_monitor_panel, mon.app->renderer,
                            mon.app->cpu_temp, mon.app->gpu_temp,
                            mon.app->system_stats, mon.app->egl_display,
                            mon.app->egl_config, mon.app->egl_context);
                    });
                bar_detail::rest_egl_current(*mon.app);
            }
            system_monitor_panel_toggle(bs.system_monitor_panel,
                                        pill_center_x(bs.capsule, PillId::Cpu));
            if (bs.system_monitor_panel.base.open) {
                cpu_temp_poll(mon.app->cpu_temp);
                gpu_temp_poll(mon.app->gpu_temp);
                system_stats_poll(mon.app->system_stats);
            }
        }};
}

Pill control_center_pill(MonitorOutput &mon) {
    BarPerMonitorState &bs = bar_state(mon);
    return Pill{PillId::ControlCenter, &bs.control_center_texture,
                user_info::username(), nullptr, [&mon, &bs] {
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

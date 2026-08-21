#include <string>

#include "bar/panel/battery_panel.h"

#include "bar/widget/battery_widget.h"

#include "modules/bar.h"

#include "render/icon.h"
#include "render/icons.h"
#include "render/palette.h"

namespace {

const float *battery_border_color(const UpowerState &u) {
    if (!u.present)
        return rgba(palette::accent);
    if (u.full)
        return rgba(palette::text);
    if (u.charging)
        return rgba(palette::accent_alt);
    if (u.percent <= 25)
        return rgba(palette::critical);
    return rgba(palette::accent);
}

const char *battery_icon_glyph(const UpowerState &u) {
    if (!u.present)
        return icon::battery_disabled;
    if (u.full)
        return icon::plugged_in;
    if (u.charging)
        return icon::battery_charging;
    if (u.percent <= 25)
        return icon::battery1;
    if (u.percent <= 50)
        return icon::battery2;
    if (u.percent <= 75)
        return icon::battery3;
    return icon::battery4;
}

std::string battery_label(const UpowerState &u) {
    if (!u.present)
        return "No Battery";
    if (u.full)
        return "Plugged in";
    return std::to_string(u.percent) + "%";
}

} // namespace

namespace bar_detail {

Pill battery_pill(MonitorOutput &mon) {
    BarPerMonitorState &bs = bar_state(mon);
    const UpowerState &u = mon.app->upower;
    const char *glyph = battery_icon_glyph(u);
    if (glyph != bs.battery_icon_glyph) {
        bs.battery_icon_texture = make_icon_texture(glyph);
        bs.battery_icon_glyph = glyph;
    }
    return Pill{PillId::Battery, &bs.battery_icon_texture, battery_label(u),
                battery_border_color(u), [&mon, &bs] {
                    close_other_overlays(mon, PillId::Battery);
                    if (!bs.battery_panel.base.open) {
                        update_pill_expand(bs.capsule, mon.animations,
                                           PillId::Battery, true, true);
                        bar_paint(mon);
                        overlay_panel_ensure(
                            bs.battery_panel.base, mon.app->display,
                            [&] {
                                return battery_panel_create_surface(
                                    bs.battery_panel, mon.app->compositor,
                                    mon.app->layer_shell, mon.output.wl);
                            },
                            [&] {
                                return battery_panel_init_egl(
                                    bs.battery_panel, mon.app->renderer,
                                    mon.app->upower, mon.app->egl_display,
                                    mon.app->egl_config, mon.app->egl_context);
                            });
                        bar_detail::rest_egl_current(*mon.app);
                    }
                    battery_panel_toggle(
                        bs.battery_panel,
                        pill_center_x(bs.capsule, PillId::Battery));
                }};
}

} // namespace bar_detail

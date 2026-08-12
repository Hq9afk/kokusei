#include "battery_widget.h"

#include "../../render/icon.h"
#include "../../render/icons.h"
#include "../../render/palette.h"
#include "../bar.h"

#include <string>

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

}

namespace bar_detail {

Pill battery_pill(MonitorOutput &mon) {
    const UpowerState &u = mon.app->upower;
    const char *glyph = battery_icon_glyph(u);
    if (glyph != mon.battery_icon_glyph) {
        mon.battery_icon_texture = make_icon_texture(glyph);
        mon.battery_icon_glyph = glyph;
    }
    return Pill{PillId::Battery, &mon.battery_icon_texture, battery_label(u),
                battery_border_color(u)};
}

}

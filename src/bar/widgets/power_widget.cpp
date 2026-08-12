#include "power_widget.h"

#include "../bar.h"

namespace bar_detail {

Pill power_pill(MonitorOutput &mon) {
    return Pill{PillId::Power, &mon.power_texture, "Power Menu", nullptr,
                [&mon] {
                    close_other_overlays(mon, PillId::Power);
                    logout_toggle(mon.app->logout, true);
                }};
}

}

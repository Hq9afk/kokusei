#pragma once

#include "widget_capsule.hpp"

namespace bar_detail {
inline Pill power_pill(MonitorOutput &mon) {
    return Pill{PillId::Power, &mon.power_texture, "Power Menu", nullptr,
                [&mon] {
                    close_other_overlays(mon, PillId::Power);
                    logout_toggle(mon.app->logout, true);
                }};
}
} // namespace bar_detail

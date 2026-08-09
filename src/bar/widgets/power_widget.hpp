#pragma once

#include "widget_capsule.hpp"

namespace bar_detail {
inline Pill power_pill(WaylandState &state) {
    return Pill{PillId::Power, &state.power_texture, "Power Menu", nullptr,
               [&state] {
                   close_other_overlays(state, PillId::Power);
                   logout_toggle(state.logout, true);
               }};
}
}

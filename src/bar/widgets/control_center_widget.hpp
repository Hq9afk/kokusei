#pragma once

#include "../../render/icon.hpp"
#include "../../render/icons.hpp"
#include "widget_capsule.hpp"

inline void init_stub_widgets(WaylandState &state) {
    state.power_texture = make_icon_texture(icon::power);
    state.tray_texture = make_icon_texture(icon::tray);
    state.cpu_texture = make_icon_texture(icon::cpu);
    state.control_center_texture = make_icon_texture(icon::control_center);
}

namespace bar_detail {
inline Pill tray_pill(WaylandState &state) {
    return Pill{PillId::Tray, &state.tray_texture, "Tray"};
}

inline Pill cpu_pill(WaylandState &state) {
    return Pill{PillId::Cpu, &state.cpu_texture, "CPU"};
}

inline Pill control_center_pill(WaylandState &state) {
    return Pill{PillId::ControlCenter, &state.control_center_texture,
                "Control Center"};
}
}

#pragma once

#include "../../render/icon.hpp"
#include "../../render/icons.hpp"
#include "../panels/tray_panel/tray_panel.hpp"
#include "widget_capsule.hpp"

inline void init_stub_widgets(WaylandState &state) {
    state.power_texture = make_icon_texture(icon::power);
    state.tray_texture = make_icon_texture(icon::tray);
    state.cpu_texture = make_icon_texture(icon::cpu);
    state.control_center_texture = make_icon_texture(icon::control_center);
}

namespace bar_detail {
inline Pill tray_pill(WaylandState &state) {
    return Pill{PillId::Tray, &state.tray_texture, "Tray", nullptr, [&state] {
                    close_other_overlays(state, PillId::Tray);
                    tray_menu_close(state.tray_menu);
                    if (!state.tray_panel.base.open) {
                        update_pill_expand(state.capsule, state.animations,
                                           PillId::Tray, true, true);
                        bar_paint(state);
                    }
                    tray_panel_toggle(state.tray_panel,
                                      pill_center_x(state.capsule, PillId::Tray));
                }};
}

inline Pill cpu_pill(WaylandState &state) {
    return Pill{PillId::Cpu, &state.cpu_texture, "CPU"};
}

inline Pill control_center_pill(WaylandState &state) {
    return Pill{PillId::ControlCenter, &state.control_center_texture,
                "Control Center"};
}
}

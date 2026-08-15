#include "app/key_dispatch.h"

#include "app/monitor_output.h"
#include "app/wayland_state.h"
#include "bar/panel/bluetooth_panel.h"
#include "bar/panel/network_panel.h"
#include "bar/panel/volume_panel.h"
#include "modules/bar.h"
#include "modules/controlcenter.h"
#include "modules/launcher.h"
#include "modules/matrix.h"
#include "modules/settings.h"
#include "modules/starward.h"
#include "modules/visualizer.h"

#include <functional>

namespace {

struct KeyDispatchTarget {
    std::function<bool()> is_open;
    std::function<void(const KeyEvent &)> handle;
    std::function<void()> after;
};

std::vector<KeyDispatchTarget> singleton_targets(WaylandState &state) {
    return {
        {[&state] { return state.launcher.open; },
         [&state](const KeyEvent &event) {
             launcher_handle_key_event(state.launcher, event);
         },
         [&state] {
             launcher_request_frame(state.launcher);
             bar_detail::rest_egl_current(state);
         }},
        {[&state] { return state.settings.base.open; },
         [&state](const KeyEvent &event) {
             settings_handle_key_event(
                 state.settings, state.cfg,
                 [&state](Config c) {
                     bar_detail::save_and_apply_config_update(state, c);
                 },
                 event);
         },
         [&state] {
             settings_request_frame(state.settings);
             bar_detail::rest_egl_current(state);
         }},
        {[&state] { return state.starward.base.open; },
         [&state](const KeyEvent &event) {
             starward_handle_key_event(state.starward, event);
         },
         [&state] {
             starward_request_frame(state.starward);
             bar_detail::rest_egl_current(state);
         }},
        {[&state] { return state.controlcenter.base.open; },
         [&state](const KeyEvent &event) {
             controlcenter_handle_key_event(state.controlcenter, event);
         },
         [&state] {
             controlcenter_request_frame(
                 state.controlcenter,
                 static_cast<float>(bar_detail::kBarHeight),
                 static_cast<float>(bar_detail::kBarTopMargin));
             bar_detail::rest_egl_current(state);
         }},
        {[&state] { return state.matrix.base.open; },
         [&state](const KeyEvent &event) {
             matrix_handle_key_event(state.matrix, state, event);
         },
         [&state] {
             matrix_request_frame(state.matrix);
             bar_detail::rest_egl_current(state);
         }},
        {[&state] { return state.visualizer.base.open; },
         [&state](const KeyEvent &event) {
             visualizer_handle_key_event(state.visualizer, state, event);
         },
         [&state] {
             visualizer_request_frame(state.visualizer);
             bar_detail::rest_egl_current(state);
         }},
    };
}

bool dispatch_to_panels(WaylandState &state,
                        const std::vector<KeyEvent> &events) {
    for (auto &mon : state.outputs) {
        if (mon->network_panel.base.open) {
            for (const KeyEvent &event : events)
                network_panel_handle_key_event(mon->network_panel,
                                               state.network, event);
            bar_detail::network_panel_dispatch(state, true);
            return true;
        }
        if (mon->bluetooth_panel.base.open) {
            for (const KeyEvent &event : events)
                bluetooth_panel_handle_key_event(mon->bluetooth_panel,
                                                 state.bluetooth, event);
            bar_detail::bluetooth_panel_dispatch(state);
            return true;
        }
        if (mon->volume_panel.base.open) {
            for (const KeyEvent &event : events)
                volume_panel_handle_key_event(mon->volume_panel, state.pipewire,
                                              event);
            bar_detail::volume_panel_dispatch(state);
            return true;
        }
    }
    return false;
}

} // namespace

void dispatch_key_events(WaylandState &state,
                         const std::vector<KeyEvent> &events) {
    if (events.empty())
        return;

    for (KeyDispatchTarget &target : singleton_targets(state)) {
        if (!target.is_open())
            continue;
        for (const KeyEvent &event : events)
            target.handle(event);
        target.after();
        return;
    }

    dispatch_to_panels(state, events);
}

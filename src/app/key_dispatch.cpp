#include "app/key_dispatch.h"
#include "app/monitor_output.h"
#include "app/wayland_state.h"

#include "modules/bar.h"

void dispatch_key_events(WaylandState &state,
                         const std::vector<KeyEvent> &events) {
    if (events.empty())
        return;

    for (auto &m : state.overlays) {
        if (!m->is_open())
            continue;
        for (const KeyEvent &event : events)
            m->handle_key_event(state, event);
        m->request_frame();
        bar_detail::rest_egl_current(state);
        return;
    }

    for (auto &mon : state.outputs) {
        for (auto &m : mon->modules) {
            if (!m->is_open())
                continue;
            for (const KeyEvent &event : events)
                m->handle_key_event(state, *mon, event);
            m->request_frame();
            bar_detail::rest_egl_current(state);
            return;
        }
    }
}

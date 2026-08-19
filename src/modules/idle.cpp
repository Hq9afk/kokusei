#include <cstdlib>

#include "app/monitor_output.h"
#include "app/wayland_state.h"

#include "core/log.h"

#include "modules/idle.h"

namespace {

void idle_run_command(const std::string &cmd) {
    if (cmd.empty())
        return;
    int rc = system(cmd.c_str());
    if (rc != 0)
        klog("idle: command exited %d: %s", rc, cmd.c_str());
}

void idle_notification_idled(void *data, ext_idle_notification_v1 *) {
    auto *state = static_cast<IdleState *>(data);
    state->idled = true;
    klog("idle: idled after %us", state->timeout_seconds);
    idle_run_command(state->on_idle_command);
}

void idle_notification_resumed(void *data, ext_idle_notification_v1 *) {
    auto *state = static_cast<IdleState *>(data);
    state->idled = false;
    klog("idle: resumed");
    idle_run_command(state->on_resume_command);
}

constexpr ext_idle_notification_v1_listener idle_notification_listener = {
    .idled = idle_notification_idled,
    .resumed = idle_notification_resumed,
};

} // namespace

bool idle_init(IdleState &state) {
    if (!state.notifier || !state.seat) {
        klog("idle: compositor is missing ext_idle_notifier_v1 or wl_seat, "
             "skipping");
        return false;
    }
    state.notification = ext_idle_notifier_v1_get_idle_notification(
        state.notifier, state.timeout_seconds * 1000, state.seat);
    if (!state.notification)
        return false;
    ext_idle_notification_v1_add_listener(state.notification,
                                          &idle_notification_listener, &state);
    klog("idle: watching for %us of inactivity", state.timeout_seconds);
    return true;
}

void idle_set_inhibited(IdleState &state, wl_surface *surface, bool inhibited) {
    if (inhibited == (state.inhibitor != nullptr))
        return;
    if (inhibited) {
        if (!state.inhibit_manager) {
            klog("idle: compositor is missing zwp_idle_inhibit_manager_v1, "
                 "cannot inhibit");
            return;
        }
        state.inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(
            state.inhibit_manager, surface);
        klog("idle: inhibit enabled");
    } else {
        zwp_idle_inhibitor_v1_destroy(state.inhibitor);
        state.inhibitor = nullptr;
        klog("idle: inhibit disabled");
    }
}

std::vector<IpcHandler> idle_ipc_handlers(WaylandState &state) {
    wl_surface *bar_surface =
        state.outputs.empty() ? nullptr : state.outputs.front()->surface;
    return {
        {"idle-inhibit on",
         [&state, bar_surface] {
             idle_set_inhibited(state.idle, bar_surface, true);
         },
         "enable the idle inhibitor"},
        {"idle-inhibit off",
         [&state, bar_surface] {
             idle_set_inhibited(state.idle, bar_surface, false);
         },
         "disable the idle inhibitor"},
    };
}

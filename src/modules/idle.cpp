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
    if (state->on_idled)
        state->on_idled();
}

void idle_notification_resumed(void *data, ext_idle_notification_v1 *) {
    auto *state = static_cast<IdleState *>(data);
    state->idled = false;
    klog("idle: resumed");
    idle_run_command(state->on_resume_command);
    if (state->on_resumed)
        state->on_resumed();
}

constexpr ext_idle_notification_v1_listener idle_notification_listener = {
    .idled = idle_notification_idled,
    .resumed = idle_notification_resumed,
};

void idle_recent_activity_idled(void *data, ext_idle_notification_v1 *) {
    static_cast<IdleState *>(data)->recent_activity_idled = true;
}

void idle_recent_activity_resumed(void *data, ext_idle_notification_v1 *) {
    static_cast<IdleState *>(data)->recent_activity_idled = false;
}

constexpr ext_idle_notification_v1_listener idle_recent_activity_listener = {
    .idled = idle_recent_activity_idled,
    .resumed = idle_recent_activity_resumed,
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
    if (state.notification)
        ext_idle_notification_v1_add_listener(
            state.notification, &idle_notification_listener, &state);
    state.recent_activity_notification =
        ext_idle_notifier_v1_get_idle_notification(
            state.notifier, kIdleRecentActivityPulseSeconds * 1000, state.seat);
    if (state.recent_activity_notification)
        ext_idle_notification_v1_add_listener(
            state.recent_activity_notification, &idle_recent_activity_listener,
            &state);
    else
        klog("idle: recent-activity notification failed, ambient/screensaver "
             "clock disabled");
    klog("idle: watching for %us of inactivity", state.timeout_seconds);
    return state.notification != nullptr;
}

void idle_tick(IdleState &state, const std::string &focused_monitor) {
    if (!state.recent_activity_idled && !focused_monitor.empty())
        state.last_activity[focused_monitor] = std::chrono::steady_clock::now();
}

bool is_idle(const IdleState &state, const std::string &monitor,
             uint32_t timeout_seconds) {
    auto it = state.last_activity.find(monitor);
    if (it == state.last_activity.end())
        return false;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::steady_clock::now() - it->second)
                       .count();
    return elapsed > static_cast<int64_t>(timeout_seconds);
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

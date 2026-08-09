#pragma once

#include "../core/log.hpp"
#include "ext-idle-notify-v1-client-protocol.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"

#include <cstdlib>
#include <string>

struct IdleState {
    ext_idle_notifier_v1 *notifier = nullptr;
    zwp_idle_inhibit_manager_v1 *inhibit_manager = nullptr;
    wl_seat *seat = nullptr;

    ext_idle_notification_v1 *notification = nullptr;
    zwp_idle_inhibitor_v1 *inhibitor =
        nullptr;

    bool idled = false;
    uint32_t timeout_seconds = 300;
    std::string on_idle_command;
    std::string on_resume_command;
};

inline void idle_run_command(const std::string &cmd) {
    if (cmd.empty())
        return;
    int rc = system(cmd.c_str());
    if (rc != 0)
        klog("idle: command exited %d: %s", rc, cmd.c_str());
}

inline void idle_notification_idled(void *data, ext_idle_notification_v1 *) {
    auto *state = static_cast<IdleState *>(data);
    state->idled = true;
    klog("idle: idled after %us", state->timeout_seconds);
    idle_run_command(state->on_idle_command);
}

inline void idle_notification_resumed(void *data, ext_idle_notification_v1 *) {
    auto *state = static_cast<IdleState *>(data);
    state->idled = false;
    klog("idle: resumed");
    idle_run_command(state->on_resume_command);
}

inline constexpr ext_idle_notification_v1_listener idle_notification_listener =
    {
        .idled = idle_notification_idled,
        .resumed = idle_notification_resumed,
};

inline bool idle_init(IdleState &state) {
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

inline void idle_set_inhibited(IdleState &state, wl_surface *surface,
                               bool inhibited) {
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

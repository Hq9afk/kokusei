#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/ipc.h"

#include "ext-idle-notify-v1-client-protocol.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"

struct WaylandState;

struct IdleState {
    ext_idle_notifier_v1 *notifier = nullptr;
    zwp_idle_inhibit_manager_v1 *inhibit_manager = nullptr;
    wl_seat *seat = nullptr;

    ext_idle_notification_v1 *notification = nullptr;
    zwp_idle_inhibitor_v1 *inhibitor = nullptr;

    bool idled = false;
    uint32_t timeout_seconds = 300;
    std::string on_idle_command;
    std::string on_resume_command;
};

bool idle_init(IdleState &state);

void idle_set_inhibited(IdleState &state, wl_surface *surface, bool inhibited);

std::vector<IpcHandler> idle_ipc_handlers(WaylandState &state);

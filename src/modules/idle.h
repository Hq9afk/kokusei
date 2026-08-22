#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "app/ipc.h"

#include "ext-idle-notify-v1-client-protocol.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"

struct WaylandState;

// Short pulse used purely to answer "was there input in the last N seconds",
// the primitive the per-monitor ambient/screensaver clock is built from.
// Independent of timeout_seconds below (the long, scriptable idle_command
// hook) - mirrors keqing-shell's IdleService.qml recentActivity IdleMonitor.
constexpr uint32_t kIdleRecentActivityPulseSeconds = 2;

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
    std::function<void()> on_idled;
    std::function<void()> on_resumed;

    ext_idle_notification_v1 *recent_activity_notification = nullptr;
    bool recent_activity_idled = false;
    std::map<std::string, std::chrono::steady_clock::time_point> last_activity;
};

bool idle_init(IdleState &state);

void idle_set_inhibited(IdleState &state, wl_surface *surface, bool inhibited);

std::vector<IpcHandler> idle_ipc_handlers(WaylandState &state);

// Called once a second with the currently Hyprland-focused monitor (empty
// string if none/not on Hyprland): stamps that monitor's activity clock
// forward when there was input in the last kIdleRecentActivityPulseSeconds.
// Unfocused monitors' clocks freeze and drift into "idle" on their own.
void idle_tick(IdleState &state, const std::string &focused_monitor);

bool is_idle(const IdleState &state, const std::string &monitor,
            uint32_t timeout_seconds);

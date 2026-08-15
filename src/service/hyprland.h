#pragma once

#include "service/workspace.h"

#include <string>
#include <unordered_map>

struct HyprlandState {
    std::string request_socket_path;
    std::string event_socket_path;
    int event_fd = -1;
    std::unordered_map<std::string, MonitorWorkspaces> by_monitor;
    std::string focused_monitor;
};

void hypr_refresh(HyprlandState &state);

bool hypr_init(HyprlandState &state);

enum class HyprEventResult {
    None,
    ActiveChanged,
    StructuralChanged,
    Disconnected
};

HyprEventResult hypr_poll_events(HyprlandState &state);

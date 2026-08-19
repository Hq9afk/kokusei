#pragma once

#include <string>
#include <unordered_map>

#include "service/workspace.h"

struct ShojiwmState {
    int fd = -1;
    std::unordered_map<std::string, MonitorWorkspaces> by_monitor;
};

bool shoji_init(ShojiwmState &state);

enum class ShojiEventResult { None, Updated, Disconnected };

ShojiEventResult shoji_poll(ShojiwmState &state);

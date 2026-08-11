#pragma once

#include <string>
#include <vector>

struct Workspace {
    int id = -1;
    std::string name;
    bool occupied = false;
};

struct MonitorWorkspaces {
    std::vector<Workspace> workspaces;
    int active_id = -1;
};

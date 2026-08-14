#pragma once

#include <functional>
#include <vector>

struct WaylandState;

struct IpcHandler {
    const char *verb;
    std::function<void()> fn;
    const char *description;
};

int open_ipc_socket();

void handle_ipc_accept(int listen_fd, WaylandState &state);

int run_ipc_client(int argc, char **argv);

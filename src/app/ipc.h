#pragma once

struct WaylandState;
struct IdleState;
struct LauncherState;
struct StarwardState;
struct ControlCenterState;

int open_ipc_socket();

void handle_ipc_accept(int listen_fd, WaylandState &state, IdleState &idle,
                       LauncherState &launcher, StarwardState &starward,
                       ControlCenterState &controlcenter, bool &running);

int run_ipc_client(int argc, char **argv);

#pragma once

struct WaylandState;
struct IdleState;
struct LauncherState;
struct LogoutState;

int open_ipc_socket();

void handle_ipc_accept(int listen_fd, WaylandState &state, IdleState &idle,
                       LauncherState &launcher, LogoutState &logout,
                       bool &running);

int run_ipc_client(int argc, char **argv);

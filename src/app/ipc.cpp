#include "app/ipc.h"

#include "app/monitor_output.h"
#include "app/wayland_state.h"
#include "core/log.h"
#include "modules/idle.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace {

std::string ipc_socket_path() {
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir)
        runtime_dir = "/tmp";
    return std::string(runtime_dir) + "/kokusei.sock";
}

std::vector<IpcHandler> ipc_handlers(WaylandState &state) {
    std::vector<IpcHandler> handlers;
    auto append = [&handlers](std::vector<IpcHandler> module_handlers) {
        for (IpcHandler &h : module_handlers)
            handlers.push_back(std::move(h));
    };
    append(idle_ipc_handlers(state));
    for (auto &m : state.overlays)
        append(m->ipc_handlers(state));
    if (!state.outputs.empty())
        for (auto &m : state.outputs.front()->modules)
            append(m->ipc_handlers(state));
    handlers.push_back({"kill", [&state] { state.running = false; },
                        "gracefully quit kokusei"});
    return handlers;
}

} // namespace

int open_ipc_socket() {
    std::string path = ipc_socket_path();
    unlink(path.c_str());

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        klog("socket: %s", strerror(errno));
        return -1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        klog("bind: %s", strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, 8) < 0) {
        klog("listen: %s", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

void handle_ipc_accept(int listen_fd, WaylandState &state) {
    int client_fd = accept(listen_fd, nullptr, nullptr);
    if (client_fd < 0)
        return;
    char buf[256];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        std::string cmd(buf);
        while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r'))
            cmd.pop_back();

        klog("ipc: %s", cmd.c_str());
        std::vector<IpcHandler> handlers = ipc_handlers(state);
        if (cmd == "--help" || cmd == "help") {
            std::string help = "kokusei <verb>:\n";
            for (const IpcHandler &h : handlers)
                help +=
                    std::string("  ") + h.verb + " - " + h.description + "\n";
            write(client_fd, help.c_str(), help.size());
        } else {
            bool matched = false;
            for (const IpcHandler &h : handlers) {
                if (cmd == h.verb) {
                    h.fn();
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                klog("ipc: unknown command '%s'", cmd.c_str());

                std::string err = "error: unknown command '" + cmd + "'\n";
                write(client_fd, err.c_str(), err.size());
            }
        }
    }
    close(client_fd);
}

int run_ipc_client(int argc, char **argv) {
    std::string cmd;
    for (int i = 1; i < argc; ++i) {
        if (i > 1)
            cmd += ' ';
        cmd += argv[i];
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "kokusei: socket: %s\n", strerror(errno));
        return 1;
    }

    timeval tv{.tv_sec = 2, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string path = ipc_socket_path();
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "kokusei: no running instance (%s: %s)\n", path.c_str(),
                strerror(errno));
        close(fd);
        return 1;
    }

    write(fd, cmd.c_str(), cmd.size());

    std::string response;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        response.append(buf, static_cast<size_t>(n));
    close(fd);

    if (response.starts_with("error: ")) {
        fprintf(stderr, "kokusei: %s", response.c_str() + 7);
        return 1;
    }
    fwrite(response.data(), 1, response.size(), stdout);
    return 0;
}

#pragma once

#include "../core/log.hpp"
#include "workspace.hpp"
#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

struct ShojiwmState {
    int fd = -1;
    std::vector<Workspace> workspaces;
    int active_id = -1;
};

namespace shoji_detail {

inline std::string socket_path() {
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    std::string dir = (runtime_dir && *runtime_dir) ? runtime_dir : "/tmp";
    std::string display =
        (wayland_display && *wayland_display) ? wayland_display : "wayland-0";
    return dir + "/shojiwm-" + display + ".sock";
}

inline bool send_line(int fd, const std::string &line) {
    std::string msg = line + "\n";
    size_t sent = 0;
    while (sent < msg.size()) {
        ssize_t n = send(fd, msg.data() + sent, msg.size() - sent, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

}

inline void shoji_apply_snapshot(ShojiwmState &state,
                                 const nlohmann::json &payload) {
    using nlohmann::json;
    std::string current_monitor =
        payload.value("currentMonitor", std::string());

    json monitor_workspaces = json::array();
    bool found_monitor = false;
    auto monitors_it = payload.find("monitors");
    if (monitors_it != payload.end()) {
        for (auto &mon : *monitors_it) {
            if (mon.value("name", std::string()) == current_monitor) {
                monitor_workspaces = mon.value("workspaces", json::array());
                found_monitor = true;
                break;
            }
        }
    }
    if (!found_monitor) {
        klog("shojiwm: no monitor named '%s' in snapshot",
             current_monitor.c_str());
        return;
    }

    std::vector<int> new_ids;
    std::vector<bool> new_occupied;
    int new_active_id = -1;
    for (auto &ws : monitor_workspaces) {
        int id = ws.value("index", -1);
        if (id < 0)
            continue;
        new_ids.push_back(id);
        new_occupied.push_back(ws.value("windowCount", 0) > 0);
        if (ws.value("active", false))
            new_active_id = id;
    }

    bool structural = new_ids.size() != state.workspaces.size();
    if (!structural) {
        for (size_t i = 0; i < new_ids.size(); ++i) {
            if (state.workspaces[i].id != new_ids[i]) {
                structural = true;
                break;
            }
        }
    }

    if (structural) {
        std::vector<Workspace> rebuilt;
        for (size_t i = 0; i < new_ids.size(); ++i) {
            Workspace ws;
            ws.id = new_ids[i];
            ws.occupied = new_occupied[i];
            rebuilt.push_back(std::move(ws));
        }
        state.workspaces = std::move(rebuilt);
    } else {

        for (size_t i = 0; i < new_ids.size(); ++i)
            state.workspaces[i].occupied = new_occupied[i];
    }
    state.active_id = new_active_id;
}

inline bool shoji_init(ShojiwmState &state) {
    std::string path = shoji_detail::socket_path();

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        klog("shojiwm: socket() failed: %s", strerror(errno));
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        klog("shojiwm: connect to %s failed: %s", path.c_str(),
             strerror(errno));
        close(fd);
        return false;
    }

    if (!shoji_detail::send_line(fd, R"({"id":1,"method":"workspaces.get"})")) {
        klog("shojiwm: send failed: %s", strerror(errno));
        close(fd);
        return false;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    state.fd = fd;
    klog("shojiwm: connected via %s", path.c_str());
    return true;
}

enum class ShojiEventResult { None, Updated, Disconnected };

inline ShojiEventResult shoji_poll(ShojiwmState &state) {
    using nlohmann::json;
    static std::string read_buffer;

    char buf[4096];
    ssize_t n;
    bool got_any = false;
    while ((n = recv(state.fd, buf, sizeof(buf), MSG_DONTWAIT)) > 0) {
        read_buffer.append(buf, static_cast<size_t>(n));
        got_any = true;
    }
    if (n == 0)
        return ShojiEventResult::Disconnected;
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        return ShojiEventResult::Disconnected;
    if (!got_any && read_buffer.empty())
        return ShojiEventResult::None;

    bool updated = false;
    size_t nl;
    while ((nl = read_buffer.find('\n')) != std::string::npos) {
        std::string line = read_buffer.substr(0, nl);
        read_buffer.erase(0, nl + 1);
        if (line.empty())
            continue;

        try {
            json msg = json::parse(line);
            if (msg.contains("result")) {
                shoji_apply_snapshot(state, msg["result"]);
                updated = true;
            } else if (msg.value("event", std::string()) ==
                           "workspaces.changed" &&
                       msg.contains("payload")) {
                shoji_apply_snapshot(state, msg["payload"]);
                updated = true;
            }
        } catch (const json::exception &e) {
            klog("shojiwm: failed to parse message: %s", e.what());
        }
    }
    return updated ? ShojiEventResult::Updated : ShojiEventResult::None;
}


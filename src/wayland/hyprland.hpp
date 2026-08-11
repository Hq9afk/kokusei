#pragma once

#include "../core/log.hpp"
#include "workspace.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

struct HyprlandState {
    std::string request_socket_path;
    std::string event_socket_path;
    int event_fd = -1;
    std::unordered_map<std::string, MonitorWorkspaces> by_monitor;
    std::string focused_monitor;
};

namespace hypr_detail {

inline bool resolve_socket_paths(HyprlandState &state) {
    const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!sig || !*sig)
        return false;

    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    std::string dir;
    if (runtime_dir && *runtime_dir) {
        dir = std::string(runtime_dir) + "/hypr/" + sig;
    }
    if (dir.empty() || !std::filesystem::is_directory(dir)) {
        dir = std::string("/tmp/hypr/") + sig;
    }
    if (!std::filesystem::is_directory(dir))
        return false;

    state.request_socket_path = dir + "/.socket.sock";
    state.event_socket_path = dir + "/.socket2.sock";
    return true;
}

inline std::string request(const std::string &socket_path,
                           const std::string &cmd) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return {};

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return {};
    }

    size_t sent = 0;
    while (sent < cmd.size()) {
        ssize_t n = send(fd, cmd.data() + sent, cmd.size() - sent, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            return {};
        }
        sent += static_cast<size_t>(n);
    }
    shutdown(fd, SHUT_WR);

    std::string result;
    char buf[4096];
    ssize_t n;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) {
        result.append(buf, static_cast<size_t>(n));
    }
    close(fd);
    return result;
}

inline std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t pos = s.find(delim, start);
        if (pos == std::string::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

} // namespace hypr_detail

inline void hypr_refresh(HyprlandState &state) {
    using nlohmann::json;

    state.by_monitor.clear();
    if (state.request_socket_path.empty())
        return;

    std::string workspaces_reply =
        hypr_detail::request(state.request_socket_path, "j/workspaces");
    try {
        json arr = json::parse(workspaces_reply);
        for (auto &w : arr) {
            Workspace ws;
            ws.id = w.value("id", -1);
            ws.name = w.value("name", std::string());
            if (ws.id < 0)
                continue;
            ws.occupied = w.value("windows", 0) > 0;
            std::string monitor = w.value("monitor", std::string());
            state.by_monitor[monitor].workspaces.push_back(std::move(ws));
        }
        for (auto &entry : state.by_monitor) {
            std::sort(entry.second.workspaces.begin(),
                      entry.second.workspaces.end(),
                      [](const Workspace &a, const Workspace &b) {
                          return a.id < b.id;
                      });
        }
    } catch (const json::exception &e) {
        klog("hyprland: failed to parse j/workspaces: %s", e.what());
    }

    std::string monitors_reply =
        hypr_detail::request(state.request_socket_path, "j/monitors");
    try {
        json arr = json::parse(monitors_reply);
        for (auto &m : arr) {
            std::string name = m.value("name", std::string());
            state.by_monitor[name].active_id =
                m.value("activeWorkspace", json::object()).value("id", -1);
            if (m.value("focused", false))
                state.focused_monitor = name;
        }
    } catch (const json::exception &e) {
        klog("hyprland: failed to parse j/monitors: %s", e.what());
    }
}

inline bool hypr_connect_events(HyprlandState &state) {
    if (state.event_socket_path.empty())
        return false;

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, state.event_socket_path.c_str(),
            sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    state.event_fd = fd;
    return true;
}

inline bool hypr_init(HyprlandState &state) {
    if (!hypr_detail::resolve_socket_paths(state)) {
        klog("hyprland: HYPRLAND_INSTANCE_SIGNATURE not set, skipping "
             "compositor integration");
        return false;
    }
    hypr_refresh(state);
    if (!hypr_connect_events(state)) {
        klog("hyprland: failed to connect event socket: %s", strerror(errno));
        return false;
    }
    return true;
}

enum class HyprEventResult {
    None,
    ActiveChanged,
    StructuralChanged,
    Disconnected
};

inline HyprEventResult hypr_poll_events(HyprlandState &state) {
    static std::string read_buffer;

    char buf[4096];
    ssize_t n;
    bool got_any = false;
    while ((n = recv(state.event_fd, buf, sizeof(buf), MSG_DONTWAIT)) > 0) {
        read_buffer.append(buf, static_cast<size_t>(n));
        got_any = true;
    }
    if (n == 0)
        return HyprEventResult::Disconnected;
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        return HyprEventResult::Disconnected;
    if (!got_any && read_buffer.empty())
        return HyprEventResult::None;

    HyprEventResult result = HyprEventResult::None;
    size_t nl;
    while ((nl = read_buffer.find('\n')) != std::string::npos) {
        std::string line = read_buffer.substr(0, nl);
        read_buffer.erase(0, nl + 1);

        size_t sep = line.find(">>");
        if (sep == std::string::npos)
            continue;
        std::string event = line.substr(0, sep);
        std::string data = line.substr(sep + 2);

        if (event == "focusedmonv2") {
            auto parts = hypr_detail::split(data, ',');
            if (parts.size() >= 2) {
                state.focused_monitor = parts[0];
                state.by_monitor[state.focused_monitor].active_id =
                    atoi(parts[1].c_str());
                if (result == HyprEventResult::None)
                    result = HyprEventResult::ActiveChanged;
            }
        } else if (event == "workspacev2") {
            // No monitor field on this event - it always applies to whichever
            // monitor last reported focus via focusedmonv2.
            auto parts = hypr_detail::split(data, ',');
            if (!parts.empty() && !state.focused_monitor.empty()) {
                state.by_monitor[state.focused_monitor].active_id =
                    atoi(parts[0].c_str());
                if (result == HyprEventResult::None)
                    result = HyprEventResult::ActiveChanged;
            }
        } else if (event == "createworkspacev2" ||
                   event == "destroyworkspacev2" ||
                   event == "renameworkspace" || event == "moveworkspacev2" ||
                   event == "openwindow" || event == "closewindow") {

            result = HyprEventResult::StructuralChanged;
        }
    }
    return result;
}

#pragma once

#include "../../core/async_process.hpp"
#include <sdbus-c++/sdbus-c++.h>

#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

struct NetworkInfo {
    std::string ssid, security;
    int signal = 0;
    bool connected = false, existing = false, in_range = false;
};

struct NetworkState {
    std::unique_ptr<sdbus::IProxy> nm;
    std::map<std::string, NetworkInfo> networks;
    std::set<std::string> existing_profiles;
    bool scanning = false, connecting = false, wifi_available = false,
        wifi_enabled = false, ethernet_available = false,
        ethernet_connected = false;
    std::string connecting_to, connectivity = "unknown",
                ethernet_connection_name, last_error;

    AsyncProcess device_proc, profile_proc, quick_scan_proc, scan_proc,
        connect_proc, disconnect_proc, forget_proc, connectivity_proc;
    bool device_running = false, profile_running = false,
        quick_scan_running = false, scan_running = false,
        connect_running = false, disconnect_running = false,
        forget_running = false, connectivity_running = false;
    std::string connect_ssid, connect_password;
    bool connect_saved = false;
    std::string disconnect_ssid, forget_ssid;

    bool init_done = false;
    std::chrono::steady_clock::time_point init_at;
    bool rescan_scheduled = false;
    std::chrono::steady_clock::time_point next_rescan_at;
    std::chrono::steady_clock::time_point next_connectivity_check_at;
    int scan_dot_step = 0;
    std::chrono::steady_clock::time_point next_dot_at;
    bool wifi_debounce_pending = false;
    std::chrono::steady_clock::time_point wifi_debounce_at;
    bool scan_pending = false;
    std::string prev_connected_ssid;

    bool dirty = false;

    std::string connected_ssid() const {
        for (const auto &[ssid, info] : networks)
            if (info.connected)
                return ssid;
        return "";
    }
    int connected_signal() const {
        for (const auto &[ssid, info] : networks)
            if (info.connected)
                return info.signal;
        return 0;
    }
};

namespace network_detail {

inline std::string trim(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

inline std::vector<std::string> nmcli_split(const std::string &line) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == ':') {
            parts.push_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

inline std::string join(const std::vector<std::string> &parts, size_t begin,
                        size_t end, char sep) {
    std::string out;
    for (size_t i = begin; i < end; ++i) {
        if (i > begin)
            out += sep;
        out += parts[i];
    }
    return out;
}

inline std::string unescape_nmcli_field(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() &&
            (s[i + 1] == ':' || s[i + 1] == '\\')) {
            out += s[i + 1];
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

} // namespace network_detail

struct NetworkDeviceStatus {
    bool wifi = false;
    bool ethernet = false;
    bool ethernet_connected = false;
    std::string ethernet_name;
};

inline std::map<std::string, NetworkInfo>
network_parse_networks(const std::string &text,
                       const std::set<std::string> &existing_profiles) {
    using namespace network_detail;
    std::map<std::string, NetworkInfo> result;
    std::istringstream stream(text);
    std::string raw_line;
    while (std::getline(stream, raw_line)) {
        std::string line = trim(raw_line);
        if (line.empty())
            continue;
        std::vector<std::string> parts = nmcli_split(line);
        if (parts.size() < 4)
            continue;

        const std::string &in_use = parts.back();
        int signal = 0;
        try {
            signal = std::stoi(parts[parts.size() - 2]);
        } catch (...) {
            signal = 0;
        }
        std::string security = parts[parts.size() - 3];
        if (!security.empty()) {
            size_t pos;
            if ((pos = security.find("WPA2 WPA3")) != std::string::npos)
                security.replace(pos, 9, "WPA2/WPA3");
            if ((pos = security.find("WPA1 WPA2")) != std::string::npos)
                security.replace(pos, 9, "WPA1/WPA2");
        }
        std::string ssid =
            unescape_nmcli_field(join(parts, 0, parts.size() - 3, ':'));
        if (ssid.empty())
            continue;

        bool is_connected = in_use == "*";
        auto it = result.find(ssid);
        if (it == result.end()) {
            NetworkInfo info;
            info.ssid = ssid;
            info.security = security.empty() ? "--" : security;
            info.signal = signal;
            info.connected = is_connected;
            info.existing = existing_profiles.count(ssid) > 0;
            info.in_range = true;
            result.emplace(ssid, std::move(info));
        } else if (is_connected) {
            it->second.connected = true;
        }
    }

    for (const std::string &name : existing_profiles) {
        if (result.count(name))
            continue;
        NetworkInfo info;
        info.ssid = name;
        info.security = "--";
        info.signal = 0;
        info.connected = false;
        info.existing = true;
        info.in_range = false;
        result.emplace(name, std::move(info));
    }
    return result;
}

inline NetworkDeviceStatus network_parse_device_status(const std::string &text) {
    using namespace network_detail;
    NetworkDeviceStatus st;
    std::istringstream stream(text);
    std::string raw_line;
    while (std::getline(stream, raw_line)) {
        std::string line = trim(raw_line);
        if (line.empty())
            continue;
        std::vector<std::string> parts = nmcli_split(line);
        if (parts.size() < 3)
            continue;
        const std::string &type = parts[1];
        const std::string &dev_state = parts[2];
        std::string conn = join(parts, 3, parts.size(), ':');
        if (dev_state == "unmanaged")
            continue;
        if (type == "wifi") {
            st.wifi = true;
        } else if (type == "ethernet") {
            st.ethernet = true;
            if (dev_state.rfind("connected", 0) == 0) {
                st.ethernet_connected = true;
                if (st.ethernet_name.empty())
                    st.ethernet_name = conn;
            }
        }
    }
    return st;
}

inline std::set<std::string> network_parse_profiles(const std::string &text) {
    using namespace network_detail;
    std::set<std::string> profiles;
    std::istringstream stream(text);
    std::string raw_line;
    while (std::getline(stream, raw_line)) {
        std::string line = trim(raw_line);
        if (line.empty())
            continue;
        size_t sep = line.rfind(':');
        if (sep == std::string::npos)
            continue;
        std::string type = line.substr(sep + 1);
        if (type != "802-11-wireless")
            continue;
        std::string name = trim(line.substr(0, sep));
        if (!name.empty())
            profiles.insert(name);
    }
    return profiles;
}

inline int network_visible_count(const NetworkState &state) {
    int n = 0;
    for (const auto &[ssid, info] : state.networks) {
        if (info.connected || (info.existing && info.in_range) ||
            !info.existing)
            ++n;
    }
    return n;
}

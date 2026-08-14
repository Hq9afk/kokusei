#pragma once

#include "core/async_process.h"
#include <sdbus-c++/sdbus-c++.h>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

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

std::string trim(const std::string &s);

}

struct NetworkDeviceStatus {
    bool wifi = false;
    bool ethernet = false;
    bool ethernet_connected = false;
    std::string ethernet_name;
};

std::map<std::string, NetworkInfo>
network_parse_networks(const std::string &text,
                       const std::set<std::string> &existing_profiles);

NetworkDeviceStatus network_parse_device_status(const std::string &text);

std::set<std::string> network_parse_profiles(const std::string &text);

int network_visible_count(const NetworkState &state);

using NetworkNotifyFn =
    std::function<void(const std::string &summary, const std::string &body)>;

bool network_init(NetworkState &state, sdbus::IConnection &bus);

void network_scan(NetworkState &state);

void network_connect(NetworkState &state, const std::string &ssid,
                     const std::string &password);

void network_disconnect(NetworkState &state, const std::string &ssid);

void network_forget(NetworkState &state, const std::string &ssid);

void network_set_wifi_enabled(NetworkState &state, bool enabled);

bool network_poll_device(NetworkState &state, const NetworkNotifyFn &notify);

bool network_poll_profile(NetworkState &state);

bool network_poll_quick_scan(NetworkState &state);

bool network_poll_scan(NetworkState &state, const NetworkNotifyFn &notify);

bool network_poll_connect(NetworkState &state, const NetworkNotifyFn &notify);

bool network_poll_disconnect(NetworkState &state,
                             const NetworkNotifyFn &notify);

bool network_poll_forget(NetworkState &state);

bool network_poll_connectivity(NetworkState &state,
                               const NetworkNotifyFn &notify);

bool network_tick(NetworkState &state,
                  std::chrono::steady_clock::time_point now);

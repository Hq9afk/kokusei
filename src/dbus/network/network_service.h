#pragma once

#include "network_types.h"

#include <sdbus-c++/sdbus-c++.h>

#include <chrono>
#include <functional>
#include <string>

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

#pragma once

#include "bluetooth_types.h"
#include <sdbus-c++/sdbus-c++.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using BluetoothNotifyFn =
    std::function<void(const std::string &summary, const std::string &body)>;

struct BluetoothState {
    std::unique_ptr<sdbus::IProxy> root;
    std::unique_ptr<sdbus::IProxy> adapter;
    std::string adapter_path;
    std::vector<BluetoothDeviceInfo> devices;
    bool adapter_present = false, powered = false, scanning = false;

    std::chrono::steady_clock::time_point next_refresh_at;
    bool init_done = false;

    std::string prev_connected_path;
};

bool bluetooth_init(BluetoothState &state, sdbus::IConnection &bus);

void bluetooth_set_powered(BluetoothState &state, bool enabled);

void bluetooth_start_discovery(BluetoothState &state);

void bluetooth_stop_discovery(BluetoothState &state);

void bluetooth_connect(BluetoothState &state, const std::string &device_path);

void bluetooth_disconnect(BluetoothState &state,
                          const std::string &device_path);

void bluetooth_pair(BluetoothState &state, const std::string &device_path);

void bluetooth_forget(BluetoothState &state, const std::string &device_path);

void bluetooth_tick(BluetoothState &state, const BluetoothNotifyFn &notify,
                    std::chrono::steady_clock::time_point now,
                    std::function<void()> on_changed);

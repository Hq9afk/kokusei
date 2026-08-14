#pragma once

#include <sdbus-c++/sdbus-c++.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rfkill_detail {

struct Entry {
    unsigned index = 0;
    std::string type;
    bool soft = false;
    bool hard = false;
};

std::optional<unsigned> read_sysfs_uint(const std::string &path);

std::optional<std::string> read_sysfs_string(const std::string &path);

} // namespace rfkill_detail

bool rfkill_bluetooth_hard_blocked();

bool rfkill_set_bluetooth_soft_blocked(bool blocked);

enum class BluetoothDeviceKind {
    Unknown,
    Headset,
    Headphones,
    Speaker,
    Mouse,
    Keyboard,
    Phone,
    Computer,
    Gamepad,
    Watch,
    Tv,
};

struct BluetoothDeviceInfo {
    std::string path, address, name;
    BluetoothDeviceKind kind = BluetoothDeviceKind::Unknown;
    bool paired = false, trusted = false, connected = false, connecting = false;
    bool has_battery = false;
    int battery_percent = 0;
};

namespace bluetooth_detail {

BluetoothDeviceKind classify_icon(const std::string &bluez_icon_name);

BluetoothDeviceKind classify_class(uint32_t class_of_device);

inline bool is_connected_bucket(const BluetoothDeviceInfo &d) {
    return d.connected;
}
inline bool is_paired_bucket(const BluetoothDeviceInfo &d) {
    return !d.connected && (d.paired || d.trusted);
}
inline bool is_nearby_bucket(const BluetoothDeviceInfo &d) {
    return !d.connected && !d.paired && !d.trusted;
}

} // namespace bluetooth_detail

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

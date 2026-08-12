#pragma once

#include <cstdint>
#include <string>

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

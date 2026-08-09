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

inline BluetoothDeviceKind classify_icon(const std::string &bluez_icon_name) {
    if (bluez_icon_name == "audio-headset")
        return BluetoothDeviceKind::Headset;
    if (bluez_icon_name == "audio-headphones")
        return BluetoothDeviceKind::Headphones;
    if (bluez_icon_name == "audio-card" || bluez_icon_name == "audio-speakers")
        return BluetoothDeviceKind::Speaker;
    if (bluez_icon_name == "input-mouse")
        return BluetoothDeviceKind::Mouse;
    if (bluez_icon_name == "input-keyboard")
        return BluetoothDeviceKind::Keyboard;
    if (bluez_icon_name == "input-gaming")
        return BluetoothDeviceKind::Gamepad;
    if (bluez_icon_name == "phone")
        return BluetoothDeviceKind::Phone;
    if (bluez_icon_name == "computer")
        return BluetoothDeviceKind::Computer;
    if (bluez_icon_name == "video-display")
        return BluetoothDeviceKind::Tv;
    return BluetoothDeviceKind::Unknown;
}

inline BluetoothDeviceKind classify_class(uint32_t class_of_device) {
    uint32_t major = (class_of_device >> 8) & 0x1F;
    uint32_t minor = (class_of_device >> 2) & 0x3F;
    switch (major) {
    case 0x01:
        return BluetoothDeviceKind::Computer;
    case 0x02:
        return BluetoothDeviceKind::Phone;
    case 0x04:
        switch (minor) {
        case 0x01:
        case 0x02:
            return BluetoothDeviceKind::Headset;
        case 0x06:
            return BluetoothDeviceKind::Headphones;
        case 0x05:
        case 0x07:
            return BluetoothDeviceKind::Speaker;
        case 0x0A:
        case 0x0B:
            return BluetoothDeviceKind::Tv;
        default:
            return BluetoothDeviceKind::Headphones;
        }
    case 0x05:
        switch (minor & 0x0F) {
        case 0x01:
            return BluetoothDeviceKind::Keyboard;
        case 0x02:
            return BluetoothDeviceKind::Mouse;
        default:
            return BluetoothDeviceKind::Unknown;
        }
    case 0x07:
        return BluetoothDeviceKind::Watch;
    case 0x08:
        return BluetoothDeviceKind::Gamepad;
    default:
        return BluetoothDeviceKind::Unknown;
    }
}

inline bool is_connected_bucket(const BluetoothDeviceInfo &d) {
    return d.connected;
}
inline bool is_paired_bucket(const BluetoothDeviceInfo &d) {
    return !d.connected && (d.paired || d.trusted);
}
inline bool is_nearby_bucket(const BluetoothDeviceInfo &d) {
    return !d.connected && !d.paired && !d.trusted;
}

}

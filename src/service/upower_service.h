#pragma once

#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <vector>

struct UpowerDeviceEntry {
    std::string path;
    std::unique_ptr<sdbus::IProxy> proxy;
    uint32_t type = 0;
    bool present = false;
    uint32_t state = 0;
    int percent = 0;
    int time_to_empty_s = 0;
    int time_to_full_s = 0;
    std::string native_path;
};

struct UpowerState {
    std::unique_ptr<sdbus::IConnection> bus;
    std::unique_ptr<sdbus::IProxy> device;
    bool present = false;
    bool charging = false;
    bool full = false;
    int percent = 0;

    std::unique_ptr<sdbus::IProxy> manager;
    std::vector<std::unique_ptr<UpowerDeviceEntry>> devices;
    bool on_battery = false;

    bool dirty = false;
};

bool upower_init(UpowerState &state);

#pragma once

#include <memory>
#include <sdbus-c++/sdbus-c++.h>

struct UpowerState {
    std::unique_ptr<sdbus::IConnection> bus;
    std::unique_ptr<sdbus::IProxy> device;
    bool present = false;
    bool charging = false;
    bool full = false;
    int percent = 0;

    bool dirty = false;
};

bool upower_init(UpowerState &state);

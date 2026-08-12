#pragma once

#include <cstdint>
#include <string>
#include <wayland-client.h>

struct Output {
    uint32_t registry_name = 0;
    wl_output *wl = nullptr;
    std::string name;
    int32_t scale = 1;
    bool done = false;
};

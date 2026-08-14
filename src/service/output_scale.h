#pragma once

#include <cstdint>
#include <functional>
#include <wayland-client.h>

struct OutputScale {
    int32_t scale = 1;
    std::function<void(int32_t)> on_change;
};

void output_scale_watch(OutputScale &state, wl_surface *surface);

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <wayland-client.h>

struct Output {
    uint32_t registry_name = 0;
    wl_output *wl = nullptr;
    std::string name;
    int32_t scale = 1;
    bool done = false;
};

// Focused-name match, then pointer_hint, then outputs.front(). Returns
// nullptr if outputs is empty.
wl_output *active_output_select(const std::vector<Output *> &outputs,
                                const std::string &focused_name,
                                wl_output *pointer_hint);

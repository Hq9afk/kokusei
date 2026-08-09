#pragma once

#include <cstdint>
#include <functional>
#include <wayland-client.h>

struct OutputScale {
    int32_t scale = 1;
    std::function<void(int32_t)> on_change;
};

namespace output_scale_detail {

inline void preferred_buffer_scale(void *data, wl_surface *surface,
                                   int32_t scale) {
    auto *state = static_cast<OutputScale *>(data);
    if (scale <= 0 || scale == state->scale)
        return;
    state->scale = scale;
    wl_surface_set_buffer_scale(surface, scale);
    if (state->on_change)
        state->on_change(scale);
}

inline const wl_surface_listener &listener() {
    static constexpr wl_surface_listener l{
        .enter = [](void *, wl_surface *, wl_output *) {},
        .leave = [](void *, wl_surface *, wl_output *) {},
        .preferred_buffer_scale = preferred_buffer_scale,
        .preferred_buffer_transform = [](void *, wl_surface *, uint32_t) {},
    };
    return l;
}

}

inline void output_scale_watch(OutputScale &state, wl_surface *surface) {
    wl_surface_add_listener(surface, &output_scale_detail::listener(), &state);
}

#include "wayland/output_scale.h"

namespace {

void preferred_buffer_scale(void *data, wl_surface *surface, int32_t scale) {
    auto *state = static_cast<OutputScale *>(data);
    if (scale <= 0 || scale == state->scale)
        return;
    state->scale = scale;
    wl_surface_set_buffer_scale(surface, scale);
    if (state->on_change)
        state->on_change(scale);
}

const wl_surface_listener &listener() {
    static constexpr wl_surface_listener l{
        .enter = [](void *, wl_surface *, wl_output *) {},
        .leave = [](void *, wl_surface *, wl_output *) {},
        .preferred_buffer_scale = preferred_buffer_scale,
        .preferred_buffer_transform = [](void *, wl_surface *, uint32_t) {},
    };
    return l;
}

} // namespace

void output_scale_watch(OutputScale &state, wl_surface *surface) {
    wl_surface_add_listener(surface, &listener(), &state);
}

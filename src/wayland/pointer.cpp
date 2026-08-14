#include "wayland/pointer.h"

namespace {

void enter_cb(void *data, wl_pointer *, uint32_t, wl_surface *surface,
             wl_fixed_t sx, wl_fixed_t sy) {
    auto *state = static_cast<PointerState *>(data);
    state->focused_surface = surface;
    state->x = wl_fixed_to_double(sx);
    state->y = wl_fixed_to_double(sy);
    state->dirty = true;
}

void leave_cb(void *data, wl_pointer *, uint32_t, wl_surface *surface) {
    auto *state = static_cast<PointerState *>(data);
    if (state->focused_surface == surface) {
        state->focused_surface = nullptr;
        state->dirty = true;
    }
}

void motion_cb(void *data, wl_pointer *, uint32_t, wl_fixed_t sx,
              wl_fixed_t sy) {
    auto *state = static_cast<PointerState *>(data);
    state->x = wl_fixed_to_double(sx);
    state->y = wl_fixed_to_double(sy);
    state->dirty = true;
}

void button_cb(void *data, wl_pointer *, uint32_t, uint32_t, uint32_t button,
               uint32_t button_state) {
    auto *state = static_cast<PointerState *>(data);
    if (button != BTN_LEFT && button != BTN_RIGHT)
        return;
    state->pending_clicks.push_back(
        {state->focused_surface,
         button_state == WL_POINTER_BUTTON_STATE_PRESSED, button, state->x,
         state->y});
}
void axis_cb(void *data, wl_pointer *, uint32_t, uint32_t axis,
            wl_fixed_t value) {
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
        return;
    auto *state = static_cast<PointerState *>(data);
    state->pending_scrolls.push_back(
        {state->focused_surface, wl_fixed_to_double(value)});
}
void frame_cb(void *, wl_pointer *) {}
void axis_source_cb(void *, wl_pointer *, uint32_t) {}
void axis_stop_cb(void *, wl_pointer *, uint32_t, uint32_t) {}
void axis_discrete_cb(void *, wl_pointer *, uint32_t, int32_t) {}
void axis_value120_cb(void *, wl_pointer *, uint32_t, int32_t) {}
void axis_relative_direction_cb(void *, wl_pointer *, uint32_t, uint32_t) {}

constexpr wl_pointer_listener kPointerListener = {
    .enter = enter_cb,
    .leave = leave_cb,
    .motion = motion_cb,
    .button = button_cb,
    .axis = axis_cb,
    .frame = frame_cb,
    .axis_source = axis_source_cb,
    .axis_stop = axis_stop_cb,
    .axis_discrete = axis_discrete_cb,
    .axis_value120 = axis_value120_cb,
    .axis_relative_direction = axis_relative_direction_cb,
};

} // namespace

void pointer_bind(PointerState &state, wl_seat *seat) {
    state.pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(state.pointer, &kPointerListener, &state);
}

void pointer_release(PointerState &state) {
    if (state.pointer) {
        wl_pointer_release(state.pointer);
        state.pointer = nullptr;
    }
    state.focused_surface = nullptr;
}

std::vector<PointerClick> pointer_drain_clicks(PointerState &state) {
    std::vector<PointerClick> clicks = std::move(state.pending_clicks);
    state.pending_clicks.clear();
    return clicks;
}

std::vector<PointerScroll> pointer_drain_scrolls(PointerState &state) {
    std::vector<PointerScroll> scrolls = std::move(state.pending_scrolls);
    state.pending_scrolls.clear();
    return scrolls;
}

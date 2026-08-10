#pragma once

#include <linux/input-event-codes.h>
#include <wayland-client.h>

#include <vector>

struct PointerClick {
    wl_surface *surface;
    bool pressed;
    uint32_t button = BTN_LEFT;
};

struct PointerScroll {
    wl_surface *surface;
    double dy;
};

struct PointerState {
    wl_pointer *pointer = nullptr;
    wl_surface *focused_surface = nullptr;
    double x = -1, y = -1;
    bool dirty = false;
    std::vector<PointerClick> pending_clicks;
    std::vector<PointerScroll> pending_scrolls;
};

namespace pointer_detail {

inline void enter_cb(void *data, wl_pointer *, uint32_t, wl_surface *surface,
                     wl_fixed_t sx, wl_fixed_t sy) {
    auto *state = static_cast<PointerState *>(data);
    state->focused_surface = surface;
    state->x = wl_fixed_to_double(sx);
    state->y = wl_fixed_to_double(sy);
    state->dirty = true;
}

inline void leave_cb(void *data, wl_pointer *, uint32_t, wl_surface *surface) {
    auto *state = static_cast<PointerState *>(data);
    if (state->focused_surface == surface) {
        state->focused_surface = nullptr;
        state->dirty = true;
    }
}

inline void motion_cb(void *data, wl_pointer *, uint32_t, wl_fixed_t sx,
                      wl_fixed_t sy) {
    auto *state = static_cast<PointerState *>(data);
    state->x = wl_fixed_to_double(sx);
    state->y = wl_fixed_to_double(sy);
    state->dirty = true;
}

inline void button_cb(void *data, wl_pointer *, uint32_t, uint32_t,
                      uint32_t button, uint32_t button_state) {
    auto *state = static_cast<PointerState *>(data);
    if (button != BTN_LEFT && button != BTN_RIGHT)
        return;
    state->pending_clicks.push_back(
        {state->focused_surface,
         button_state == WL_POINTER_BUTTON_STATE_PRESSED, button});
}
inline void axis_cb(void *data, wl_pointer *, uint32_t, uint32_t axis,
                    wl_fixed_t value) {
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
        return;
    auto *state = static_cast<PointerState *>(data);
    state->pending_scrolls.push_back(
        {state->focused_surface, wl_fixed_to_double(value)});
}
inline void frame_cb(void *, wl_pointer *) {}
inline void axis_source_cb(void *, wl_pointer *, uint32_t) {}
inline void axis_stop_cb(void *, wl_pointer *, uint32_t, uint32_t) {}
inline void axis_discrete_cb(void *, wl_pointer *, uint32_t, int32_t) {}
inline void axis_value120_cb(void *, wl_pointer *, uint32_t, int32_t) {}
inline void axis_relative_direction_cb(void *, wl_pointer *, uint32_t,
                                       uint32_t) {}

inline constexpr wl_pointer_listener kPointerListener = {
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

}

inline void pointer_bind(PointerState &state, wl_seat *seat) {
    state.pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(state.pointer, &pointer_detail::kPointerListener,
                            &state);
}

inline void pointer_release(PointerState &state) {
    if (state.pointer) {
        wl_pointer_release(state.pointer);
        state.pointer = nullptr;
    }
    state.focused_surface = nullptr;
}

inline std::vector<PointerClick> pointer_drain_clicks(PointerState &state) {
    std::vector<PointerClick> clicks = std::move(state.pending_clicks);
    state.pending_clicks.clear();
    return clicks;
}

inline std::vector<PointerScroll> pointer_drain_scrolls(PointerState &state) {
    std::vector<PointerScroll> scrolls = std::move(state.pending_scrolls);
    state.pending_scrolls.clear();
    return scrolls;
}

#pragma once

#include <functional>
#include <wayland-client.h>

struct FrameClock {
    wl_surface *surface = nullptr;
    wl_callback *callback = nullptr;
    bool redraw_requested = false;
    std::function<void()> draw;
};

inline void request_frame(FrameClock &clock);

namespace frame_clock_detail {

inline void frame_done(void *data, wl_callback *cb, uint32_t) {
    auto *clock = static_cast<FrameClock *>(data);
    wl_callback_destroy(cb);
    clock->callback = nullptr;
    if (clock->redraw_requested) {
        clock->redraw_requested = false;
        request_frame(*clock);
    }
}

inline const wl_callback_listener &listener() {
    static constexpr wl_callback_listener l{.done = frame_done};
    return l;
}

}

inline void request_frame(FrameClock &clock) {
    if (clock.callback) {
        clock.redraw_requested = true;
        return;
    }
    clock.callback = wl_surface_frame(clock.surface);
    wl_callback_add_listener(clock.callback, &frame_clock_detail::listener(),
                             &clock);
    clock.draw();
}


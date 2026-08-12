#include "frame_clock.h"

namespace {

void frame_done(void *data, wl_callback *cb, uint32_t) {
    auto *clock = static_cast<FrameClock *>(data);
    wl_callback_destroy(cb);
    clock->callback = nullptr;
    if (clock->redraw_requested) {
        clock->redraw_requested = false;
        request_frame(*clock);
    }
}

const wl_callback_listener &listener() {
    static constexpr wl_callback_listener l{.done = frame_done};
    return l;
}

} // namespace

void request_frame(FrameClock &clock) {
    if (clock.callback) {
        clock.redraw_requested = true;
        return;
    }
    clock.callback = wl_surface_frame(clock.surface);
    wl_callback_add_listener(clock.callback, &listener(), &clock);
    clock.draw();
}

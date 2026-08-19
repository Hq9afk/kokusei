#pragma once

#include <functional>
#include <utility>
#include <vector>
#include <wayland-client.h>

#include "app/ipc.h"

#include "service/keyboard.h"

struct WaylandState;

class Module {
  public:
    virtual ~Module() = default;

    virtual const char *name() const = 0;
    virtual bool is_open() const = 0;
    virtual bool create_surface(WaylandState &app, wl_output *output) = 0;
    virtual bool init_egl(WaylandState &app) = 0;
    virtual bool configured() const = 0;
    virtual wl_surface *surface() const = 0;
    virtual void request_frame() = 0;

    virtual bool tick() { return false; }
    virtual int poll_timeout_ms() const { return -1; }
    virtual bool timer_tick(WaylandState &) { return false; }
    virtual void handle_pointer_move(WaylandState &, wl_surface *, double,
                                     double) {}
    virtual void handle_pointer_release() {}
    virtual bool opened_by_widget() const { return false; }
    virtual void toggle_from_widget(WaylandState &) {}

    virtual void handle_click(WaylandState &, double, double) {}
    virtual void handle_key_event(WaylandState &, const KeyEvent &) {}
    virtual void handle_scroll(WaylandState &, double) {}
    virtual std::vector<IpcHandler> ipc_handlers(WaylandState &) { return {}; }

    virtual std::vector<std::pair<int, std::function<void()>>>
    extra_poll_sources(WaylandState &) {
        return {};
    }
};

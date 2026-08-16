#pragma once

#include <wayland-client.h>

#include <functional>
#include <utility>
#include <vector>

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
    virtual bool timer_tick(WaylandState &app) { return false; }
    virtual void handle_pointer_move(WaylandState &app,
                                     wl_surface *focused_surface, double x,
                                     double y) {}
    virtual void handle_pointer_release() {}
    virtual bool opened_by_widget() const { return false; }
    virtual void toggle_from_widget(WaylandState &app) {}

    virtual void handle_click(WaylandState &app, double x, double y) {}
    virtual void handle_key_event(WaylandState &app, const KeyEvent &event) {}
    virtual void handle_scroll(WaylandState &app, double dy) {}
    virtual std::vector<IpcHandler> ipc_handlers(WaylandState &app) {
        return {};
    }

    virtual std::vector<std::pair<int, std::function<void()>>>
    extra_poll_sources(WaylandState &app) {
        return {};
    }
};

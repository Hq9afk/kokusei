#pragma once

#include <vector>
#include <wayland-client.h>

#include "app/ipc.h"

struct WaylandState;
struct MonitorOutput;
struct KeyEvent;

class PerMonitorModule {
  public:
    virtual ~PerMonitorModule() = default;

    virtual bool create_surface(WaylandState &app, MonitorOutput &mon,
                                wl_output *output) = 0;
    virtual bool configured() const = 0;
    virtual bool init_egl(WaylandState &app, MonitorOutput &mon) = 0;
    virtual void destroy(WaylandState &app, MonitorOutput &mon) = 0;
    virtual bool owns_surface(wl_surface *surface) const = 0;

    virtual void request_frame() {}
    virtual void tick(WaylandState &app, MonitorOutput &mon) {}
    virtual void timer_tick(WaylandState &app, MonitorOutput &mon) {}
    virtual bool is_open() const { return false; }
    virtual std::vector<IpcHandler> ipc_handlers(WaylandState &app) {
        return {};
    }
    virtual void handle_click(WaylandState &app, MonitorOutput &mon,
                              wl_surface *surface, int button, double x,
                              double y) {}
    virtual void handle_scroll(WaylandState &app, MonitorOutput &mon,
                               wl_surface *surface, double dy) {}
    virtual void handle_key_event(WaylandState &app, MonitorOutput &mon,
                                  const KeyEvent &event) {}
    virtual void handle_pointer_move(WaylandState &app, MonitorOutput &mon,
                                     double x, double y) {}
    virtual void handle_pointer_release() {}
};

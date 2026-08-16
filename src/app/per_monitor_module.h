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
    virtual void tick(WaylandState &, MonitorOutput &) {}
    virtual void timer_tick(WaylandState &, MonitorOutput &) {}
    virtual bool is_open() const { return false; }
    virtual std::vector<IpcHandler> ipc_handlers(WaylandState &) { return {}; }
    virtual void handle_click(WaylandState &, MonitorOutput &, wl_surface *,
                              int, double, double) {}
    virtual void handle_scroll(WaylandState &, MonitorOutput &, wl_surface *,
                               double) {}
    virtual void handle_key_event(WaylandState &, MonitorOutput &,
                                  const KeyEvent &) {}
    virtual void handle_pointer_move(WaylandState &, MonitorOutput &, double,
                                     double) {}
    virtual void handle_pointer_release() {}
};

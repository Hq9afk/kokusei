#pragma once

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "app/ipc.h"
#include "app/monitor_output.h"
#include "app/wayland_state.h"
#include "bar/widget/widget_capsule.h"
#include "render/renderer.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace bar_detail {

void bar_autohide_set_surface_geometry(
    zwlr_layer_surface_v1 *layer_surface, wl_surface *surface,
    wl_egl_window *egl_window, int32_t width, int32_t height_px,
    int32_t margin_top, int32_t margin_right, int32_t margin_left,
    int32_t exclusive_zone, int32_t output_scale);

void close_other_overlays(MonitorOutput &mon, PillId keep);

struct BarGeometry {
    int32_t height;
    int32_t margin_top;
    int32_t exclusive_zone;
};

BarGeometry bar_autohide_geometry(bool autohide, bool collapsed,
                                  int32_t cfg_height);

int32_t bar_current_height(const MonitorOutput &mon);

void bar_autohide_apply_geometry(MonitorOutput &mon, bool autohide,
                                 bool collapsed);

void monitor_autohide_apply(MonitorOutput &mon, bool enabled);

void rest_egl_current(WaylandState &app);

void network_panel_dispatch(WaylandState &app, bool changed);
void bluetooth_panel_dispatch(WaylandState &app);
void volume_panel_dispatch(WaylandState &app);

bool volume_pill_peek_expire(MonitorOutput &mon);
void volume_pill_peek_tick(MonitorOutput &mon);
void volume_pill_handle_wheel(MonitorOutput &mon, double dy);

}

std::vector<IpcHandler> bar_ipc_handlers(WaylandState &state);

void bar_paint(MonitorOutput &mon);
void bar_request_frame(MonitorOutput &mon);
extern const wl_registry_listener registry_listener;
extern const zwlr_layer_surface_v1_listener bar_layer_surface_listener;
bool bootstrap_egl(WaylandState &state);
bool bar_init_egl(MonitorOutput &mon, Renderer &renderer, EGLDisplay display,
                  EGLConfig config, EGLContext context);
void dispatch_pill_click(MonitorOutput &mon, double click_x, double click_y);
void update_clock(MonitorOutput &mon);
void init_stub_widgets(MonitorOutput &mon);

#pragma once

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "app/monitor_output.h"
#include "app/wayland_state.h"
#include "bar/panel/bluetooth_panel.h"
#include "bar/panel/network_panel.h"
#include "bar/panel/tray_panel.h"
#include "bar/panel/volume_panel.h"
#include "bar/widget/clock_widget.h"
#include "bar/widget/widget_capsule.h"
#include "bar/widget/workspace_widget.h"
#include "render/animation.h"
#include "render/renderer.h"
#include "render/scene.h"
#include "render/texture.h"
#include "service/active_output.h"
#include "service/bluetooth_service.h"
#include "service/frame_clock.h"
#include "service/hyprland.h"
#include "service/keyboard.h"
#include "service/mpris_service.h"
#include "service/network_service.h"
#include "service/output_scale.h"
#include "service/pipewire.h"
#include "service/pointer.h"
#include "service/shojiwm.h"
#include "service/system_telemetry.h"
#include "service/upower_service.h"
#include "service/workspace.h"

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

void bar_autohide_set_enabled(WaylandState &app, bool enabled);

} // namespace bar_detail

void bar_paint(MonitorOutput &mon);
void bar_request_frame(MonitorOutput &mon);
extern const wl_registry_listener registry_listener;
extern const zwlr_layer_surface_v1_listener bar_layer_surface_listener;
bool bootstrap_egl(WaylandState &state);
bool bar_init_egl(MonitorOutput &mon, Renderer &renderer, EGLDisplay display,
                  EGLConfig config, EGLContext context);
void dispatch_pill_click(MonitorOutput &mon, double click_x, double click_y);
void update_clock(MonitorOutput &mon);

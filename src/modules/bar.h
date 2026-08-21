#pragma once

#include <EGL/egl.h>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "app/ipc.h"
#include "app/monitor_output.h"
#include "app/per_monitor_module.h"
#include "app/wayland_state.h"

#include "bar/panel/battery_panel.h"
#include "bar/panel/bluetooth_panel.h"
#include "bar/panel/network_panel.h"
#include "bar/panel/system_monitor_panel.h"
#include "bar/panel/tray_panel.h"
#include "bar/panel/volume_panel.h"
#include "bar/widget/widget_capsule.h"
#include "bar/widget/workspace_widget.h"

#include "render/renderer.h"
#include "render/texture.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct BarPerMonitorState {
    WidgetCapsuleState capsule;
    WorkspaceWidgetState workspace_widget;
    NetworkPanelState network_panel;
    BluetoothPanelState bluetooth_panel;
    VolumePanelState volume_panel;
    TrayPanelState tray_panel;
    TrayMenuState tray_menu;
    BatteryPanelState battery_panel;
    SystemMonitorPanelState system_monitor_panel;

    Texture clock_texture;
    Texture starward_texture;
    Texture dock_texture;
    Texture tray_texture;
    Texture cpu_texture;
    Texture dashboard_texture;
    Texture battery_icon_texture;
    const char *battery_icon_glyph = nullptr;
    Texture wifi_icon_texture;
    const char *wifi_icon_glyph_cached = nullptr;
    Texture bluetooth_icon_texture;
    const char *bluetooth_icon_glyph_cached = nullptr;
    Texture volume_icon_texture;
    const char *volume_icon_glyph_cached = nullptr;

    bool volume_peek_active = false;
    bool volume_peek_ready = false;
    std::chrono::steady_clock::time_point volume_peek_started_at =
        std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point volume_peek_deadline{};
    float volume_peek_last_level = -1.0f;
    bool volume_peek_last_muted = false;
};

class BarPerMonitorModule final : public PerMonitorModule {
  public:
    BarPerMonitorState state;

    bool create_surface(WaylandState &app, MonitorOutput &mon,
                        wl_output *output) override;
    bool configured() const override;
    bool init_egl(WaylandState &app, MonitorOutput &mon) override;
    void destroy(WaylandState &app, MonitorOutput &mon) override;
    bool owns_surface(wl_surface *surface) const override;
    void request_frame() override;
    void tick(WaylandState &app, MonitorOutput &mon) override;
    void timer_tick(WaylandState &app, MonitorOutput &mon) override;
    bool is_open() const override;
    void handle_click(WaylandState &app, MonitorOutput &mon,
                      wl_surface *surface, int button, double x,
                      double y) override;
    void handle_scroll(WaylandState &app, MonitorOutput &mon,
                       wl_surface *surface, double dy) override;
    void handle_key_event(WaylandState &app, MonitorOutput &mon,
                          const KeyEvent &event) override;
    void handle_pointer_move(WaylandState &app, MonitorOutput &mon, double x,
                             double y) override;
    void handle_pointer_release() override;
    std::vector<IpcHandler> ipc_handlers(WaylandState &app) override;

  private:
    MonitorOutput *mon_ = nullptr;
    int poll_tick_ = 0;
};

BarPerMonitorState &bar_state(MonitorOutput &mon);

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

bool volume_pill_peek_expire(MonitorOutput &mon);
void volume_pill_peek_tick(MonitorOutput &mon);
void volume_pill_handle_wheel(MonitorOutput &mon, double dy);

} // namespace bar_detail

void bar_paint(MonitorOutput &mon);
void bar_request_frame(MonitorOutput &mon);
bool bar_init_egl(MonitorOutput &mon, Renderer &renderer, EGLDisplay display,
                  EGLConfig config, EGLContext context);
void dispatch_pill_click(MonitorOutput &mon, double click_x, double click_y);
void update_clock(MonitorOutput &mon);
void init_stub_widgets(MonitorOutput &mon);

#pragma once

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "../app/config.h"
#include "../app/wayland_state.h"
#include "../controlcenter/controlcenter.h"
#include "../dbus/bluetooth/bluetooth_service.h"
#include "../dbus/mpris/mpris_service.h"
#include "../dbus/network/network_service.h"
#include "../dbus/upower.h"
#include "../idle/idle.h"
#include "../launcher/launcher.h"
#include "../notification/notification.h"
#include "../osd/osd.h"
#include "../render/animation.h"
#include "../render/renderer.h"
#include "../render/scene.h"
#include "../render/texture.h"
#include "../settings/settings.h"
#include "../starward/starward.h"
#include "../system/cpu_temp.h"
#include "../system/gpu_temp.h"
#include "../system/pipewire.h"
#include "../system/system_stats.h"
#include "../wallpaper/wallpaper.h"
#include "../wayland/frame_clock.h"
#include "../wayland/hyprland.h"
#include "../wayland/keyboard.h"
#include "../wayland/output.h"
#include "../wayland/output_scale.h"
#include "../wayland/pointer.h"
#include "../wayland/shojiwm.h"
#include "../wayland/workspace.h"
#include "panel/bluetooth_panel.h"
#include "panel/network_panel.h"
#include "panel/tray_panel.h"
#include "panel/volume_panel.h"
#include "widget/clock_widget.h"
#include "widget/widget_capsule.h"
#include "widget/workspace_widget.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

struct AutoHideState {
    bool hidden = false;
    bool collapsed = false;
    float opacity = 1.0f;

    bool enabled = false;
};

struct MonitorOutput {
    WaylandState *app = nullptr;
    Output output;
    bool activated = false;
    wl_surface *surface = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    int32_t width = 0;
    bool configured = false;
    OutputScale output_scale;
    FrameClock frame_clock;
    Scene scene;
    Texture clock_texture;
    Texture starward_texture;
    Texture dock_texture;
    Texture tray_texture;
    Texture cpu_texture;
    Texture control_center_texture;
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
    WidgetCapsuleState capsule;
    WorkspaceWidgetState workspace_widget;
    AutoHideState autohide;
    AnimationManager animations;
    NetworkPanelState network_panel;
    BluetoothPanelState bluetooth_panel;
    VolumePanelState volume_panel;
    TrayPanelState tray_panel;
    TrayMenuState tray_menu;
    WallpaperState wallpaper;
    OsdState osd;
    NotificationView notification_view;
};

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

void wallpaper_load_all_columns(MonitorOutput &mon, const Config &cfg);

void wallpaper_diff_columns(MonitorOutput &mon, const Config &old_cfg,
                            const Config &new_cfg);

const std::vector<Workspace> &monitor_workspaces(const MonitorOutput &mon);

int monitor_active_workspace_id(const MonitorOutput &mon);

} // namespace bar_detail

void bar_paint(MonitorOutput &mon);
void bar_request_frame(MonitorOutput &mon);
extern const wl_registry_listener registry_listener;
bool bootstrap_egl(WaylandState &state);
bool bar_init_egl(MonitorOutput &mon, Renderer &renderer, EGLDisplay display,
                  EGLConfig config, EGLContext context);
void monitor_output_create_surfaces(WaylandState &app, MonitorOutput &mon);
void monitor_output_wait_configured(WaylandState &app, MonitorOutput &mon);
void monitor_output_finish_egl(WaylandState &app, MonitorOutput &mon);
void monitor_output_activate(WaylandState &app, MonitorOutput &mon);
void dispatch_pill_click(MonitorOutput &mon, double click_x, double click_y);
void update_clock(MonitorOutput &mon);

namespace bar_detail {
void apply_config_update(WaylandState &app, Config new_cfg);
void save_and_apply_config_update(WaylandState &app, Config new_cfg);
MonitorOutput *active_target_monitor(WaylandState &app);
void settings_retarget(WaylandState &app, MonitorOutput &target);
} // namespace bar_detail

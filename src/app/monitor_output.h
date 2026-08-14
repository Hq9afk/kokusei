#pragma once

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "app/wayland_state.h"
#include "bar/panel/bluetooth_panel.h"
#include "bar/panel/network_panel.h"
#include "bar/panel/tray_panel.h"
#include "bar/panel/volume_panel.h"
#include "bar/widget/widget_capsule.h"
#include "bar/widget/workspace_widget.h"
#include "notification/notification.h"
#include "osd/osd.h"
#include "render/animation.h"
#include "render/renderer.h"
#include "render/scene.h"
#include "render/texture.h"
#include "service/active_output.h"
#include "service/frame_clock.h"
#include "service/output_scale.h"
#include "service/workspace.h"
#include "wallpaper/wallpaper.h"

#include <chrono>
#include <cstdint>
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

void monitor_output_destroy(MonitorOutput &mon);
void monitor_output_create_surfaces(WaylandState &app, MonitorOutput &mon);
void monitor_output_wait_configured(WaylandState &app, MonitorOutput &mon);
void monitor_output_finish_egl(WaylandState &app, MonitorOutput &mon);
void monitor_output_activate(WaylandState &app, MonitorOutput &mon);

MonitorOutput *find_monitor_by_name_wl(WaylandState &app, wl_output *wl);

namespace bar_detail {

const std::vector<Workspace> &monitor_workspaces(const MonitorOutput &mon);

int monitor_active_workspace_id(const MonitorOutput &mon);

void apply_config_update(WaylandState &app, Config new_cfg);
void save_and_apply_config_update(WaylandState &app, Config new_cfg);
MonitorOutput *active_target_monitor(WaylandState &app);
void settings_retarget(WaylandState &app, MonitorOutput &target);

} // namespace bar_detail

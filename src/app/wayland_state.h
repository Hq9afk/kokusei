#pragma once

#include <EGL/egl.h>
#include <cstring>
#include <memory>
#include <vector>
#include <wayland-client.h>

#include "app/config.h"
#include "app/module.h"
#include "app/service.h"

#include "modules/idle.h"
#include "modules/notification.h"
#include "modules/osd.h"

#include "render/renderer.h"

#include "service/bluetooth_service.h"
#include "service/hyprland.h"
#include "service/keyboard.h"
#include "service/mpris_service.h"
#include "service/network_service.h"
#include "service/pipewire.h"
#include "service/pointer.h"
#include "service/shojiwm.h"
#include "service/system_telemetry.h"
#include "service/tray_service.h"
#include "service/upower_service.h"

#include "hyprland-toplevel-export-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct MonitorOutput;

struct WaylandState {
    wl_display *display = nullptr;
    wl_compositor *compositor = nullptr;
    zwlr_layer_shell_v1 *layer_shell = nullptr;
    xdg_wm_base *wm_base = nullptr;
    wl_shm *shm = nullptr;
    hyprland_toplevel_export_manager_v1 *toplevel_export_manager = nullptr;
    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLConfig egl_config = nullptr;
    EGLContext egl_context = EGL_NO_CONTEXT;
    Config cfg;
    bool running = true;
    Renderer renderer;
    IdleState idle;
    NotificationService notification;
    std::vector<std::unique_ptr<Module>> overlays;
    std::vector<std::unique_ptr<Service>> services;
    UpowerState upower;
    NetworkState network;
    BluetoothState bluetooth;
    TrayState tray;
    KeyboardState keyboard;
    PointerState pointer;
    SeatCapabilityState seat_caps;
    BrightnessBackend brightness;
    PipewireState pipewire;
    CpuTempState cpu_temp;
    GpuTempState gpu_temp;
    SystemStatsState system_stats;
    MprisState mpris;
    int brightness_watch_fd = -1;
    int config_watch_fd = -1;
    bool config_own_write_pending = false;
    MonitorOutput *last_pointer_monitor = nullptr;
    bool settings_enabled = false;
    wl_output *settings_bound_output = nullptr;
    enum class CompositorBackend { None, Hyprland, ShojiWM };
    CompositorBackend compositor_backend = CompositorBackend::None;
    HyprlandState hypr;
    ShojiwmState shoji;
    std::vector<std::unique_ptr<MonitorOutput>> outputs;
};

inline Module *find_overlay_by_name(WaylandState &app, const char *name) {
    for (auto &m : app.overlays)
        if (strcmp(m->name(), name) == 0)
            return m.get();
    return nullptr;
}

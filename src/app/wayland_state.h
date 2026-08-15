#pragma once

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include <EGL/egl.h>
#include <wayland-client.h>

#include "app/config.h"
#include "modules/controlcenter.h"
#include "modules/idle.h"
#include "modules/launcher.h"
#include "modules/matrix.h"
#include "modules/notification.h"
#include "modules/osd.h"
#include "modules/settings.h"
#include "modules/starward.h"
#include "modules/visualizer.h"
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

#include <memory>
#include <vector>

struct MonitorOutput;

struct WaylandState {
    wl_display *display = nullptr;
    wl_compositor *compositor = nullptr;
    zwlr_layer_shell_v1 *layer_shell = nullptr;
    xdg_wm_base *wm_base = nullptr;
    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLConfig egl_config = nullptr;
    EGLContext egl_context = EGL_NO_CONTEXT;
    Config cfg;
    bool running = true;
    Renderer renderer;
    IdleState idle;
    NotificationService notification;
    LauncherState launcher;
    StarwardState starward;
    ControlCenterState controlcenter;
    MatrixState matrix;
    VisualizerState visualizer;
    SettingsState settings;
    NetworkState network;
    BluetoothState bluetooth;
    TrayState tray;
    KeyboardState keyboard;
    PointerState pointer;
    SeatCapabilityState seat_caps;
    BrightnessBackend brightness;
    PipewireState pipewire;
    UpowerState upower;
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

#pragma once

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <EGL/egl.h>
#include <wayland-client.h>

#include "../controlcenter/controlcenter.h"
#include "../dbus/bluetooth/bluetooth_service.h"
#include "../dbus/mpris/mpris_service.h"
#include "../dbus/network/network_service.h"
#include "../dbus/tray/tray_service.h"
#include "../dbus/upower.h"
#include "../idle/idle.h"
#include "../launcher/launcher.h"
#include "../notification/notification.h"
#include "../osd/osd.h"
#include "../render/renderer.h"
#include "../settings/settings.h"
#include "../starward/starward.h"
#include "../system/cpu_temp.h"
#include "../system/gpu_temp.h"
#include "../system/pipewire.h"
#include "../system/system_stats.h"
#include "../wayland/hyprland.h"
#include "../wayland/keyboard.h"
#include "../wayland/pointer.h"
#include "../wayland/shojiwm.h"
#include "config.h"

#include <memory>
#include <vector>

struct MonitorOutput;

struct WaylandState {
    wl_display *display = nullptr;
    wl_compositor *compositor = nullptr;
    zwlr_layer_shell_v1 *layer_shell = nullptr;
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

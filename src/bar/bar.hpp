#pragma once

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "../app/config.hpp"
#include "../core/log.hpp"
#include "../dbus/bluetooth/bluetooth_service.hpp"
#include "../dbus/network/network_service.hpp"
#include "../dbus/upower.hpp"
#include "../idle/idle.hpp"
#include "../launcher/launcher_draw.hpp"
#include "../logout/logout_draw.hpp"
#include "../notification/notification_draw.hpp"
#include "../osd/osd.hpp"
#include "../render/animation/animation.hpp"
#include "../render/icon.hpp"
#include "../render/icons.hpp"
#include "../render/node.hpp"
#include "../render/palette.hpp"
#include "../render/rect.hpp"
#include "../render/renderer.hpp"
#include "../render/scene.hpp"
#include "../render/text.hpp"
#include "../render/texture.hpp"
#include "../system/pipewire.hpp"
#include "../wallpaper/wallpaper.hpp"
#include "../wayland/frame_clock.hpp"
#include "../wayland/hyprland.hpp"
#include "../wayland/keyboard.hpp"
#include "../wayland/output_scale.hpp"
#include "../wayland/pointer.hpp"
#include "../wayland/shojiwm.hpp"
#include "../wayland/workspace.hpp"
#include "bar_autohide.hpp"
#include "panels/bluetooth_panel/bluetooth_panel.hpp"
#include "panels/network_panel/network_panel.hpp"
#include "panels/tray_panel/tray_panel.hpp"
#include "panels/volume_panel/volume_panel.hpp"
#include "widgets/clock_widget.hpp"
#include "widgets/widget_capsule.hpp"
#include "widgets/workspace_widget.hpp"
#include <algorithm>

#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

struct WaylandState {
    wl_display *display = nullptr;
    wl_compositor *compositor = nullptr;
    zwlr_layer_shell_v1 *layer_shell = nullptr;
    wl_surface *surface = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLConfig egl_config = nullptr;
    EGLContext egl_context = EGL_NO_CONTEXT;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    int32_t width = 0;
    Config cfg;
    bool configured = false;
    bool running = true;
    OutputScale output_scale;
    FrameClock frame_clock;
    Scene scene;

    Renderer renderer;
    Texture clock_texture;

    Texture power_texture;
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

    WallpaperState wallpaper;
    IdleState idle;
    NotificationState notification;
    OsdState osd;
    LauncherState launcher;
    LogoutState logout;
    NetworkState network;
    NetworkPanelState network_panel;
    BluetoothState bluetooth;
    BluetoothPanelState bluetooth_panel;
    VolumePanelState volume_panel;
    TrayState tray;
    TrayPanelState tray_panel;
    TrayMenuState tray_menu;
    KeyboardState keyboard;
    PointerState pointer;
    SeatCapabilityState seat_caps;
    BrightnessBackend brightness;
    PipewireState pipewire;
    UpowerState upower;
    int brightness_watch_fd = -1;
    int config_watch_fd = -1;

    enum class CompositorBackend { None, Hyprland, ShojiWM };
    CompositorBackend compositor_backend = CompositorBackend::None;
    HyprlandState hypr;
    ShojiwmState shoji;

    const std::vector<Workspace> &workspaces() const {
        static const std::vector<Workspace> empty;
        switch (compositor_backend) {
        case CompositorBackend::Hyprland:
            return hypr.workspaces;
        case CompositorBackend::ShojiWM:
            return shoji.workspaces;
        default:
            return empty;
        }
    }
    int active_workspace_id() const {
        switch (compositor_backend) {
        case CompositorBackend::Hyprland:
            return hypr.active_id;
        case CompositorBackend::ShojiWM:
            return shoji.active_id;
        default:
            return -1;
        }
    }
};

namespace bar_detail {
inline void close_other_overlays(WaylandState &state, PillId keep) {
    if (keep != PillId::Power && state.logout.base.open)
        logout_toggle(state.logout);
    if (keep != PillId::Wifi && state.network_panel.base.open)
        network_panel_toggle(state.network_panel);
    if (keep != PillId::Bluetooth && state.bluetooth_panel.base.open)
        bluetooth_panel_toggle(state.bluetooth_panel, state.bluetooth);
    if (keep != PillId::Volume && state.volume_panel.base.open)
        volume_panel_toggle(state.volume_panel);
    if (keep != PillId::Tray && state.tray_panel.base.open)
        tray_panel_toggle(state.tray_panel);
    if (keep != PillId::Tray && state.tray_menu.base.open)
        tray_menu_close(state.tray_menu);
}

struct BarGeometry {
    int32_t height;
    int32_t margin_top;
    int32_t exclusive_zone;
};

inline BarGeometry bar_autohide_geometry(bool autohide, bool collapsed,
                                         int32_t cfg_height) {
    if (!autohide)
        return {cfg_height, kBarTopMargin, cfg_height + kBarTopMargin};
    if (collapsed)
        return {kAutoHideStripPx, 0, 0};
    return {kBarTopMargin + cfg_height, 0, 0};
}

inline int32_t bar_current_height(const WaylandState &state) {
    return bar_autohide_geometry(state.cfg.autohide, state.autohide.collapsed,
                                 state.cfg.height)
        .height;
}

inline void bar_autohide_apply_geometry(WaylandState &state, bool autohide,
                                        bool collapsed) {
    BarGeometry g = bar_autohide_geometry(autohide, collapsed, state.cfg.height);
    bar_autohide_set_surface_geometry(
        state.layer_surface, state.surface, state.egl_window, state.width,
        g.height, g.margin_top, static_cast<int32_t>(kPanelSideMargin),
        static_cast<int32_t>(kPanelSideMargin), g.exclusive_zone,
        state.output_scale.scale);
}

inline void bar_autohide_set_enabled(WaylandState &state, bool enabled) {
    if (enabled == state.cfg.autohide)
        return;
    state.cfg.autohide = enabled;
    state.autohide.hidden = false;
    state.autohide.collapsed = enabled && state.autohide.collapsed;
    state.autohide.opacity = state.autohide.collapsed ? 0.0f : 1.0f;
    bar_autohide_apply_geometry(state, enabled, state.autohide.collapsed);
}
}

inline void bar_paint(WaylandState &state);

#include "widgets/battery_widget.hpp"
#include "widgets/bluetooth_widget.hpp"
#include "widgets/control_center_widget.hpp"
#include "widgets/network_widget.hpp"
#include "widgets/power_widget.hpp"
#include "widgets/volume_widget.hpp"

inline void layer_surface_configure(void *data,
                                    zwlr_layer_surface_v1 *layer_surface,
                                    uint32_t serial, uint32_t width, uint32_t) {
    auto *state = static_cast<WaylandState *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    state->width = static_cast<int32_t>(width);
    if (state->egl_window) {
        int32_t scale = state->output_scale.scale;
        wl_egl_window_resize(state->egl_window, state->width * scale,
                             bar_detail::bar_current_height(*state) * scale, 0,
                             0);
    }
    state->configured = true;
}

inline void layer_surface_closed(void *data, zwlr_layer_surface_v1 *) {
    static_cast<WaylandState *>(data)->running = false;
}

inline constexpr zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

inline void registry_global(void *data, wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version) {
    auto *state = static_cast<WaylandState *>(data);
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = static_cast<wl_compositor *>(wl_registry_bind(
            registry, name, &wl_compositor_interface, std::min(version, 6u)));
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        state->layer_shell =
            static_cast<zwlr_layer_shell_v1 *>(wl_registry_bind(
                registry, name, &zwlr_layer_shell_v1_interface, 1));
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {

        state->idle.seat = static_cast<wl_seat *>(
            wl_registry_bind(registry, name, &wl_seat_interface, 3));
        state->seat_caps.keyboard = &state->keyboard;
        state->seat_caps.pointer = &state->pointer;
        keyboard_attach_seat(state->seat_caps, state->idle.seat);
    } else if (strcmp(interface, ext_idle_notifier_v1_interface.name) == 0) {
        state->idle.notifier =
            static_cast<ext_idle_notifier_v1 *>(wl_registry_bind(
                registry, name, &ext_idle_notifier_v1_interface, 1));
    } else if (strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) ==
               0) {
        state->idle.inhibit_manager =
            static_cast<zwp_idle_inhibit_manager_v1 *>(wl_registry_bind(
                registry, name, &zwp_idle_inhibit_manager_v1_interface, 1));
    }
}

inline void registry_global_remove(void *, wl_registry *, uint32_t) {}

inline constexpr wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

inline void bar_request_frame(WaylandState &state);

inline bool init_egl(WaylandState &state) {
    state.egl_display =
        eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(state.display));
    if (state.egl_display == EGL_NO_DISPLAY)
        return false;
    if (!eglInitialize(state.egl_display, nullptr, nullptr))
        return false;
    eglBindAPI(EGL_OPENGL_ES_API);

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,
        EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,
        8,
        EGL_GREEN_SIZE,
        8,
        EGL_BLUE_SIZE,
        8,
        EGL_ALPHA_SIZE,
        8,
        EGL_NONE,
    };
    EGLint num_configs = 0;
    if (!eglChooseConfig(state.egl_display, config_attribs, &state.egl_config,
                         1, &num_configs) ||
        num_configs == 0) {
        return false;
    }

    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    state.egl_context = eglCreateContext(state.egl_display, state.egl_config,
                                         EGL_NO_CONTEXT, context_attribs);
    if (state.egl_context == EGL_NO_CONTEXT)
        return false;

    int32_t scale = state.output_scale.scale;
    state.egl_window = wl_egl_window_create(
        state.surface, state.width * scale,
        bar_detail::bar_current_height(state) * scale);
    state.egl_surface = eglCreateWindowSurface(
        state.egl_display, state.egl_config,
        reinterpret_cast<EGLNativeWindowType>(state.egl_window), nullptr);
    if (state.egl_surface == EGL_NO_SURFACE)
        return false;

    if (!eglMakeCurrent(state.egl_display, state.egl_surface, state.egl_surface,
                        state.egl_context))
        return false;

    const char *renderer =
        reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    klog("egl: renderer=%s", renderer ? renderer : "(unknown)");

    state.frame_clock.surface = state.surface;
    state.frame_clock.draw = [&state] { bar_paint(state); };
    return true;
}

inline void dispatch_pill_click(WaylandState &state) {
    PointerState p = state.pointer;
    if (state.cfg.autohide)
        p.y -= bar_detail::kBarTopMargin;
    bar_detail::dispatch_pill_click(state.capsule, p, state.surface);
}

inline void update_clock(WaylandState &state) {
    bar_detail::update_clock(state.clock_texture);
}

inline void bar_paint(WaylandState &state) {
    using namespace bar_detail;

    state.animations.tick(std::chrono::steady_clock::now());

    PillId current_panel_pill =
        panel_pill(state.network_panel, state.bluetooth_panel,
                   state.volume_panel, state.tray_panel, state.logout);

    if (state.cfg.autohide) {
        bool want_shown = state.pointer.focused_surface == state.surface ||
                          current_panel_pill != PillId::None;
        if (want_shown == state.autohide.hidden) {
            state.autohide.hidden = !want_shown;
            if (want_shown && state.autohide.collapsed) {
                state.autohide.collapsed = false;
                bar_autohide_apply_geometry(state, true, false);
            }
            float target = want_shown ? 1.0f : 0.0f;
            float duration = want_shown ? kAutoHideRevealMs : kAutoHideHideMs;
            state.animations.animate(
                state.autohide.opacity, target, duration,
                Easing::EaseOutCubic,
                [&state](float v) { state.autohide.opacity = v; },
                [&state] {
                    if (state.autohide.hidden && !state.autohide.collapsed) {
                        state.autohide.collapsed = true;
                        bar_autohide_apply_geometry(state, true, true);
                    }
                },
                kAutoHideAnimOwner);
        }
    }
    int32_t surface_height = bar_current_height(state);
    float content_y_offset =
        state.cfg.autohide ? static_cast<float>(kBarTopMargin) : 0.0f;
    float height = static_cast<float>(state.cfg.height);

    eglMakeCurrent(state.egl_display, state.egl_surface, state.egl_surface,
                   state.egl_context);
    state.renderer.begin_frame(state.width, surface_height,
                               state.output_scale.scale);
    state.renderer.set_opacity(state.cfg.autohide ? state.autohide.opacity
                                                  : 1.0f);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.scene.rebuild();
    Node *root = &state.scene.root;
    Node *content = node_add_group(root, 0.0f, content_y_offset,
                                   static_cast<float>(state.width), height);

    const float *white = rgba(palette::text);
    float pill_bg[4] = {state.cfg.bg[0], state.cfg.bg[1], state.cfg.bg[2],
                        state.cfg.bg[3]};

    if (current_panel_pill == PillId::None &&
        state.capsule.panel_pill_prev != PillId::None) {
        state.capsule.label_linger_pill = state.capsule.panel_pill_prev;
        state.capsule.label_linger_until =
            std::chrono::steady_clock::now() + kPillCloseLingerMs;
    }
    state.capsule.panel_pill_prev = current_panel_pill;
    bool lingering =
        state.capsule.label_linger_pill != PillId::None &&
        std::chrono::steady_clock::now() < state.capsule.label_linger_until;
    PointerState hit_pointer = state.pointer;
    hit_pointer.y -= content_y_offset;
    PillId hovered = current_panel_pill != PillId::None ? current_panel_pill
                     : lingering ? state.capsule.label_linger_pill
                                 : hit_test_pills(state.capsule, hit_pointer,
                                                  state.surface);
    if (hovered == PillId::None && state.volume_peek_active)
        hovered = PillId::Volume;

    float x = 0.0f;
    std::vector<Pill> power_pills = {power_pill(state)};
    x = draw_pills(content, state.capsule, state.animations, x, height,
                   power_pills, white, pill_bg, hovered, current_panel_pill);

    int active_id = state.active_workspace_id();
    const std::vector<Workspace> &ws_list = state.workspaces();
    x = draw_workspace_row(content, state.workspace_widget, state.animations,
                           x, height, ws_list, active_id, pill_bg);

    draw_static_pill_row(content, x, height, {&state.dock_texture}, white,
                         pill_bg);

    draw_clock_pill(content, height, state.width, state.clock_texture, white,
                    pill_bg);

    std::vector<Pill> control_center_pills = {control_center_pill(state)};
    std::vector<Pill> battery_pills = {battery_pill(state)};
    std::vector<Pill> right_stub_pills = {
        tray_pill(state),      cpu_pill(state),    wifi_pill(state),
        bluetooth_pill(state), volume_pill(state),
    };

    float cc_w = pills_row_width(state.capsule, state.animations,
                                 control_center_pills, hovered, height);
    float batt_w = pills_row_width(state.capsule, state.animations,
                                   battery_pills, hovered, height);
    float stub_w =
        pills_row_width(state.capsule, state.animations, right_stub_pills,
                        hovered, height, current_panel_pill);

    float cc_x = state.width - cc_w;
    float batt_x = cc_x - (batt_w > 0 ? kCapsuleGap : 0.0f) - batt_w;
    float stub_x = batt_x - (stub_w > 0 ? kCapsuleGap : 0.0f) - stub_w;

    if (stub_w > 0) {
        draw_pills(content, state.capsule, state.animations, stub_x, height,
                   right_stub_pills, white, pill_bg, hovered,
                   current_panel_pill);
    }
    if (batt_w > 0) {
        draw_pills(content, state.capsule, state.animations, batt_x, height,
                   battery_pills, white, pill_bg, hovered);
        const UpowerState &u = state.upower;
        if (u.present && !u.charging && !u.full && u.percent <= 10) {
            const Rect &r = state.capsule.pill_rects[pill_idx(PillId::Battery)];
            add_rrect_node(content, r.x, r.y, r.w, r.h, metrics::radius_md,
                           0.0f, rgba(palette::critical_alpha15),
                           rgba(palette::critical_alpha15));
        }
    }
    if (cc_w > 0) {
        draw_pills(content, state.capsule, state.animations, cc_x, height,
                   control_center_pills, white, pill_bg, hovered);
    }

    state.scene.draw(state.renderer);
    eglSwapBuffers(state.egl_display, state.egl_surface);

    if (state.animations.hasActive())
        bar_request_frame(state);
}

inline void bar_request_frame(WaylandState &state) {
    if (state.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(state.frame_clock);
}

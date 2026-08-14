#include "modules/bar.h"

#include "bar/panel/bluetooth_panel.h"
#include "bar/panel/network_panel.h"
#include "bar/panel/volume_panel.h"
#include "bar/widget/battery_widget.h"
#include "bar/widget/bluetooth_widget.h"
#include "bar/widget/clock_widget.h"
#include "bar/widget/control_center_widget.h"
#include "bar/widget/network_widget.h"
#include "bar/widget/starward_widget.h"
#include "bar/widget/volume_widget.h"
#include "core/log.h"
#include "render/palette.h"
#include "service/active_output.h"

#include <GLES2/gl2.h>
#include <algorithm>
#include <cstring>

namespace {

void layer_surface_configure(void *data, zwlr_layer_surface_v1 *layer_surface,
                             uint32_t serial, uint32_t width, uint32_t) {
    auto *mon = static_cast<MonitorOutput *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    mon->width = static_cast<int32_t>(width);
    if (mon->egl_window) {
        int32_t scale = mon->output_scale.scale;
        wl_egl_window_resize(mon->egl_window, mon->width * scale,
                             bar_detail::bar_current_height(*mon) * scale, 0,
                             0);
    }
    mon->configured = true;
}

void layer_surface_closed(void *data, zwlr_layer_surface_v1 *) {
    static_cast<MonitorOutput *>(data)->app->running = false;
}

namespace output_detail {
void geometry(void *, wl_output *, int32_t, int32_t, int32_t, int32_t, int32_t,
              const char *, const char *, int32_t) {}
void mode(void *, wl_output *, uint32_t, int32_t, int32_t, int32_t) {}
void scale_event(void *data, wl_output *, int32_t factor) {
    static_cast<MonitorOutput *>(data)->output.scale = factor;
}
void name_event(void *data, wl_output *, const char *name) {
    static_cast<MonitorOutput *>(data)->output.name = name;
}
void description(void *, wl_output *, const char *) {}
void done(void *data, wl_output *) {
    auto *mon = static_cast<MonitorOutput *>(data);
    mon->output.done = true;
    if (!mon->activated && mon->app->egl_context != EGL_NO_CONTEXT)
        monitor_output_activate(*mon->app, *mon);
}

const wl_output_listener &listener() {
    static constexpr wl_output_listener l{
        .geometry = geometry,
        .mode = mode,
        .done = done,
        .scale = scale_event,
        .name = name_event,
        .description = description,
    };
    return l;
}
} // namespace output_detail

void registry_global(void *data, wl_registry *registry, uint32_t name,
                     const char *interface, uint32_t version) {
    auto *state = static_cast<WaylandState *>(data);
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = static_cast<wl_compositor *>(wl_registry_bind(
            registry, name, &wl_compositor_interface, std::min(version, 6u)));
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        state->layer_shell =
            static_cast<zwlr_layer_shell_v1 *>(wl_registry_bind(
                registry, name, &zwlr_layer_shell_v1_interface, 1));
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        auto mon = std::make_unique<MonitorOutput>();
        mon->app = state;
        mon->output.registry_name = name;
        mon->output.wl = static_cast<wl_output *>(wl_registry_bind(
            registry, name, &wl_output_interface, std::min(version, 4u)));
        wl_output_add_listener(mon->output.wl, &output_detail::listener(),
                               mon.get());
        state->outputs.push_back(std::move(mon));
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

void registry_global_remove(void *data, wl_registry *, uint32_t name) {
    auto *state = static_cast<WaylandState *>(data);
    auto it = std::find_if(state->outputs.begin(), state->outputs.end(),
                           [name](const std::unique_ptr<MonitorOutput> &m) {
                               return m->output.registry_name == name;
                           });
    if (it == state->outputs.end())
        return;
    klog("output: '%s' removed", (*it)->output.name.c_str());
    monitor_output_destroy(**it);
    state->outputs.erase(it);
}

} // namespace

const wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

const zwlr_layer_surface_v1_listener bar_layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

namespace bar_detail {

void bar_autohide_set_surface_geometry(
    zwlr_layer_surface_v1 *layer_surface, wl_surface *surface,
    wl_egl_window *egl_window, int32_t width, int32_t height_px,
    int32_t margin_top, int32_t margin_right, int32_t margin_left,
    int32_t exclusive_zone, int32_t output_scale) {
    zwlr_layer_surface_v1_set_size(layer_surface, 0, height_px);
    zwlr_layer_surface_v1_set_margin(layer_surface, margin_top, margin_right, 0,
                                     margin_left);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, exclusive_zone);
    wl_surface_commit(surface);
    if (egl_window)
        wl_egl_window_resize(egl_window, width * output_scale,
                             height_px * output_scale, 0, 0);
}

void close_other_overlays(MonitorOutput &mon, PillId keep) {
    if (keep != PillId::Starward && mon.app->starward.base.open)
        starward_toggle(mon.app->starward);
    if (keep != PillId::ControlCenter && mon.app->controlcenter.base.open)
        controlcenter_toggle(mon.app->controlcenter);
    if (keep != PillId::Wifi && mon.network_panel.base.open)
        network_panel_toggle(mon.network_panel);
    if (keep != PillId::Bluetooth && mon.bluetooth_panel.base.open)
        bluetooth_panel_toggle(mon.bluetooth_panel, mon.app->bluetooth);
    if (keep != PillId::Volume && mon.volume_panel.base.open)
        volume_panel_toggle(mon.volume_panel);
    if (keep != PillId::Tray && mon.tray_panel.base.open)
        tray_panel_toggle(mon.tray_panel);
    if (keep != PillId::Tray && mon.tray_menu.base.open)
        tray_menu_close(mon.tray_menu);
}

BarGeometry bar_autohide_geometry(bool autohide, bool collapsed,
                                  int32_t cfg_height) {
    if (!autohide)
        return {cfg_height, kBarTopMargin, cfg_height};
    if (collapsed)
        return {kAutoHideStripPx, 0, 0};
    return {kBarTopMargin + cfg_height, 0, 0};
}

int32_t bar_current_height(const MonitorOutput &mon) {
    return bar_autohide_geometry(mon.autohide.enabled, mon.autohide.collapsed,
                                 kBarHeight)
        .height;
}

void bar_autohide_apply_geometry(MonitorOutput &mon, bool autohide,
                                 bool collapsed) {
    BarGeometry g = bar_autohide_geometry(autohide, collapsed, kBarHeight);
    bar_autohide_set_surface_geometry(
        mon.layer_surface, mon.surface, mon.egl_window, mon.width, g.height,
        g.margin_top, static_cast<int32_t>(kPanelSideMargin),
        static_cast<int32_t>(kPanelSideMargin), g.exclusive_zone,
        mon.output_scale.scale);
}

void monitor_autohide_apply(MonitorOutput &mon, bool enabled) {
    mon.autohide.enabled = enabled;
    mon.autohide.hidden = false;
    mon.autohide.collapsed = enabled && mon.autohide.collapsed;
    mon.autohide.opacity = mon.autohide.collapsed ? 0.0f : 1.0f;
    bar_autohide_apply_geometry(mon, enabled, mon.autohide.collapsed);
}

void bar_autohide_set_enabled(WaylandState &app, bool enabled) {
    if (enabled == app.cfg.autohide)
        return;
    app.cfg.autohide = enabled;
    for (auto &mon : app.outputs) {
        auto it = app.cfg.monitor_overrides.find(mon->output.name);
        if (it != app.cfg.monitor_overrides.end() && it->second.enabled)
            continue;
        monitor_autohide_apply(*mon, enabled);
    }
}

void rest_egl_current(WaylandState &app) {
    if (!app.outputs.empty())
        eglMakeCurrent(app.egl_display, app.outputs.front()->egl_surface,
                       app.outputs.front()->egl_surface, app.egl_context);
}

void network_panel_dispatch(WaylandState &app, bool changed) {
    if (!changed)
        return;
    for (auto &mon : app.outputs) {
        bar_request_frame(*mon);
        network_panel_request_frame(
            mon->network_panel, pill_center_x(mon->capsule, PillId::Wifi),
            static_cast<float>(kBarHeight), kBarTopMargin);
    }
    rest_egl_current(app);
}

void bluetooth_panel_dispatch(WaylandState &app) {
    for (auto &mon : app.outputs) {
        bar_request_frame(*mon);
        bluetooth_panel_request_frame(
            mon->bluetooth_panel,
            pill_center_x(mon->capsule, PillId::Bluetooth),
            static_cast<float>(kBarHeight), kBarTopMargin);
    }
    rest_egl_current(app);
}

void volume_panel_dispatch(WaylandState &app) {
    for (auto &mon : app.outputs) {
        bar_request_frame(*mon);
        volume_panel_request_frame(
            mon->volume_panel, pill_center_x(mon->capsule, PillId::Volume),
            static_cast<float>(kBarHeight), kBarTopMargin);
    }
    rest_egl_current(app);
}

} // namespace bar_detail

std::vector<IpcHandler> bar_ipc_handlers(WaylandState &state) {
    return {
        {"bar",
         [&state] {
             bar_detail::bar_autohide_set_enabled(state, !state.cfg.autohide);
         },
         "toggle the bar's autohide"},
    };
}

bool bootstrap_egl(WaylandState &state) {
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
    return state.egl_context != EGL_NO_CONTEXT;
}

bool bar_init_egl(MonitorOutput &mon, Renderer &renderer, EGLDisplay display,
                  EGLConfig config, EGLContext context) {
    int32_t scale = mon.output_scale.scale;
    mon.egl_window =
        wl_egl_window_create(mon.surface, mon.width * scale,
                             bar_detail::bar_current_height(mon) * scale);
    mon.egl_surface = eglCreateWindowSurface(
        display, config, reinterpret_cast<EGLNativeWindowType>(mon.egl_window),
        nullptr);
    if (mon.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!eglMakeCurrent(display, mon.egl_surface, mon.egl_surface, context))
        return false;

    const char *renderer_name =
        reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    klog("egl: renderer=%s output='%s'",
         renderer_name ? renderer_name : "(unknown)", mon.output.name.c_str());

    mon.frame_clock.surface = mon.surface;
    mon.frame_clock.draw = [&mon] { bar_paint(mon); };
    (void)renderer;
    return true;
}

void dispatch_pill_click(MonitorOutput &mon, double click_x, double click_y) {
    PointerState p = mon.app->pointer;
    p.x = click_x;
    p.y = click_y;
    if (mon.autohide.enabled)
        p.y -= bar_detail::kBarTopMargin;
    bar_detail::dispatch_pill_click(mon.capsule, p, mon.surface);
}

void update_clock(MonitorOutput &mon) {
    bar_detail::update_clock(mon.clock_texture);
}

void bar_paint(MonitorOutput &mon) {
    using namespace bar_detail;
    WaylandState &app = *mon.app;

    mon.animations.tick(std::chrono::steady_clock::now());

    PillId current_panel_pill =
        panel_pill(mon.network_panel, mon.bluetooth_panel, mon.volume_panel,
                   mon.tray_panel, app.starward, app.controlcenter);

    if (mon.autohide.enabled) {
        bool want_shown = app.pointer.focused_surface == mon.surface ||
                          current_panel_pill != PillId::None;
        if (want_shown == mon.autohide.hidden) {
            mon.autohide.hidden = !want_shown;
            if (want_shown && mon.autohide.collapsed) {
                mon.autohide.collapsed = false;
                bar_autohide_apply_geometry(mon, true, false);
            }
            float target = want_shown ? 1.0f : 0.0f;
            float duration = want_shown ? kAutoHideRevealMs : kAutoHideHideMs;
            mon.animations.animate(
                mon.autohide.opacity, target, duration, Easing::EaseOutCubic,
                [&mon](float v) { mon.autohide.opacity = v; },
                [&mon] {
                    if (mon.autohide.hidden && !mon.autohide.collapsed) {
                        mon.autohide.collapsed = true;
                        bar_autohide_apply_geometry(mon, true, true);
                    }
                },
                kAutoHideAnimOwner);
        }
    }
    int32_t surface_height = bar_current_height(mon);
    float content_y_offset =
        mon.autohide.enabled ? static_cast<float>(kBarTopMargin) : 0.0f;
    float height = static_cast<float>(kBarHeight);

    eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                   app.egl_context);
    app.renderer.begin_frame(mon.width, surface_height, mon.output_scale.scale);
    app.renderer.set_opacity(mon.autohide.enabled ? mon.autohide.opacity
                                                  : 1.0f);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    mon.scene.rebuild();
    Node *root = &mon.scene.root;
    Node *content = node_add_group(root, 0.0f, content_y_offset,
                                   static_cast<float>(mon.width), height);

    const float *white = rgba(palette::text);
    float pill_bg[4] = {palette::overlay.r, palette::overlay.g,
                        palette::overlay.b, palette::overlay.a};

    if (current_panel_pill == PillId::None &&
        mon.capsule.panel_pill_prev != PillId::None) {
        mon.capsule.label_linger_pill = mon.capsule.panel_pill_prev;
        mon.capsule.label_linger_until =
            std::chrono::steady_clock::now() + kPillCloseLingerMs;
    }
    mon.capsule.panel_pill_prev = current_panel_pill;
    bool lingering =
        mon.capsule.label_linger_pill != PillId::None &&
        std::chrono::steady_clock::now() < mon.capsule.label_linger_until;
    PointerState hit_pointer = app.pointer;
    hit_pointer.y -= content_y_offset;
    PillId hovered =
        current_panel_pill != PillId::None ? current_panel_pill
        : lingering                        ? mon.capsule.label_linger_pill
                    : hit_test_pills(mon.capsule, hit_pointer, mon.surface);
    if (hovered == PillId::None && mon.volume_peek_active)
        hovered = PillId::Volume;

    float x = 0.0f;
    std::vector<Pill> starward_pills = {starward_pill(mon)};
    x = draw_pills(content, mon.capsule, mon.animations, x, height,
                   starward_pills, white, pill_bg, hovered, current_panel_pill);

    int active_id = monitor_active_workspace_id(mon);
    const std::vector<Workspace> &ws_list = monitor_workspaces(mon);
    x = draw_workspace_row(content, mon.workspace_widget, mon.animations, x,
                           height, ws_list, active_id, pill_bg);

    draw_static_pill_row(content, x, height, {&mon.dock_texture}, white,
                         pill_bg);

    draw_clock_pill(content, height, mon.width, mon.clock_texture, white,
                    pill_bg);

    std::vector<Pill> control_center_pills = {control_center_pill(mon)};
    std::vector<Pill> battery_pills = {battery_pill(mon)};
    std::vector<Pill> right_stub_pills = {
        tray_pill(mon),      cpu_pill(mon),    wifi_pill(mon),
        bluetooth_pill(mon), volume_pill(mon),
    };

    float cc_w = pills_row_width(mon.capsule, mon.animations,
                                 control_center_pills, hovered, height);
    float batt_w = pills_row_width(mon.capsule, mon.animations, battery_pills,
                                   hovered, height);
    float stub_w =
        pills_row_width(mon.capsule, mon.animations, right_stub_pills, hovered,
                        height, current_panel_pill);

    float cc_x = mon.width - cc_w;
    float batt_x = cc_x - (batt_w > 0 ? kCapsuleGap : 0.0f) - batt_w;
    float stub_x = batt_x - (stub_w > 0 ? kCapsuleGap : 0.0f) - stub_w;

    if (stub_w > 0) {
        draw_pills(content, mon.capsule, mon.animations, stub_x, height,
                   right_stub_pills, white, pill_bg, hovered,
                   current_panel_pill);
    }
    if (batt_w > 0) {
        draw_pills(content, mon.capsule, mon.animations, batt_x, height,
                   battery_pills, white, pill_bg, hovered);
        const UpowerState &u = app.upower;
        if (u.present && !u.charging && !u.full && u.percent <= 10) {
            const Rect &r = mon.capsule.pill_rects[pill_idx(PillId::Battery)];
            add_rrect_node(content, r.x, r.y, r.w, r.h, metrics::radius_md,
                           0.0f, rgba(palette::critical_alpha15),
                           rgba(palette::critical_alpha15));
        }
    }
    if (cc_w > 0) {
        draw_pills(content, mon.capsule, mon.animations, cc_x, height,
                   control_center_pills, white, pill_bg, hovered);
    }

    mon.scene.draw(app.renderer);
    eglSwapBuffers(app.egl_display, mon.egl_surface);

    if (mon.animations.hasActive())
        bar_request_frame(mon);
}

void bar_request_frame(MonitorOutput &mon) {
    if (mon.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(mon.frame_clock);
}

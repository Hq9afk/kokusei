#include <GLES2/gl2.h>
#include <algorithm>
#include <cstring>

#include "app/wayland_registry.h"

#include "bar/widget/battery_widget.h"
#include "bar/widget/bluetooth_widget.h"
#include "bar/widget/clock_widget.h"
#include "bar/widget/control_center_widget.h"
#include "bar/widget/network_widget.h"
#include "bar/widget/starward_widget.h"
#include "bar/widget/volume_widget.h"

#include "core/log.h"

#include "modules/bar.h"

#include "render/icon.h"
#include "render/icons.h"
#include "render/palette.h"

#include "service/active_output.h"
#include "service/layer_surface.h"

BarPerMonitorState &bar_state(MonitorOutput &mon) {
    return mon.module<BarPerMonitorModule>()->state;
}

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
    BarPerMonitorState &bs = bar_state(mon);
    if (keep != PillId::Starward) {
        if (Module *m = find_overlay_by_name(*mon.app, "starward");
            m && m->is_open())
            m->toggle_from_widget(*mon.app);
    }
    if (keep != PillId::ControlCenter) {
        if (Module *m = find_overlay_by_name(*mon.app, "controlcenter");
            m && m->is_open())
            m->toggle_from_widget(*mon.app);
    }
    if (keep != PillId::Wifi && bs.network_panel.base.open)
        network_panel_toggle(bs.network_panel);
    if (keep != PillId::Bluetooth && bs.bluetooth_panel.base.open)
        bluetooth_panel_toggle(bs.bluetooth_panel, mon.app->bluetooth);
    if (keep != PillId::Volume && bs.volume_panel.base.open)
        volume_panel_toggle(bs.volume_panel);
    if (keep != PillId::Tray && bs.tray_panel.base.open)
        tray_panel_toggle(bs.tray_panel);
    if (keep != PillId::Tray && bs.tray_menu.base.open)
        tray_menu_close(bs.tray_menu);
    if (keep != PillId::Battery && bs.battery_panel.base.open)
        battery_panel_toggle(bs.battery_panel);
    if (keep != PillId::Cpu && bs.system_monitor_panel.base.open)
        system_monitor_panel_toggle(bs.system_monitor_panel);
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

} // namespace bar_detail

namespace {

void bar_dispatch_request_frame(WaylandState &app) {
    for (auto &mon : app.outputs)
        mon->module<BarPerMonitorModule>()->request_frame();
    bar_detail::rest_egl_current(app);
}

void network_panel_dispatch(WaylandState &app, bool changed) {
    if (changed)
        bar_dispatch_request_frame(app);
}

void bluetooth_panel_dispatch(WaylandState &app) {
    bar_dispatch_request_frame(app);
}

void volume_panel_dispatch(WaylandState &app) {
    bar_dispatch_request_frame(app);
}

void tray_dispatch(WaylandState &app) { bar_dispatch_request_frame(app); }

void battery_panel_dispatch(WaylandState &app) {
    bar_dispatch_request_frame(app);
}

void system_monitor_panel_dispatch(WaylandState &app) {
    bar_dispatch_request_frame(app);
}

} // namespace

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
    bar_detail::dispatch_pill_click(bar_state(mon).capsule, p, mon.surface);
}

void update_clock(MonitorOutput &mon) {
    bar_detail::update_clock(bar_state(mon).clock_texture);
}

void init_stub_widgets(MonitorOutput &mon) {
    BarPerMonitorState &bs = bar_state(mon);
    bs.starward_texture = make_icon_texture(icon::power);
    bs.tray_texture = make_icon_texture(icon::tray);
    bs.cpu_texture = make_icon_texture(icon::cpu);
    bs.control_center_texture = make_icon_texture(icon::control_center);
}

void bar_paint(MonitorOutput &mon) {
    using namespace bar_detail;
    WaylandState &app = *mon.app;
    BarPerMonitorState &bs = bar_state(mon);

    mon.animations.tick(std::chrono::steady_clock::now());

    Module *starward_m = find_overlay_by_name(app, "starward");
    Module *controlcenter_m = find_overlay_by_name(app, "controlcenter");
    PillId current_panel_pill = panel_pill(
        bs.network_panel, bs.bluetooth_panel, bs.volume_panel, bs.tray_panel,
        bs.battery_panel, bs.system_monitor_panel,
        starward_m && starward_m->is_open() && starward_m->opened_by_widget(),
        controlcenter_m && controlcenter_m->is_open() &&
            controlcenter_m->opened_by_widget());

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
        bs.capsule.panel_pill_prev != PillId::None) {
        bs.capsule.label_linger_pill = bs.capsule.panel_pill_prev;
        bs.capsule.label_linger_until =
            std::chrono::steady_clock::now() + kPillCloseLingerMs;
    }
    bs.capsule.panel_pill_prev = current_panel_pill;
    bool lingering =
        bs.capsule.label_linger_pill != PillId::None &&
        std::chrono::steady_clock::now() < bs.capsule.label_linger_until;
    PointerState hit_pointer = app.pointer;
    hit_pointer.y -= content_y_offset;
    PillId hovered = current_panel_pill != PillId::None ? current_panel_pill
                     : lingering
                         ? bs.capsule.label_linger_pill
                         : hit_test_pills(bs.capsule, hit_pointer, mon.surface);
    if (hovered == PillId::None && bs.volume_peek_active)
        hovered = PillId::Volume;

    float x = 0.0f;
    std::vector<Pill> starward_pills = {starward_pill(mon)};
    x = draw_pills(content, bs.capsule, mon.animations, x, height,
                   starward_pills, white, pill_bg, hovered, current_panel_pill);

    int active_id = monitor_active_workspace_id(mon);
    const std::vector<Workspace> &ws_list = monitor_workspaces(mon);
    x = draw_workspace_row(content, bs.workspace_widget, mon.animations, x,
                           height, ws_list, active_id, pill_bg);

    draw_static_pill_row(content, x, height, {&bs.dock_texture}, white,
                         pill_bg);

    draw_clock_pill(content, height, mon.width, bs.clock_texture, white,
                    pill_bg);

    std::vector<Pill> control_center_pills = {control_center_pill(mon)};
    std::vector<Pill> battery_pills = {battery_pill(mon)};
    std::vector<Pill> right_stub_pills = {
        tray_pill(mon),      cpu_pill(mon),    wifi_pill(mon),
        bluetooth_pill(mon), volume_pill(mon),
    };

    float cc_w = pills_row_width(bs.capsule, mon.animations,
                                 control_center_pills, hovered, height);
    float batt_w = pills_row_width(bs.capsule, mon.animations, battery_pills,
                                   hovered, height);
    float stub_w = pills_row_width(bs.capsule, mon.animations, right_stub_pills,
                                   hovered, height, current_panel_pill);

    float cc_x = mon.width - cc_w;
    float batt_x = cc_x - (batt_w > 0 ? kCapsuleGap : 0.0f) - batt_w;
    float stub_x = batt_x - (stub_w > 0 ? kCapsuleGap : 0.0f) - stub_w;

    if (stub_w > 0) {
        draw_pills(content, bs.capsule, mon.animations, stub_x, height,
                   right_stub_pills, white, pill_bg, hovered,
                   current_panel_pill);
    }
    if (batt_w > 0) {
        draw_pills(content, bs.capsule, mon.animations, batt_x, height,
                   battery_pills, white, pill_bg, hovered);
        const UpowerState &u = app.upower;
        if (u.present && !u.charging && !u.full && u.percent <= 10) {
            const Rect &r = bs.capsule.pill_rects[pill_idx(PillId::Battery)];
            add_rrect_node(content, r.x, r.y, r.w, r.h, metrics::radius_md,
                           0.0f, rgba(palette::critical_alpha15),
                           rgba(palette::critical_alpha15));
        }
    }
    if (cc_w > 0) {
        draw_pills(content, bs.capsule, mon.animations, cc_x, height,
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

bool BarPerMonitorModule::create_surface(WaylandState &app, MonitorOutput &mon,
                                         wl_output *output) {
    mon_ = &mon;
    mon.autohide.enabled = autohide_effective_enabled(app.cfg, mon.output.name);
    LayerSurfaceConfig bar_cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        .name_space = "kokusei",
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
        .height = bar_detail::bar_current_height(mon),
        .margin_top = bar_detail::bar_autohide_geometry(mon.autohide.enabled,
                                                        mon.autohide.collapsed,
                                                        bar_detail::kBarHeight)
                          .margin_top,
        .margin_right = static_cast<int32_t>(kPanelSideMargin),
        .margin_left = static_cast<int32_t>(kPanelSideMargin),
        .exclusive_zone = bar_detail::bar_autohide_geometry(
                              mon.autohide.enabled, mon.autohide.collapsed,
                              bar_detail::kBarHeight)
                              .exclusive_zone,
    };
    mon.layer_surface = layer_surface_create(
        mon.surface, app.compositor, app.layer_shell, bar_cfg,
        &bar_layer_surface_listener, &mon, output);
    mon.output_scale.on_change = [&mon](int32_t scale) {
        if (mon.egl_window)
            wl_egl_window_resize(mon.egl_window, mon.width * scale,
                                 bar_detail::bar_current_height(mon) * scale, 0,
                                 0);
        if (mon.frame_clock.surface)
            ::request_frame(mon.frame_clock);
    };
    output_scale_watch(mon.output_scale, mon.surface);
    wl_surface_commit(mon.surface);

    if (!network_panel_create_surface(state.network_panel, app.compositor,
                                      app.layer_shell, output))
        klog("network_panel: failed to create layer surface on '%s'",
             mon.output.name.c_str());
    if (!bluetooth_panel_create_surface(state.bluetooth_panel, app.compositor,
                                        app.layer_shell, output))
        klog("bluetooth_panel: failed to create layer surface on '%s'",
             mon.output.name.c_str());
    if (!volume_panel_create_surface(state.volume_panel, app.compositor,
                                     app.layer_shell, output))
        klog("volume_panel: failed to create layer surface on '%s'",
             mon.output.name.c_str());
    if (!tray_panel_create_surface(state.tray_panel, app.compositor,
                                   app.layer_shell, output))
        klog("tray_panel: failed to create layer surface on '%s'",
             mon.output.name.c_str());
    if (!tray_menu_create_surface(state.tray_menu, app.compositor,
                                  app.layer_shell, output))
        klog("tray_menu: failed to create layer surface on '%s'",
             mon.output.name.c_str());
    if (!battery_panel_create_surface(state.battery_panel, app.compositor,
                                      app.layer_shell, output))
        klog("battery_panel: failed to create layer surface on '%s'",
             mon.output.name.c_str());
    if (!system_monitor_panel_create_surface(state.system_monitor_panel,
                                             app.compositor, app.layer_shell,
                                             output))
        klog("system_monitor_panel: failed to create layer surface on '%s'",
             mon.output.name.c_str());
    return true;
}

bool BarPerMonitorModule::configured() const {
    return mon_->configured &&
           (!state.network_panel.base.layer_surface ||
            state.network_panel.base.configured) &&
           (!state.bluetooth_panel.base.layer_surface ||
            state.bluetooth_panel.base.configured) &&
           (!state.volume_panel.base.layer_surface ||
            state.volume_panel.base.configured) &&
           (!state.tray_panel.base.layer_surface ||
            state.tray_panel.base.configured) &&
           (!state.tray_menu.base.layer_surface ||
            state.tray_menu.base.configured) &&
           (!state.battery_panel.base.layer_surface ||
            state.battery_panel.base.configured) &&
           (!state.system_monitor_panel.base.layer_surface ||
            state.system_monitor_panel.base.configured);
}

bool BarPerMonitorModule::init_egl(WaylandState &app, MonitorOutput &mon) {
    if (!bar_init_egl(mon, app.renderer, app.egl_display, app.egl_config,
                      app.egl_context))
        return false;
    update_clock(mon);
    init_stub_widgets(mon);

    if (state.network_panel.base.layer_surface &&
        network_panel_init_egl(state.network_panel, app.renderer, app.network,
                               app.egl_display, app.egl_config,
                               app.egl_context)) {
        network_panel_request_frame(state.network_panel, 0.0f, 0.0f, 0.0f);
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    }
    if (state.bluetooth_panel.base.layer_surface &&
        bluetooth_panel_init_egl(state.bluetooth_panel, app.renderer,
                                 app.bluetooth, app.egl_display, app.egl_config,
                                 app.egl_context)) {
        bluetooth_panel_request_frame(state.bluetooth_panel, 0.0f, 0.0f, 0.0f);
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    }
    if (state.volume_panel.base.layer_surface &&
        volume_panel_init_egl(state.volume_panel, app.renderer, app.pipewire,
                              app.egl_display, app.egl_config,
                              app.egl_context)) {
        volume_panel_request_frame(state.volume_panel, 0.0f, 0.0f, 0.0f);
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    }
    if (state.tray_panel.base.layer_surface &&
        tray_panel_init_egl(state.tray_panel, app.renderer, app.tray,
                            app.egl_display, app.egl_config, app.egl_context)) {
        tray_panel_request_frame(state.tray_panel, 0.0f, 0.0f, 0.0f);
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    }
    if (state.tray_menu.base.layer_surface &&
        tray_menu_init_egl(state.tray_menu, app.renderer, app.tray,
                           app.egl_display, app.egl_config, app.egl_context)) {
        tray_menu_request_frame(state.tray_menu);
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    }
    if (state.battery_panel.base.layer_surface &&
        battery_panel_init_egl(state.battery_panel, app.renderer, app.upower,
                               app.egl_display, app.egl_config,
                               app.egl_context)) {
        battery_panel_request_frame(state.battery_panel, 0.0f, 0.0f, 0.0f);
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    }
    if (state.system_monitor_panel.base.layer_surface &&
        system_monitor_panel_init_egl(state.system_monitor_panel, app.renderer,
                                      app.cpu_temp, app.gpu_temp,
                                      app.system_stats, app.egl_display,
                                      app.egl_config, app.egl_context)) {
        system_monitor_panel_request_frame(state.system_monitor_panel, 0.0f,
                                           0.0f, 0.0f);
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    }

    if (mon.autohide.enabled) {
        mon.autohide.hidden = true;
        mon.autohide.collapsed = true;
        mon.autohide.opacity = 0.0f;
    }
    bar_request_frame(mon);
    return true;
}

void BarPerMonitorModule::destroy(WaylandState &app, MonitorOutput &mon) {
    EGLDisplay d = app.egl_display;
    destroy_layer_surface(d, mon.surface, mon.layer_surface, mon.egl_window,
                          mon.egl_surface);
    destroy_layer_surface(d, state.network_panel.base.surface,
                          state.network_panel.base.layer_surface,
                          state.network_panel.base.egl_window,
                          state.network_panel.base.egl_surface);
    destroy_layer_surface(d, state.bluetooth_panel.base.surface,
                          state.bluetooth_panel.base.layer_surface,
                          state.bluetooth_panel.base.egl_window,
                          state.bluetooth_panel.base.egl_surface);
    destroy_layer_surface(d, state.volume_panel.base.surface,
                          state.volume_panel.base.layer_surface,
                          state.volume_panel.base.egl_window,
                          state.volume_panel.base.egl_surface);
    destroy_layer_surface(
        d, state.tray_panel.base.surface, state.tray_panel.base.layer_surface,
        state.tray_panel.base.egl_window, state.tray_panel.base.egl_surface);
    destroy_layer_surface(
        d, state.tray_menu.base.surface, state.tray_menu.base.layer_surface,
        state.tray_menu.base.egl_window, state.tray_menu.base.egl_surface);
    destroy_layer_surface(d, state.battery_panel.base.surface,
                          state.battery_panel.base.layer_surface,
                          state.battery_panel.base.egl_window,
                          state.battery_panel.base.egl_surface);
    destroy_layer_surface(d, state.system_monitor_panel.base.surface,
                          state.system_monitor_panel.base.layer_surface,
                          state.system_monitor_panel.base.egl_window,
                          state.system_monitor_panel.base.egl_surface);
}

bool BarPerMonitorModule::owns_surface(wl_surface *surface) const {
    return surface == mon_->surface ||
           surface == state.network_panel.base.surface ||
           surface == state.bluetooth_panel.base.surface ||
           surface == state.volume_panel.base.surface ||
           surface == state.tray_panel.base.surface ||
           surface == state.tray_menu.base.surface ||
           surface == state.battery_panel.base.surface ||
           surface == state.system_monitor_panel.base.surface;
}

void BarPerMonitorModule::request_frame() {
    if (!mon_)
        return;
    bar_request_frame(*mon_);
    network_panel_request_frame(
        state.network_panel,
        bar_detail::pill_center_x(state.capsule, PillId::Wifi),
        static_cast<float>(bar_detail::kBarHeight), bar_detail::kBarTopMargin);
    bluetooth_panel_request_frame(
        state.bluetooth_panel,
        bar_detail::pill_center_x(state.capsule, PillId::Bluetooth),
        static_cast<float>(bar_detail::kBarHeight), bar_detail::kBarTopMargin);
    volume_panel_request_frame(
        state.volume_panel,
        bar_detail::pill_center_x(state.capsule, PillId::Volume),
        static_cast<float>(bar_detail::kBarHeight), bar_detail::kBarTopMargin);
    tray_panel_request_frame(
        state.tray_panel,
        bar_detail::pill_center_x(state.capsule, PillId::Tray),
        static_cast<float>(bar_detail::kBarHeight), bar_detail::kBarTopMargin);
    tray_menu_request_frame(state.tray_menu);
    battery_panel_request_frame(
        state.battery_panel,
        bar_detail::pill_center_x(state.capsule, PillId::Battery),
        static_cast<float>(bar_detail::kBarHeight), bar_detail::kBarTopMargin);
    system_monitor_panel_request_frame(
        state.system_monitor_panel,
        bar_detail::pill_center_x(state.capsule, PillId::Cpu),
        static_cast<float>(bar_detail::kBarHeight), bar_detail::kBarTopMargin);
}

void BarPerMonitorModule::tick(WaylandState &, MonitorOutput &mon) {
    bar_detail::volume_pill_peek_tick(mon);
}

void BarPerMonitorModule::timer_tick(WaylandState &app, MonitorOutput &mon) {
    update_clock(mon);
    bar_request_frame(mon);
    if (bar_detail::volume_pill_peek_expire(mon))
        bar_request_frame(mon);

    if (state.system_monitor_panel.base.open) {
        ++poll_tick_;
        if (poll_tick_ % 2 == 0) {
            cpu_temp_poll(app.cpu_temp);
            system_stats_poll(app.system_stats);
        }
        if (poll_tick_ % 5 == 0)
            gpu_temp_poll(app.gpu_temp);
        system_monitor_panel_dispatch(app);
    }
}

std::vector<IpcHandler> BarPerMonitorModule::ipc_handlers(WaylandState &app) {
    return {
        {"bar",
         [&app] {
             Config updated = app.cfg;
             updated.autohide = !updated.autohide;
             bar_detail::save_and_apply_config_update(app, updated);
         },
         "toggle the bar's autohide"},
    };
}

bool BarPerMonitorModule::is_open() const {
    return state.network_panel.base.open || state.bluetooth_panel.base.open ||
           state.volume_panel.base.open || state.tray_panel.base.open ||
           state.battery_panel.base.open ||
           state.system_monitor_panel.base.open;
}

void BarPerMonitorModule::handle_click(WaylandState &app, MonitorOutput &mon,
                                       wl_surface *surface, int button,
                                       double x, double y) {
    if (button != BTN_LEFT && surface != state.tray_panel.base.surface)
        return;

    if (state.tray_menu.base.open && surface != state.tray_menu.base.surface &&
        surface != state.tray_panel.base.surface) {
        tray_menu_close(state.tray_menu);
        tray_menu_request_frame(state.tray_menu);
        bar_detail::rest_egl_current(app);
    }
    if (surface == state.tray_panel.base.surface) {
        tray_panel_handle_click(state.tray_panel, app.tray, state.tray_menu,
                                app, mon.output.wl, x, y, button);
        tray_dispatch(app);
    } else if (surface == state.tray_menu.base.surface) {
        tray_menu_handle_click(state.tray_menu, app.tray, x, y);
        tray_menu_request_frame(state.tray_menu);
        bar_detail::rest_egl_current(app);
    } else if (surface == state.network_panel.base.surface) {
        network_panel_handle_click(state.network_panel, app.network, x, y);
        network_panel_dispatch(app, true);
    } else if (surface == state.bluetooth_panel.base.surface) {
        bluetooth_panel_handle_click(state.bluetooth_panel, app.bluetooth, x,
                                     y);
        bluetooth_panel_dispatch(app);
    } else if (surface == state.volume_panel.base.surface) {
        volume_panel_handle_click(state.volume_panel, app.pipewire, x, y);
        volume_panel_dispatch(app);
    } else if (surface == state.battery_panel.base.surface) {
        battery_panel_handle_click(state.battery_panel, x, y);
        battery_panel_dispatch(app);
    } else if (surface == state.system_monitor_panel.base.surface) {
        system_monitor_panel_handle_click(state.system_monitor_panel, x, y);
        system_monitor_panel_dispatch(app);
    } else if (surface == mon.surface) {
        dispatch_pill_click(mon, x, y);
        network_panel_dispatch(app, true);
        bluetooth_panel_dispatch(app);
        volume_panel_dispatch(app);
        tray_dispatch(app);
        battery_panel_dispatch(app);
        system_monitor_panel_dispatch(app);
        for (auto &m : app.overlays) {
            m->request_frame();
            bar_detail::rest_egl_current(app);
        }
    }
}

void BarPerMonitorModule::handle_scroll(WaylandState &app, MonitorOutput &mon,
                                        wl_surface *surface, double dy) {
    if (surface == state.network_panel.base.surface) {
        network_panel_handle_scroll(state.network_panel, app.network, dy);
        network_panel_dispatch(app, true);
    } else if (surface == state.bluetooth_panel.base.surface) {
        bluetooth_panel_handle_scroll(state.bluetooth_panel, app.bluetooth, dy);
        bluetooth_panel_dispatch(app);
    } else if (surface == state.volume_panel.base.surface) {
        volume_panel_handle_scroll(state.volume_panel, app.pipewire, dy);
        volume_panel_dispatch(app);
    } else if (surface == state.battery_panel.base.surface) {
        battery_panel_handle_scroll(state.battery_panel, app.upower, dy);
        battery_panel_dispatch(app);
    } else if (surface == state.system_monitor_panel.base.surface) {
        system_monitor_panel_handle_scroll(state.system_monitor_panel,
                                           app.gpu_temp, dy);
        system_monitor_panel_dispatch(app);
    } else if (surface == mon.surface &&
               bar_detail::hit_test_pills(state.capsule, app.pointer,
                                          mon.surface) == PillId::Volume) {
        bar_detail::volume_pill_handle_wheel(mon, dy);
    }
}

void BarPerMonitorModule::handle_key_event(WaylandState &app,
                                           MonitorOutput &mon,
                                           const KeyEvent &event) {
    (void)mon;
    if (state.network_panel.base.open) {
        network_panel_handle_key_event(state.network_panel, app.network, event);
        network_panel_dispatch(app, true);
    } else if (state.bluetooth_panel.base.open) {
        bluetooth_panel_handle_key_event(state.bluetooth_panel, app.bluetooth,
                                         event);
        bluetooth_panel_dispatch(app);
    } else if (state.volume_panel.base.open) {
        volume_panel_handle_key_event(state.volume_panel, app.pipewire, event);
        volume_panel_dispatch(app);
    } else if (state.battery_panel.base.open) {
        battery_panel_handle_key_event(state.battery_panel, event);
        battery_panel_dispatch(app);
    } else if (state.system_monitor_panel.base.open) {
        system_monitor_panel_handle_key_event(state.system_monitor_panel,
                                              event);
        system_monitor_panel_dispatch(app);
    }
}

void BarPerMonitorModule::handle_pointer_move(WaylandState &app,
                                              MonitorOutput &mon, double x,
                                              double y) {
    (void)mon;
    (void)y;
    if (state.volume_panel.dragging) {
        volume_panel_handle_pointer_move(state.volume_panel, app.pipewire, x);
        request_frame();
    }
}

void BarPerMonitorModule::handle_pointer_release() {
    if (state.volume_panel.dragging) {
        state.volume_panel.dragging.reset();
        request_frame();
    }
}

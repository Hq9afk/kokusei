#include "bar.h"

#include "../core/log.h"
#include "../render/palette.h"
#include "widgets/battery_widget.h"
#include "widgets/bluetooth_widget.h"
#include "widgets/control_center_widget.h"
#include "widgets/network_widget.h"
#include "widgets/power_widget.h"
#include "widgets/volume_widget.h"

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

constexpr zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

namespace output_detail {
void geometry(void *, wl_output *, int32_t, int32_t, int32_t, int32_t,
             int32_t, const char *, const char *, int32_t) {}
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
}

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

void destroy_layer_surface(EGLDisplay display, wl_surface *&surface,
                           zwlr_layer_surface_v1 *&layer_surface,
                           wl_egl_window *&egl_window,
                           EGLSurface &egl_surface) {
    if (egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(display, egl_surface);
        egl_surface = EGL_NO_SURFACE;
    }
    if (egl_window) {
        wl_egl_window_destroy(egl_window);
        egl_window = nullptr;
    }
    if (layer_surface) {
        zwlr_layer_surface_v1_destroy(layer_surface);
        layer_surface = nullptr;
    }
    if (surface) {
        wl_surface_destroy(surface);
        surface = nullptr;
    }
}

void monitor_output_destroy(MonitorOutput &mon) {
    EGLDisplay d = mon.app->egl_display;
    destroy_layer_surface(d, mon.surface, mon.layer_surface, mon.egl_window,
                          mon.egl_surface);
    destroy_layer_surface(d, mon.wallpaper.surface, mon.wallpaper.layer_surface,
                          mon.wallpaper.egl_window, mon.wallpaper.egl_surface);
    destroy_layer_surface(d, mon.osd.surface, mon.osd.layer_surface,
                          mon.osd.egl_window, mon.osd.egl_surface);
    destroy_layer_surface(d, mon.notification_view.surface,
                          mon.notification_view.layer_surface,
                          mon.notification_view.egl_window,
                          mon.notification_view.egl_surface);
    destroy_layer_surface(
        d, mon.network_panel.base.surface, mon.network_panel.base.layer_surface,
        mon.network_panel.base.egl_window, mon.network_panel.base.egl_surface);
    destroy_layer_surface(d, mon.bluetooth_panel.base.surface,
                          mon.bluetooth_panel.base.layer_surface,
                          mon.bluetooth_panel.base.egl_window,
                          mon.bluetooth_panel.base.egl_surface);
    destroy_layer_surface(
        d, mon.volume_panel.base.surface, mon.volume_panel.base.layer_surface,
        mon.volume_panel.base.egl_window, mon.volume_panel.base.egl_surface);
    destroy_layer_surface(
        d, mon.tray_panel.base.surface, mon.tray_panel.base.layer_surface,
        mon.tray_panel.base.egl_window, mon.tray_panel.base.egl_surface);
    destroy_layer_surface(
        d, mon.tray_menu.base.surface, mon.tray_menu.base.layer_surface,
        mon.tray_menu.base.egl_window, mon.tray_menu.base.egl_surface);
    if (mon.output.wl)
        wl_output_release(mon.output.wl);
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

MonitorOutput *find_monitor_by_name(WaylandState &app,
                                    const std::string &name) {
    for (auto &mon : app.outputs)
        if (mon->output.name == name)
            return mon.get();
    return nullptr;
}

}

const wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

namespace bar_detail {

void close_other_overlays(MonitorOutput &mon, PillId keep) {
    if (keep != PillId::Power && mon.app->logout.base.open)
        logout_toggle(mon.app->logout);
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

void wallpaper_load_all_columns(MonitorOutput &mon, const Config &cfg) {
    int count = wallpaper_effective_column_count(cfg, mon.output.name);
    mon.wallpaper.column_textures.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        std::string path =
            wallpaper_effective_column_path(cfg, mon.output.name, i);
        if (!path.empty())
            wallpaper_decode_column_async(mon.wallpaper, path, i, count);
    }
}

void wallpaper_diff_columns(MonitorOutput &mon, const Config &old_cfg,
                           const Config &new_cfg) {
    int old_count = wallpaper_effective_column_count(old_cfg, mon.output.name);
    int new_count = wallpaper_effective_column_count(new_cfg, mon.output.name);
    if (new_count != static_cast<int>(mon.wallpaper.column_textures.size()))
        mon.wallpaper.column_textures.resize(static_cast<size_t>(new_count));
    int limit = std::max(old_count, new_count);
    for (int i = 0; i < limit; ++i) {
        std::string old_path =
            i < old_count
                ? wallpaper_effective_column_path(old_cfg, mon.output.name, i)
                : "";
        std::string new_path =
            i < new_count
                ? wallpaper_effective_column_path(new_cfg, mon.output.name, i)
                : "";
        if (new_path != old_path && !new_path.empty())
            wallpaper_decode_column_async(mon.wallpaper, new_path, i,
                                          new_count);
    }
}

const std::vector<Workspace> &
monitor_workspaces(const MonitorOutput &mon) {
    static const std::vector<Workspace> empty;
    switch (mon.app->compositor_backend) {
    case WaylandState::CompositorBackend::Hyprland: {
        auto it = mon.app->hypr.by_monitor.find(mon.output.name);
        return it != mon.app->hypr.by_monitor.end() ? it->second.workspaces
                                                    : empty;
    }
    case WaylandState::CompositorBackend::ShojiWM: {
        auto it = mon.app->shoji.by_monitor.find(mon.output.name);
        return it != mon.app->shoji.by_monitor.end() ? it->second.workspaces
                                                     : empty;
    }
    default:
        return empty;
    }
}

int monitor_active_workspace_id(const MonitorOutput &mon) {
    switch (mon.app->compositor_backend) {
    case WaylandState::CompositorBackend::Hyprland: {
        auto it = mon.app->hypr.by_monitor.find(mon.output.name);
        return it != mon.app->hypr.by_monitor.end() ? it->second.active_id : -1;
    }
    case WaylandState::CompositorBackend::ShojiWM: {
        auto it = mon.app->shoji.by_monitor.find(mon.output.name);
        return it != mon.app->shoji.by_monitor.end() ? it->second.active_id
                                                     : -1;
    }
    default:
        return -1;
    }
}
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

bool bar_init_egl(MonitorOutput &mon, Renderer &renderer,
                  EGLDisplay display, EGLConfig config,
                  EGLContext context) {
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

void monitor_output_create_surfaces(WaylandState &app,
                                    MonitorOutput &mon) {
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
        .exclusive_zone =
            bar_detail::bar_autohide_geometry(mon.autohide.enabled,
                                              mon.autohide.collapsed,
                                              bar_detail::kBarHeight)
                .exclusive_zone,
    };
    mon.layer_surface = layer_surface_create(
        mon.surface, app.compositor, app.layer_shell, bar_cfg,
        &layer_surface_listener, &mon, mon.output.wl);
    mon.output_scale.on_change = [&mon](int32_t scale) {
        if (mon.egl_window)
            wl_egl_window_resize(mon.egl_window, mon.width * scale,
                                 bar_detail::bar_current_height(mon) * scale, 0,
                                 0);
        if (mon.frame_clock.surface)
            request_frame(mon.frame_clock);
    };
    output_scale_watch(mon.output_scale, mon.surface);
    wl_surface_commit(mon.surface);

    if (!wallpaper_effective_column_path(app.cfg, mon.output.name, 0).empty() &&
        !wallpaper_create_surface(mon.wallpaper, app.compositor,
                                  app.layer_shell, mon.output.wl))
        klog("wallpaper: failed to create layer surface on '%s'",
             mon.output.name.c_str());

    if (!osd_create_surface(mon.osd, app.compositor, app.layer_shell,
                            mon.output.wl))
        klog("osd: failed to create layer surface on '%s'",
             mon.output.name.c_str());

    if (notifications_effective_enabled(app.cfg, mon.output.name) &&
        !notification_view_create_surface(mon.notification_view,
                                          app.compositor, app.layer_shell,
                                          mon.output.wl))
        klog("notification: failed to create layer surface on '%s'",
             mon.output.name.c_str());

    if (!network_panel_create_surface(mon.network_panel, app.compositor,
                                      app.layer_shell, mon.output.wl))
        klog("network_panel: failed to create layer surface on '%s'",
             mon.output.name.c_str());
    if (!bluetooth_panel_create_surface(mon.bluetooth_panel, app.compositor,
                                        app.layer_shell, mon.output.wl))
        klog("bluetooth_panel: failed to create layer surface on '%s'",
             mon.output.name.c_str());
    if (!volume_panel_create_surface(mon.volume_panel, app.compositor,
                                     app.layer_shell, mon.output.wl))
        klog("volume_panel: failed to create layer surface on '%s'",
             mon.output.name.c_str());
    if (!tray_panel_create_surface(mon.tray_panel, app.compositor,
                                   app.layer_shell, mon.output.wl))
        klog("tray_panel: failed to create layer surface on '%s'",
             mon.output.name.c_str());
    if (!tray_menu_create_surface(mon.tray_menu, app.compositor,
                                  app.layer_shell, mon.output.wl))
        klog("tray_menu: failed to create layer surface on '%s'",
             mon.output.name.c_str());
}

void monitor_output_wait_configured(WaylandState &app,
                                    MonitorOutput &mon) {
    bool have_wallpaper = mon.wallpaper.layer_surface != nullptr;
    while (!(
        mon.configured && (!have_wallpaper || mon.wallpaper.configured) &&
        (!mon.osd.layer_surface || mon.osd.configured) &&
        (!mon.network_panel.base.layer_surface ||
         mon.network_panel.base.configured) &&
        (!mon.bluetooth_panel.base.layer_surface ||
         mon.bluetooth_panel.base.configured) &&
        (!mon.volume_panel.base.layer_surface ||
         mon.volume_panel.base.configured) &&
        (!mon.tray_panel.base.layer_surface ||
         mon.tray_panel.base.configured) &&
        (!mon.tray_menu.base.layer_surface || mon.tray_menu.base.configured) &&
        (!mon.notification_view.layer_surface ||
         mon.notification_view.configured))) {
        wl_display_dispatch(app.display);
    }
}

void monitor_output_finish_egl(WaylandState &app, MonitorOutput &mon) {
    if (mon.wallpaper.layer_surface &&
        wallpaper_init_egl(mon.wallpaper, app.renderer, app.egl_display,
                           app.egl_config, app.egl_context)) {
        mon.wallpaper.fill_mode =
            wallpaper_effective_fill_mode(app.cfg, mon.output.name) == "fit"
                ? FillMode::Fit
                : FillMode::Crop;
        bar_detail::wallpaper_load_all_columns(mon, app.cfg);
        wallpaper_request_frame(mon.wallpaper);
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    }
    if (mon.notification_view.layer_surface &&
        notification_view_init_egl(mon.notification_view, app.notification,
                                   app.renderer, app.egl_display,
                                   app.egl_config, app.egl_context)) {
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    }
    if (mon.osd.layer_surface &&
        osd_init_egl(mon.osd, app.renderer, app.egl_display, app.egl_config,
                     app.egl_context)) {
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    }
    if (mon.network_panel.base.layer_surface &&
        network_panel_init_egl(mon.network_panel, app.renderer, app.network,
                               app.egl_display, app.egl_config,
                               app.egl_context)) {
        network_panel_request_frame(mon.network_panel, 0.0f, 0.0f, 0.0f);
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    }
    if (mon.bluetooth_panel.base.layer_surface &&
        bluetooth_panel_init_egl(mon.bluetooth_panel, app.renderer,
                                 app.bluetooth, app.egl_display, app.egl_config,
                                 app.egl_context)) {
        bluetooth_panel_request_frame(mon.bluetooth_panel, 0.0f, 0.0f, 0.0f);
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    }
    if (mon.volume_panel.base.layer_surface &&
        volume_panel_init_egl(mon.volume_panel, app.renderer, app.pipewire,
                              app.egl_display, app.egl_config,
                              app.egl_context)) {
        volume_panel_request_frame(mon.volume_panel, 0.0f, 0.0f, 0.0f);
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    }
    if (mon.tray_panel.base.layer_surface &&
        tray_panel_init_egl(mon.tray_panel, app.renderer, app.tray,
                            app.egl_display, app.egl_config, app.egl_context)) {
        tray_panel_request_frame(mon.tray_panel, 0.0f, 0.0f, 0.0f);
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    }
    if (mon.tray_menu.base.layer_surface &&
        tray_menu_init_egl(mon.tray_menu, app.renderer, app.tray,
                           app.egl_display, app.egl_config, app.egl_context)) {
        tray_menu_request_frame(mon.tray_menu);
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    }

    if (mon.autohide.enabled) {
        mon.autohide.hidden = true;
        mon.autohide.collapsed = true;
        mon.autohide.opacity = 0.0f;
    }
    bar_request_frame(mon);
}

void monitor_output_activate(WaylandState &app, MonitorOutput &mon) {
    if (mon.activated)
        return;
    klog("output: activating '%s'", mon.output.name.c_str());
    monitor_output_create_surfaces(app, mon);
    monitor_output_wait_configured(app, mon);
    if (!bar_init_egl(mon, app.renderer, app.egl_display, app.egl_config,
                      app.egl_context)) {
        klog("output: '%s' bar EGL init failed", mon.output.name.c_str());
        return;
    }
    monitor_output_finish_egl(app, mon);
    mon.activated = true;
}

void dispatch_pill_click(MonitorOutput &mon, double click_x,
                         double click_y) {
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
                   mon.tray_panel, app.logout);

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
    app.renderer.set_opacity(mon.autohide.enabled ? mon.autohide.opacity : 1.0f);
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
    std::vector<Pill> power_pills = {power_pill(mon)};
    x = draw_pills(content, mon.capsule, mon.animations, x, height, power_pills,
                   white, pill_bg, hovered, current_panel_pill);

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

namespace bar_detail {

void apply_config_update(WaylandState &app, Config new_cfg) {
    app.idle.timeout_seconds = new_cfg.idle_timeout_seconds;
    app.idle.on_idle_command = new_cfg.idle_command;
    app.idle.on_resume_command = new_cfg.idle_resume_command;

    for (auto &mon : app.outputs) {
        wallpaper_diff_columns(*mon, app.cfg, new_cfg);

        std::string new_fill =
            wallpaper_effective_fill_mode(new_cfg, mon->output.name);
        FillMode new_mode = new_fill == "fit" ? FillMode::Fit : FillMode::Crop;
        if (new_mode != mon->wallpaper.fill_mode) {
            mon->wallpaper.fill_mode = new_mode;
            wallpaper_request_frame(mon->wallpaper);
        }

        bool new_autohide = autohide_effective_enabled(new_cfg, mon->output.name);
        if (new_autohide != mon->autohide.enabled) {
            monitor_autohide_apply(*mon, new_autohide);
        }

        bool new_notif = notifications_effective_enabled(new_cfg, mon->output.name);
        bool had_notif_view = mon->notification_view.layer_surface != nullptr;
        if (new_notif && !had_notif_view) {
            if (notification_view_create_surface(mon->notification_view,
                                                 app.compositor, app.layer_shell,
                                                 mon->output.wl)) {
                while (!mon->notification_view.configured)
                    wl_display_dispatch(app.display);
                if (notification_view_init_egl(
                        mon->notification_view, app.notification, app.renderer,
                        app.egl_display, app.egl_config, app.egl_context))
                    eglMakeCurrent(app.egl_display, mon->egl_surface,
                                   mon->egl_surface, app.egl_context);
            }
        } else if (!new_notif && had_notif_view) {
            destroy_layer_surface(app.egl_display, mon->notification_view.surface,
                                  mon->notification_view.layer_surface,
                                  mon->notification_view.egl_window,
                                  mon->notification_view.egl_surface);
            mon->notification_view.configured = false;
        }
    }

    app.cfg = new_cfg;
    for (auto &mon : app.outputs)
        bar_request_frame(*mon);
}

void save_and_apply_config_update(WaylandState &app, Config new_cfg) {
    apply_config_update(app, new_cfg);
    save_config(app.cfg);
    app.config_own_write_pending = true;
}

MonitorOutput *settings_target_monitor(WaylandState &app) {
    if (app.compositor_backend == WaylandState::CompositorBackend::Hyprland &&
        !app.hypr.focused_monitor.empty()) {
        if (MonitorOutput *m = find_monitor_by_name(app, app.hypr.focused_monitor))
            return m;
    }
    if (app.last_pointer_monitor)
        return app.last_pointer_monitor;
    return app.outputs.empty() ? nullptr : app.outputs.front().get();
}

void settings_retarget(WaylandState &app, MonitorOutput &target) {
    SettingsState &s = app.settings;
    wl_output *previous_output = app.settings_bound_output;
    klog("settings: retargeting from output=%p to '%s'",
         static_cast<void *>(previous_output), target.output.name.c_str());
    destroy_layer_surface(app.egl_display, s.base.surface, s.base.layer_surface,
                          s.base.egl_window, s.base.egl_surface);
    s.base.configured = false;
    s.base.open = false;
    s.base.opacity = 0.0f;

    auto bind_to = [&](wl_output *out, const char *out_name) -> bool {
        if (!settings_create_surface(s, app.compositor, app.layer_shell, out)) {
            klog("settings: failed to recreate layer surface on '%s'", out_name);
            return false;
        }
        while (!s.base.configured)
            wl_display_dispatch(app.display);
        if (!settings_init_egl(
                s, app.cfg, app.renderer, app.egl_display, app.egl_config,
                app.egl_context, [&app] {
                    std::vector<std::string> names;
                    for (const auto &mon : app.outputs)
                        names.push_back(mon->output.name);
                    return names;
                })) {
            klog("settings: EGL surface init failed after retargeting to '%s'",
                 out_name);
            return false;
        }
        return true;
    };

    if (bind_to(target.output.wl, target.output.name.c_str())) {
        app.settings_bound_output = target.output.wl;
    } else if (previous_output != nullptr &&
               bind_to(previous_output, "previous output")) {
        app.settings_bound_output = previous_output;
    } else {
        klog("settings: retarget fallback also failed, disabling settings panel");
        app.settings_enabled = false;
    }

    if (!app.outputs.empty())
        eglMakeCurrent(app.egl_display, app.outputs.front()->egl_surface,
                       app.outputs.front()->egl_surface, app.egl_context);
}

}

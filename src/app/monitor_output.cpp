#include "app/monitor_output.h"

#include "core/log.h"
#include "modules/bar.h"
#include "modules/controlcenter.h"
#include "modules/settings.h"
#include "modules/starward.h"
#include "render/palette.h"
#include "service/layer_surface.h"
#include "service/settings_service.h"
#include "service/wallpaper_service.h"

#include <algorithm>

namespace {

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

} // namespace

void monitor_output_destroy(MonitorOutput &mon) {
    EGLDisplay d = mon.app->egl_display;
    destroy_layer_surface(d, mon.surface, mon.layer_surface, mon.egl_window,
                          mon.egl_surface);
    destroy_layer_surface(d, mon.wallpaper.surface, mon.wallpaper.layer_surface,
                          mon.wallpaper.egl_window, mon.wallpaper.egl_surface);
    destroy_layer_surface(d, mon.osd.surface, mon.osd.layer_surface,
                          mon.osd.egl_window, mon.osd.egl_surface);
    destroy_layer_surface(
        d, mon.notification_view.surface, mon.notification_view.layer_surface,
        mon.notification_view.egl_window, mon.notification_view.egl_surface);
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

MonitorOutput *find_monitor_by_name_wl(WaylandState &app, wl_output *wl) {
    for (auto &mon : app.outputs)
        if (mon->output.wl == wl)
            return mon.get();
    return nullptr;
}

void monitor_output_create_surfaces(WaylandState &app, MonitorOutput &mon) {
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
        &bar_layer_surface_listener, &mon, mon.output.wl);
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

    if (!wallpaper_service_column_path(app.cfg, mon.output.name, 0).empty() &&
        !wallpaper_create_surface(mon.wallpaper, app.compositor,
                                  app.layer_shell, mon.output.wl))
        klog("wallpaper: failed to create layer surface on '%s'",
             mon.output.name.c_str());

    if (!osd_create_surface(mon.osd, app.compositor, app.layer_shell,
                            mon.output.wl))
        klog("osd: failed to create layer surface on '%s'",
             mon.output.name.c_str());

    if (notifications_effective_enabled(app.cfg, mon.output.name) &&
        !notification_view_create_surface(mon.notification_view, app.compositor,
                                          app.layer_shell, mon.output.wl))
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

void monitor_output_wait_configured(WaylandState &app, MonitorOutput &mon) {
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
            wallpaper_service_fill_mode(app.cfg, mon.output.name) == "fit"
                ? FillMode::Fit
                : FillMode::Crop;
        wallpaper_sync_from_config(mon.wallpaper, app.cfg, mon.output.name);
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

namespace bar_detail {

const std::vector<Workspace> &monitor_workspaces(const MonitorOutput &mon) {
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

void apply_config_update(WaylandState &app, Config new_cfg) {
    app.idle.timeout_seconds = new_cfg.idle_timeout_seconds;
    app.idle.on_idle_command = new_cfg.idle_command;
    app.idle.on_resume_command = new_cfg.idle_resume_command;

    for (auto &mon : app.outputs) {
        wallpaper_sync_from_config(mon->wallpaper, new_cfg, mon->output.name);

        std::string new_fill =
            wallpaper_service_fill_mode(new_cfg, mon->output.name);
        FillMode new_mode = new_fill == "fit" ? FillMode::Fit : FillMode::Crop;
        if (new_mode != mon->wallpaper.fill_mode) {
            mon->wallpaper.fill_mode = new_mode;
            wallpaper_request_frame(mon->wallpaper);
        }

        bool new_autohide =
            autohide_effective_enabled(new_cfg, mon->output.name);
        if (new_autohide != mon->autohide.enabled) {
            monitor_autohide_apply(*mon, new_autohide);
        }

        bool new_notif =
            notifications_effective_enabled(new_cfg, mon->output.name);
        bool had_notif_view = mon->notification_view.layer_surface != nullptr;
        if (new_notif && !had_notif_view) {
            if (notification_view_create_surface(
                    mon->notification_view, app.compositor, app.layer_shell,
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
            destroy_layer_surface(app.egl_display,
                                  mon->notification_view.surface,
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
    settings_service_save(app.cfg);
    app.config_own_write_pending = true;
}

MonitorOutput *active_target_monitor(WaylandState &app) {
    std::vector<Output *> outputs;
    for (auto &mon : app.outputs)
        outputs.push_back(&mon->output);
    std::string focused_name =
        app.compositor_backend == WaylandState::CompositorBackend::Hyprland
            ? app.hypr.focused_monitor
            : std::string();
    wl_output *pointer_hint = app.last_pointer_monitor
                                  ? app.last_pointer_monitor->output.wl
                                  : nullptr;
    wl_output *target =
        active_output_select(outputs, focused_name, pointer_hint);
    return target ? find_monitor_by_name_wl(app, target) : nullptr;
}

void settings_retarget(WaylandState &app, MonitorOutput &target) {
    SettingsState &s = app.settings;
    wl_output *bound = overlay_panel_retarget(
        s.base, app.display, app.settings_bound_output, target.output.wl,
        target.output.name.c_str(),
        [&](wl_output *out) {
            return settings_create_surface(s, app.compositor, app.layer_shell,
                                           out);
        },
        [&] {
            return settings_init_egl(s, app.cfg, app.renderer, app.egl_display,
                                     app.egl_config, app.egl_context, [&app] {
                                         std::vector<std::string> names;
                                         for (const auto &mon : app.outputs)
                                             names.push_back(mon->output.name);
                                         return names;
                                     });
        });
    if (bound)
        app.settings_bound_output = bound;
    else
        app.settings_enabled = false;

    if (!app.outputs.empty())
        eglMakeCurrent(app.egl_display, app.outputs.front()->egl_surface,
                       app.outputs.front()->egl_surface, app.egl_context);
}

} // namespace bar_detail

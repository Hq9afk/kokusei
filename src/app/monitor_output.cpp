#include <algorithm>

#include "app/module_registry.h"
#include "app/monitor_output.h"

#include "core/log.h"

#include "modules/bar.h"
#include "modules/settings.h"

#include "render/overlay_panel.h"

#include "service/settings_service.h"

void monitor_output_destroy(MonitorOutput &mon) {
    for (auto &m : mon.modules)
        m->destroy(*mon.app, mon);
    if (mon.output.wl)
        wl_output_release(mon.output.wl);
}

MonitorOutput *find_monitor_by_name_wl(WaylandState &app, wl_output *wl) {
    for (auto &mon : app.outputs)
        if (mon->output.wl == wl)
            return mon.get();
    return nullptr;
}

MonitorOutput *find_monitor_for_surface(WaylandState &app,
                                        wl_surface *surface) {
    if (!surface)
        return nullptr;
    for (auto &mon : app.outputs)
        for (auto &m : mon->modules)
            if (m->owns_surface(surface))
                return mon.get();
    return nullptr;
}

void monitor_output_create_surfaces(WaylandState &app, MonitorOutput &mon) {
    mon.modules = build_per_monitor_modules();
    for (auto &m : mon.modules)
        m->create_surface(app, mon, mon.output.wl);
}

void monitor_output_wait_configured(WaylandState &app, MonitorOutput &mon) {
    for (;;) {
        bool all_configured = true;
        for (auto &m : mon.modules)
            if (!m->configured())
                all_configured = false;
        if (all_configured)
            return;
        wl_display_dispatch(app.display);
    }
}

void monitor_output_finish_egl(WaylandState &app, MonitorOutput &mon) {
    for (auto &m : mon.modules)
        m->init_egl(app, mon);
}

void monitor_output_activate(WaylandState &app, MonitorOutput &mon) {
    if (mon.activated)
        return;
    klog("output: activating '%s'", mon.output.name.c_str());
    monitor_output_create_surfaces(app, mon);
    monitor_output_wait_configured(app, mon);
    monitor_output_finish_egl(app, mon);
    mon.activated = true;
}

void request_all_frames(MonitorOutput &mon) {
    for (auto &m : mon.modules)
        m->request_frame();
}

namespace bar_detail {

void rest_egl_current(WaylandState &app) {
    if (!app.outputs.empty())
        eglMakeCurrent(app.egl_display, app.outputs.front()->egl_surface,
                       app.outputs.front()->egl_surface, app.egl_context);
}

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
        if (auto *wp = mon->module<WallpaperPerMonitorModule>())
            wp->resync(app, *mon, new_cfg);

        bool new_autohide =
            autohide_effective_enabled(new_cfg, mon->output.name);
        if (new_autohide != mon->autohide.enabled)
            monitor_autohide_apply(*mon, new_autohide);

        if (auto *nv = mon->module<NotificationViewPerMonitorModule>())
            nv->resync(app, *mon);
    }

    app.cfg = new_cfg;
    for (auto &mon : app.outputs)
        for (auto &m : mon->modules)
            m->request_frame();
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

void settings_retarget(WaylandState &app, SettingsState &settings,
                       MonitorOutput &target) {
    SettingsState &s = settings;
    wl_output *bound = overlay_panel_retarget(
        s.base, app.display, app.settings_bound_output, target.output.wl,
        target.output.name.c_str(),
        [&](wl_output *out) {
            return settings_create_surface(s, app.compositor, app.layer_shell,
                                           out);
        },
        [&] {
            return settings_init_egl(
                s, app.cfg, app.renderer, app.egl_display, app.egl_config,
                app.egl_context,
                [&app] {
                    std::vector<std::string> names;
                    for (const auto &mon : app.outputs)
                        names.push_back(mon->output.name);
                    return names;
                },
                [&app] {
                    return app.compositor_backend ==
                                   WaylandState::CompositorBackend::Hyprland
                               ? app.hypr.focused_monitor
                               : std::string();
                },
                [&app](const std::string &name,
                       int column) -> WallpaperHwDecodeStatus {
                    for (auto &mon : app.outputs) {
                        if (mon->output.name != name)
                            continue;
                        if (auto *wp = mon->module<WallpaperPerMonitorModule>())
                            return wp->decode_status(column);
                    }
                    return WallpaperHwDecodeStatus::Idle;
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

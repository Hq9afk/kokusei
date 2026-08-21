#include <filesystem>

#include "app/module_registry.h"
#include "app/monitor_output.h"
#include "app/wayland_state.h"

#include "config/bar_config.h"

#include "modules/bar.h"
#include "modules/dashboard.h"
#include "modules/launcher.h"
#include "modules/matrix.h"
#include "modules/notification.h"
#include "modules/osd.h"
#include "modules/overview.h"
#include "modules/settings.h"
#include "modules/starward.h"
#include "modules/visualizer.h"
#include "modules/wallpaper.h"

#include "render/image.h"

#include "service/layer_surface.h"
#include "service/mpris_service.h"
#include "service/wallpaper_service.h"

namespace {

class LauncherModule final : public Module {
  public:
    const char *name() const override { return "launcher"; }
    bool is_open() const override { return state_.open; }

    bool create_surface(WaylandState &app, wl_output *output) override {
        output_ = output;
        want_ = launcher_create_surface(state_, app.compositor, app.layer_shell,
                                        output);
        return want_;
    }

    bool init_egl(WaylandState &app) override {
        if (!launcher_init_egl(state_, app.renderer, app.egl_display,
                               app.egl_config, app.egl_context))
            return false;
        state_.bound_output = output_;
        request_frame();
        return true;
    }

    bool configured() const override { return !want_ || state_.configured; }
    wl_surface *surface() const override { return state_.surface; }
    void request_frame() override { launcher_request_frame(state_); }

    bool tick() override {
        launcher_search_start_pending(state_);
        return launcher_tick(state_);
    }
    int poll_timeout_ms() const override {
        return launcher_poll_timeout_ms(state_);
    }
    bool timer_tick(WaylandState &) override {
        if (!state_.open)
            return false;
        state_.cursor_blink_visible = !state_.cursor_blink_visible;
        request_frame();
        return true;
    }

    void handle_click(WaylandState &, double x, double y) override {
        launcher_handle_click(state_, x, y);
    }
    void handle_key_event(WaylandState &, const KeyEvent &event) override {
        launcher_handle_key_event(state_, event);
    }

    std::vector<IpcHandler> ipc_handlers(WaylandState &app) override {
        auto toggle_retargeted = [this, &app](bool global) {
            if (!state_.open) {
                MonitorOutput *target = bar_detail::active_target_monitor(app);
                if (target && (target->output.wl != state_.bound_output ||
                               !state_.layer_surface))
                    launcher_retarget(state_, app.compositor, app.layer_shell,
                                      app.display, app.renderer,
                                      app.egl_display, app.egl_config,
                                      app.egl_context, target->output.wl,
                                      target->output.name.c_str());
            }
            launcher_toggle(state_, global);
        };
        return {
            {"launcher", [toggle_retargeted] { toggle_retargeted(false); },
             "toggle the launcher, searching from $HOME"},
            {"launcher global",
             [toggle_retargeted] { toggle_retargeted(true); },
             "toggle the launcher, searching from /"},
        };
    }

    std::vector<std::pair<int, std::function<void()>>>
    extra_poll_sources(WaylandState &app) override {
        std::vector<std::pair<int, std::function<void()>>> sources;
        auto dispatch = [this, &app] {
            if (launcher_search_poll(state_)) {
                request_frame();
                bar_detail::rest_egl_current(app);
            }
        };
        if (state_.search_dirs_proc.wake_fd >= 0)
            sources.push_back({state_.search_dirs_proc.wake_fd, dispatch});
        if (state_.search_files_proc.wake_fd >= 0)
            sources.push_back({state_.search_files_proc.wake_fd, dispatch});
        return sources;
    }

  private:
    LauncherState state_;
    wl_output *output_ = nullptr;
    bool want_ = false;
};

class StarwardModule final : public Module {
  public:
    const char *name() const override { return "starward"; }
    bool is_open() const override { return state_.base.open; }

    bool create_surface(WaylandState &app, wl_output *output) override {
        output_ = output;
        want_ = starward_create_surface(state_, app.compositor, app.layer_shell,
                                        output);
        return want_;
    }

    bool init_egl(WaylandState &app) override {
        if (!starward_init_egl(state_, app.renderer, app.egl_display,
                               app.egl_config, app.egl_context))
            return false;
        state_.bound_output = output_;
        request_frame();

        const char *logo_candidates[] = {KOKUSEI_STARWARD_LOGO,
                                         "assets/logo.png"};
        std::string logo_path = logo_candidates[1];
        for (const char *candidate : logo_candidates) {
            if (std::filesystem::exists(candidate)) {
                logo_path = candidate;
                break;
            }
        }
        state_.logo_tex = load_image_texture(logo_path);
        return true;
    }

    bool configured() const override {
        return !want_ || state_.base.configured;
    }
    wl_surface *surface() const override { return state_.base.surface; }
    void request_frame() override { starward_request_frame(state_); }

    bool timer_tick(WaylandState &) override { return false; }

    void handle_pointer_move(WaylandState &, wl_surface *focused_surface,
                             double x, double y) override {
        if (!state_.base.open)
            return;
        if (focused_surface == state_.base.surface)
            starward_handle_hover(state_, x, y);
        else
            starward_clear_hover(state_);
        request_frame();
    }

    void handle_click(WaylandState &, double x, double y) override {
        starward_handle_click(state_, x, y);
    }
    void handle_key_event(WaylandState &, const KeyEvent &event) override {
        starward_handle_key_event(state_, event);
    }

    std::vector<IpcHandler> ipc_handlers(WaylandState &app) override {
        return starward_ipc_handlers(state_, app);
    }

    bool opened_by_widget() const override { return state_.opened_by_widget; }
    void toggle_from_widget(WaylandState &app) override {
        if (!state_.base.open) {
            MonitorOutput *target = bar_detail::active_target_monitor(app);
            if (target && (target->output.wl != state_.bound_output ||
                           !state_.base.layer_surface))
                starward_retarget(state_, app.compositor, app.layer_shell,
                                  app.display, app.renderer, app.egl_display,
                                  app.egl_config, app.egl_context,
                                  target->output.wl,
                                  target->output.name.c_str());
        }
        starward_toggle(state_, true);
    }

  private:
    StarwardState state_;
    wl_output *output_ = nullptr;
    bool want_ = false;
};

class DashboardModule final : public Module {
  public:
    const char *name() const override { return "dashboard"; }
    bool is_open() const override { return state_.base.open; }

    bool create_surface(WaylandState &app, wl_output *output) override {
        output_ = output;
        want_ = dashboard_create_surface(state_, app.compositor,
                                         app.layer_shell, output);
        return want_;
    }

    bool init_egl(WaylandState &app) override {
        if (!dashboard_init_egl(state_, app.renderer, app, app.egl_display,
                                app.egl_config, app.egl_context))
            return false;
        state_.bound_output = output_;
        request_frame();
        return true;
    }

    bool configured() const override {
        return !want_ || state_.base.configured;
    }
    wl_surface *surface() const override { return state_.base.surface; }
    void request_frame() override {
        dashboard_request_frame(state_,
                                static_cast<float>(bar_detail::kBarHeight),
                                static_cast<float>(bar_detail::kBarTopMargin));
    }

    bool timer_tick(WaylandState &app) override {
        if (!state_.base.open)
            return false;
        ++poll_tick_;
        if (poll_tick_ % 2 == 0) {
            cpu_temp_poll(app.cpu_temp);
            system_stats_poll(app.system_stats);
        }
        if (poll_tick_ % 5 == 0)
            gpu_temp_poll(app.gpu_temp);
        mpris_poll_position(app.mpris);
        request_frame();
        return true;
    }

    void handle_pointer_move(WaylandState &app, wl_surface *, double x,
                             double) override {
        if (!state_.dragging)
            return;
        dashboard_handle_pointer_move(state_, app.pipewire, x);
        request_frame();
    }

    void handle_pointer_release() override {
        if (state_.dragging)
            state_.dragging.reset();
    }

    void handle_click(WaylandState &app, double x, double y) override {
        dashboard_handle_click(state_, app, x, y);
    }
    void handle_key_event(WaylandState &app, const KeyEvent &event) override {
        dashboard_handle_key_event(state_, app.pipewire, event);
    }
    void handle_scroll(WaylandState &, double dy) override {
        dashboard_handle_scroll(state_, dy);
        request_frame();
    }

    std::vector<IpcHandler> ipc_handlers(WaylandState &app) override {
        return dashboard_ipc_handlers(state_, app);
    }

    bool opened_by_widget() const override { return state_.opened_by_widget; }
    void toggle_from_widget(WaylandState &app) override {
        if (!state_.base.open) {
            MonitorOutput *target = bar_detail::active_target_monitor(app);
            if (target && (target->output.wl != state_.bound_output ||
                           !state_.base.layer_surface))
                dashboard_retarget(state_, app.compositor, app.layer_shell,
                                   app.display, app.renderer, app,
                                   app.egl_display, app.egl_config,
                                   app.egl_context, target->output.wl,
                                   target->output.name.c_str());
        }
        dashboard_toggle(state_, true);
    }

  private:
    DashboardState state_;
    wl_output *output_ = nullptr;
    bool want_ = false;
    int poll_tick_ = 0;
};

class OverviewModule final : public Module {
  public:
    const char *name() const override { return "overview"; }
    bool is_open() const override { return state_.base.open; }

    bool create_surface(WaylandState &app, wl_output *output) override {
        output_ = output;
        want_ = overview_create_surface(state_, app.compositor, app.layer_shell,
                                        output);
        return want_;
    }

    bool init_egl(WaylandState &app) override {
        if (!overview_init_egl(state_, app.renderer, app.egl_display,
                               app.egl_config, app.egl_context))
            return false;
        state_.bound_output = output_;
        state_.app_ptr = &app;
        return true;
    }

    bool configured() const override {
        return !want_ || state_.base.configured;
    }
    wl_surface *surface() const override { return state_.base.surface; }
    void request_frame() override { overview_request_frame(state_); }

    void handle_pointer_move(WaylandState &app, wl_surface *, double x,
                             double y) override {
        overview_handle_pointer_move(state_, app, x, y);
    }
    void handle_pointer_release() override {
        if (state_.app_ptr)
            overview_handle_pointer_release(state_, *state_.app_ptr);
    }

    void handle_click(WaylandState &app, double x, double y) override {
        overview_handle_click(state_, app, x, y);
        request_frame();
    }
    void handle_key_event(WaylandState &app, const KeyEvent &event) override {
        overview_handle_key_event(state_, app, event);
    }

    std::vector<IpcHandler> ipc_handlers(WaylandState &app) override {
        return overview_ipc_handlers(state_, app);
    }

    bool opened_by_widget() const override { return state_.opened_by_widget; }
    void toggle_from_widget(WaylandState &app) override {
        if (!state_.base.open) {
            MonitorOutput *target = bar_detail::active_target_monitor(app);
            if (target && (target->output.wl != state_.bound_output ||
                           !state_.base.layer_surface))
                overview_retarget(state_, app.compositor, app.layer_shell,
                                  app.display, app.renderer, app.egl_display,
                                  app.egl_config, app.egl_context,
                                  target->output.wl,
                                  target->output.name.c_str());
        }
        overview_toggle(state_, app, true);
    }

  private:
    OverviewState state_;
    wl_output *output_ = nullptr;
    bool want_ = false;
};

class SettingsModule final : public Module {
  public:
    const char *name() const override { return "settings"; }
    bool is_open() const override { return state_.base.open; }

    bool create_surface(WaylandState &app, wl_output *output) override {
        output_ = output;
        want_ = settings_create_surface(state_, app.compositor, app.layer_shell,
                                        output);
        return want_;
    }

    bool init_egl(WaylandState &app) override {
        if (!settings_init_egl(
                state_, app.cfg, app.renderer, app.egl_display, app.egl_config,
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
                }))
            return false;
        app.settings_bound_output = output_;
        app.settings_enabled = true;
        return true;
    }

    bool configured() const override {
        return !want_ || state_.base.configured;
    }
    wl_surface *surface() const override { return state_.base.surface; }
    void request_frame() override { settings_request_frame(state_); }

    bool timer_tick(WaylandState &) override {
        if (state_.focused_field == SettingsFieldId::None)
            return false;
        state_.field_buffer.cursor_blink_visible =
            !state_.field_buffer.cursor_blink_visible;
        request_frame();
        return true;
    }

    void handle_click(WaylandState &app, double x, double y) override {
        settings_handle_click(
            state_, app.cfg,
            [&app](Config c) {
                bar_detail::save_and_apply_config_update(app, c);
            },
            x, y);
    }
    void handle_pointer_move(WaylandState &, wl_surface *focused_surface,
                             double x, double y) override {
        hovering_clickable_ = state_.base.open &&
                              focused_surface == state_.base.surface &&
                              settings_point_is_clickable(state_, x, y);
    }
    bool wants_pointing_hand_cursor() const override {
        return hovering_clickable_;
    }
    void handle_key_event(WaylandState &app, const KeyEvent &event) override {
        settings_handle_key_event(
            state_, app.cfg,
            [&app](Config c) {
                bar_detail::save_and_apply_config_update(app, c);
            },
            event);
    }
    void handle_scroll(WaylandState &, double dy) override {
        settings_handle_scroll(state_, dy);
    }

    std::vector<IpcHandler> ipc_handlers(WaylandState &app) override {
        return settings_ipc_handlers(state_, app);
    }

  private:
    SettingsState state_;
    wl_output *output_ = nullptr;
    bool want_ = false;
    bool hovering_clickable_ = false;
};

class MatrixModule final : public Module {
  public:
    const char *name() const override { return "matrix"; }
    bool is_open() const override { return state_.base.open; }

    bool create_surface(WaylandState &, wl_output *) override { return true; }
    bool init_egl(WaylandState &) override { return true; }
    bool configured() const override { return true; }
    wl_surface *surface() const override { return state_.base.surface; }
    void request_frame() override { matrix_request_frame(state_); }

    void handle_key_event(WaylandState &app, const KeyEvent &event) override {
        matrix_handle_key_event(state_, app, event);
    }

    std::vector<IpcHandler> ipc_handlers(WaylandState &app) override {
        return matrix_ipc_handlers(state_, app);
    }

  private:
    MatrixState state_;
};

class VisualizerModule final : public Module {
  public:
    ~VisualizerModule() override { visualizer_shutdown(state_); }

    const char *name() const override { return "visualizer"; }
    bool is_open() const override { return state_.base.open; }

    bool create_surface(WaylandState &, wl_output *) override { return true; }
    bool init_egl(WaylandState &) override { return true; }
    bool configured() const override { return true; }
    wl_surface *surface() const override { return state_.base.surface; }
    void request_frame() override { visualizer_request_frame(state_); }

    void handle_key_event(WaylandState &app, const KeyEvent &event) override {
        visualizer_handle_key_event(state_, app, event);
    }

    std::vector<IpcHandler> ipc_handlers(WaylandState &app) override {
        return visualizer_ipc_handlers(state_, app);
    }

  private:
    VisualizerState state_;
};

std::vector<FillMode> resolve_wallpaper_fill_modes(const Config &cfg,
                                                   const std::string &name) {
    if (cfg.wallpaper_animated_enabled) {
        int count = wallpaper_service_animated_column_count(cfg, name);
        std::vector<FillMode> modes(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            std::string mode =
                wallpaper_service_animated_fill_mode(cfg, name, i);
            modes[static_cast<size_t>(i)] =
                mode == "fit" ? FillMode::Fit : FillMode::Crop;
        }
        return modes;
    }
    int count = wallpaper_service_column_count(cfg, name);
    std::vector<FillMode> modes(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        std::string mode = wallpaper_service_fill_mode(cfg, name, i);
        modes[static_cast<size_t>(i)] =
            mode == "fit" ? FillMode::Fit : FillMode::Crop;
    }
    return modes;
}

void wallpaper_sync_active_mode(WallpaperState &wp, const Config &cfg,
                                const std::string &monitor_name) {
    if (cfg.wallpaper_animated_enabled) {
        wallpaper_animate_sync_from_config(wp, cfg, monitor_name);
    } else {
        if (!wp.column_animations.empty())
            wallpaper_animate_stop_all(wp);
        wallpaper_sync_from_config(wp, cfg, monitor_name);
    }
}

} // namespace

bool OsdPerMonitorModule::create_surface(WaylandState &app, MonitorOutput &mon,
                                         wl_output *output) {
    if (!osd_create_surface(state_, app.compositor, app.layer_shell, output))
        klog("osd: failed to create layer surface on '%s'",
             mon.output.name.c_str());
    return true;
}

bool OsdPerMonitorModule::configured() const {
    return !state_.layer_surface || state_.configured;
}

bool OsdPerMonitorModule::init_egl(WaylandState &app, MonitorOutput &mon) {
    if (state_.layer_surface &&
        osd_init_egl(state_, app.renderer, app.egl_display, app.egl_config,
                     app.egl_context))
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    return true;
}

void OsdPerMonitorModule::destroy(WaylandState &app, MonitorOutput &) {
    destroy_layer_surface(app.egl_display, state_.surface, state_.layer_surface,
                          state_.egl_window, state_.egl_surface);
}

bool OsdPerMonitorModule::owns_surface(wl_surface *surface) const {
    return surface == state_.surface;
}

void OsdPerMonitorModule::tick(WaylandState &, MonitorOutput &) {
    if (state_.visible && std::chrono::steady_clock::now() >= state_.hide_at)
        osd_hide(state_);
}

bool WallpaperPerMonitorModule::create_surface(WaylandState &app,
                                               MonitorOutput &mon,
                                               wl_output *output) {
    if (!wallpaper_create_surface(state_, app.compositor, app.layer_shell,
                                  output))
        klog("wallpaper: failed to create layer surface on '%s'",
             mon.output.name.c_str());
    return true;
}

bool WallpaperPerMonitorModule::configured() const {
    return !state_.layer_surface || state_.configured;
}

bool WallpaperPerMonitorModule::init_egl(WaylandState &app,
                                         MonitorOutput &mon) {
    if (!state_.layer_surface)
        return true;
    if (!wallpaper_init_egl(state_, app.renderer, app.egl_display,
                            app.egl_config, app.egl_context))
        return true;
    state_.column_fill_modes =
        resolve_wallpaper_fill_modes(app.cfg, mon.output.name);
    wallpaper_sync_active_mode(state_, app.cfg, mon.output.name);
    state_.on_resize = [&app, &mon, this] {
        if (!app.cfg.wallpaper_animated_enabled)
            return;
        wallpaper_animate_stop_all(state_);
        wallpaper_animate_sync_from_config(state_, app.cfg, mon.output.name);
    };
    wallpaper_request_frame(state_);
    eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                   app.egl_context);
    return true;
}

void WallpaperPerMonitorModule::destroy(WaylandState &app, MonitorOutput &) {
    wallpaper_animate_stop_all(state_);
    destroy_layer_surface(app.egl_display, state_.surface, state_.layer_surface,
                          state_.egl_window, state_.egl_surface);
}

bool WallpaperPerMonitorModule::owns_surface(wl_surface *surface) const {
    return surface == state_.surface;
}

void WallpaperPerMonitorModule::pause_animation() {
    wallpaper_animate_pause_all(state_);
}

void WallpaperPerMonitorModule::resume_animation() {
    wallpaper_animate_resume_all(state_);
}

WallpaperHwDecodeStatus
WallpaperPerMonitorModule::decode_status(int column_index) const {
    return wallpaper_animate_decode_status(state_, column_index);
}

void WallpaperPerMonitorModule::resync(WaylandState &, MonitorOutput &mon,
                                       const Config &new_cfg) {
    wallpaper_sync_active_mode(state_, new_cfg, mon.output.name);
    std::vector<FillMode> new_modes =
        resolve_wallpaper_fill_modes(new_cfg, mon.output.name);
    if (new_modes != state_.column_fill_modes) {
        state_.column_fill_modes = new_modes;
        wallpaper_request_frame(state_);
    }
}

bool NotificationViewPerMonitorModule::create_surface(WaylandState &app,
                                                      MonitorOutput &mon,
                                                      wl_output *output) {
    if (notifications_effective_enabled(app.cfg, mon.output.name) &&
        !notification_view_create_surface(state_, app.compositor,
                                          app.layer_shell, output))
        klog("notification: failed to create layer surface on '%s'",
             mon.output.name.c_str());
    return true;
}

bool NotificationViewPerMonitorModule::configured() const {
    return !state_.layer_surface || state_.configured;
}

bool NotificationViewPerMonitorModule::init_egl(WaylandState &app,
                                                MonitorOutput &mon) {
    if (state_.layer_surface &&
        notification_view_init_egl(state_, app.notification, app.renderer,
                                   app.egl_display, app.egl_config,
                                   app.egl_context))
        eglMakeCurrent(app.egl_display, mon.egl_surface, mon.egl_surface,
                       app.egl_context);
    return true;
}

void NotificationViewPerMonitorModule::destroy(WaylandState &app,
                                               MonitorOutput &) {
    destroy_layer_surface(app.egl_display, state_.surface, state_.layer_surface,
                          state_.egl_window, state_.egl_surface);
}

bool NotificationViewPerMonitorModule::owns_surface(wl_surface *surface) const {
    return surface == state_.surface;
}

void NotificationViewPerMonitorModule::request_frame() {
    notification_view_request_frame(state_);
}

void NotificationViewPerMonitorModule::resync(WaylandState &app,
                                              MonitorOutput &mon) {
    bool want = notifications_effective_enabled(app.cfg, mon.output.name);
    bool have = state_.layer_surface != nullptr;
    if (want && !have) {
        if (notification_view_create_surface(state_, app.compositor,
                                             app.layer_shell, mon.output.wl)) {
            while (!state_.configured)
                wl_display_dispatch(app.display);
            if (notification_view_init_egl(state_, app.notification,
                                           app.renderer, app.egl_display,
                                           app.egl_config, app.egl_context))
                eglMakeCurrent(app.egl_display, mon.egl_surface,
                               mon.egl_surface, app.egl_context);
        }
    } else if (!want && have) {
        destroy_layer_surface(app.egl_display, state_.surface,
                              state_.layer_surface, state_.egl_window,
                              state_.egl_surface);
        state_.configured = false;
    }
}

std::vector<std::unique_ptr<Module>> build_app_modules() {
    std::vector<std::unique_ptr<Module>> modules;
    modules.push_back(std::make_unique<LauncherModule>());
    modules.push_back(std::make_unique<StarwardModule>());
    modules.push_back(std::make_unique<DashboardModule>());
    modules.push_back(std::make_unique<OverviewModule>());
    modules.push_back(std::make_unique<SettingsModule>());
    modules.push_back(std::make_unique<MatrixModule>());
    modules.push_back(std::make_unique<VisualizerModule>());
    return modules;
}

std::vector<std::unique_ptr<PerMonitorModule>> build_per_monitor_modules() {
    std::vector<std::unique_ptr<PerMonitorModule>> modules;
    modules.push_back(std::make_unique<BarPerMonitorModule>());
    modules.push_back(std::make_unique<WallpaperPerMonitorModule>());
    modules.push_back(std::make_unique<OsdPerMonitorModule>());
    modules.push_back(std::make_unique<NotificationViewPerMonitorModule>());
    return modules;
}

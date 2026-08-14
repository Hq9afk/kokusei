#include "app/config.h"
#include "app/ipc.h"
#include "app/single_instance_lock.h"
#include "bar/bar.h"
#include "bar/widget/control_center_widget.h"
#include "bar/widget/volume_widget.h"
#include "core/deferred_call.h"
#include "core/log.h"
#include "core/poll_source.h"
#include "core/sdbus_poll_source.h"
#include "idle/idle.h"
#include "notification/notification.h"
#include "render/image.h"
#include "service/hyprland.h"
#include "service/layer_surface.h"
#include "service/shojiwm.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <vector>

inline void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        perror("kokusei: fork");
        exit(1);
    }
    if (pid > 0)
        _exit(0);
    setsid();
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
}

inline MonitorOutput *find_monitor_for_surface(WaylandState &app,
                                               wl_surface *surface) {
    for (auto &mon : app.outputs) {
        if (surface == mon->surface ||
            surface == mon->tray_panel.base.surface ||
            surface == mon->tray_menu.base.surface ||
            surface == mon->network_panel.base.surface ||
            surface == mon->bluetooth_panel.base.surface ||
            surface == mon->volume_panel.base.surface)
            return mon.get();
    }
    return nullptr;
}

int main(int argc, char **argv) {

    bool want_daemonize = argc == 1;
    bool want_debug = argc > 1 && strcmp(argv[1], "debug") == 0;
    if (argc > 1 && !want_daemonize && !want_debug)
        return run_ipc_client(argc, argv);

    if (!single_instance_try_acquire()) {
        fprintf(stderr, "kokusei: already running\n");
        return 1;
    }
    if (want_daemonize)
        daemonize();

    WaylandState app;
    app.cfg = load_config();
    app.config_watch_fd = config_watch_init(config_path());
    DeferredCall::init();

    app.display = wl_display_connect(nullptr);
    if (!app.display) {
        klog("failed to connect to Wayland display");
        return 1;
    }

    wl_registry *registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    wl_display_roundtrip(app.display);

    if (!app.compositor || !app.layer_shell) {
        klog("compositor is missing wl_compositor or zwlr_layer_shell_v1");
        return 1;
    }
    if (app.outputs.empty()) {
        klog("no wl_output advertised by the compositor");
        return 1;
    }

    if (!bootstrap_egl(app)) {
        klog("EGL init failed");
        return 1;
    }

    MonitorOutput &first = *app.outputs.front();
    monitor_output_create_surfaces(app, first);
    monitor_output_wait_configured(app, first);
    if (!bar_init_egl(first, app.renderer, app.egl_display, app.egl_config,
                      app.egl_context)) {
        klog("EGL init failed");
        return 1;
    }
    if (!app.renderer.init()) {
        klog("renderer init failed");
        return 1;
    }
    monitor_output_finish_egl(app, first);
    first.activated = true;

    bool want_launcher = launcher_create_surface(
        app.launcher, app.compositor, app.layer_shell, first.output.wl);
    if (!want_launcher)
        klog("launcher: failed to create layer surface");

    bool want_starward = starward_create_surface(
        app.starward, app.compositor, app.layer_shell, first.output.wl);
    if (!want_starward)
        klog("starward: failed to create layer surface");

    bool want_controlcenter = controlcenter_create_surface(
        app.controlcenter, app.compositor, app.layer_shell, first.output.wl);
    if (!want_controlcenter)
        klog("controlcenter: failed to create layer surface");

    bool want_settings = settings_create_surface(
        app.settings, app.compositor, app.layer_shell, first.output.wl);
    if (!want_settings)
        klog("settings: failed to create layer surface");

    while (!((!want_launcher || app.launcher.configured) &&
             (!want_starward || app.starward.base.configured) &&
             (!want_controlcenter || app.controlcenter.base.configured) &&
             (!want_settings || app.settings.base.configured))) {
        wl_display_dispatch(app.display);
    }

    if (want_launcher) {
        if (!launcher_init_egl(app.launcher, app.renderer, app.egl_display,
                               app.egl_config, app.egl_context)) {
            klog("launcher: EGL surface init failed");
            want_launcher = false;
        } else {
            app.launcher.bound_output = first.output.wl;
            launcher_request_frame(app.launcher);
            eglMakeCurrent(app.egl_display, first.egl_surface,
                           first.egl_surface, app.egl_context);
        }
    }
    if (want_starward) {
        if (!starward_init_egl(app.starward, app.renderer, app.egl_display,
                               app.egl_config, app.egl_context)) {
            klog("starward: EGL surface init failed");
            want_starward = false;
        } else {
            app.starward.bound_output = first.output.wl;
            starward_request_frame(app.starward);
            eglMakeCurrent(app.egl_display, first.egl_surface,
                           first.egl_surface, app.egl_context);

            const char *logo_candidates[] = {KOKUSEI_STARWARD_LOGO,
                                             "assets/logo.png"};
            std::string logo_path = logo_candidates[1];
            for (const char *candidate : logo_candidates) {
                if (std::filesystem::exists(candidate)) {
                    logo_path = candidate;
                    break;
                }
            }
            app.starward.logo_tex = load_image_texture(logo_path);
        }
    }
    if (want_controlcenter) {
        if (!controlcenter_init_egl(app.controlcenter, app.renderer, app,
                                    app.egl_display, app.egl_config,
                                    app.egl_context)) {
            klog("controlcenter: EGL surface init failed");
            want_controlcenter = false;
        } else {
            app.controlcenter.bound_output = first.output.wl;
            controlcenter_request_frame(
                app.controlcenter, static_cast<float>(bar_detail::kBarHeight),
                static_cast<float>(bar_detail::kBarTopMargin));
            eglMakeCurrent(app.egl_display, first.egl_surface,
                           first.egl_surface, app.egl_context);
        }
    }
    if (want_settings) {
        if (!settings_init_egl(app.settings, app.cfg, app.renderer,
                               app.egl_display, app.egl_config, app.egl_context,
                               [&app] {
                                   std::vector<std::string> names;
                                   for (const auto &mon : app.outputs)
                                       names.push_back(mon->output.name);
                                   return names;
                               })) {
            klog("settings: EGL surface init failed");
            want_settings = false;
        } else {
            eglMakeCurrent(app.egl_display, first.egl_surface,
                           first.egl_surface, app.egl_context);
            app.settings_bound_output = first.output.wl;
        }
    }
    app.settings_enabled = want_settings;

    for (size_t i = 1; i < app.outputs.size(); ++i)
        monitor_output_activate(app, *app.outputs[i]);

    bool want_notification = notification_init(app.notification, [&app] {
        for (auto &mon : app.outputs)
            notification_view_request_frame(mon->notification_view);
    });
    if (!want_notification)
        klog("notification: D-Bus registration failed");

    brightness_init(app.brightness);
    app.brightness_watch_fd = brightness_watch_init(app.brightness);
    pipewire_init(app.pipewire);
    upower_init(app.upower);
    bool want_network =
        app.upower.bus && network_init(app.network, *app.upower.bus);
    if (!want_network)
        klog("network: no system bus available - network info unavailable");
    bool want_bluetooth =
        app.upower.bus && bluetooth_init(app.bluetooth, *app.upower.bus);
    if (!want_bluetooth)
        klog("bluetooth: no system bus available - bluetooth info unavailable");
    bool want_tray = tray_init(app.tray);
    if (!want_tray)
        klog("tray: no session bus available - system tray unavailable");
    bool want_mpris = mpris_init(app.mpris);
    if (!want_mpris)
        klog("mpris: no session bus available - media info unavailable");
    cpu_temp_init(app.cpu_temp);
    gpu_temp_init(app.gpu_temp);

    for (auto &mon : app.outputs) {
        update_clock(*mon);
        init_stub_widgets(*mon);
    }

    if (shoji_init(app.shoji)) {
        app.compositor_backend = WaylandState::CompositorBackend::ShojiWM;
    } else if (hypr_init(app.hypr)) {
        app.compositor_backend = WaylandState::CompositorBackend::Hyprland;
    }
    klog("compositor backend: %s",
         app.compositor_backend == WaylandState::CompositorBackend::ShojiWM
             ? "shojiwm"
         : app.compositor_backend == WaylandState::CompositorBackend::Hyprland
             ? "hyprland"
             : "none");

    app.idle.timeout_seconds = app.cfg.idle_timeout_seconds;
    app.idle.on_idle_command = app.cfg.idle_command;
    app.idle.on_resume_command = app.cfg.idle_resume_command;
    idle_init(app.idle);

    int ipc_fd = open_ipc_socket();

    int timer_fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK);
    if (timer_fd >= 0) {
        itimerspec spec{};
        spec.it_value.tv_sec = 1;
        spec.it_interval.tv_sec = 1;
        timerfd_settime(timer_fd, 0, &spec, nullptr);
    } else {
        klog("timerfd_create: %s", strerror(errno));
    }

    int controlcenter_poll_tick = 0;

    klog("started: %zu monitor(s), bar height=%d, ipc_fd=%d, timer_fd=%d",
         app.outputs.size(), bar_detail::kBarHeight, ipc_fd, timer_fd);
    for (auto &mon : app.outputs)
        bar_request_frame(*mon);

    while (app.running) {
        wl_display_flush(app.display);

        std::vector<FnPollSource> fn_sources;

        auto rest_egl_current = [&app] {
            if (!app.outputs.empty())
                eglMakeCurrent(
                    app.egl_display, app.outputs.front()->egl_surface,
                    app.outputs.front()->egl_surface, app.egl_context);
        };

        auto settings_commit = [&app](Config c) {
            bar_detail::save_and_apply_config_update(app, c);
        };

        auto network_notify = [&app](const std::string &summary,
                                     const std::string &body) {
            notification_push(app.notification, "Network", summary, body, 6000);
        };

        auto network_dispatch = [&](bool changed) {
            if (!changed)
                return;
            for (auto &mon : app.outputs) {
                bar_request_frame(*mon);
                network_panel_request_frame(
                    mon->network_panel,
                    bar_detail::pill_center_x(mon->capsule, PillId::Wifi),
                    static_cast<float>(bar_detail::kBarHeight),
                    bar_detail::kBarTopMargin);
            }
            rest_egl_current();
        };

        auto bluetooth_notify = [&app](const std::string &summary,
                                       const std::string &body) {
            notification_push(app.notification, "Bluetooth", summary, body,
                              6000);
        };

        auto bluetooth_dispatch = [&] {
            for (auto &mon : app.outputs) {
                bar_request_frame(*mon);
                bluetooth_panel_request_frame(
                    mon->bluetooth_panel,
                    bar_detail::pill_center_x(mon->capsule, PillId::Bluetooth),
                    static_cast<float>(bar_detail::kBarHeight),
                    bar_detail::kBarTopMargin);
            }
            rest_egl_current();
        };

        auto volume_dispatch = [&] {
            for (auto &mon : app.outputs) {
                bar_request_frame(*mon);
                volume_panel_request_frame(
                    mon->volume_panel,
                    bar_detail::pill_center_x(mon->capsule, PillId::Volume),
                    static_cast<float>(bar_detail::kBarHeight),
                    bar_detail::kBarTopMargin);
            }
            rest_egl_current();
        };

        auto tray_dispatch = [&] {
            for (auto &mon : app.outputs) {
                bar_request_frame(*mon);
                tray_panel_request_frame(
                    mon->tray_panel,
                    bar_detail::pill_center_x(mon->capsule, PillId::Tray),
                    static_cast<float>(bar_detail::kBarHeight),
                    bar_detail::kBarTopMargin);
                tray_menu_request_frame(mon->tray_menu);
            }
            rest_egl_current();
        };

        if (ipc_fd >= 0) {
            fn_sources.emplace_back(ipc_fd, POLLIN, [&] {
                handle_ipc_accept(ipc_fd, app, app.idle, app.launcher,
                                  app.starward, app.controlcenter, app.running);
                if (want_launcher) {
                    launcher_request_frame(app.launcher);
                    rest_egl_current();
                }
                if (want_starward) {
                    starward_request_frame(app.starward);
                    rest_egl_current();
                }
                if (want_controlcenter) {
                    controlcenter_request_frame(
                        app.controlcenter,
                        static_cast<float>(bar_detail::kBarHeight),
                        static_cast<float>(bar_detail::kBarTopMargin));
                    rest_egl_current();
                }
                bar_paint(first);
                bluetooth_dispatch();
            });
        }

        if (timer_fd >= 0) {
            fn_sources.emplace_back(timer_fd, POLLIN, [&] {
                uint64_t expirations;
                read(timer_fd, &expirations, sizeof(expirations));
                for (auto &mon : app.outputs) {
                    update_clock(*mon);
                    bar_request_frame(*mon);
                    if (bar_detail::volume_pill_peek_expire(*mon))
                        bar_request_frame(*mon);
                }
                if (notification_sweep_expired(app.notification)) {
                    for (auto &mon : app.outputs)
                        notification_view_request_frame(mon->notification_view);
                }
                if (want_network) {
                    network_dispatch(network_tick(
                        app.network, std::chrono::steady_clock::now()));
                }
                if (want_bluetooth) {
                    bluetooth_tick(app.bluetooth, bluetooth_notify,
                                   std::chrono::steady_clock::now(),
                                   bluetooth_dispatch);
                }
                if (want_controlcenter && app.controlcenter.base.open) {
                    ++controlcenter_poll_tick;
                    if (controlcenter_poll_tick % 2 == 0) {
                        cpu_temp_poll(app.cpu_temp);
                        system_stats_poll(app.system_stats);
                    }
                    if (controlcenter_poll_tick % 5 == 0)
                        gpu_temp_poll(app.gpu_temp);
                    controlcenter_request_frame(
                        app.controlcenter,
                        static_cast<float>(bar_detail::kBarHeight),
                        static_cast<float>(bar_detail::kBarTopMargin));
                    rest_egl_current();
                }
                if (want_launcher && app.launcher.open) {
                    app.launcher.cursor_blink_visible =
                        !app.launcher.cursor_blink_visible;
                    launcher_request_frame(app.launcher);
                    rest_egl_current();
                }
                if (want_settings &&
                    app.settings.focused_field != SettingsFieldId::None) {
                    app.settings.field_buffer.cursor_blink_visible =
                        !app.settings.field_buffer.cursor_blink_visible;
                    settings_request_frame(app.settings);
                    rest_egl_current();
                }
            });
        }

        for (auto &mon : app.outputs) {
            if (!mon->osd.visible ||
                std::chrono::steady_clock::now() < mon->osd.hide_at)
                continue;
            osd_hide(mon->osd);
        }

        int compositor_fd = (app.compositor_backend ==
                             WaylandState::CompositorBackend::Hyprland)
                                ? app.hypr.event_fd
                            : (app.compositor_backend ==
                               WaylandState::CompositorBackend::ShojiWM)
                                ? app.shoji.fd
                                : -1;
        if (compositor_fd >= 0) {
            fn_sources.emplace_back(compositor_fd, POLLIN, [&] {
                auto redraw_all = [&app] {
                    for (auto &mon : app.outputs)
                        bar_request_frame(*mon);
                };
                if (app.compositor_backend ==
                    WaylandState::CompositorBackend::Hyprland) {
                    HyprEventResult r = hypr_poll_events(app.hypr);
                    if (r == HyprEventResult::Disconnected) {
                        close(app.hypr.event_fd);
                        app.hypr.event_fd = -1;
                        app.compositor_backend =
                            WaylandState::CompositorBackend::None;
                        klog("hyprland: event socket disconnected");
                    } else if (r == HyprEventResult::StructuralChanged) {
                        hypr_refresh(app.hypr);
                        redraw_all();
                    } else if (r == HyprEventResult::ActiveChanged) {
                        redraw_all();
                    }
                } else if (app.compositor_backend ==
                           WaylandState::CompositorBackend::ShojiWM) {
                    ShojiEventResult r = shoji_poll(app.shoji);
                    if (r == ShojiEventResult::Disconnected) {
                        close(app.shoji.fd);
                        app.shoji.fd = -1;
                        app.compositor_backend =
                            WaylandState::CompositorBackend::None;
                        klog("shojiwm: event socket disconnected");
                    } else if (r == ShojiEventResult::Updated) {
                        redraw_all();
                    }
                }
            });
        }

        if (app.notification.bus != nullptr) {
            fn_sources.push_back(sdbus_poll_source(*app.notification.bus, [&] {
                int budget = 32;
                while (budget-- > 0 &&
                       app.notification.bus->processPendingEvent()) {
                }
                for (auto &mon : app.outputs)
                    notification_view_request_frame(mon->notification_view);
            }));
        }

        if (app.upower.bus != nullptr) {
            fn_sources.push_back(sdbus_poll_source(*app.upower.bus, [&] {
                int budget = 32;
                while (budget-- > 0 && app.upower.bus->processPendingEvent()) {
                }
                if (app.upower.dirty) {
                    app.upower.dirty = false;
                    for (auto &mon : app.outputs)
                        bar_request_frame(*mon);
                }

                network_dispatch(app.network.dirty);
                app.network.dirty = false;
            }));
        }

        if (app.tray.bus != nullptr) {
            fn_sources.push_back(sdbus_poll_source(*app.tray.bus, [&] {
                int budget = 32;
                while (budget-- > 0 && app.tray.bus->processPendingEvent()) {
                }
                if (app.tray.dirty) {
                    app.tray.dirty = false;
                    tray_dispatch();
                }
            }));
        }

        if (app.mpris.bus != nullptr) {
            fn_sources.push_back(sdbus_poll_source(*app.mpris.bus, [&] {
                int budget = 32;
                while (budget-- > 0 && app.mpris.bus->processPendingEvent()) {
                }
                if (want_controlcenter && app.controlcenter.base.open) {
                    controlcenter_request_frame(
                        app.controlcenter,
                        static_cast<float>(bar_detail::kBarHeight),
                        static_cast<float>(bar_detail::kBarTopMargin));
                    rest_egl_current();
                }
            }));
        }

        if (app.network.device_proc.wake_fd >= 0)
            fn_sources.emplace_back(
                app.network.device_proc.wake_fd, POLLIN, [&] {
                    network_dispatch(
                        network_poll_device(app.network, network_notify));
                });
        if (app.network.profile_proc.wake_fd >= 0)
            fn_sources.emplace_back(
                app.network.profile_proc.wake_fd, POLLIN,
                [&] { network_dispatch(network_poll_profile(app.network)); });
        if (app.network.quick_scan_proc.wake_fd >= 0)
            fn_sources.emplace_back(
                app.network.quick_scan_proc.wake_fd, POLLIN, [&] {
                    network_dispatch(network_poll_quick_scan(app.network));
                });
        if (app.network.scan_proc.wake_fd >= 0)
            fn_sources.emplace_back(app.network.scan_proc.wake_fd, POLLIN, [&] {
                network_dispatch(
                    network_poll_scan(app.network, network_notify));
            });
        if (app.network.connect_proc.wake_fd >= 0)
            fn_sources.emplace_back(
                app.network.connect_proc.wake_fd, POLLIN, [&] {
                    network_dispatch(
                        network_poll_connect(app.network, network_notify));
                });
        if (app.network.disconnect_proc.wake_fd >= 0)
            fn_sources.emplace_back(
                app.network.disconnect_proc.wake_fd, POLLIN, [&] {
                    network_dispatch(
                        network_poll_disconnect(app.network, network_notify));
                });
        if (app.network.forget_proc.wake_fd >= 0)
            fn_sources.emplace_back(
                app.network.forget_proc.wake_fd, POLLIN,
                [&] { network_dispatch(network_poll_forget(app.network)); });
        if (app.network.connectivity_proc.wake_fd >= 0)
            fn_sources.emplace_back(
                app.network.connectivity_proc.wake_fd, POLLIN, [&] {
                    network_dispatch(
                        network_poll_connectivity(app.network, network_notify));
                });

        int pipewire_fd_value = pipewire_fd(app.pipewire);
        if (pipewire_fd_value >= 0) {
            fn_sources.emplace_back(pipewire_fd_value, POLLIN, [&] {
                PipewireChange change = pipewire_poll(app.pipewire);
                if (change.sink) {
                    bool muted = false;
                    float level = pipewire_sink_level(app.pipewire, muted);
                    for (auto &mon : app.outputs) {
                        if (osd_effective_enabled(app.cfg, mon->output.name)) {
                            osd_show(mon->osd, OsdKind::Volume, level, muted);
                            osd_request_frame(mon->osd);
                        }
                        bar_detail::volume_pill_peek_tick(*mon);
                    }
                }
                if (change.source) {
                    bool muted = false;
                    float level = pipewire_source_level(app.pipewire, muted);
                    for (auto &mon : app.outputs) {
                        if (!osd_effective_enabled(app.cfg, mon->output.name))
                            continue;
                        osd_show(mon->osd, OsdKind::Mic, level, muted);
                        osd_request_frame(mon->osd);
                    }
                }
                if (change.sink || change.source)
                    volume_dispatch();
            });
        }

        if (app.brightness_watch_fd >= 0) {
            fn_sources.emplace_back(app.brightness_watch_fd, POLLIN, [&] {
                if (brightness_watch_poll(app.brightness_watch_fd)) {
                    float level = brightness_get(app.brightness);
                    for (auto &mon : app.outputs) {
                        if (!osd_effective_enabled(app.cfg, mon->output.name))
                            continue;
                        osd_show(mon->osd, OsdKind::Brightness, level, false);
                        osd_request_frame(mon->osd);
                    }
                }
            });
        }

        if (DeferredCall::poll_fd() >= 0) {
            fn_sources.emplace_back(DeferredCall::poll_fd(), POLLIN,
                                    [] { DeferredCall::drain(); });
        }

        if (app.config_watch_fd >= 0) {
            fn_sources.emplace_back(app.config_watch_fd, POLLIN, [&] {
                ConfigWatchEvent ev = config_watch_poll(app.config_watch_fd);
                if (ev.removed) {
                    close(app.config_watch_fd);
                    app.config_watch_fd = config_watch_init(config_path());
                }
                if (!ev.changed)
                    return;
                if (app.config_own_write_pending) {

                    app.config_own_write_pending = false;
                    return;
                }
                bar_detail::apply_config_update(app, load_config());
            });
        }

        auto launcher_search_dispatch = [&] {
            if (launcher_search_poll(app.launcher) && want_launcher) {
                launcher_request_frame(app.launcher);
                rest_egl_current();
            }
        };
        if (app.launcher.search_dirs_proc.wake_fd >= 0)
            fn_sources.emplace_back(app.launcher.search_dirs_proc.wake_fd,
                                    POLLIN, launcher_search_dispatch);
        if (app.launcher.search_files_proc.wake_fd >= 0)
            fn_sources.emplace_back(app.launcher.search_files_proc.wake_fd,
                                    POLLIN, launcher_search_dispatch);

        if (app.keyboard.repeat_timer_fd >= 0) {

            fn_sources.emplace_back(app.keyboard.repeat_timer_fd, POLLIN, [&] {
                keyboard_repeat_tick(app.keyboard);
            });
        }

        std::vector<pollfd> fds;
        fds.push_back({.fd = wl_display_get_fd(app.display),
                       .events = POLLIN,
                       .revents = 0});
        struct SourceRange {
            PollSource *src;
            std::size_t start;
        };
        std::vector<SourceRange> ranges;
        for (FnPollSource &src : fn_sources) {
            std::size_t start = fds.size();
            if (src.add_poll_fds(fds) > 0)
                ranges.push_back({&src, start});
        }

        int poll_timeout_ms = launcher_poll_timeout_ms(app.launcher);
        if (poll(fds.data(), fds.size(), poll_timeout_ms) < 0)
            break;

        if (fds[0].revents & POLLIN) {
            wl_display_dispatch(app.display);

            if (app.pointer.dirty) {
                app.pointer.dirty = false;
                for (auto &mon : app.outputs)
                    bar_request_frame(*mon);
                if (app.pointer.focused_surface) {
                    if (MonitorOutput *m = find_monitor_for_surface(
                            app, app.pointer.focused_surface))
                        app.last_pointer_monitor = m;
                }
                MonitorOutput *dragging_mon = nullptr;
                for (auto &mon : app.outputs)
                    if (mon->volume_panel.dragging)
                        dragging_mon = mon.get();
                if (dragging_mon) {
                    volume_panel_handle_pointer_move(dragging_mon->volume_panel,
                                                     app.pipewire,
                                                     app.pointer.x);
                    volume_dispatch();
                }
            }
        }
        for (SourceRange &r : ranges)
            r.src->dispatch(fds, r.start);

        std::vector<KeyEvent> key_events = keyboard_drain_events(app.keyboard);
        if (!key_events.empty()) {
            if (want_launcher && app.launcher.open) {
                for (const KeyEvent &event : key_events)
                    launcher_handle_key_event(app.launcher, event);
                launcher_request_frame(app.launcher);
                rest_egl_current();
            } else if (want_settings && app.settings.base.open) {
                for (const KeyEvent &event : key_events)
                    settings_handle_key_event(app.settings, app.cfg,
                                              settings_commit, event);
                settings_request_frame(app.settings);
                rest_egl_current();
            } else if (want_starward && app.starward.base.open) {
                for (const KeyEvent &event : key_events)
                    starward_handle_key_event(app.starward, event);
                starward_request_frame(app.starward);
                rest_egl_current();
            } else if (want_controlcenter && app.controlcenter.base.open) {
                for (const KeyEvent &event : key_events)
                    controlcenter_handle_key_event(app.controlcenter, event);
                controlcenter_request_frame(
                    app.controlcenter,
                    static_cast<float>(bar_detail::kBarHeight),
                    static_cast<float>(bar_detail::kBarTopMargin));
                rest_egl_current();
            } else {
                for (auto &mon : app.outputs) {
                    if (mon->network_panel.base.open) {
                        for (const KeyEvent &event : key_events)
                            network_panel_handle_key_event(mon->network_panel,
                                                           app.network, event);
                        network_dispatch(true);
                        break;
                    }
                    if (mon->bluetooth_panel.base.open) {
                        for (const KeyEvent &event : key_events)
                            bluetooth_panel_handle_key_event(
                                mon->bluetooth_panel, app.bluetooth, event);
                        bluetooth_dispatch();
                        break;
                    }
                    if (mon->volume_panel.base.open) {
                        for (const KeyEvent &event : key_events)
                            volume_panel_handle_key_event(mon->volume_panel,
                                                          app.pipewire, event);
                        volume_dispatch();
                        break;
                    }
                }
            }
        }

        if (want_launcher)
            launcher_search_start_pending(app.launcher);

        if (want_launcher && launcher_tick(app.launcher)) {
            launcher_request_frame(app.launcher);
            rest_egl_current();
        }

        for (const PointerClick &click : pointer_drain_clicks(app.pointer)) {
            MonitorOutput *mon = find_monitor_for_surface(app, click.surface);
            if (click.button != BTN_LEFT &&
                !(mon && click.surface == mon->tray_panel.base.surface))
                continue;
            if (!click.pressed) {
                for (auto &m : app.outputs) {
                    if (m->volume_panel.dragging) {
                        m->volume_panel.dragging.reset();
                        volume_dispatch();
                    }
                }
                continue;
            }
            if (mon && mon->tray_menu.base.open &&
                click.surface != mon->tray_menu.base.surface &&
                click.surface != mon->tray_panel.base.surface) {
                tray_menu_close(mon->tray_menu);
                tray_menu_request_frame(mon->tray_menu);
                rest_egl_current();
            }
            if (mon && click.surface == mon->tray_panel.base.surface) {
                tray_panel_handle_click(mon->tray_panel, app.tray,
                                        mon->tray_menu, click.x, click.y,
                                        click.button);
                tray_dispatch();
            } else if (mon && click.surface == mon->tray_menu.base.surface) {
                tray_menu_handle_click(mon->tray_menu, app.tray, click.x,
                                       click.y);
                tray_menu_request_frame(mon->tray_menu);
                rest_egl_current();
            } else if (want_starward &&
                       click.surface == app.starward.base.surface) {
                starward_handle_click(app.starward, click.x, click.y);
                starward_request_frame(app.starward);
                rest_egl_current();
            } else if (want_controlcenter &&
                       click.surface == app.controlcenter.base.surface) {
                controlcenter_handle_click(app.controlcenter, app, click.x,
                                           click.y);
                controlcenter_request_frame(
                    app.controlcenter,
                    static_cast<float>(bar_detail::kBarHeight),
                    static_cast<float>(bar_detail::kBarTopMargin));
                rest_egl_current();
            } else if (mon &&
                       click.surface == mon->network_panel.base.surface) {
                network_panel_handle_click(mon->network_panel, app.network,
                                           click.x, click.y);
                network_dispatch(true);
            } else if (mon &&
                       click.surface == mon->bluetooth_panel.base.surface) {
                bluetooth_panel_handle_click(mon->bluetooth_panel,
                                             app.bluetooth, click.x, click.y);
                bluetooth_dispatch();
            } else if (mon && click.surface == mon->volume_panel.base.surface) {
                volume_panel_handle_click(mon->volume_panel, app.pipewire,
                                          click.x, click.y);
                volume_dispatch();
            } else if (want_launcher && click.surface == app.launcher.surface) {
                launcher_handle_click(app.launcher, click.x, click.y);
                launcher_request_frame(app.launcher);
                rest_egl_current();
            } else if (want_settings &&
                       click.surface == app.settings.base.surface) {
                settings_handle_click(app.settings, app.cfg, settings_commit,
                                      click.x, click.y);
                settings_request_frame(app.settings);
                rest_egl_current();
            } else if (mon && click.surface == mon->surface) {
                dispatch_pill_click(*mon, click.x, click.y);
                network_dispatch(true);
                bluetooth_dispatch();
                volume_dispatch();
                tray_dispatch();
                if (want_starward) {
                    starward_request_frame(app.starward);
                    rest_egl_current();
                }
                if (want_controlcenter) {
                    controlcenter_request_frame(
                        app.controlcenter,
                        static_cast<float>(bar_detail::kBarHeight),
                        static_cast<float>(bar_detail::kBarTopMargin));
                    rest_egl_current();
                }
            }
        }

        for (const PointerScroll &scroll : pointer_drain_scrolls(app.pointer)) {
            MonitorOutput *mon = find_monitor_for_surface(app, scroll.surface);
            if (mon && scroll.surface == mon->network_panel.base.surface) {
                network_panel_handle_scroll(mon->network_panel, app.network,
                                            scroll.dy);
                network_dispatch(true);
            } else if (mon &&
                       scroll.surface == mon->bluetooth_panel.base.surface) {
                bluetooth_panel_handle_scroll(mon->bluetooth_panel,
                                              app.bluetooth, scroll.dy);
                bluetooth_dispatch();
            } else if (mon &&
                       scroll.surface == mon->volume_panel.base.surface) {
                volume_panel_handle_scroll(mon->volume_panel, app.pipewire,
                                           scroll.dy);
                volume_dispatch();
            } else if (mon && scroll.surface == mon->surface &&
                       bar_detail::hit_test_pills(mon->capsule, app.pointer,
                                                  mon->surface) ==
                           PillId::Volume) {
                bar_detail::volume_pill_handle_wheel(*mon, scroll.dy);
            } else if (want_settings &&
                       scroll.surface == app.settings.base.surface) {
                settings_handle_scroll(app.settings, scroll.dy);
            }
        }
    }

    if (ipc_fd >= 0)
        close(ipc_fd);
    if (timer_fd >= 0)
        close(timer_fd);
    return 0;
}

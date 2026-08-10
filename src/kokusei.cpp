#include "app/config.hpp"
#include "app/ipc.hpp"
#include "app/single_instance_lock.hpp"
#include "bar/bar.hpp"
#include "core/deferred_call.hpp"
#include "core/log.hpp"
#include "core/poll_source.hpp"
#include "core/sdbus_poll_source.hpp"
#include "idle/idle.hpp"
#include "notification/notification_draw.hpp"
#include "render/image.hpp"
#include "wallpaper/wallpaper.hpp"
#include "wayland/hyprland.hpp"
#include "wayland/layer_surface.hpp"
#include "wayland/shojiwm.hpp"

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

    WaylandState state;
    state.cfg = load_config();
    if (state.cfg.autohide) {
        state.autohide.hidden = true;
        state.autohide.height_px = static_cast<float>(bar_detail::kAutoHideStripPx);
    } else {
        state.autohide.height_px = static_cast<float>(state.cfg.height);
    }
    state.config_watch_fd = config_watch_init(config_path());
    DeferredCall::init();

    state.display = wl_display_connect(nullptr);
    if (!state.display) {
        klog("failed to connect to Wayland display");
        return 1;
    }

    wl_registry *registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    if (!state.compositor || !state.layer_shell) {
        klog("compositor is missing wl_compositor or zwlr_layer_shell_v1");
        return 1;
    }

    LayerSurfaceConfig bar_cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        .name_space = "kokusei",
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
        .height = bar_detail::bar_current_height(state),
        .margin_top = bar_detail::kBarTopMargin,
        .margin_right = static_cast<int32_t>(kPanelSideMargin),
        .margin_left = static_cast<int32_t>(kPanelSideMargin),
        .exclusive_zone =
            bar_detail::bar_current_height(state) + bar_detail::kBarTopMargin,
    };
    state.layer_surface =
        layer_surface_create(state.surface, state.compositor, state.layer_shell,
                             bar_cfg, &layer_surface_listener, &state);
    state.output_scale.on_change = [&state](int32_t scale) {
        if (state.egl_window)
            wl_egl_window_resize(state.egl_window, state.width * scale,
                                 bar_detail::bar_current_height(state) * scale,
                                 0, 0);
        if (state.frame_clock.surface)
            request_frame(state.frame_clock);
    };
    output_scale_watch(state.output_scale, state.surface);
    wl_surface_commit(state.surface);

    bool want_wallpaper = !state.cfg.wallpaper_path.empty();
    if (want_wallpaper &&
        !wallpaper_create_surface(state.wallpaper, state.compositor,
                                  state.layer_shell)) {
        klog("wallpaper: failed to create layer surface");
        want_wallpaper = false;
    }
    bool want_notification = notification_create_surface(
        state.notification, state.compositor, state.layer_shell);
    if (!want_notification)
        klog("notification: failed to create layer surface");

    bool want_osd =
        osd_create_surface(state.osd, state.compositor, state.layer_shell);
    if (!want_osd)
        klog("osd: failed to create layer surface");

    bool want_launcher = launcher_create_surface(
        state.launcher, state.compositor, state.layer_shell);
    if (!want_launcher)
        klog("launcher: failed to create layer surface");

    bool want_logout = logout_create_surface(state.logout, state.compositor,
                                             state.layer_shell);
    if (!want_logout)
        klog("logout: failed to create layer surface");

    bool want_network_panel = network_panel_create_surface(
        state.network_panel, state.compositor, state.layer_shell);
    if (!want_network_panel)
        klog("network_panel: failed to create layer surface");

    bool want_bluetooth_panel = bluetooth_panel_create_surface(
        state.bluetooth_panel, state.compositor, state.layer_shell);
    if (!want_bluetooth_panel)
        klog("bluetooth_panel: failed to create layer surface");

    bool want_volume_panel = volume_panel_create_surface(
        state.volume_panel, state.compositor, state.layer_shell);
    if (!want_volume_panel)
        klog("volume_panel: failed to create layer surface");

    bool want_tray_panel = tray_panel_create_surface(
        state.tray_panel, state.compositor, state.layer_shell);
    if (!want_tray_panel)
        klog("tray_panel: failed to create layer surface");

    bool want_tray_menu = tray_menu_create_surface(
        state.tray_menu, state.compositor, state.layer_shell);
    if (!want_tray_menu)
        klog("tray_menu: failed to create layer surface");

    while (state.running &&
           !(state.configured &&
             (!want_wallpaper || state.wallpaper.configured) &&
             (!want_notification || state.notification.configured) &&
             (!want_osd || state.osd.configured) &&
             (!want_launcher || state.launcher.configured) &&
             (!want_logout || state.logout.base.configured) &&
             (!want_network_panel || state.network_panel.base.configured) &&
             (!want_bluetooth_panel || state.bluetooth_panel.base.configured) &&
             (!want_volume_panel || state.volume_panel.base.configured) &&
             (!want_tray_panel || state.tray_panel.base.configured) &&
             (!want_tray_menu || state.tray_menu.base.configured))) {
        wl_display_dispatch(state.display);
    }
    if (!state.configured)
        return 1;

    if (!init_egl(state)) {
        klog("EGL init failed");
        return 1;
    }

    if (!state.renderer.init()) {
        klog("renderer init failed");
        return 1;
    }

    if (want_wallpaper) {
        wallpaper_decode_async(state.wallpaper, state.cfg.wallpaper_path);

        if (wallpaper_init_egl(state.wallpaper, state.renderer,
                               state.egl_display, state.egl_config,
                               state.egl_context)) {
            wallpaper_request_frame(state.wallpaper);
            wallpaper_upload_pending(state.wallpaper);
        } else {
            klog("wallpaper: EGL surface init failed");
        }

        eglMakeCurrent(state.egl_display, state.egl_surface, state.egl_surface,
                       state.egl_context);
    }

    if (want_notification) {
        if (!notification_init_egl(state.notification, state.renderer,
                                   state.egl_display, state.egl_config,
                                   state.egl_context)) {
            klog("notification: EGL surface init failed");
            want_notification = false;
        } else {
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);
        }
    }
    if (want_notification)
        notification_init(state.notification);

    if (want_osd) {
        if (!osd_init_egl(state.osd, state.renderer, state.egl_display,
                          state.egl_config, state.egl_context)) {
            klog("osd: EGL surface init failed");
            want_osd = false;
        } else {
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);
        }
    }
    if (want_launcher) {
        if (!launcher_init_egl(state.launcher, state.renderer,
                               state.egl_display, state.egl_config,
                               state.egl_context)) {
            klog("launcher: EGL surface init failed");
            want_launcher = false;
        } else {

            launcher_request_frame(state.launcher);
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);
        }
    }
    if (want_logout) {
        if (!logout_init_egl(state.logout, state.renderer, state.egl_display,
                             state.egl_config, state.egl_context)) {
            klog("logout: EGL surface init failed");
            want_logout = false;
        } else {

            logout_request_frame(state.logout);
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);

            const char *logo_candidates[] = {KOKUSEI_LOGOUT_LOGO,
                                             "assets/logo.png"};
            std::string logo_path = logo_candidates[1];
            for (const char *candidate : logo_candidates) {
                if (std::filesystem::exists(candidate)) {
                    logo_path = candidate;
                    break;
                }
            }
            state.logout.logo_tex = load_image_texture(logo_path);
        }
    }
    if (want_network_panel) {
        if (!network_panel_init_egl(state.network_panel, state.renderer,
                                    state.network, state.egl_display,
                                    state.egl_config, state.egl_context)) {
            klog("network_panel: EGL surface init failed");
            want_network_panel = false;
        } else {

            network_panel_request_frame(state.network_panel, 0.0f, 0.0f, 0.0f);
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);
        }
    }
    if (want_bluetooth_panel) {
        if (!bluetooth_panel_init_egl(state.bluetooth_panel, state.renderer,
                                      state.bluetooth, state.egl_display,
                                      state.egl_config, state.egl_context)) {
            klog("bluetooth_panel: EGL surface init failed");
            want_bluetooth_panel = false;
        } else {

            bluetooth_panel_request_frame(state.bluetooth_panel, 0.0f, 0.0f,
                                          0.0f);
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);
        }
    }
    if (want_volume_panel) {
        if (!volume_panel_init_egl(state.volume_panel, state.renderer,
                                   state.pipewire, state.egl_display,
                                   state.egl_config, state.egl_context)) {
            klog("volume_panel: EGL surface init failed");
            want_volume_panel = false;
        } else {

            volume_panel_request_frame(state.volume_panel, 0.0f, 0.0f, 0.0f);
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);
        }
    }
    if (want_tray_panel) {
        if (!tray_panel_init_egl(state.tray_panel, state.renderer, state.tray,
                                 state.egl_display, state.egl_config,
                                 state.egl_context)) {
            klog("tray_panel: EGL surface init failed");
            want_tray_panel = false;
        } else {

            tray_panel_request_frame(state.tray_panel, 0.0f, 0.0f, 0.0f);
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);
        }
    }
    if (want_tray_menu) {
        if (!tray_menu_init_egl(state.tray_menu, state.renderer, state.tray,
                                state.egl_display, state.egl_config,
                                state.egl_context)) {
            klog("tray_menu: EGL surface init failed");
            want_tray_menu = false;
        } else {
            tray_menu_request_frame(state.tray_menu);
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);
        }
    }
    brightness_init(state.brightness);
    state.brightness_watch_fd = brightness_watch_init(state.brightness);
    pipewire_init(state.pipewire);
    upower_init(state.upower);
    bool want_network =
        state.upower.bus && network_init(state.network, *state.upower.bus);
    if (!want_network)
        klog("network: no system bus available - network info unavailable");
    bool want_bluetooth =
        state.upower.bus && bluetooth_init(state.bluetooth, *state.upower.bus);
    if (!want_bluetooth)
        klog("bluetooth: no system bus available - bluetooth info unavailable");
    bool want_tray = tray_init(state.tray);
    if (!want_tray)
        klog("tray: no session bus available - system tray unavailable");

    update_clock(state);
    init_stub_widgets(state);

    if (shoji_init(state.shoji)) {
        state.compositor_backend = WaylandState::CompositorBackend::ShojiWM;
    } else if (hypr_init(state.hypr)) {
        state.compositor_backend = WaylandState::CompositorBackend::Hyprland;
    }
    klog("compositor backend: %s",
         state.compositor_backend == WaylandState::CompositorBackend::ShojiWM
             ? "shojiwm"
         : state.compositor_backend == WaylandState::CompositorBackend::Hyprland
             ? "hyprland"
             : "none");

    state.idle.timeout_seconds = state.cfg.idle_timeout_seconds;
    state.idle.on_idle_command = state.cfg.idle_command;
    state.idle.on_resume_command = state.cfg.idle_resume_command;
    idle_init(state.idle);

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

    klog("started: %dx%d bar, ipc_fd=%d, timer_fd=%d", state.width,
         state.cfg.height, ipc_fd, timer_fd);
    bar_request_frame(state);

    while (state.running) {
        wl_display_flush(state.display);

        std::vector<FnPollSource> fn_sources;

        auto network_notify = [&state](const std::string &summary,
                                       const std::string &body) {
            notification_push(state.notification, "Network", summary, body,
                              6000);
        };

        auto network_dispatch = [&](bool changed) {
            if (!changed)
                return;
            bar_request_frame(state);
            if (want_network_panel) {
                network_panel_request_frame(
                    state.network_panel,
                    bar_detail::pill_center_x(state.capsule, PillId::Wifi),
                    static_cast<float>(state.cfg.height), bar_detail::kBarTopMargin);
            }
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);
        };

        auto bluetooth_notify = [&state](const std::string &summary,
                                         const std::string &body) {
            notification_push(state.notification, "Bluetooth", summary, body,
                              6000);
        };

        auto bluetooth_dispatch = [&] {
            bar_request_frame(state);
            if (want_bluetooth_panel) {
                bluetooth_panel_request_frame(
                    state.bluetooth_panel,
                    bar_detail::pill_center_x(state.capsule, PillId::Bluetooth),
                    static_cast<float>(state.cfg.height), bar_detail::kBarTopMargin);
            }
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);
        };

        auto volume_dispatch = [&] {
            bar_request_frame(state);
            if (want_volume_panel) {
                volume_panel_request_frame(
                    state.volume_panel,
                    bar_detail::pill_center_x(state.capsule, PillId::Volume),
                    static_cast<float>(state.cfg.height), bar_detail::kBarTopMargin);
            }
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);
        };

        auto tray_dispatch = [&] {
            bar_request_frame(state);
            if (want_tray_panel) {
                tray_panel_request_frame(
                    state.tray_panel,
                    bar_detail::pill_center_x(state.capsule, PillId::Tray),
                    static_cast<float>(state.cfg.height), bar_detail::kBarTopMargin);
            }
            if (want_tray_menu)
                tray_menu_request_frame(state.tray_menu);
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);
        };

        if (ipc_fd >= 0) {
            fn_sources.emplace_back(ipc_fd, POLLIN, [&] {
                handle_ipc_accept(ipc_fd, state.surface, state.idle,
                                  state.launcher, state.logout,
                                  state.bluetooth_panel, state.bluetooth,
                                  state.running);
                if (want_launcher) {
                    launcher_request_frame(state.launcher);
                    eglMakeCurrent(state.egl_display, state.egl_surface,
                                   state.egl_surface, state.egl_context);
                }
                if (want_logout) {
                    logout_request_frame(state.logout);
                    eglMakeCurrent(state.egl_display, state.egl_surface,
                                   state.egl_surface, state.egl_context);
                }
                if (want_bluetooth_panel) {
                    bar_paint(state);
                    bluetooth_dispatch();
                }
            });
        }

        if (timer_fd >= 0) {
            fn_sources.emplace_back(timer_fd, POLLIN, [&] {
                uint64_t expirations;
                read(timer_fd, &expirations, sizeof(expirations));
                update_clock(state);
                bar_request_frame(state);
                if (notification_sweep_expired(state.notification))
                    notification_request_frame(state.notification);
                if (state.osd.visible &&
                    std::chrono::steady_clock::now() >= state.osd.hide_at) {
                    osd_hide(state.osd);
                }
                if (want_network) {
                    network_dispatch(network_tick(
                        state.network, std::chrono::steady_clock::now()));
                }
                if (want_bluetooth) {
                    bluetooth_tick(state.bluetooth, bluetooth_notify,
                                   std::chrono::steady_clock::now(),
                                   bluetooth_dispatch);
                }
                if (bar_detail::volume_pill_peek_expire(state))
                    bar_request_frame(state);
                if (want_launcher && state.launcher.open) {
                    state.launcher.cursor_blink_visible =
                        !state.launcher.cursor_blink_visible;
                    launcher_request_frame(state.launcher);
                    eglMakeCurrent(state.egl_display, state.egl_surface,
                                   state.egl_surface, state.egl_context);
                }
            });
        }

        int compositor_fd = (state.compositor_backend ==
                             WaylandState::CompositorBackend::Hyprland)
                                ? state.hypr.event_fd
                            : (state.compositor_backend ==
                               WaylandState::CompositorBackend::ShojiWM)
                                ? state.shoji.fd
                                : -1;
        if (compositor_fd >= 0) {
            fn_sources.emplace_back(compositor_fd, POLLIN, [&] {
                if (state.compositor_backend ==
                    WaylandState::CompositorBackend::Hyprland) {
                    HyprEventResult r = hypr_poll_events(state.hypr);
                    if (r == HyprEventResult::Disconnected) {
                        close(state.hypr.event_fd);
                        state.hypr.event_fd = -1;
                        state.compositor_backend =
                            WaylandState::CompositorBackend::None;
                        klog("hyprland: event socket disconnected");
                    } else if (r == HyprEventResult::StructuralChanged) {
                        hypr_refresh(state.hypr);
                        bar_request_frame(state);
                    } else if (r == HyprEventResult::ActiveChanged) {
                        bar_request_frame(state);
                    }
                } else if (state.compositor_backend ==
                           WaylandState::CompositorBackend::ShojiWM) {
                    ShojiEventResult r = shoji_poll(state.shoji);
                    if (r == ShojiEventResult::Disconnected) {
                        close(state.shoji.fd);
                        state.shoji.fd = -1;
                        state.compositor_backend =
                            WaylandState::CompositorBackend::None;
                        klog("shojiwm: event socket disconnected");
                    } else if (r == ShojiEventResult::Updated) {
                        bar_request_frame(state);
                    }
                }
            });
        }

        if (state.notification.bus != nullptr) {
            fn_sources.push_back(
                sdbus_poll_source(*state.notification.bus, [&] {
                    int budget = 32;
                    while (budget-- > 0 &&
                           state.notification.bus->processPendingEvent()) {
                    }
                    notification_request_frame(state.notification);
                }));
        }

        if (state.upower.bus != nullptr) {
            fn_sources.push_back(sdbus_poll_source(*state.upower.bus, [&] {
                int budget = 32;
                while (budget-- > 0 &&
                       state.upower.bus->processPendingEvent()) {
                }
                if (state.upower.dirty) {
                    state.upower.dirty = false;
                    bar_request_frame(state);
                }

                network_dispatch(state.network.dirty);
                state.network.dirty = false;
            }));
        }

        if (state.tray.bus != nullptr) {
            fn_sources.push_back(sdbus_poll_source(*state.tray.bus, [&] {
                int budget = 32;
                while (budget-- > 0 && state.tray.bus->processPendingEvent()) {
                }
                if (state.tray.dirty) {
                    state.tray.dirty = false;
                    tray_dispatch();
                }
            }));
        }

        if (state.network.device_proc.wake_fd >= 0)
            fn_sources.emplace_back(
                state.network.device_proc.wake_fd, POLLIN, [&] {
                    network_dispatch(
                        network_poll_device(state.network, network_notify));
                });
        if (state.network.profile_proc.wake_fd >= 0)
            fn_sources.emplace_back(
                state.network.profile_proc.wake_fd, POLLIN,
                [&] { network_dispatch(network_poll_profile(state.network)); });
        if (state.network.quick_scan_proc.wake_fd >= 0)
            fn_sources.emplace_back(
                state.network.quick_scan_proc.wake_fd, POLLIN, [&] {
                    network_dispatch(network_poll_quick_scan(state.network));
                });
        if (state.network.scan_proc.wake_fd >= 0)
            fn_sources.emplace_back(
                state.network.scan_proc.wake_fd, POLLIN, [&] {
                    network_dispatch(
                        network_poll_scan(state.network, network_notify));
                });
        if (state.network.connect_proc.wake_fd >= 0)
            fn_sources.emplace_back(
                state.network.connect_proc.wake_fd, POLLIN, [&] {
                    network_dispatch(
                        network_poll_connect(state.network, network_notify));
                });
        if (state.network.disconnect_proc.wake_fd >= 0)
            fn_sources.emplace_back(
                state.network.disconnect_proc.wake_fd, POLLIN, [&] {
                    network_dispatch(
                        network_poll_disconnect(state.network, network_notify));
                });
        if (state.network.forget_proc.wake_fd >= 0)
            fn_sources.emplace_back(
                state.network.forget_proc.wake_fd, POLLIN,
                [&] { network_dispatch(network_poll_forget(state.network)); });
        if (state.network.connectivity_proc.wake_fd >= 0)
            fn_sources.emplace_back(
                state.network.connectivity_proc.wake_fd, POLLIN, [&] {
                    network_dispatch(network_poll_connectivity(state.network,
                                                               network_notify));
                });

        int pipewire_fd_value = pipewire_fd(state.pipewire);
        if (pipewire_fd_value >= 0) {
            fn_sources.emplace_back(pipewire_fd_value, POLLIN, [&] {
                PipewireChange change = pipewire_poll(state.pipewire);
                if (change.sink) {
                    bool muted = false;
                    float level = pipewire_sink_level(state.pipewire, muted);
                    osd_show(state.osd, OsdKind::Volume, level, muted);
                    osd_request_frame(state.osd);
                    bar_detail::volume_pill_peek_tick(state);
                }
                if (change.source) {
                    bool muted = false;
                    float level = pipewire_source_level(state.pipewire, muted);
                    osd_show(state.osd, OsdKind::Mic, level, muted);
                    osd_request_frame(state.osd);
                }
                if (change.sink || change.source)
                    volume_dispatch();
            });
        }

        if (state.brightness_watch_fd >= 0) {
            fn_sources.emplace_back(state.brightness_watch_fd, POLLIN, [&] {
                if (brightness_watch_poll(state.brightness_watch_fd)) {
                    float level = brightness_get(state.brightness);
                    osd_show(state.osd, OsdKind::Brightness, level, false);
                    osd_request_frame(state.osd);
                }
            });
        }

        if (DeferredCall::poll_fd() >= 0) {
            fn_sources.emplace_back(DeferredCall::poll_fd(), POLLIN,
                                    [] { DeferredCall::drain(); });
        }

        if (state.config_watch_fd >= 0) {
            fn_sources.emplace_back(state.config_watch_fd, POLLIN, [&] {
                ConfigWatchEvent ev = config_watch_poll(state.config_watch_fd);
                if (ev.removed) {
                    close(state.config_watch_fd);
                    state.config_watch_fd = config_watch_init(config_path());
                }
                if (!ev.changed)
                    return;
                Config new_cfg = load_config();
                state.idle.timeout_seconds = new_cfg.idle_timeout_seconds;
                state.idle.on_idle_command = new_cfg.idle_command;
                state.idle.on_resume_command = new_cfg.idle_resume_command;
                if (new_cfg.wallpaper_path != state.cfg.wallpaper_path) {
                    wallpaper_load_async(state.wallpaper,
                                         new_cfg.wallpaper_path);
                }
                bool autohide_changed = new_cfg.autohide != state.cfg.autohide;
                bool height_changed = new_cfg.height != state.cfg.height;
                if (autohide_changed && !new_cfg.autohide)
                    state.autohide.hidden = false;
                bool currently_shown =
                    !new_cfg.autohide || !state.autohide.hidden;
                if ((height_changed || autohide_changed) && currently_shown) {
                    zwlr_layer_surface_v1_set_size(state.layer_surface, 0,
                                                   new_cfg.height);
                    zwlr_layer_surface_v1_set_exclusive_zone(
                        state.layer_surface,
                        new_cfg.height + bar_detail::kBarTopMargin);
                    wl_surface_commit(state.surface);
                    if (state.egl_window)
                        wl_egl_window_resize(
                            state.egl_window,
                            state.width * state.output_scale.scale,
                            new_cfg.height * state.output_scale.scale, 0, 0);
                    state.autohide.height_px = static_cast<float>(new_cfg.height);
                }
                state.cfg = new_cfg;
                bar_request_frame(state);
            });
        }

        auto launcher_search_dispatch = [&] {
            if (launcher_search_poll(state.launcher) && want_launcher) {
                launcher_request_frame(state.launcher);
                eglMakeCurrent(state.egl_display, state.egl_surface,
                               state.egl_surface, state.egl_context);
            }
        };
        if (state.launcher.search_dirs_proc.wake_fd >= 0)
            fn_sources.emplace_back(state.launcher.search_dirs_proc.wake_fd,
                                    POLLIN, launcher_search_dispatch);
        if (state.launcher.search_files_proc.wake_fd >= 0)
            fn_sources.emplace_back(state.launcher.search_files_proc.wake_fd,
                                    POLLIN, launcher_search_dispatch);

        if (state.keyboard.repeat_timer_fd >= 0) {

            fn_sources.emplace_back(
                state.keyboard.repeat_timer_fd, POLLIN,
                [&] { keyboard_repeat_tick(state.keyboard); });
        }

        std::vector<pollfd> fds;
        fds.push_back({.fd = wl_display_get_fd(state.display),
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

        int poll_timeout_ms = launcher_poll_timeout_ms(state.launcher);
        if (poll(fds.data(), fds.size(), poll_timeout_ms) < 0)
            break;

        if (fds[0].revents & POLLIN) {
            wl_display_dispatch(state.display);

            if (state.pointer.dirty) {
                state.pointer.dirty = false;
                bar_request_frame(state);
                if (want_volume_panel && state.volume_panel.dragging) {
                    volume_panel_handle_pointer_move(
                        state.volume_panel, state.pipewire, state.pointer.x);
                    volume_dispatch();
                }
            }
        }
        for (SourceRange &r : ranges)
            r.src->dispatch(fds, r.start);

        std::vector<KeyEvent> key_events =
            keyboard_drain_events(state.keyboard);
        if (want_launcher && state.launcher.open && !key_events.empty()) {
            for (const KeyEvent &event : key_events)
                launcher_handle_key_event(state.launcher, event);
            launcher_request_frame(state.launcher);
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);
        } else if (want_logout && state.logout.base.open &&
                   !key_events.empty()) {
            for (const KeyEvent &event : key_events)
                logout_handle_key_event(state.logout, event);
            logout_request_frame(state.logout);
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);
        } else if (want_network_panel && state.network_panel.base.open &&
                   !key_events.empty()) {
            for (const KeyEvent &event : key_events)
                network_panel_handle_key_event(state.network_panel,
                                               state.network, event);
            network_dispatch(true);
        } else if (want_bluetooth_panel && state.bluetooth_panel.base.open &&
                   !key_events.empty()) {
            for (const KeyEvent &event : key_events)
                bluetooth_panel_handle_key_event(state.bluetooth_panel,
                                                 state.bluetooth, event);
            bluetooth_dispatch();
        } else if (want_volume_panel && state.volume_panel.base.open &&
                   !key_events.empty()) {
            for (const KeyEvent &event : key_events)
                volume_panel_handle_key_event(state.volume_panel,
                                              state.pipewire, event);
            volume_dispatch();
        }

        if (want_launcher)
            launcher_search_start_pending(state.launcher);

        if (want_launcher && launcher_tick(state.launcher)) {
            launcher_request_frame(state.launcher);
            eglMakeCurrent(state.egl_display, state.egl_surface,
                           state.egl_surface, state.egl_context);
        }

        for (const PointerClick &click : pointer_drain_clicks(state.pointer)) {
            if (click.button != BTN_LEFT &&
                !(want_tray_panel && click.surface == state.tray_panel.base.surface))
                continue;
            if (!click.pressed) {
                if (want_volume_panel && state.volume_panel.dragging) {
                    state.volume_panel.dragging.reset();
                    volume_dispatch();
                }
                continue;
            }
            if (want_tray_menu && state.tray_menu.base.open &&
                click.surface != state.tray_menu.base.surface &&
                click.surface != state.tray_panel.base.surface) {
                tray_menu_close(state.tray_menu);
                tray_menu_request_frame(state.tray_menu);
                eglMakeCurrent(state.egl_display, state.egl_surface,
                               state.egl_surface, state.egl_context);
            }
            if (want_tray_panel && state.tray_panel.base.open &&
                click.surface == state.tray_panel.base.surface) {
                tray_panel_handle_click(state.tray_panel, state.tray,
                                        state.tray_menu, state.pointer.x,
                                        state.pointer.y, click.button);
                tray_dispatch();
            } else if (want_tray_menu && state.tray_menu.base.open &&
                       click.surface == state.tray_menu.base.surface) {
                tray_menu_handle_click(state.tray_menu, state.tray,
                                       state.pointer.x, state.pointer.y);
                tray_menu_request_frame(state.tray_menu);
                eglMakeCurrent(state.egl_display, state.egl_surface,
                               state.egl_surface, state.egl_context);
            } else if (want_logout && state.logout.base.open &&
                click.surface == state.logout.base.surface) {
                logout_handle_click(state.logout, state.pointer.x,
                                    state.pointer.y);
                logout_request_frame(state.logout);
                eglMakeCurrent(state.egl_display, state.egl_surface,
                               state.egl_surface, state.egl_context);
            } else if (want_network_panel && state.network_panel.base.open &&
                       click.surface == state.network_panel.base.surface) {
                network_panel_handle_click(state.network_panel, state.network,
                                           state.pointer.x, state.pointer.y);
                network_dispatch(true);
            } else if (want_bluetooth_panel &&
                       state.bluetooth_panel.base.open &&
                       click.surface == state.bluetooth_panel.base.surface) {
                bluetooth_panel_handle_click(state.bluetooth_panel,
                                             state.bluetooth, state.pointer.x,
                                             state.pointer.y);
                bluetooth_dispatch();
            } else if (want_volume_panel && state.volume_panel.base.open &&
                       click.surface == state.volume_panel.base.surface) {
                volume_panel_handle_click(state.volume_panel, state.pipewire,
                                          state.pointer.x, state.pointer.y);
                volume_dispatch();
            } else if (want_launcher && state.launcher.open &&
                       click.surface == state.launcher.surface) {
                launcher_handle_click(state.launcher, state.pointer.x,
                                      state.pointer.y);
                launcher_request_frame(state.launcher);
                eglMakeCurrent(state.egl_display, state.egl_surface,
                               state.egl_surface, state.egl_context);
            } else if (click.surface == state.surface) {
                dispatch_pill_click(state);
                network_dispatch(true);

                if (want_bluetooth_panel)
                    bluetooth_dispatch();
                if (want_volume_panel)
                    volume_dispatch();
                if (want_tray_panel)
                    tray_dispatch();
                if (want_logout) {
                    logout_request_frame(state.logout);
                    eglMakeCurrent(state.egl_display, state.egl_surface,
                                   state.egl_surface, state.egl_context);
                }
            }
        }

        for (const PointerScroll &scroll :
             pointer_drain_scrolls(state.pointer)) {
            if (want_network_panel && state.network_panel.base.open &&
                scroll.surface == state.network_panel.base.surface) {
                network_panel_handle_scroll(state.network_panel, state.network,
                                            scroll.dy);
                network_dispatch(true);
            } else if (want_bluetooth_panel &&
                       state.bluetooth_panel.base.open &&
                       scroll.surface == state.bluetooth_panel.base.surface) {
                bluetooth_panel_handle_scroll(state.bluetooth_panel,
                                              state.bluetooth, scroll.dy);
                bluetooth_dispatch();
            } else if (want_volume_panel && state.volume_panel.base.open &&
                       scroll.surface == state.volume_panel.base.surface) {
                volume_panel_handle_scroll(state.volume_panel, state.pipewire,
                                           scroll.dy);
                volume_dispatch();
            } else if (scroll.surface == state.surface &&
                       bar_detail::hit_test_pills(state.capsule, state.pointer,
                                                  state.surface) ==
                           PillId::Volume) {
                bar_detail::volume_pill_handle_wheel(state, scroll.dy);
            }
        }
    }

    if (ipc_fd >= 0)
        close(ipc_fd);
    if (timer_fd >= 0)
        close(timer_fd);
    return 0;
}

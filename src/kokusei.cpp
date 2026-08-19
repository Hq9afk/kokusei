#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <vector>

#include "app/config.h"
#include "app/ipc.h"
#include "app/key_dispatch.h"
#include "app/module_registry.h"
#include "app/monitor_output.h"
#include "app/service_registry.h"
#include "app/single_instance_lock.h"
#include "app/wayland_registry.h"

#include "core/deferred_call.h"
#include "core/log.h"
#include "core/poll_source.h"

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

inline Module *find_overlay_for_surface(WaylandState &app,
                                        wl_surface *surface) {
    if (!surface)
        return nullptr;
    for (auto &m : app.overlays)
        if (m->surface() == surface)
            return m.get();
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

    if (!app.compositor || !app.layer_shell || !app.wm_base) {
        klog("compositor is missing wl_compositor, zwlr_layer_shell_v1, or "
             "xdg_wm_base");
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

    if (!renderer_bootstrap_init(app)) {
        klog("renderer init failed");
        return 1;
    }

    MonitorOutput &first = *app.outputs.front();
    monitor_output_create_surfaces(app, first);
    monitor_output_wait_configured(app, first);
    monitor_output_finish_egl(app, first);
    first.activated = true;

    app.overlays = build_app_modules();
    for (auto &m : app.overlays) {
        if (!m->create_surface(app, first.output.wl))
            klog("overlay: failed to create layer surface");
    }

    for (;;) {
        bool all_configured = true;
        for (auto &m : app.overlays)
            if (!m->configured())
                all_configured = false;
        if (all_configured)
            break;
        wl_display_dispatch(app.display);
    }

    for (auto &m : app.overlays) {
        if (!m->surface())
            continue;
        if (!m->init_egl(app)) {
            klog("overlay: EGL surface init failed");
            continue;
        }
        eglMakeCurrent(app.egl_display, first.egl_surface, first.egl_surface,
                       app.egl_context);
    }

    for (size_t i = 1; i < app.outputs.size(); ++i)
        monitor_output_activate(app, *app.outputs[i]);

    cpu_temp_init(app.cpu_temp);
    gpu_temp_init(app.gpu_temp);

    app.services = build_services();
    for (auto &s : app.services)
        if (!s->init(app))
            klog("%s: init failed", s->name());

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

    klog("started: %zu monitor(s), ipc_fd=%d, timer_fd=%d", app.outputs.size(),
         ipc_fd, timer_fd);
    for (auto &mon : app.outputs)
        request_all_frames(*mon);

    while (app.running) {
        wl_display_flush(app.display);

        std::vector<FnPollSource> fn_sources;

        auto rest_egl_current = [&app] { bar_detail::rest_egl_current(app); };

        if (ipc_fd >= 0) {
            fn_sources.emplace_back(ipc_fd, POLLIN, [&] {
                handle_ipc_accept(ipc_fd, app);
                for (auto &m : app.overlays)
                    m->request_frame();
                rest_egl_current();
                for (auto &mon : app.outputs)
                    request_all_frames(*mon);
                rest_egl_current();
            });
        }

        if (timer_fd >= 0) {
            fn_sources.emplace_back(timer_fd, POLLIN, [&] {
                uint64_t expirations;
                read(timer_fd, &expirations, sizeof(expirations));
                for (auto &mon : app.outputs)
                    for (auto &m : mon->modules)
                        m->timer_tick(app, *mon);
                for (auto &s : app.services)
                    s->timer_tick(app);
                for (auto &m : app.overlays)
                    if (m->timer_tick(app))
                        rest_egl_current();
            });
        }

        for (auto &mon : app.outputs)
            for (auto &m : mon->modules)
                m->tick(app, *mon);

        for (auto &s : app.services)
            for (auto &src : s->poll_sources(app))
                fn_sources.push_back(std::move(src));

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

        for (auto &m : app.overlays)
            for (auto &[fd, cb] : m->extra_poll_sources(app))
                fn_sources.emplace_back(fd, POLLIN, cb);

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

        int poll_timeout_ms = -1;
        for (auto &m : app.overlays) {
            int t = m->poll_timeout_ms();
            if (t >= 0 && (poll_timeout_ms < 0 || t < poll_timeout_ms))
                poll_timeout_ms = t;
        }
        if (poll(fds.data(), fds.size(), poll_timeout_ms) < 0)
            break;

        if (fds[0].revents & POLLIN) {
            wl_display_dispatch(app.display);

            if (app.pointer.dirty) {
                app.pointer.dirty = false;
                for (auto &mon : app.outputs)
                    request_all_frames(*mon);
                for (auto &m : app.overlays)
                    m->handle_pointer_move(app, app.pointer.focused_surface,
                                           app.pointer.x, app.pointer.y);
                if (app.pointer.focused_surface) {
                    if (MonitorOutput *m = find_monitor_for_surface(
                            app, app.pointer.focused_surface))
                        app.last_pointer_monitor = m;
                }
                for (auto &mon : app.outputs)
                    for (auto &pm : mon->modules)
                        pm->handle_pointer_move(app, *mon, app.pointer.x,
                                                app.pointer.y);
            }
        }
        for (SourceRange &r : ranges)
            r.src->dispatch(fds, r.start);

        std::vector<KeyEvent> key_events = keyboard_drain_events(app.keyboard);
        dispatch_key_events(app, key_events);

        for (auto &m : app.overlays)
            if (m->tick()) {
                m->request_frame();
                rest_egl_current();
            }

        for (const PointerClick &click : pointer_drain_clicks(app.pointer)) {
            MonitorOutput *mon = find_monitor_for_surface(app, click.surface);
            if (!click.pressed) {
                for (auto &m : app.outputs)
                    for (auto &pm : m->modules)
                        pm->handle_pointer_release();
                for (auto &m : app.overlays)
                    m->handle_pointer_release();
                continue;
            }
            if (click.button == BTN_LEFT) {
                if (Module *m = find_overlay_for_surface(app, click.surface)) {
                    m->handle_click(app, click.x, click.y);
                    m->request_frame();
                    rest_egl_current();
                    continue;
                }
            }
            if (!mon)
                continue;
            for (auto &pm : mon->modules) {
                if (pm->owns_surface(click.surface)) {
                    pm->handle_click(app, *mon, click.surface, click.button,
                                     click.x, click.y);
                    break;
                }
            }
        }

        for (const PointerScroll &scroll : pointer_drain_scrolls(app.pointer)) {
            MonitorOutput *mon = find_monitor_for_surface(app, scroll.surface);
            if (Module *m = find_overlay_for_surface(app, scroll.surface)) {
                m->handle_scroll(app, scroll.dy);
                continue;
            }
            if (!mon)
                continue;
            for (auto &pm : mon->modules) {
                if (pm->owns_surface(scroll.surface)) {
                    pm->handle_scroll(app, *mon, scroll.surface, scroll.dy);
                    break;
                }
            }
        }
    }

    if (ipc_fd >= 0)
        close(ipc_fd);
    if (timer_fd >= 0)
        close(timer_fd);
    return 0;
}

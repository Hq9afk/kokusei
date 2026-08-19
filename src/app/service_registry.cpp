#include <chrono>
#include <unistd.h>

#include "app/module_registry.h"
#include "app/monitor_output.h"
#include "app/service_registry.h"
#include "app/wayland_state.h"

#include "core/log.h"

#include "modules/idle.h"
#include "modules/notification.h"
#include "modules/osd.h"

#include "service/bluetooth_service.h"
#include "service/hyprland.h"
#include "service/mpris_service.h"
#include "service/network_service.h"
#include "service/pipewire.h"
#include "service/shojiwm.h"
#include "service/tray_service.h"
#include "service/upower_service.h"

namespace {

void redraw_all_monitors(WaylandState &app) {
    for (auto &mon : app.outputs)
        request_all_frames(*mon);
}

void redraw_and_present(WaylandState &app) {
    redraw_all_monitors(app);
    bar_detail::rest_egl_current(app);
}

void network_dispatch(WaylandState &app, bool changed) {
    if (changed)
        redraw_and_present(app);
}

void network_notify(WaylandState &app, const std::string &summary,
                    const std::string &body) {
    notification_push(app.notification, "Network", summary, body, 6000);
}

void bluetooth_notify(WaylandState &app, const std::string &summary,
                      const std::string &body) {
    notification_push(app.notification, "Bluetooth", summary, body, 6000);
}

class NotificationBusService final : public Service {
  public:
    const char *name() const override { return "notification"; }

    bool init(WaylandState &app) override {
        if (!notification_init(app.notification, [&app] {
                for (auto &mon : app.outputs)
                    if (auto *nv =
                            mon->module<NotificationViewPerMonitorModule>())
                        nv->request_frame();
            }))
            klog("notification: D-Bus registration failed");
        return true;
    }

    void timer_tick(WaylandState &app) override {
        if (!notification_sweep_expired(app.notification))
            return;
        for (auto &mon : app.outputs)
            if (auto *nv = mon->module<NotificationViewPerMonitorModule>())
                nv->request_frame();
    }

    std::vector<FnPollSource> poll_sources(WaylandState &app) override {
        std::vector<FnPollSource> sources;
        if (!app.notification.bus)
            return sources;
        sources.push_back(sdbus_poll_source(*app.notification.bus, [&app] {
            int budget = 32;
            while (budget-- > 0 &&
                   app.notification.bus->processPendingEvent()) {
            }
            for (auto &mon : app.outputs)
                if (auto *nv = mon->module<NotificationViewPerMonitorModule>())
                    nv->request_frame();
        }));
        return sources;
    }
};

class BrightnessOsdService final : public Service {
  public:
    const char *name() const override { return "brightness"; }

    bool init(WaylandState &app) override {
        brightness_init(app.brightness);
        app.brightness_watch_fd = brightness_watch_init(app.brightness);
        return true;
    }

    std::vector<FnPollSource> poll_sources(WaylandState &app) override {
        std::vector<FnPollSource> sources;
        if (app.brightness_watch_fd < 0)
            return sources;
        sources.emplace_back(app.brightness_watch_fd, POLLIN, [&app] {
            if (!brightness_watch_poll(app.brightness_watch_fd))
                return;
            float level = brightness_get(app.brightness);
            for (auto &mon : app.outputs) {
                if (!osd_effective_enabled(app.cfg, mon->output.name))
                    continue;
                OsdState &osd = mon->module<OsdPerMonitorModule>()->state();
                osd_show(osd, OsdKind::Brightness, level, false);
                osd_request_frame(osd);
            }
        });
        return sources;
    }
};

class PipewireOsdService final : public Service {
  public:
    const char *name() const override { return "pipewire"; }

    bool init(WaylandState &app) override {
        return pipewire_init(app.pipewire);
    }

    std::vector<FnPollSource> poll_sources(WaylandState &app) override {
        std::vector<FnPollSource> sources;
        int fd = pipewire_fd(app.pipewire);
        if (fd < 0)
            return sources;
        sources.emplace_back(fd, POLLIN, [&app] {
            PipewireChange change = pipewire_poll(app.pipewire);
            if (change.sink) {
                bool muted = false;
                float level = pipewire_sink_level(app.pipewire, muted);
                for (auto &mon : app.outputs) {
                    if (!osd_effective_enabled(app.cfg, mon->output.name))
                        continue;
                    OsdState &osd = mon->module<OsdPerMonitorModule>()->state();
                    osd_show(osd, OsdKind::Volume, level, muted);
                    osd_request_frame(osd);
                }
            }
            if (change.source) {
                bool muted = false;
                float level = pipewire_source_level(app.pipewire, muted);
                for (auto &mon : app.outputs) {
                    if (!osd_effective_enabled(app.cfg, mon->output.name))
                        continue;
                    OsdState &osd = mon->module<OsdPerMonitorModule>()->state();
                    osd_show(osd, OsdKind::Mic, level, muted);
                    osd_request_frame(osd);
                }
            }
            if (change.sink || change.source)
                redraw_and_present(app);
        });
        return sources;
    }
};

class UpowerService final : public Service {
  public:
    const char *name() const override { return "upower"; }

    bool init(WaylandState &app) override { return upower_init(app.upower); }

    std::vector<FnPollSource> poll_sources(WaylandState &app) override {
        std::vector<FnPollSource> sources;
        if (!app.upower.bus)
            return sources;
        sources.push_back(sdbus_poll_source(*app.upower.bus, [&app] {
            int budget = 32;
            while (budget-- > 0 && app.upower.bus->processPendingEvent()) {
            }
            if (app.upower.dirty) {
                app.upower.dirty = false;
                redraw_all_monitors(app);
            }
            network_dispatch(app, app.network.dirty);
            app.network.dirty = false;
        }));
        return sources;
    }
};

class NetworkService final : public Service {
  public:
    const char *name() const override { return "network"; }

    bool init(WaylandState &app) override {
        want_ = app.upower.bus && network_init(app.network, *app.upower.bus);
        if (!want_)
            klog("network: no system bus available - network info "
                 "unavailable");
        return true;
    }

    void timer_tick(WaylandState &app) override {
        if (want_)
            network_dispatch(
                app,
                network_tick(app.network, std::chrono::steady_clock::now()));
    }

    std::vector<FnPollSource> poll_sources(WaylandState &app) override {
        std::vector<FnPollSource> sources;
        auto notify = [&app](const std::string &summary,
                             const std::string &body) {
            network_notify(app, summary, body);
        };
        if (app.network.device_proc.wake_fd >= 0)
            sources.emplace_back(
                app.network.device_proc.wake_fd, POLLIN, [&app, notify] {
                    network_dispatch(app,
                                     network_poll_device(app.network, notify));
                });
        if (app.network.profile_proc.wake_fd >= 0)
            sources.emplace_back(
                app.network.profile_proc.wake_fd, POLLIN, [&app] {
                    network_dispatch(app, network_poll_profile(app.network));
                });
        if (app.network.quick_scan_proc.wake_fd >= 0)
            sources.emplace_back(
                app.network.quick_scan_proc.wake_fd, POLLIN, [&app] {
                    network_dispatch(app, network_poll_quick_scan(app.network));
                });
        if (app.network.scan_proc.wake_fd >= 0)
            sources.emplace_back(
                app.network.scan_proc.wake_fd, POLLIN, [&app, notify] {
                    network_dispatch(app,
                                     network_poll_scan(app.network, notify));
                });
        if (app.network.connect_proc.wake_fd >= 0)
            sources.emplace_back(
                app.network.connect_proc.wake_fd, POLLIN, [&app, notify] {
                    network_dispatch(app,
                                     network_poll_connect(app.network, notify));
                });
        if (app.network.disconnect_proc.wake_fd >= 0)
            sources.emplace_back(
                app.network.disconnect_proc.wake_fd, POLLIN, [&app, notify] {
                    network_dispatch(
                        app, network_poll_disconnect(app.network, notify));
                });
        if (app.network.forget_proc.wake_fd >= 0)
            sources.emplace_back(
                app.network.forget_proc.wake_fd, POLLIN, [&app] {
                    network_dispatch(app, network_poll_forget(app.network));
                });
        if (app.network.connectivity_proc.wake_fd >= 0)
            sources.emplace_back(
                app.network.connectivity_proc.wake_fd, POLLIN, [&app, notify] {
                    network_dispatch(
                        app, network_poll_connectivity(app.network, notify));
                });
        return sources;
    }

  private:
    bool want_ = false;
};

class BluetoothService final : public Service {
  public:
    const char *name() const override { return "bluetooth"; }

    bool init(WaylandState &app) override {
        want_ =
            app.upower.bus && bluetooth_init(app.bluetooth, *app.upower.bus);
        if (!want_)
            klog("bluetooth: no system bus available - bluetooth info "
                 "unavailable");
        return true;
    }

    void timer_tick(WaylandState &app) override {
        if (!want_)
            return;
        bluetooth_tick(
            app.bluetooth,
            [&app](const std::string &summary, const std::string &body) {
                bluetooth_notify(app, summary, body);
            },
            std::chrono::steady_clock::now(),
            [&app] { redraw_and_present(app); });
    }

  private:
    bool want_ = false;
};

class TrayService final : public Service {
  public:
    const char *name() const override { return "tray"; }

    bool init(WaylandState &app) override {
        if (!tray_init(app.tray))
            klog("tray: no session bus available - system tray unavailable");
        return true;
    }

    std::vector<FnPollSource> poll_sources(WaylandState &app) override {
        std::vector<FnPollSource> sources;
        if (!app.tray.bus)
            return sources;
        sources.push_back(sdbus_poll_source(*app.tray.bus, [&app] {
            int budget = 32;
            while (budget-- > 0 && app.tray.bus->processPendingEvent()) {
            }
            if (app.tray.dirty) {
                app.tray.dirty = false;
                redraw_and_present(app);
            }
        }));
        return sources;
    }
};

class MprisService final : public Service {
  public:
    const char *name() const override { return "mpris"; }

    bool init(WaylandState &app) override {
        if (!mpris_init(app.mpris))
            klog("mpris: no session bus available - media info unavailable");
        return true;
    }

    std::vector<FnPollSource> poll_sources(WaylandState &app) override {
        std::vector<FnPollSource> sources;
        if (!app.mpris.bus)
            return sources;
        sources.push_back(sdbus_poll_source(*app.mpris.bus, [&app] {
            int budget = 32;
            while (budget-- > 0 && app.mpris.bus->processPendingEvent()) {
            }
            for (auto &m : app.overlays)
                if (m->is_open()) {
                    m->request_frame();
                    bar_detail::rest_egl_current(app);
                }
        }));
        return sources;
    }
};

class CompositorWorkspaceService final : public Service {
  public:
    const char *name() const override { return "compositor-workspace"; }

    bool init(WaylandState &app) override {
        if (shoji_init(app.shoji))
            app.compositor_backend = WaylandState::CompositorBackend::ShojiWM;
        else if (hypr_init(app.hypr))
            app.compositor_backend = WaylandState::CompositorBackend::Hyprland;
        klog("compositor backend: %s",
             app.compositor_backend == WaylandState::CompositorBackend::ShojiWM
                 ? "shojiwm"
             : app.compositor_backend ==
                     WaylandState::CompositorBackend::Hyprland
                 ? "hyprland"
                 : "none");
        return true;
    }

    std::vector<FnPollSource> poll_sources(WaylandState &app) override {
        std::vector<FnPollSource> sources;
        int fd =
            app.compositor_backend == WaylandState::CompositorBackend::Hyprland
                ? app.hypr.event_fd
            : app.compositor_backend == WaylandState::CompositorBackend::ShojiWM
                ? app.shoji.fd
                : -1;
        if (fd < 0)
            return sources;
        sources.emplace_back(fd, POLLIN, [&app] {
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
                    redraw_all_monitors(app);
                } else if (r == HyprEventResult::ActiveChanged) {
                    redraw_all_monitors(app);
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
                    redraw_all_monitors(app);
                }
            }
        });
        return sources;
    }
};

class IdleService final : public Service {
  public:
    const char *name() const override { return "idle"; }

    bool init(WaylandState &app) override {
        app.idle.timeout_seconds = app.cfg.idle_timeout_seconds;
        app.idle.on_idle_command = app.cfg.idle_command;
        app.idle.on_resume_command = app.cfg.idle_resume_command;
        return idle_init(app.idle);
    }
};

} // namespace

std::vector<std::unique_ptr<Service>> build_services() {
    std::vector<std::unique_ptr<Service>> services;
    services.push_back(std::make_unique<NotificationBusService>());
    services.push_back(std::make_unique<BrightnessOsdService>());
    services.push_back(std::make_unique<PipewireOsdService>());
    services.push_back(std::make_unique<UpowerService>());
    services.push_back(std::make_unique<NetworkService>());
    services.push_back(std::make_unique<BluetoothService>());
    services.push_back(std::make_unique<TrayService>());
    services.push_back(std::make_unique<MprisService>());
    services.push_back(std::make_unique<CompositorWorkspaceService>());
    services.push_back(std::make_unique<IdleService>());
    return services;
}

#pragma once

#include <memory>
#include <vector>

#include "app/module.h"
#include "app/per_monitor_module.h"

#include "modules/notification.h"
#include "modules/osd.h"
#include "modules/wallpaper.h"

struct Config;

class OsdPerMonitorModule final : public PerMonitorModule {
  public:
    OsdState &state() { return state_; }

    bool create_surface(WaylandState &app, MonitorOutput &mon,
                        wl_output *output) override;
    bool configured() const override;
    bool init_egl(WaylandState &app, MonitorOutput &mon) override;
    void destroy(WaylandState &app, MonitorOutput &mon) override;
    bool owns_surface(wl_surface *surface) const override;
    void tick(WaylandState &app, MonitorOutput &mon) override;

  private:
    OsdState state_;
};

class WallpaperPerMonitorModule final : public PerMonitorModule {
  public:
    bool create_surface(WaylandState &app, MonitorOutput &mon,
                        wl_output *output) override;
    bool configured() const override;
    bool init_egl(WaylandState &app, MonitorOutput &mon) override;
    void destroy(WaylandState &app, MonitorOutput &mon) override;
    bool owns_surface(wl_surface *surface) const override;

    void resync(WaylandState &app, MonitorOutput &mon, const Config &new_cfg);

    void pause_animation();
    void resume_animation();
    WallpaperHwDecodeStatus decode_status(int column_index) const;

  private:
    WallpaperState state_;
};

class NotificationViewPerMonitorModule final : public PerMonitorModule {
  public:
    bool create_surface(WaylandState &app, MonitorOutput &mon,
                        wl_output *output) override;
    bool configured() const override;
    bool init_egl(WaylandState &app, MonitorOutput &mon) override;
    void destroy(WaylandState &app, MonitorOutput &mon) override;
    bool owns_surface(wl_surface *surface) const override;
    void request_frame() override;

    void resync(WaylandState &app, MonitorOutput &mon);

  private:
    NotificationView state_;
};

std::vector<std::unique_ptr<Module>> build_app_modules();
std::vector<std::unique_ptr<PerMonitorModule>> build_per_monitor_modules();

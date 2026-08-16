#pragma once

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "app/per_monitor_module.h"
#include "app/wayland_state.h"
#include "render/animation.h"
#include "render/renderer.h"
#include "render/scene.h"
#include "service/active_output.h"
#include "service/frame_clock.h"
#include "service/output_scale.h"
#include "service/workspace.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct AutoHideState {
    bool hidden = false;
    bool collapsed = false;
    float opacity = 1.0f;

    bool enabled = false;
};

struct MonitorOutput {
    WaylandState *app = nullptr;
    Output output;
    bool activated = false;
    wl_surface *surface = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    int32_t width = 0;
    bool configured = false;
    OutputScale output_scale;
    FrameClock frame_clock;
    Scene scene;
    AnimationManager animations;
    AutoHideState autohide;
    std::vector<std::unique_ptr<PerMonitorModule>> modules;

    template <typename T> T *module() const {
        for (auto &m : modules)
            if (T *t = dynamic_cast<T *>(m.get()))
                return t;
        return nullptr;
    }
};

void monitor_output_destroy(MonitorOutput &mon);
void monitor_output_create_surfaces(WaylandState &app, MonitorOutput &mon);
void monitor_output_wait_configured(WaylandState &app, MonitorOutput &mon);
void monitor_output_finish_egl(WaylandState &app, MonitorOutput &mon);
void monitor_output_activate(WaylandState &app, MonitorOutput &mon);
void request_all_frames(MonitorOutput &mon);

MonitorOutput *find_monitor_by_name_wl(WaylandState &app, wl_output *wl);
MonitorOutput *find_monitor_for_surface(WaylandState &app, wl_surface *surface);

struct SettingsState;

namespace bar_detail {

const std::vector<Workspace> &monitor_workspaces(const MonitorOutput &mon);

int monitor_active_workspace_id(const MonitorOutput &mon);

void rest_egl_current(WaylandState &app);

void apply_config_update(WaylandState &app, Config new_cfg);
void save_and_apply_config_update(WaylandState &app, Config new_cfg);
MonitorOutput *active_target_monitor(WaylandState &app);
void settings_retarget(WaylandState &app, SettingsState &settings,
                       MonitorOutput &target);

}

#pragma once

#include <EGL/egl.h>
#include <string>
#include <vector>
#include <wayland-client.h>

#include "render/overlay_panel.h"
#include "render/panel_chrome.h"
#include "render/rect.h"
#include "render/renderer.h"
#include "render/scene.h"
#include "render/texture_cache.h"

#include "service/keyboard.h"
#include "service/system_telemetry.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

constexpr float kSysMonTextRowHeight = 18.0f;
constexpr float kSysMonBarHeight = 6.0f;
constexpr float kSysMonBarRadius = 3.0f;
constexpr float kSysMonBarTopGap = 12.0f;
constexpr float kSysMonRowBottomPad = 10.0f;
constexpr float kSysMonRowHeight = kSysMonTextRowHeight + kSysMonBarTopGap +
                                   kSysMonBarHeight + kSysMonRowBottomPad;
constexpr float kSysMonNetRowHeight = 18.0f;
constexpr float kSysMonNetLabelWidth = 90.0f;

struct SystemMonitorPanelState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    TextureCache tcache;

    Rect panel_rect;
    std::vector<PanelClickRegion> click_regions;
    float locked_center_x = -1.0f;
    float locked_panel_h = -1.0f;
    float visible_height = -1.0f;
    float scroll_offset = 0.0f;
    float visible_content_height = 0.0f;

    float pending_pill_center_x = 0.0f;
    float pending_bar_height = 0.0f;
    float pending_bar_top_margin = 0.0f;
};

namespace system_monitor_panel_detail {

enum class RowKind {
    Cpu,
    Gpu,
    Ram,
    Disk,
    Divider,
    Network,
    Spacer,
};

struct PanelRow {
    RowKind kind;
    float height;
};

std::vector<PanelRow> build_rows(const GpuTempState &gpu);

float content_height(const std::vector<PanelRow> &rows);

float panel_height(const std::vector<PanelRow> &rows);

} // namespace system_monitor_panel_detail

bool system_monitor_panel_create_surface(SystemMonitorPanelState &state,
                                         wl_compositor *compositor,
                                         zwlr_layer_shell_v1 *layer_shell,
                                         wl_output *output = nullptr);

bool system_monitor_panel_init_egl(SystemMonitorPanelState &state,
                                   Renderer &renderer,
                                   const CpuTempState &cpu_temp,
                                   const GpuTempState &gpu_temp,
                                   const SystemStatsState &stats,
                                   EGLDisplay display, EGLConfig config,
                                   EGLContext context);

void system_monitor_panel_request_frame(SystemMonitorPanelState &state,
                                        float pill_center_x, float bar_height,
                                        float bar_top_margin);

void system_monitor_panel_toggle(SystemMonitorPanelState &state,
                                 float pill_center_x = -1.0f);

void system_monitor_panel_handle_scroll(SystemMonitorPanelState &state,
                                        const GpuTempState &gpu, double dy);

void system_monitor_panel_handle_click(SystemMonitorPanelState &state,
                                       double px, double py);

void system_monitor_panel_handle_key_event(SystemMonitorPanelState &state,
                                           const KeyEvent &event);

void system_monitor_panel_paint(SystemMonitorPanelState &state,
                                const CpuTempState &cpu_temp,
                                const GpuTempState &gpu_temp,
                                const SystemStatsState &stats,
                                float pill_center_x, float bar_height,
                                float bar_top_margin);

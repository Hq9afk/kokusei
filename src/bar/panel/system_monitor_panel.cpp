#include <GLES2/gl2.h>
#include <algorithm>
#include <cmath>

#include "bar/panel/system_monitor_panel.h"

#include "render/icon.h"
#include "render/icons.h"
#include "render/palette.h"
#include "render/panel_scroll.h"
#include "render/text.h"

#include "service/layer_surface.h"

namespace system_monitor_panel_detail {

namespace {
constexpr float kWarnColor[4] = {0.8784f, 0.6588f, 0.2275f, 1.0f};
} // namespace

std::vector<PanelRow> build_rows(const GpuTempState &gpu) {
    std::vector<PanelRow> rows;
    rows.push_back({RowKind::Cpu, kSysMonRowHeight});
    if (gpu_stats_available(gpu))
        rows.push_back({RowKind::Gpu, kSysMonRowHeight});
    rows.push_back({RowKind::Ram, kSysMonRowHeight});
    rows.push_back({RowKind::Disk, kSysMonRowHeight});
    rows.push_back({RowKind::Divider, 1.0f});
    rows.push_back({RowKind::Network, kSysMonNetRowHeight});
    rows.push_back({RowKind::Spacer, kPanelTrailingSpacerHeight});
    return rows;
}

float content_height(const std::vector<PanelRow> &rows) {
    float h = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i > 0)
            h += kPanelListSpacing;
        h += rows[i].height;
    }
    return h;
}

float panel_height(const std::vector<PanelRow> &rows) {
    float h = kPanelPadding + kPanelHeaderHeight + kPanelHeaderDividerGap +
              1.0f + kPanelContentGap + content_height(rows) + kPanelPadding;
    return std::min(kPanelMaxHeight, h);
}

} // namespace system_monitor_panel_detail

bool system_monitor_panel_create_surface(SystemMonitorPanelState &state,
                                         wl_compositor *compositor,
                                         zwlr_layer_shell_v1 *layer_shell,
                                         wl_output *output) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-system-monitor-panel", output);
}

bool system_monitor_panel_init_egl(SystemMonitorPanelState &state,
                                   Renderer &renderer,
                                   const CpuTempState &cpu_temp,
                                   const GpuTempState &gpu_temp,
                                   const SystemStatsState &stats,
                                   EGLDisplay display, EGLConfig config,
                                   EGLContext context) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state, &cpu_temp, &gpu_temp, &stats] {
        system_monitor_panel_paint(
            state, cpu_temp, gpu_temp, stats, state.pending_pill_center_x,
            state.pending_bar_height, state.pending_bar_top_margin);
    };
    return true;
}

void system_monitor_panel_request_frame(SystemMonitorPanelState &state,
                                        float pill_center_x, float bar_height,
                                        float bar_top_margin) {
    state.pending_pill_center_x = pill_center_x;
    state.pending_bar_height = bar_height;
    state.pending_bar_top_margin = bar_top_margin;
    overlay_panel_request_frame(state.base);
}

void system_monitor_panel_toggle(SystemMonitorPanelState &state,
                                 float pill_center_x) {
    panel_lock_toggle(
        state.base, state.locked_center_x, pill_center_x,
        [&state] { state.visible_height = -1.0f; },
        [&state] {
            state.scroll_offset = 0.0f;
            state.base.animations.animate(
                state.visible_height, 0.0f, kOverlayFadeMs,
                Easing::EaseOutCubic,
                [&state](float v) { state.visible_height = v; }, {},
                kPanelHeightAnimOwner);
        });
}

void system_monitor_panel_handle_scroll(SystemMonitorPanelState &state,
                                        const GpuTempState &gpu, double dy) {
    state.scroll_offset =
        panel_clamp_scroll(state.scroll_offset, static_cast<float>(dy),
                           system_monitor_panel_detail::content_height(
                               system_monitor_panel_detail::build_rows(gpu)),
                           state.visible_content_height);
}

void system_monitor_panel_handle_click(SystemMonitorPanelState &state,
                                       double px, double py) {
    auto hit = [](const Rect &r, double x, double y) {
        return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y &&
               y < r.y + r.h;
    };

    for (const PanelClickRegion &region : state.click_regions) {
        if (!hit(region.rect, px, py))
            continue;
        if (region.kind == PanelClickKind::Close)
            system_monitor_panel_toggle(state);
        return;
    }

    if (!hit(state.panel_rect, px, py))
        system_monitor_panel_toggle(state);
}

void system_monitor_panel_handle_key_event(SystemMonitorPanelState &state,
                                           const KeyEvent &event) {
    if (event.kind == KeyKind::Escape)
        system_monitor_panel_toggle(state);
}

using namespace system_monitor_panel_detail;

namespace {

const float *stat_bar_color(float pct) {
    if (pct > 0.9f)
        return rgba(palette::critical);
    if (pct > 0.7f)
        return kWarnColor;
    return rgba(palette::accent);
}

} // namespace

void system_monitor_panel_paint(SystemMonitorPanelState &state,
                                const CpuTempState &cpu_temp,
                                const GpuTempState &gpu_temp,
                                const SystemStatsState &stats,
                                float pill_center_x, float bar_height,
                                float bar_top_margin) {
    using namespace panel_chrome_detail;
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    state.base.animations.tick(std::chrono::steady_clock::now());
    eglMakeCurrent(state.base.egl_display, state.base.egl_surface,
                   state.base.egl_surface, state.base.egl_context);
    int32_t scale = state.base.output_scale.scale;
    state.renderer->begin_frame(state.base.width, state.base.height, scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.click_regions.clear();
    state.panel_rect = {};
    state.scene.rebuild();

    if (!state.base.open) {
        state.scene.draw(*state.renderer);
        eglSwapBuffers(state.base.egl_display, state.base.egl_surface);
        return;
    }

    Node *root = &state.scene.root;

    std::vector<PanelRow> rows = build_rows(gpu_temp);
    float panel_w = kPanelWidth;
    float panel_h = panel_height(rows);
    if (state.locked_center_x < 0.0f)
        state.locked_center_x = pill_center_x;
    if (state.visible_height < 0.0f) {
        state.visible_height = 0.0f;
        state.base.animations.animate(
            state.visible_height, panel_h, kOverlayFadeMs, Easing::EaseOutCubic,
            [&state](float v) { state.visible_height = v; }, {},
            kPanelHeightAnimOwner);
    }
    float panel_x = std::clamp(
        state.locked_center_x - panel_w / 2.0f, kPanelSideMargin,
        static_cast<float>(state.base.width) - panel_w - kPanelSideMargin);
    float panel_y = bar_height + bar_top_margin + kPanelGap;
    state.panel_rect = {panel_x, panel_y, panel_w, panel_h};

    const float *white = rgba(palette::text);
    const float *dim = rgba(palette::text_dim);

    panel_draw_box(root, panel_x, panel_y, panel_w, panel_h);
    float header_y = panel_y + kPanelPadding;
    panel_draw_header(root, state.tcache, scale, "System", panel_x, panel_y,
                      panel_w, state.click_regions);

    float divider_y = header_y + kPanelHeaderHeight + kPanelHeaderDividerGap;
    node_add_rect(root, panel_x + kPanelPadding, divider_y,
                  panel_w - 2 * kPanelPadding, 1.0f,
                  rgba(palette::text_alpha06));

    PanelScrollRegion region =
        panel_scroll_region(panel_x, panel_y, panel_w, panel_h);
    float content_x = region.content_x;
    float content_w = region.content_w;
    float content_top = region.content_top;
    float content_bottom = region.content_bottom;

    state.visible_content_height = std::max(0.0f, content_bottom - content_top);

    Node *scroll_clip =
        node_add_group(root, panel_x, content_top, panel_w,
                       std::max(0.0f, content_bottom - content_top), true);

    auto rx = [&](float v) { return v - panel_x; };
    auto ry = [&](float v) { return v - content_top; };

    auto draw_stat_row = [&](Node *clip, float y, const std::string &label,
                             const std::string &extra, const std::string &value,
                             float pct) {
        const float *bar_color = stat_bar_color(pct);
        std::string full_label = extra.empty() ? label : label + "  " + extra;
        const Texture *label_tex = cached_text(state.tcache, full_label, scale);
        if (label_tex)
            node_add_texture(
                clip, rx(content_x),
                ry(y + (kSysMonTextRowHeight - label_tex->height) / 2.0f),
                *label_tex, dim);
        const Texture *value_tex = cached_text(state.tcache, value, scale);
        if (value_tex)
            node_add_texture(
                clip, rx(content_x + content_w - value_tex->width),
                ry(y + (kSysMonTextRowHeight - value_tex->height) / 2.0f),
                *value_tex, bar_color);

        float bar_y = y + kSysMonTextRowHeight + kSysMonBarTopGap;
        node_add_rrect(clip, rx(content_x), ry(bar_y), content_w,
                       kSysMonBarHeight, kSysMonBarRadius, 0.0f,
                       rgba(palette::text_alpha08), kPanelNoBorder);
        node_add_rrect(
            clip, rx(content_x), ry(bar_y),
            std::max(kSysMonBarRadius * 2, content_w * std::min(pct, 1.0f)),
            kSysMonBarHeight, kSysMonBarRadius, 0.0f, bar_color,
            kPanelNoBorder);
    };

    float y = content_top - state.scroll_offset;
    for (size_t i = 0; i < rows.size(); ++i) {
        const PanelRow &row = rows[i];
        if (i > 0)
            y += kPanelListSpacing;
        Node *clip = scroll_clip;
        float row_h = row.height;
        bool row_visible = y + row_h > content_top && y < content_bottom;
        if (!row_visible) {
            y += row_h;
            continue;
        }

        switch (row.kind) {
        case RowKind::Cpu: {
            std::string extra =
                (cpu_temp_available(cpu_temp)
                     ? std::to_string(static_cast<int>(cpu_temp.celsius)) +
                           "\xC2\xB0"
                           "C \xE2\x80\x94 "
                     : std::string()) +
                (stats.cpu_freq_ghz > 0.0f
                     ? std::to_string(stats.cpu_freq_ghz).substr(0, 3) + "GHz"
                     : std::string());
            int pct = stats.cpu_usage >= 0.0f
                          ? static_cast<int>(std::lround(stats.cpu_usage * 100))
                          : 0;
            draw_stat_row(clip, y, "CPU", extra, std::to_string(pct) + "%",
                          std::max(0.0f, stats.cpu_usage));
            break;
        }
        case RowKind::Gpu: {
            std::string extra =
                gpu_temp_available(gpu_temp)
                    ? std::to_string(static_cast<int>(gpu_temp.celsius)) +
                          "\xC2\xB0"
                          "C"
                    : std::string();
            int pct = static_cast<int>(std::max(0.0f, gpu_temp.usage_percent));
            draw_stat_row(clip, y, "GPU", extra, std::to_string(pct) + "%",
                          gpu_temp.usage_percent / 100.0f);
            break;
        }
        case RowKind::Ram: {
            std::string extra =
                stats.mem_total_gb > 0.0f
                    ? std::to_string(stats.mem_used_gb).substr(0, 3) +
                          " GiB / " +
                          std::to_string(stats.mem_total_gb).substr(0, 3) +
                          " GiB"
                    : std::string();
            int pct = stats.mem_usage >= 0.0f
                          ? static_cast<int>(std::lround(stats.mem_usage * 100))
                          : 0;
            draw_stat_row(clip, y, "RAM", extra, std::to_string(pct) + "%",
                          std::max(0.0f, stats.mem_usage));
            break;
        }
        case RowKind::Disk: {
            std::string extra =
                stats.disk_total_gb > 0.0f
                    ? std::to_string(stats.disk_used_gb).substr(0, 3) +
                          " GiB / " +
                          std::to_string(stats.disk_total_gb).substr(0, 3) +
                          " GiB"
                    : std::string();
            int pct =
                static_cast<int>(std::lround(std::max(0.0f, stats.disk_pct)));
            draw_stat_row(clip, y, "Disk", extra, std::to_string(pct) + "%",
                          std::max(0.0f, stats.disk_pct) / 100.0f);
            break;
        }
        case RowKind::Divider:
            node_add_rect(clip, rx(content_x), ry(y), content_w, 1.0f,
                          rgba(palette::text_alpha06));
            break;
        case RowKind::Network: {
            const Texture *down_icon =
                cached_icon(state.tcache, icon::arrow_narrow_down, scale);
            const Texture *up_icon =
                cached_icon(state.tcache, icon::arrow_narrow_up, scale);
            std::string rx_str = system_stats_detail_format_speed(
                std::max(0.0, stats.net_rx_bps));
            std::string tx_str = system_stats_detail_format_speed(
                std::max(0.0, stats.net_tx_bps));
            float ix = content_x;
            if (down_icon) {
                node_add_texture(clip, rx(ix), ry(y), *down_icon, dim);
                ix += static_cast<float>(down_icon->width) + kPanelTightGap;
            }
            const Texture *rx_tex = cached_text(state.tcache, rx_str, scale);
            if (rx_tex)
                node_add_texture(clip, rx(ix), ry(y), *rx_tex, white);
            ix = content_x + kSysMonNetLabelWidth;
            if (up_icon) {
                node_add_texture(clip, rx(ix), ry(y), *up_icon, dim);
                ix += static_cast<float>(up_icon->width) + kPanelTightGap;
            }
            const Texture *tx_tex = cached_text(state.tcache, tx_str, scale);
            if (tx_tex)
                node_add_texture(clip, rx(ix), ry(y), *tx_tex, white);
            break;
        }
        case RowKind::Spacer:
            break;
        }
        y += row_h;
    }

    if (state.base.animations.hasActive()) {
        ScopedClip clip(*state.renderer, panel_x, panel_y, panel_w,
                        std::max(0.0f, state.visible_height));
        state.scene.draw(*state.renderer);
    } else {
        state.scene.draw(*state.renderer);
    }
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);

    if (state.base.animations.hasActive())
        overlay_panel_request_frame(state.base);
}

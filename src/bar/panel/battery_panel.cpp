#include <GLES2/gl2.h>
#include <algorithm>
#include <cstdio>

#include "bar/panel/battery_panel.h"

#include "render/icon.h"
#include "render/icons.h"
#include "render/palette.h"
#include "render/panel_scroll.h"
#include "render/text.h"

#include "service/layer_surface.h"

namespace battery_panel_detail {

namespace {

constexpr uint32_t kTypeBattery = 2;
constexpr uint32_t kTypeMonitor = 4;
constexpr uint32_t kStateCharging = 1;
constexpr uint32_t kStateDischarging = 2;
constexpr uint32_t kStateFullyCharged = 4;
constexpr uint32_t kStatePendingCharge = 5;

constexpr float kWarnColor[4] = {0.8784f, 0.6588f, 0.2275f, 1.0f};

} // namespace

bool is_battery_type(uint32_t type) {
    return type == kTypeBattery || type == kTypeMonitor;
}

std::string format_time(int seconds) {
    if (seconds <= 0)
        return "";
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    char buf[16];
    if (h > 0)
        std::snprintf(buf, sizeof(buf), "%dh %dm", h, m);
    else
        std::snprintf(buf, sizeof(buf), "%dm", m);
    return buf;
}

std::vector<PanelRow> build_rows(const UpowerState &u) {
    std::vector<PanelRow> rows;

    bool detected = false;
    std::vector<const UpowerDeviceEntry *> matched;
    for (const auto &d : u.devices) {
        if (is_battery_type(d->type) && d->present)
            matched.push_back(d.get());
        if (d->type == kTypeBattery && d->present)
            detected = true;
    }

    bool all_full = !u.on_battery;
    for (const UpowerDeviceEntry *d : matched) {
        if (d->state != kStateFullyCharged) {
            all_full = false;
            break;
        }
    }

    if (detected && all_full)
        rows.push_back({RowKind::EmptyPluggedIn, kBatteryEmptyStateHeight});
    if (!all_full)
        for (const UpowerDeviceEntry *d : matched)
            rows.push_back({RowKind::Device, kBatteryDeviceRowHeight, d});
    if (!detected)
        rows.push_back({RowKind::EmptyNoBattery, kBatteryEmptyStateHeight});

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

} // namespace battery_panel_detail

bool battery_panel_create_surface(BatteryPanelState &state,
                                  wl_compositor *compositor,
                                  zwlr_layer_shell_v1 *layer_shell,
                                  wl_output *output) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-battery-panel", output);
}

bool battery_panel_init_egl(BatteryPanelState &state, Renderer &renderer,
                            UpowerState &u, EGLDisplay display,
                            EGLConfig config, EGLContext context) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state, &u] {
        battery_panel_paint(state, u, state.pending_pill_center_x,
                            state.pending_bar_height,
                            state.pending_bar_top_margin);
    };
    return true;
}

void battery_panel_request_frame(BatteryPanelState &state, float pill_center_x,
                                 float bar_height, float bar_top_margin) {
    state.pending_pill_center_x = pill_center_x;
    state.pending_bar_height = bar_height;
    state.pending_bar_top_margin = bar_top_margin;
    overlay_panel_request_frame(state.base);
}

void battery_panel_toggle(BatteryPanelState &state, float pill_center_x) {
    panel_lock_toggle(
        state.base, state.locked_center_x, pill_center_x,
        [&state] { state.visible_height = -1.0f; },
        [&state] {
            state.scroll_offset = 0.0f;
            state.base.animations.animate(
                state.visible_height, 0.0f, kOverlayFadeMs,
                Easing::EaseOutCubic,
                [&state](float v) { state.visible_height = v; },
                [&state] {
                    state.visible_height = -1.0f;
                    state.locked_center_x = -1.0f;
                    state.locked_panel_h = -1.0f;
                },
                kPanelHeightAnimOwner);
        });
}

void battery_panel_handle_scroll(BatteryPanelState &state, const UpowerState &u,
                                 double dy) {
    state.scroll_offset =
        panel_clamp_scroll(state.scroll_offset, static_cast<float>(dy),
                           battery_panel_detail::content_height(
                               battery_panel_detail::build_rows(u)),
                           state.visible_content_height);
}

void battery_panel_handle_click(BatteryPanelState &state, double px,
                                double py) {
    auto hit = [](const Rect &r, double x, double y) {
        return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y &&
               y < r.y + r.h;
    };

    for (const PanelClickRegion &region : state.click_regions) {
        if (!hit(region.rect, px, py))
            continue;
        if (region.kind == PanelClickKind::Close)
            battery_panel_toggle(state);
        return;
    }

    if (!hit(state.panel_rect, px, py))
        battery_panel_toggle(state);
}

void battery_panel_handle_key_event(BatteryPanelState &state,
                                    const KeyEvent &event) {
    if (event.kind == KeyKind::Escape)
        battery_panel_toggle(state);
}

using namespace battery_panel_detail;

void battery_panel_paint(BatteryPanelState &state, const UpowerState &u,
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

    std::vector<PanelRow> rows = build_rows(u);
    float panel_w = kPanelWidth;
    if (state.locked_center_x < 0.0f)
        state.locked_center_x = pill_center_x;
    if (state.locked_panel_h < 0.0f)
        state.locked_panel_h = panel_height(rows);
    float panel_h = state.locked_panel_h;
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
    panel_draw_header(root, state.tcache, scale, "Battery", panel_x, panel_y,
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
        case RowKind::EmptyPluggedIn:
        case RowKind::EmptyNoBattery: {
            const char *text = row.kind == RowKind::EmptyPluggedIn
                                   ? "Plugged in"
                                   : "No Battery Detected";
            const Texture *t = cached_text(state.tcache, text, scale);
            if (t)
                node_add_texture(clip,
                                 rx(content_x + (content_w - t->width) / 2.0f),
                                 ry(y + (row_h - t->height) / 2.0f), *t, dim);
            break;
        }
        case RowKind::Device: {
            const UpowerDeviceEntry &d = *row.entry;
            bool charging = d.state == kStateCharging;
            bool full = d.state == kStateFullyCharged;
            bool pending = d.state == kStatePendingCharge ||
                           (d.state == kStateDischarging && !u.on_battery);

            const float *bar_color = white;
            if (charging || full)
                bar_color = rgba(palette::accent);
            else if (d.percent <= 15)
                bar_color = rgba(palette::critical);
            else if (d.percent <= 30)
                bar_color = kWarnColor;
            else
                bar_color = rgba(palette::text_muted);

            std::string name =
                d.native_path.empty() ? "Battery" : d.native_path;
            const char *state_str = charging  ? "Charging"
                                    : full    ? "Full"
                                    : pending ? "Pending"
                                              : "Discharging";
            std::string time_str =
                format_time(charging ? d.time_to_full_s : d.time_to_empty_s);

            const Texture *name_tex = cached_text(state.tcache, name, scale);
            float name_w = 0.0f;
            if (name_tex) {
                node_add_texture(
                    clip, rx(content_x),
                    ry(y + (kBatteryTextRowHeight - name_tex->height) / 2.0f),
                    *name_tex, white);
                name_w = static_cast<float>(name_tex->width) + kPanelTightGap;
            }
            std::string status =
                std::string(state_str) +
                (time_str.empty() ? "" : " \xE2\x80\x94 " + time_str);
            const Texture *status_tex =
                cached_text(state.tcache, status, scale);
            if (status_tex)
                node_add_texture(
                    clip, rx(content_x + name_w),
                    ry(y + (kBatteryTextRowHeight - status_tex->height) / 2.0f),
                    *status_tex, charging ? rgba(palette::accent) : dim);

            std::string pct_str = std::to_string(d.percent) + "%";
            const Texture *pct_tex = cached_text(state.tcache, pct_str, scale);
            float pct_w = pct_tex ? static_cast<float>(pct_tex->width) : 0.0f;

            float bar_y = y + kBatteryTextRowHeight + kBatteryBarTopGap;
            float bar_w =
                content_w - pct_w - (pct_tex ? kPanelContentGap : 0.0f);
            node_add_rrect(clip, rx(content_x), ry(bar_y), bar_w,
                           kBatteryPanelBarHeight, kBatteryPanelBarRadius, 0.0f,
                           rgba(palette::text_alpha08), kPanelNoBorder);
            node_add_rrect(clip, rx(content_x), ry(bar_y),
                           std::max(kBatteryPanelBarRadius * 2,
                                    bar_w * std::min(d.percent, 100) / 100.0f),
                           kBatteryPanelBarHeight, kBatteryPanelBarRadius, 0.0f,
                           bar_color, kPanelNoBorder);
            if (pct_tex)
                node_add_texture(
                    clip, rx(content_x + bar_w + kPanelContentGap),
                    ry(bar_y +
                       (kBatteryPanelBarHeight - pct_tex->height) / 2.0f),
                    *pct_tex, bar_color);
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

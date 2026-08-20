#include <GLES2/gl2.h>
#include <algorithm>
#include <cairo/cairo.h>
#include <cmath>
#include <filesystem>
#include <string>

#include "app/monitor_output.h"
#include "app/user_info.h"
#include "app/wayland_state.h"

#include "modules/controlcenter.h"

#include "render/icon.h"
#include "render/icons.h"
#include "render/image.h"
#include "render/node.h"
#include "render/palette.h"
#include "render/panel_scroll.h"
#include "render/slider.h"
#include "render/text.h"

#include "service/layer_surface.h"
#include "service/mpris_service.h"
#include "service/pipewire.h"
#include "service/system_telemetry.h"
#include "service/upower_service.h"

bool controlcenter_create_surface(ControlCenterState &state,
                                  wl_compositor *compositor,
                                  zwlr_layer_shell_v1 *layer_shell,
                                  wl_output *output) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-controlcenter", output);
}

bool controlcenter_init_egl(ControlCenterState &state, Renderer &renderer,
                            WaylandState &app, EGLDisplay display,
                            EGLConfig config, EGLContext context) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state, &app] {
        controlcenter_paint(state, app, state.pending_bar_height,
                            state.pending_bar_top_margin);
    };
    // ponytail: only ~/.face (PNG/JPG) is tried; keqing-shell's userPfp is a
    // GIF, which this codebase's image loader can't decode. Upgrade path:
    // add GIF support to render/image.cpp, or a config field for the path.
    if (const char *home = getenv("HOME")) {
        std::string face_path = std::string(home) + "/.face";
        if (std::filesystem::exists(face_path))
            state.avatar_tex = load_image_texture(face_path);
    }
    return true;
}

void controlcenter_retarget(ControlCenterState &state,
                            wl_compositor *compositor,
                            zwlr_layer_shell_v1 *layer_shell,
                            wl_display *display, Renderer &renderer,
                            WaylandState &app, EGLDisplay egl_display,
                            EGLConfig egl_config, EGLContext egl_context,
                            wl_output *target_output, const char *target_name) {
    wl_output *bound = overlay_panel_retarget(
        state.base, display, state.bound_output, target_output, target_name,
        [&](wl_output *out) {
            return controlcenter_create_surface(state, compositor, layer_shell,
                                                out);
        },
        [&] {
            return controlcenter_init_egl(state, renderer, app, egl_display,
                                          egl_config, egl_context);
        });
    if (bound)
        state.bound_output = bound;
}

void controlcenter_request_frame(ControlCenterState &state, float bar_height,
                                 float bar_top_margin) {
    state.pending_bar_height = bar_height;
    state.pending_bar_top_margin = bar_top_margin;
    overlay_panel_request_frame(state.base);
}

void controlcenter_toggle(ControlCenterState &state, bool by_widget) {
    if (!state.base.open)
        state.opened_by_widget = by_widget;
    else
        state.dragging.reset();
    overlay_panel_toggle(state.base);
}

std::vector<IpcHandler>
controlcenter_ipc_handlers(ControlCenterState &controlcenter,
                           WaylandState &state) {
    return {
        {"controlcenter",
         [&controlcenter, &state] {
             if (!controlcenter.base.open) {
                 MonitorOutput *target =
                     bar_detail::active_target_monitor(state);
                 if (target &&
                     (target->output.wl != controlcenter.bound_output ||
                      !controlcenter.base.layer_surface))
                     controlcenter_retarget(
                         controlcenter, state.compositor, state.layer_shell,
                         state.display, state.renderer, state,
                         state.egl_display, state.egl_config, state.egl_context,
                         target->output.wl, target->output.name.c_str());
             }
             controlcenter_toggle(controlcenter);
         },
         "toggle the control center"},
    };
}

namespace {
void open_settings(WaylandState &app) {
    for (auto &m : app.overlays) {
        if (std::string(m->name()) != "settings")
            continue;
        for (IpcHandler &h : m->ipc_handlers(app))
            if (std::string(h.verb) == "settings") {
                h.fn();
                return;
            }
    }
}
} // namespace

void controlcenter_handle_click(ControlCenterState &state, WaylandState &app,
                                double px, double py) {
    auto hit = [](const Rect &r, double x, double y) {
        return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y &&
               y < r.y + r.h;
    };

    double scrolled_py = py + state.scroll_offset;
    for (const PanelClickRegion &region : state.click_regions) {
        if (!hit(region.rect, px, scrolled_py))
            continue;
        switch (region.kind) {
        case PanelClickKind::Close:
            controlcenter_toggle(state);
            break;
        case PanelClickKind::MuteToggle: {
            uint32_t id =
                volume_slider_resolve_tag_id(app.pipewire, region.tag);
            if (id != 0) {
                auto it = app.pipewire.nodes.find(id);
                if (it != app.pipewire.nodes.end())
                    pipewire_set_node_muted(app.pipewire, id,
                                            !it->second.muted);
            }
            break;
        }
        case PanelClickKind::SliderDrag: {
            state.dragging = DraggedSlider{region.tag, region.rect};
            state.selected_slider_tag = region.tag;
            volume_slider_apply_drag(app.pipewire, *state.dragging, px);
            break;
        }
        case PanelClickKind::MediaPlayPause:
            mpris_play_pause(app.mpris);
            break;
        case PanelClickKind::MediaNext:
            mpris_next(app.mpris);
            break;
        case PanelClickKind::MediaPrevious:
            mpris_previous(app.mpris);
            break;
        case PanelClickKind::ProfileSettings:
            open_settings(app);
            break;
        default:
            break;
        }
        return;
    }

    if (!hit(state.panel_rect, px, py))
        controlcenter_toggle(state);
}

void controlcenter_handle_pointer_move(ControlCenterState &state,
                                       PipewireState &pw, double px) {
    if (!state.dragging)
        return;
    volume_slider_apply_drag(pw, *state.dragging, px);
}

void controlcenter_handle_scroll(ControlCenterState &state, double dy) {
    state.scroll_offset = panel_clamp_scroll(
        state.scroll_offset, static_cast<float>(dy), state.content_height,
        state.visible_height);
}

void controlcenter_handle_key_event(ControlCenterState &state,
                                    PipewireState &pw, const KeyEvent &event) {
    switch (event.kind) {
    case KeyKind::Escape:
        controlcenter_toggle(state);
        break;
    case KeyKind::Left:
    case KeyKind::Right: {
        if (state.selected_slider_tag.empty())
            break;
        uint32_t id =
            volume_slider_resolve_tag_id(pw, state.selected_slider_tag);
        if (id == 0)
            break;
        auto it = pw.nodes.find(id);
        if (it == pw.nodes.end())
            break;
        float step = event.kind == KeyKind::Right ? 0.01f : -0.01f;
        pipewire_set_node_volume(
            pw, id, std::clamp(it->second.level + step, 0.0f, 1.0f));
        break;
    }
    default:
        break;
    }
}

using namespace panel_chrome_detail;

namespace {

struct CardChrome {
    float content_x;
    float content_y;
    float box_h;
};

CardChrome card_chrome_draw(Node *root, TextureCache &tcache, int32_t scale,
                            float x, float y, float w, float content_h,
                            const std::string &title) {
    float box_h = kCardTopPadding + kCardHeaderHeight + kCardHeaderContentGap +
                  content_h + kCardBottomPadding;
    node_add_rrect(root, x, y, w, box_h, kCardRadius, kCardBorderWidth,
                   rgba(palette::overlay), rgba(palette::accent));

    float header_y = y + kCardTopPadding;
    const Texture *title_tex = cached_text(tcache, title, scale);
    if (title_tex)
        node_add_texture(root, x + kCardHorizontalPadding,
                         header_y +
                             (kCardHeaderHeight - title_tex->height) / 2.0f,
                         *title_tex, rgba(palette::text));

    float content_x = x + kCardHorizontalPadding;
    float content_y = header_y + kCardHeaderHeight + kCardHeaderContentGap;
    return {content_x, content_y, box_h};
}

float draw_profile_card(Node *root, TextureCache &tcache, int32_t scale,
                        float x, float y, float w, const Texture &avatar_tex,
                        std::vector<PanelClickRegion> &regions) {
    const Texture *name_tex = cached_text(tcache, user_info::username(), scale);
    const Texture *uptime_tex =
        cached_text(tcache, user_info::uptime_string(), scale);
    float info_h = (name_tex ? name_tex->height : 0) + kProfileInfoSpacing +
                   (uptime_tex ? uptime_tex->height : 0);
    float h = kProfileVerticalPadding + kProfileAvatarSize + kProfileAvatarGap +
              info_h;

    node_add_rrect(root, x, y, w, h, kProfileRadius, kProfileBorderWidth,
                   rgba(palette::overlay), rgba(palette::accent));

    float avatar_x = x + (w - kProfileAvatarSize) / 2.0f;
    float avatar_y = y + kProfileTopPadding;
    node_add_rrect(root, avatar_x, avatar_y, kProfileAvatarSize,
                   kProfileAvatarSize, kProfileAvatarSize / 2.0f,
                   kProfileAvatarRingWidth, rgba(palette::overlay),
                   rgba(palette::accent));
    if (avatar_tex.id) {
        // ponytail: no circular texture clip in this renderer, so the photo
        // is a plain square inside the ring. Upgrade if that reads wrong.
        node_add_texture_rect(root, avatar_x, avatar_y, kProfileAvatarSize,
                              kProfileAvatarSize, avatar_tex,
                              rgba(palette::text));
    } else {
        const Texture *avatar_icon = cached_icon(tcache, icon::user, scale);
        if (avatar_icon)
            node_add_texture(root,
                             avatar_x +
                                 (kProfileAvatarSize - avatar_icon->width) /
                                     2.0f,
                             avatar_y +
                                 (kProfileAvatarSize - avatar_icon->height) /
                                     2.0f,
                             *avatar_icon, rgba(palette::text));
    }

    const Texture *settings_icon = cached_icon(tcache, icon::settings, scale);
    if (settings_icon) {
        float sx = x + w - kCardHorizontalPadding - settings_icon->width;
        float sy = y + kProfileTopPadding;
        node_add_texture(root, sx, sy, *settings_icon, rgba(palette::text));
        Rect hit = {sx - kProfileSettingsHitPadding,
                   sy - kProfileSettingsHitPadding,
                   settings_icon->width + 2 * kProfileSettingsHitPadding,
                   settings_icon->height + 2 * kProfileSettingsHitPadding};
        regions.push_back({PanelClickKind::ProfileSettings, hit, ""});
    }

    float info_y = avatar_y + kProfileAvatarSize + kProfileAvatarGap;
    if (name_tex)
        node_add_texture(root, x + (w - name_tex->width) / 2.0f, info_y,
                         *name_tex, rgba(palette::text));
    if (uptime_tex)
        node_add_texture(root, x + (w - uptime_tex->width) / 2.0f,
                         info_y + (name_tex ? name_tex->height : 0) +
                             kProfileInfoSpacing,
                         *uptime_tex, rgba(palette::text_dim));

    return h;
}

const char *battery_glyph(const UpowerState &u) {
    if (u.full)
        return icon::plugged_in;
    if (u.charging)
        return icon::battery_charging;
    if (u.percent <= 25)
        return icon::battery1;
    if (u.percent <= 50)
        return icon::battery2;
    if (u.percent <= 75)
        return icon::battery3;
    return icon::battery4;
}

float draw_battery_card(Node *root, TextureCache &tcache, int32_t scale,
                        float x, float y, float w, const UpowerState &upower) {
    if (!upower.present)
        return kCardGatedHeight;

    const Texture *icon_tex = cached_icon(tcache, battery_glyph(upower), scale);
    std::string label =
        upower.full ? "Plugged in" : (std::to_string(upower.percent) + "%");
    const Texture *label_tex = cached_text(tcache, "Battery  " + label, scale);

    float header_h =
        std::max(icon_tex ? icon_tex->height : 0.0f,
                 label_tex ? static_cast<float>(label_tex->height) : 0.0f);
    float content_h = header_h + kBatteryRowSpacing + kBatteryBarHeight;

    CardChrome chrome =
        card_chrome_draw(root, tcache, scale, x, y, w, content_h, "Battery");
    float cx = chrome.content_x, cy = chrome.content_y;
    float content_w = w - 2 * kCardHorizontalPadding;

    if (icon_tex)
        node_add_texture(root, cx, cy + (header_h - icon_tex->height) / 2.0f,
                         *icon_tex, rgba(palette::text));
    if (label_tex)
        node_add_texture(
            root, cx + (icon_tex ? icon_tex->width : 0) + kBatteryHeaderSpacing,
            cy + (header_h - label_tex->height) / 2.0f, *label_tex,
            rgba(palette::text));

    float bar_y = cy + header_h + kBatteryRowSpacing;
    node_add_rrect(root, cx, bar_y, content_w, kBatteryBarHeight,
                   kBatteryBarRadius, 0.0f, rgba(palette::text_alpha11),
                   kPanelNoBorder);
    float fill_w = content_w * std::clamp(upower.percent / 100.0f, 0.0f, 1.0f);
    if (fill_w > 0.0f)
        node_add_rrect(root, cx, bar_y, fill_w, kBatteryBarHeight,
                       kBatteryBarRadius, 0.0f, rgba(palette::accent),
                       kPanelNoBorder);

    return chrome.box_h;
}

const Texture *cached_gauge(TextureCache &tcache, int32_t scale, float value01,
                            const Color &fill_color) {
    int diameter = static_cast<int>(kGaugeDiameter * scale);
    int bucket =
        static_cast<int>(std::round(std::clamp(value01, 0.0f, 1.0f) * 100.0f));
    std::string key =
        "gauge:" + std::to_string(diameter) + ":" + std::to_string(bucket) +
        ":" + std::to_string(static_cast<int>(fill_color.r * 255)) + "," +
        std::to_string(static_cast<int>(fill_color.g * 255)) + "," +
        std::to_string(static_cast<int>(fill_color.b * 255));
    return tcache.get(key, [&]() -> RasterizedText {
        cairo_surface_t *surface =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, diameter, diameter);
        cairo_t *cr = cairo_create(surface);
        float cx = diameter / 2.0f, cy = diameter / 2.0f;
        float stroke = kGaugeStroke * scale;
        float radius = diameter / 2.0f - stroke / 2.0f;
        float start = -static_cast<float>(M_PI) / 2.0f;
        float full = 2.0f * static_cast<float>(M_PI);

        cairo_set_line_width(cr, stroke);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

        cairo_set_source_rgba(cr, fill_color.r, fill_color.g, fill_color.b,
                              0.15f);
        cairo_arc(cr, cx, cy, radius, 0.0, full);
        cairo_stroke(cr);

        float value = static_cast<float>(bucket) / 100.0f;
        if (value > 0.0f) {
            cairo_set_source_rgba(cr, fill_color.r, fill_color.g, fill_color.b,
                                  fill_color.a);
            cairo_arc(cr, cx, cy, radius, start, start + full * value);
            cairo_stroke(cr);
        }

        cairo_surface_flush(surface);
        RasterizedText result = surface_to_rgba(surface, diameter, diameter);
        result.scale = scale;
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return result;
    });
}

void draw_gauge(Node *root, TextureCache &tcache, int32_t scale, float x,
                float y, float value01, const Color &fill_color,
                const char *icon_glyph, const std::string &value_label,
                const std::string &sub_label) {
    const Texture *gauge_tex = cached_gauge(tcache, scale, value01, fill_color);
    if (gauge_tex)
        node_add_texture(root, x, y, *gauge_tex, rgba(palette::text));

    const Texture *icon_tex = cached_icon(tcache, icon_glyph, scale);
    const Texture *value_tex = cached_text_clipped(
        tcache, value_label, scale, static_cast<int>(kGaugeDiameter));

    float icon_h = icon_tex ? icon_tex->height : 0.0f;
    float value_h = value_tex ? value_tex->height : 0.0f;
    float gap = icon_tex && value_tex ? kGaugeIconValueGap : 0.0f;
    float stack_y = y + (kGaugeDiameter - icon_h - gap - value_h) / 2.0f;

    if (icon_tex) {
        node_add_texture(root, x + (kGaugeDiameter - icon_tex->width) / 2.0f,
                         stack_y, *icon_tex, rgba(fill_color));
        stack_y += icon_h + gap;
    }
    if (value_tex)
        node_add_texture(root, x + (kGaugeDiameter - value_tex->width) / 2.0f,
                         stack_y, *value_tex, rgba(palette::text));

    const Texture *sub_tex = cached_text(tcache, sub_label, scale);
    float sub_y = y + kGaugeDiameter + kStatsGaugeLabelSpacing;
    if (sub_tex)
        node_add_texture(root, x + (kGaugeDiameter - sub_tex->width) / 2.0f,
                         sub_y, *sub_tex, rgba(palette::text_dim));
}

constexpr Color kGaugeColorCpu = color("#ef4444");
constexpr Color kGaugeColorGpu = color("#a855f7");
constexpr Color kGaugeColorRam = color("#3b82f6");
constexpr Color kGaugeColorDisk = color("#22c55e");
constexpr Color kTempWarnColor = color("#f97316");

const Color &temp_color(float celsius) {
    if (celsius >= 85.0f)
        return palette::critical;
    if (celsius >= 70.0f)
        return kTempWarnColor;
    return palette::text;
}

float draw_system_stats_card(Node *root, TextureCache &tcache, int32_t scale,
                             float x, float y, float w,
                             const SystemStatsState &stats,
                             const GpuTempState &gpu_temp) {
    bool show_gpu = gpu_stats_available(gpu_temp);
    bool show_disk = stats.disk_pct >= 0.0f;
    int gauge_count = 2 + (show_gpu ? 1 : 0) + (show_disk ? 1 : 0);
    float total_gauge_w = kGaugeDiameter * gauge_count;
    float content_w = w - 2 * kCardHorizontalPadding;
    float gap = kStatsColumnGap;
    const Texture *label_h_tex = cached_text(tcache, "CPU", scale);
    float content_h = kGaugeDiameter + kStatsGaugeLabelSpacing +
                      (label_h_tex ? label_h_tex->height : 0.0f);

    CardChrome chrome =
        card_chrome_draw(root, tcache, scale, x, y, w, content_h, "System");
    float cx = chrome.content_x, cy = chrome.content_y;

    float row_w = total_gauge_w + gap * (gauge_count - 1);
    float gx = cx + (content_w - row_w) / 2.0f;

    float cpu01 = std::max(0.0f, stats.cpu_usage);
    draw_gauge(root, tcache, scale, gx, cy, cpu01, kGaugeColorCpu, icon::cpu,
              stats.cpu_usage >= 0.0f
                  ? std::to_string(static_cast<int>(cpu01 * 100.0f)) + "%"
                  : "--",
              "CPU");
    gx += kGaugeDiameter + gap;

    if (show_gpu) {
        float gpu01 = std::max(0.0f, gpu_temp.usage_percent / 100.0f);
        draw_gauge(root, tcache, scale, gx, cy, gpu01, kGaugeColorGpu,
                  icon::gpu,
                  std::to_string(static_cast<int>(gpu_temp.usage_percent)) +
                      "%",
                  "GPU");
        gx += kGaugeDiameter + gap;
    }

    float mem01 = std::max(0.0f, stats.mem_usage);
    draw_gauge(root, tcache, scale, gx, cy, mem01, kGaugeColorRam,
              icon::settings,
              stats.mem_usage >= 0.0f
                  ? std::to_string(static_cast<int>(mem01 * 100.0f)) + "%"
                  : "--",
              "RAM");
    gx += kGaugeDiameter + gap;

    if (show_disk) {
        float disk01 = std::clamp(stats.disk_pct / 100.0f, 0.0f, 1.0f);
        draw_gauge(root, tcache, scale, gx, cy, disk01, kGaugeColorDisk,
                  icon::folder,
                  std::to_string(static_cast<int>(stats.disk_pct)) + "%",
                  "DISK");
    }

    return chrome.box_h;
}

float draw_cpu_temp_card(Node *root, TextureCache &tcache, int32_t scale,
                         float x, float y, float w,
                         const CpuTempState &cpu_temp) {
    std::vector<const CpuCoreTemp *> cores;
    for (const CpuCoreTemp &core : cpu_temp.cores)
        if (core.celsius >= 0.0f)
            cores.push_back(&core);

    std::string headline =
        (cpu_temp_available(cpu_temp)
             ? std::to_string(static_cast<int>(cpu_temp.celsius))
             : "--") +
        "°C";
    const Texture *headline_tex = cached_text_large(tcache, headline, scale);
    float headline_h =
        std::max(headline_tex ? static_cast<float>(headline_tex->height) : 0.0f,
                 kTempRowHeight);

    float content_w = w - 2 * kCardHorizontalPadding;
    float cell_w =
        (content_w - (kCpuCoreColumns - 1) * kCpuCoreColumnSpacing) /
        kCpuCoreColumns;
    int rows = cores.empty()
                  ? 0
                  : static_cast<int>(
                        (cores.size() + kCpuCoreColumns - 1) / kCpuCoreColumns);
    float grid_h =
        cores.empty() ? 0.0f
                      : kCpuTempGridTopMargin + rows * kCpuCoreItemHeight +
                            std::max(0, rows - 1) * kCpuCoreRowSpacing;
    float content_h = headline_h + grid_h;

    CardChrome chrome = card_chrome_draw(root, tcache, scale, x, y, w,
                                         content_h, "CPU Temperature");
    float cx = chrome.content_x, cy = chrome.content_y;

    if (headline_tex)
        node_add_texture(root, cx, cy, *headline_tex,
                         rgba(temp_color(cpu_temp.celsius)));

    float grid_y = cy + headline_h + kCpuTempGridTopMargin;
    for (size_t i = 0; i < cores.size(); ++i) {
        const CpuCoreTemp &core = *cores[i];
        int col = static_cast<int>(i) % kCpuCoreColumns;
        int row = static_cast<int>(i) / kCpuCoreColumns;
        float cell_x = cx + col * (cell_w + kCpuCoreColumnSpacing);
        float cell_y = grid_y + row * (kCpuCoreItemHeight + kCpuCoreRowSpacing);
        node_add_rrect(root, cell_x, cell_y, cell_w, kCpuCoreItemHeight,
                       kCpuCoreItemRadius, 0.0f, rgba(palette::text_alpha08),
                       kPanelNoBorder);

        const Texture *name_tex = cached_text(tcache, core.label, scale);
        if (name_tex)
            node_add_texture(
                root, cell_x + kCpuCoreTextMargin,
                cell_y + (kCpuCoreItemHeight - name_tex->height) / 2.0f,
                *name_tex, rgba(palette::text_dim));

        std::string value_label =
            std::to_string(static_cast<int>(core.celsius)) + "°C";
        const Texture *value_tex = cached_text(tcache, value_label, scale);
        if (value_tex)
            node_add_texture(root,
                             cell_x + cell_w - kCpuCoreTextMargin -
                                 value_tex->width,
                             cell_y +
                                 (kCpuCoreItemHeight - value_tex->height) /
                                     2.0f,
                             *value_tex, rgba(temp_color(core.celsius)));
    }

    return chrome.box_h;
}

float draw_gpu_temp_card(Node *root, TextureCache &tcache, int32_t scale,
                         float x, float y, float w,
                         const GpuTempState &gpu_temp) {
    if (!gpu_temp_available(gpu_temp))
        return kCardGatedHeight;

    std::string headline =
        std::to_string(static_cast<int>(gpu_temp.celsius)) + "°C";
    const Texture *headline_tex = cached_text_large(tcache, headline, scale);
    float content_h =
        std::max(headline_tex ? static_cast<float>(headline_tex->height) : 0.0f,
                 kTempRowHeight);

    CardChrome chrome = card_chrome_draw(root, tcache, scale, x, y, w,
                                         content_h, "GPU Temperature");
    float cx = chrome.content_x, cy = chrome.content_y;

    if (headline_tex)
        node_add_texture(root, cx, cy, *headline_tex,
                         rgba(temp_color(gpu_temp.celsius)));

    return chrome.box_h;
}

Rect draw_media_button(Node *root, TextureCache &tcache, int32_t scale, float x,
                       float y, float size, float radius, const char *glyph,
                       std::vector<PanelClickRegion> &regions,
                       PanelClickKind kind) {
    Rect rect = {x, y, size, size};
    node_add_rrect(root, rect.x, rect.y, rect.w, rect.h, radius, 0.0f,
                   rgba(palette::overlay), kPanelNoBorder);
    const Texture *tex = cached_icon(tcache, glyph, scale);
    if (tex)
        node_add_texture(root, rect.x + (rect.w - tex->width) / 2.0f,
                         rect.y + (rect.h - tex->height) / 2.0f, *tex,
                         rgba(palette::text));
    regions.push_back({kind, rect, ""});
    return rect;
}

float draw_media_card(Node *root, TextureCache &tcache, int32_t scale, float x,
                      float y, float w, const MprisState &mpris,
                      std::unordered_map<std::string, Texture> &art_cache,
                      std::vector<PanelClickRegion> &regions) {
    float content_h = kMediaThumbSize + kMediaProgressTopMargin +
                      kMediaProgressRowHeight + kMediaCtrlTopMargin +
                      kMediaCtrlRowHeight;

    CardChrome chrome =
        card_chrome_draw(root, tcache, scale, x, y, w, content_h, "Media");
    float cx = chrome.content_x, cy = chrome.content_y;
    float content_w = w - 2 * kCardHorizontalPadding;

    node_add_rrect(root, cx, cy, kMediaThumbSize, kMediaThumbSize,
                   kMediaThumbRadius, 0.0f, rgba(palette::overlay),
                   kPanelNoBorder);
    const Texture *art_tex = nullptr;
    if (mpris.has_player &&
        mpris_detail_is_local_art_url(mpris.track.art_url)) {
        // ponytail: no percent-decoding of the file:// path. Local players
        // write plain-ASCII cache paths in practice; upgrade if one doesn't.
        std::string path = mpris.track.art_url.substr(7);
        auto it = art_cache.find(path);
        if (it == art_cache.end())
            it = art_cache.emplace(path, load_image_texture(path)).first;
        if (it->second.id)
            art_tex = &it->second;
    }
    if (art_tex) {
        // ponytail: square crop, no rounded-corner texture draw in this
        // renderer. Upgrade if the square corners peeking out look wrong.
        node_add_texture_rect(root, cx, cy, kMediaThumbSize, kMediaThumbSize,
                              *art_tex, rgba(palette::text));
    } else {
        const Texture *note_tex = cached_icon(tcache, icon::music_note, scale);
        if (note_tex)
            node_add_texture(
                root, cx + (kMediaThumbSize - note_tex->width) / 2.0f,
                cy + (kMediaThumbSize - note_tex->height) / 2.0f, *note_tex,
                rgba(palette::text_dim));
    }

    float text_x = cx + kMediaThumbSize + kMediaTitleLeftMargin;
    float text_w = content_w - kMediaThumbSize - kMediaTitleLeftMargin;
    std::string title = mpris.has_player ? mpris.track.title : "No player";
    std::string artist = mpris.has_player ? mpris.track.artist : "";
    const Texture *title_tex =
        cached_text_clipped(tcache, title.empty() ? "Unknown" : title, scale,
                            static_cast<int>(std::max(20.0f, text_w)));
    const Texture *artist_tex = cached_text_clipped(
        tcache, artist, scale, static_cast<int>(std::max(20.0f, text_w)));
    float title_h = title_tex ? title_tex->height : 0.0f;
    if (title_tex)
        node_add_texture(root, text_x,
                         cy + kMediaThumbSize / 2.0f - title_h -
                             kMediaTitleSpacing / 2.0f,
                         *title_tex, rgba(palette::text));
    if (artist_tex)
        node_add_texture(root, text_x,
                         cy + kMediaThumbSize / 2.0f +
                             kMediaTitleSpacing / 2.0f,
                         *artist_tex, rgba(palette::text_dim));

    float progress_y = cy + kMediaThumbSize + kMediaProgressTopMargin;
    if (mpris.has_player) {
        std::string progress_label =
            mpris_detail_format_position(mpris.track.position_us) + " / " +
            mpris_detail_format_position(mpris.track.length_us);
        const Texture *progress_tex =
            cached_text(tcache, progress_label, scale);
        if (progress_tex)
            node_add_texture(
                root, cx + (content_w - progress_tex->width) / 2.0f,
                progress_y +
                    (kMediaProgressRowHeight - progress_tex->height) / 2.0f,
                *progress_tex, rgba(palette::text_dim));
    }

    float ctrl_y =
        progress_y + kMediaProgressRowHeight + kMediaCtrlTopMargin;
    float ctrl_row_w =
        2 * kMediaSideBtnSize + kMediaPlayBtnSize + 2 * kMediaCtrlSpacing;
    float btn_x = cx + (content_w - ctrl_row_w) / 2.0f;
    float side_btn_y =
        ctrl_y + (kMediaCtrlRowHeight - kMediaSideBtnSize) / 2.0f;
    float play_btn_y =
        ctrl_y + (kMediaCtrlRowHeight - kMediaPlayBtnSize) / 2.0f;

    draw_media_button(root, tcache, scale, btn_x, side_btn_y, kMediaSideBtnSize,
                      kMediaSideBtnRadius, icon::player_prev, regions,
                      PanelClickKind::MediaPrevious);
    btn_x += kMediaSideBtnSize + kMediaCtrlSpacing;
    const char *play_glyph = mpris.status == MprisPlaybackStatus::Playing
                                 ? icon::player_pause
                                 : icon::player_play;
    draw_media_button(root, tcache, scale, btn_x, play_btn_y, kMediaPlayBtnSize,
                      kMediaPlayBtnRadius, play_glyph, regions,
                      PanelClickKind::MediaPlayPause);
    btn_x += kMediaPlayBtnSize + kMediaCtrlSpacing;
    draw_media_button(root, tcache, scale, btn_x, side_btn_y, kMediaSideBtnSize,
                      kMediaSideBtnRadius, icon::player_next, regions,
                      PanelClickKind::MediaNext);

    return chrome.box_h;
}

std::string default_node_label(const PipewireState &pw, bool is_sink) {
    uint32_t id = is_sink ? pw.default_sink_id : pw.default_source_id;
    auto it = pw.nodes.find(id);
    if (it == pw.nodes.end())
        return "";
    return it->second.description.empty() ? it->second.name
                                          : it->second.description;
}

float draw_volume_row(Node *root, TextureCache &tcache, int32_t scale, float x,
                      float y, float w, const char *label,
                      const std::string &device, const char *glyph, bool muted,
                      float level, std::vector<PanelClickRegion> &regions,
                      const char *region_tag) {
    const Texture *label_tex = cached_text(tcache, label, scale);
    std::string device_text = device.empty() ? "" : " \xE2\x80\x94 " + device;
    const Texture *device_tex =
        cached_text_clipped(tcache, device_text, scale,
                            static_cast<int>(kVolumeDeviceTextMaxWidth));
    float label_row_h = label_tex ? label_tex->height : 0.0f;

    if (label_tex)
        node_add_texture(root, x, y, *label_tex, rgba(palette::text));
    if (device_tex)
        node_add_texture(root,
                         x + (label_tex ? label_tex->width : 0) +
                             kVolumeLabelRowSpacing,
                         y, *device_tex, rgba(palette::text_dim));

    float slider_y = y + label_row_h + kVolumeRowSpacing;
    float mute_x = x + w - kVolumeMuteBtnSize;
    float pct_x = mute_x - kVolumePctMuteGap - kVolumePctTextWidth;
    float slider_right = pct_x - kVolumeSliderPctGap;
    Rect slider_rect = {x, slider_y, slider_right - x, kVolumeSliderRowHeight};
    draw_slider_track(root, regions, slider_rect, slider_rect,
                      kVolumeCardSliderTrackHeight, muted ? 0.0f : level, muted,
                      region_tag);

    std::string pct_label =
        muted ? "muted"
              : std::to_string(static_cast<int>(std::round(level * 100.0f))) +
                    "%";
    const Texture *pct_tex = cached_text(tcache, pct_label, scale);
    if (pct_tex)
        node_add_texture(root, pct_x + kVolumePctTextWidth - pct_tex->width,
                         slider_y +
                             (kVolumeSliderRowHeight - pct_tex->height) / 2.0f,
                         *pct_tex, rgba(palette::text_dim));

    Rect mute_rect = {
        mute_x, slider_y + (kVolumeSliderRowHeight - kVolumeMuteBtnSize) / 2.0f,
        kVolumeMuteBtnSize, kVolumeMuteBtnSize};
    node_add_rrect(root, mute_rect.x, mute_rect.y, mute_rect.w, mute_rect.h,
                   kVolumeMuteBtnRadius, 0.0f, rgba(palette::overlay),
                   kPanelNoBorder);
    const Texture *icon_tex = cached_icon(tcache, glyph, scale);
    if (icon_tex)
        node_add_texture(root,
                         mute_rect.x + (mute_rect.w - icon_tex->width) / 2.0f,
                         mute_rect.y + (mute_rect.h - icon_tex->height) / 2.0f,
                         *icon_tex, rgba(palette::text));
    regions.push_back({PanelClickKind::MuteToggle, mute_rect, region_tag});

    return label_row_h + kVolumeRowSpacing + kVolumeSliderRowHeight;
}

float draw_volume_card(Node *root, TextureCache &tcache, int32_t scale, float x,
                       float y, float w, const PipewireState &pw,
                       std::vector<PanelClickRegion> &regions) {
    bool sink_muted = false, source_muted = false;
    float sink_level = pipewire_sink_level(pw, sink_muted);
    float source_level = pipewire_source_level(pw, source_muted);

    const Texture *probe = cached_text(tcache, "Output", scale);
    float row_h = (probe ? probe->height : 0.0f) + kVolumeRowSpacing +
                  kVolumeSliderRowHeight;
    float content_h = 2 * row_h + kVolumeCardSpacing;

    CardChrome chrome =
        card_chrome_draw(root, tcache, scale, x, y, w, content_h, "Volume");
    float cx = chrome.content_x, cy = chrome.content_y;
    float content_w = w - 2 * kCardHorizontalPadding;

    float sink_glyph_level = sink_muted ? 0.0f : sink_level;
    const char *sink_glyph =
        volume_threshold_icon(sink_muted, sink_glyph_level);
    float row1_h =
        draw_volume_row(root, tcache, scale, cx, cy, content_w, "Output",
                        default_node_label(pw, true), sink_glyph, sink_muted,
                        sink_level, regions, "sink");

    float row2_y = cy + row1_h + kVolumeCardSpacing;
    const char *source_glyph = source_muted ? icon::mic_off : icon::mic_on;
    draw_volume_row(root, tcache, scale, cx, row2_y, content_w, "Input",
                    default_node_label(pw, false), source_glyph, source_muted,
                    source_level, regions, "source");

    return chrome.box_h;
}

} // namespace

void controlcenter_paint(ControlCenterState &state, WaylandState &app,
                         float bar_height, float bar_top_margin) {
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

    float panel_w = kControlCenterPanelWidth;
    float panel_x =
        static_cast<float>(state.base.width) - panel_w - kPanelSideMargin;
    float panel_y = bar_top_margin + bar_height + kPanelGap;

    // ponytail: visible_height uses last frame's content_height, so a card
    // appearing/disappearing (battery, gpu temp) lags the panel height by one
    // frame. Upgrade to a real two-pass measure if that lag ever shows.
    float screen_budget = std::max(
        0.0f, static_cast<float>(state.base.height) - panel_y - kPanelSideMargin);
    float content_h_est =
        state.content_height > 0.0f ? state.content_height : screen_budget;
    float visible_height = std::min(screen_budget, content_h_est);
    state.scroll_offset = panel_clamp_scroll(state.scroll_offset, 0.0f,
                                             content_h_est, visible_height);

    Node *scroll_clip =
        node_add_group(root, panel_x, panel_y, panel_w, visible_height, true);
    Node *scroll_content =
        node_add_group(scroll_clip, -panel_x,
                       -panel_y - state.scroll_offset, panel_w, content_h_est,
                       false);

    float content_y = panel_y;
    content_y += draw_profile_card(scroll_content, state.tcache, scale,
                                   panel_x, content_y, panel_w,
                                   state.avatar_tex, state.click_regions);

    float battery_y = content_y + kPanelColumnSpacing;
    float battery_h = draw_battery_card(scroll_content, state.tcache, scale,
                                        panel_x, battery_y, panel_w,
                                        app.upower);
    if (battery_h > 0.0f)
        content_y = battery_y + battery_h;

    float stats_y = content_y + kPanelColumnSpacing;
    content_y = stats_y + draw_system_stats_card(
                              scroll_content, state.tcache, scale, panel_x,
                              stats_y, panel_w, app.system_stats,
                              app.gpu_temp);

    float cpu_y = content_y + kPanelColumnSpacing;
    content_y = cpu_y + draw_cpu_temp_card(scroll_content, state.tcache, scale,
                                           panel_x, cpu_y, panel_w,
                                           app.cpu_temp);

    float gpu_y = content_y + kPanelColumnSpacing;
    float gpu_h = draw_gpu_temp_card(scroll_content, state.tcache, scale,
                                     panel_x, gpu_y, panel_w, app.gpu_temp);
    if (gpu_h > 0.0f)
        content_y = gpu_y + gpu_h;

    float media_y = content_y + kPanelColumnSpacing;
    content_y = media_y + draw_media_card(scroll_content, state.tcache, scale,
                                          panel_x, media_y, panel_w, app.mpris,
                                          state.art_cache,
                                          state.click_regions);

    float volume_y = content_y + kPanelColumnSpacing;
    content_y = volume_y +
               draw_volume_card(scroll_content, state.tcache, scale, panel_x,
                                volume_y, panel_w, app.pipewire,
                                state.click_regions);

    state.content_height = content_y - panel_y;
    state.visible_height = visible_height;
    state.panel_rect = {panel_x, panel_y, panel_w, visible_height};

    state.renderer->set_opacity(state.base.opacity);
    state.scene.draw(*state.renderer);
    state.renderer->set_opacity(1.0f);
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);

    if (state.base.animations.hasActive())
        overlay_panel_request_frame(state.base);
}

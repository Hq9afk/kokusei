#include "modules/controlcenter.h"

#include "app/monitor_output.h"
#include "app/wayland_state.h"
#include "render/icon.h"
#include "render/icons.h"
#include "render/node.h"
#include "render/palette.h"
#include "render/text.h"
#include "service/layer_surface.h"
#include "service/mpris_service.h"
#include "service/pipewire.h"
#include "service/system_telemetry.h"
#include "service/upower_service.h"

#include <GLES2/gl2.h>
#include <cairo/cairo.h>
#include <pwd.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

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
    overlay_panel_toggle(state.base);
}

std::vector<IpcHandler> controlcenter_ipc_handlers(WaylandState &state) {
    return {
        {"controlcenter",
         [&state] {
             ControlCenterState &controlcenter = state.controlcenter;
             if (!controlcenter.base.open) {
                 MonitorOutput *target =
                     bar_detail::active_target_monitor(state);
                 if (target && target->output.wl != controlcenter.bound_output)
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

void controlcenter_handle_click(ControlCenterState &state, WaylandState &app,
                                double px, double py) {
    auto hit = [](const Rect &r, double x, double y) {
        return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y &&
               y < r.y + r.h;
    };

    for (const PanelClickRegion &region : state.click_regions) {
        if (!hit(region.rect, px, py))
            continue;
        switch (region.kind) {
        case PanelClickKind::Close:
            controlcenter_toggle(state);
            break;
        case PanelClickKind::MuteToggle: {
            bool muted = false;
            pipewire_sink_level(app.pipewire, muted);
            pipewire_set_node_muted(app.pipewire, app.pipewire.default_sink_id,
                                    !muted);
            break;
        }
        case PanelClickKind::SliderDrag: {
            float value01 =
                region.rect.w > 0.0f
                    ? static_cast<float>(px - region.rect.x) / region.rect.w
                    : 0.0f;
            pipewire_set_node_volume(app.pipewire, app.pipewire.default_sink_id,
                                     std::clamp(value01, 0.0f, 1.0f));
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
        default:
            break;
        }
        return;
    }

    if (!hit(state.panel_rect, px, py))
        controlcenter_toggle(state);
}

void controlcenter_handle_key_event(ControlCenterState &state,
                                    const KeyEvent &event) {
    if (event.kind == KeyKind::Escape)
        controlcenter_toggle(state);
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

std::string profile_username() {
    struct passwd *pw = getpwuid(getuid());
    if (!pw)
        return "unknown";
    if (pw->pw_gecos && pw->pw_gecos[0] != '\0') {
        std::string gecos = pw->pw_gecos;
        std::string name = gecos.substr(0, gecos.find(','));
        if (!name.empty())
            return name;
    }
    return pw->pw_name ? pw->pw_name : "unknown";
}

std::string profile_uptime() {
    struct sysinfo info;
    if (sysinfo(&info) != 0)
        return "";
    long hours = info.uptime / 3600;
    long minutes = (info.uptime % 3600) / 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%ldh %ldm", hours, minutes);
    return buf;
}

float draw_profile_card(Node *root, TextureCache &tcache, int32_t scale,
                        float x, float y, float w) {
    const Texture *name_tex = cached_text(tcache, profile_username(), scale);
    const Texture *uptime_tex = cached_text(tcache, profile_uptime(), scale);
    float info_h = (name_tex ? name_tex->height : 0) + kProfileInfoSpacing +
                   (uptime_tex ? uptime_tex->height : 0);
    float h = kProfileVerticalPadding + kProfileAvatarSize + kProfileAvatarGap +
              info_h;

    node_add_rrect(root, x, y, w, h, kProfileRadius, kProfileBorderWidth,
                   rgba(palette::overlay), rgba(palette::accent));

    float avatar_x = x + (w - kProfileAvatarSize) / 2.0f;
    float avatar_y = y + kProfileTopPadding;
    node_add_rrect(root, avatar_x, avatar_y, kProfileAvatarSize,
                   kProfileAvatarSize, kProfileAvatarSize / 2.0f, 0.0f,
                   rgba(palette::overlay), kPanelNoBorder);
    const Texture *avatar_icon = cached_icon(tcache, icon::user, scale);
    if (avatar_icon)
        node_add_texture(
            root, avatar_x + (kProfileAvatarSize - avatar_icon->width) / 2.0f,
            avatar_y + (kProfileAvatarSize - avatar_icon->height) / 2.0f,
            *avatar_icon, rgba(palette::text));

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

        cairo_set_source_rgba(cr, palette::text_alpha11.r,
                              palette::text_alpha11.g, palette::text_alpha11.b,
                              palette::text_alpha11.a);
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
                const std::string &value_label, const std::string &sub_label) {
    const Texture *gauge_tex = cached_gauge(tcache, scale, value01, fill_color);
    if (gauge_tex)
        node_add_texture(root, x, y, *gauge_tex, rgba(palette::text));

    const Texture *value_tex = cached_text_clipped(
        tcache, value_label, scale, static_cast<int>(kGaugeDiameter));
    if (value_tex)
        node_add_texture(root, x + (kGaugeDiameter - value_tex->width) / 2.0f,
                         y + (kGaugeDiameter - value_tex->height) / 2.0f,
                         *value_tex, rgba(palette::text));

    const Texture *sub_tex = cached_text(tcache, sub_label, scale);
    float sub_y = y + kGaugeDiameter + kStatsGaugeLabelSpacing;
    if (sub_tex)
        node_add_texture(root, x + (kGaugeDiameter - sub_tex->width) / 2.0f,
                         sub_y, *sub_tex, rgba(palette::text_dim));
}

float draw_system_stats_card(Node *root, TextureCache &tcache, int32_t scale,
                             float x, float y, float w,
                             const SystemStatsState &stats,
                             const GpuTempState &gpu_temp) {
    int gauge_count = gpu_temp_available(gpu_temp) ? 3 : 2;
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
    draw_gauge(root, tcache, scale, gx, cy, cpu01, palette::accent,
               stats.cpu_usage >= 0.0f
                   ? std::to_string(static_cast<int>(cpu01 * 100.0f)) + "%"
                   : "--",
               "CPU");
    gx += kGaugeDiameter + gap;

    float mem01 = std::max(0.0f, stats.mem_usage);
    draw_gauge(root, tcache, scale, gx, cy, mem01, palette::accent_alt,
               stats.mem_usage >= 0.0f
                   ? std::to_string(static_cast<int>(mem01 * 100.0f)) + "%"
                   : "--",
               "RAM");
    gx += kGaugeDiameter + gap;

    if (gpu_temp_available(gpu_temp)) {
        float gpu01 = std::clamp(gpu_temp.celsius / 100.0f, 0.0f, 1.0f);
        draw_gauge(root, tcache, scale, gx, cy, gpu01, palette::critical,
                   std::to_string(static_cast<int>(gpu_temp.celsius)) + "C",
                   "GPU");
    }

    return chrome.box_h;
}

float draw_cpu_temp_card(Node *root, TextureCache &tcache, int32_t scale,
                         float x, float y, float w,
                         const CpuTempState &cpu_temp) {
    const Texture *icon_tex = cached_icon(tcache, icon::cpu, scale);
    std::string label =
        "CPU  " +
        (cpu_temp_available(cpu_temp)
             ? std::to_string(static_cast<int>(cpu_temp.celsius)) + "C"
             : "--");
    const Texture *label_tex = cached_text(tcache, label, scale);
    float content_h =
        std::max(icon_tex ? icon_tex->height : 0.0f,
                 label_tex ? static_cast<float>(label_tex->height) : 0.0f);
    content_h = std::max(content_h, kTempRowHeight);

    CardChrome chrome = card_chrome_draw(root, tcache, scale, x, y, w,
                                         content_h, "CPU Temperature");
    float cx = chrome.content_x, cy = chrome.content_y;

    if (icon_tex)
        node_add_texture(root, cx, cy + (content_h - icon_tex->height) / 2.0f,
                         *icon_tex, rgba(palette::text));
    if (label_tex)
        node_add_texture(root,
                         cx + (icon_tex ? icon_tex->width : 0) +
                             kCardHorizontalPadding,
                         cy + (content_h - label_tex->height) / 2.0f,
                         *label_tex, rgba(palette::text));

    return chrome.box_h;
}

float draw_gpu_temp_card(Node *root, TextureCache &tcache, int32_t scale,
                         float x, float y, float w,
                         const GpuTempState &gpu_temp) {
    if (!gpu_temp_available(gpu_temp))
        return kCardGatedHeight;

    const Texture *icon_tex = cached_icon(tcache, icon::gpu, scale);
    std::string label =
        "GPU  " + std::to_string(static_cast<int>(gpu_temp.celsius)) + "C";
    const Texture *label_tex = cached_text(tcache, label, scale);
    float content_h =
        std::max(icon_tex ? icon_tex->height : 0.0f,
                 label_tex ? static_cast<float>(label_tex->height) : 0.0f);
    content_h = std::max(content_h, kTempRowHeight);

    CardChrome chrome = card_chrome_draw(root, tcache, scale, x, y, w,
                                         content_h, "GPU Temperature");
    float cx = chrome.content_x, cy = chrome.content_y;

    if (icon_tex)
        node_add_texture(root, cx, cy + (content_h - icon_tex->height) / 2.0f,
                         *icon_tex, rgba(palette::text));
    if (label_tex)
        node_add_texture(root,
                         cx + (icon_tex ? icon_tex->width : 0) +
                             kCardHorizontalPadding,
                         cy + (content_h - label_tex->height) / 2.0f,
                         *label_tex, rgba(palette::text));

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
                      std::vector<PanelClickRegion> &regions) {
    float content_h =
        kMediaThumbSize + kMediaCtrlTopMargin + kMediaCtrlRowHeight;

    CardChrome chrome =
        card_chrome_draw(root, tcache, scale, x, y, w, content_h, "Media");
    float cx = chrome.content_x, cy = chrome.content_y;
    float content_w = w - 2 * kCardHorizontalPadding;

    node_add_rrect(root, cx, cy, kMediaThumbSize, kMediaThumbSize,
                   kMediaThumbRadius, 0.0f, rgba(palette::overlay),
                   kPanelNoBorder);
    const Texture *note_tex = cached_icon(tcache, icon::music_note, scale);
    if (note_tex)
        node_add_texture(root, cx + (kMediaThumbSize - note_tex->width) / 2.0f,
                         cy + (kMediaThumbSize - note_tex->height) / 2.0f,
                         *note_tex, rgba(palette::text_dim));

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

    float ctrl_y = cy + kMediaThumbSize + kMediaCtrlTopMargin;
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
    std::string device_text = device.empty() ? "" : " " + device;
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
    float slider_track_y =
        slider_y + (kVolumeSliderRowHeight - kVolumeCardSliderHeight) / 2.0f;

    node_add_rrect(root, x, slider_track_y, slider_right - x,
                   kVolumeCardSliderHeight, kVolumeCardSliderHeight / 2.0f,
                   0.0f, rgba(palette::text_alpha11), kPanelNoBorder);
    float fill_w = (slider_right - x) * std::clamp(level, 0.0f, 1.0f);
    if (fill_w > 0.0f)
        node_add_rrect(root, x, slider_track_y, fill_w, kVolumeCardSliderHeight,
                       kVolumeCardSliderHeight / 2.0f, 0.0f,
                       muted ? rgba(palette::text_muted)
                             : rgba(palette::accent),
                       kPanelNoBorder);
    regions.push_back({PanelClickKind::SliderDrag,
                       {x, slider_y, slider_right - x, kVolumeSliderRowHeight},
                       region_tag});

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

    float content_y = panel_y;
    content_y += draw_profile_card(root, state.tcache, scale, panel_x,
                                   content_y, panel_w);

    float battery_y = content_y + kPanelColumnSpacing;
    float battery_h = draw_battery_card(root, state.tcache, scale, panel_x,
                                        battery_y, panel_w, app.upower);
    if (battery_h > 0.0f)
        content_y = battery_y + battery_h;

    float stats_y = content_y + kPanelColumnSpacing;
    content_y = stats_y + draw_system_stats_card(
                              root, state.tcache, scale, panel_x, stats_y,
                              panel_w, app.system_stats, app.gpu_temp);

    float cpu_y = content_y + kPanelColumnSpacing;
    content_y = cpu_y + draw_cpu_temp_card(root, state.tcache, scale, panel_x,
                                           cpu_y, panel_w, app.cpu_temp);

    float gpu_y = content_y + kPanelColumnSpacing;
    float gpu_h = draw_gpu_temp_card(root, state.tcache, scale, panel_x, gpu_y,
                                     panel_w, app.gpu_temp);
    if (gpu_h > 0.0f)
        content_y = gpu_y + gpu_h;

    float media_y = content_y + kPanelColumnSpacing;
    content_y =
        media_y + draw_media_card(root, state.tcache, scale, panel_x, media_y,
                                  panel_w, app.mpris, state.click_regions);

    float volume_y = content_y + kPanelColumnSpacing;
    content_y = volume_y + draw_volume_card(root, state.tcache, scale, panel_x,
                                            volume_y, panel_w, app.pipewire,
                                            state.click_regions);

    state.panel_rect = {panel_x, panel_y, panel_w, content_y - panel_y};

    state.renderer->set_opacity(state.base.opacity);
    state.scene.draw(*state.renderer);
    state.renderer->set_opacity(1.0f);
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);

    if (state.base.animations.hasActive())
        overlay_panel_request_frame(state.base);
}

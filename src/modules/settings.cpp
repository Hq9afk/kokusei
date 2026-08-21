#include <GLES2/gl2.h>
#include <algorithm>
#include <chrono>

#include "app/monitor_output.h"
#include "app/user_info.h"
#include "app/wayland_state.h"

#include "core/log.h"

#include "modules/settings.h"

#include "render/renderer.h"

#include "service/settings_service.h"
#include "service/wallpaper_service.h"

#include "settings/displays_tab.h"
#include "settings/idle_tab.h"

std::string settings_detail_format_field(const Config &cfg,
                                         SettingsFieldId id) {
    switch (id) {
    case SettingsFieldId::WallpaperPath:
        return cfg.wallpaper_path;
    case SettingsFieldId::WallpaperDir:
        return cfg.wallpaper_dir;
    case SettingsFieldId::WallpaperAnimatedDir:
        return cfg.wallpaper_animated_dir;
    case SettingsFieldId::IdleTimeout:
        return std::to_string(cfg.idle_timeout_seconds);
    case SettingsFieldId::IdleCommand:
        return cfg.idle_command;
    case SettingsFieldId::IdleResumeCommand:
        return cfg.idle_resume_command;
    default:
        return "";
    }
}

bool settings_create_surface(SettingsState &state, wl_compositor *compositor,
                             zwlr_layer_shell_v1 *layer_shell,
                             wl_output *output) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-settings", output);
}

bool settings_init_egl(
    SettingsState &state, const Config &cfg, Renderer &renderer,
    EGLDisplay display, EGLConfig config, EGLContext context,
    std::function<std::vector<std::string>()> monitor_names_fn,
    std::function<std::string()> focused_monitor_fn,
    std::function<WallpaperHwDecodeStatus(const std::string &, int)>
        wallpaper_decode_status_fn) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.wallpaper_decode_status = std::move(wallpaper_decode_status_fn);
    state.base.frame_clock.draw = [&state, &cfg, monitor_names_fn,
                                   focused_monitor_fn] {
        settings_paint(state, cfg, monitor_names_fn(), focused_monitor_fn());
    };
    state.wallpaper_static.picker.request_frame = [&state] {
        settings_request_frame(state);
    };
    state.wallpaper_animated.picker.request_frame = [&state] {
        settings_request_frame(state);
    };
    return true;
}

void settings_request_frame(SettingsState &state) {
    overlay_panel_request_frame(state.base);
}

void settings_commit_focused_field(SettingsState &state, const Config &cfg,
                                   const SettingsCommitFn &on_commit) {
    if (state.focused_field == SettingsFieldId::None)
        return;
    Config updated = cfg;
    settings_service_apply_field_text(updated, state.focused_field,
                                      state.field_buffer.text);
    on_commit(updated);
    state.focused_field = SettingsFieldId::None;
}

void settings_toggle(SettingsState &state, const Config &cfg,
                     const SettingsCommitFn &on_commit) {
    if (!state.base.layer_surface || state.base.egl_surface == EGL_NO_SURFACE) {
        klog("settings: toggle ignored, surface not ready (layer_surface=%p "
             "egl_surface_ready=%d)",
             static_cast<void *>(state.base.layer_surface),
             state.base.egl_surface != EGL_NO_SURFACE);
        return;
    }
    klog("settings: toggle called (was_open=%d opacity=%.2f "
         "focused_field=%d)",
         state.base.open, static_cast<double>(state.base.opacity),
         static_cast<int>(state.focused_field));
    if (state.base.open) {
        settings_commit_focused_field(state, cfg, on_commit);
        state.open_dropdown_id.clear();
    } else {
        state.active_tab = SettingsTab::Wallpaper;
    }
    overlay_panel_toggle(state.base);
    settings_request_frame(state);
}

std::vector<IpcHandler> settings_ipc_handlers(SettingsState &settings,
                                              WaylandState &state) {
    return {
        {"settings",
         [&settings, &state] {
             if (!settings.base.open && state.settings_enabled) {
                 MonitorOutput *target =
                     bar_detail::active_target_monitor(state);
                 if (target &&
                     (target->output.wl != state.settings_bound_output ||
                      !settings.base.layer_surface))
                     bar_detail::settings_retarget(state, settings, *target);
             }
             settings_toggle(settings, state.cfg, [&state](Config c) {
                 bar_detail::save_and_apply_config_update(state, c);
             });
         },
         "toggle the settings panel"},
    };
}

void settings_focus_field(SettingsState &state, const Config &cfg,
                          const SettingsCommitFn &on_commit,
                          SettingsFieldId id) {
    if (id == state.focused_field)
        return;
    settings_commit_focused_field(state, cfg, on_commit);
    state.focused_field = id;
    state.field_buffer.text = settings_detail_format_field(cfg, id);
    state.field_buffer.cursor_blink_visible = true;
}

void settings_handle_click(SettingsState &state, const Config &cfg,
                           const SettingsCommitFn &on_commit, double px,
                           double py) {
    auto hit = [](const Rect &r, double x, double y) {
        return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y &&
               y < r.y + r.h;
    };

    klog("settings: handle_click at (%.0f,%.0f), panel_rect=%.0f,%.0f "
         "%.0fx%.0f",
         px, py, static_cast<double>(state.panel_rect.x),
         static_cast<double>(state.panel_rect.y),
         static_cast<double>(state.panel_rect.w),
         static_cast<double>(state.panel_rect.h));
    for (const PanelClickRegion &region : state.click_regions) {
        if (!hit(region.rect, px, py))
            continue;
        klog("settings: click (%.0f,%.0f) hit region kind=%d", px, py,
             static_cast<int>(region.kind));
        if (!state.open_dropdown_id.empty() &&
            region.kind != PanelClickKind::DropdownToggle &&
            region.kind != PanelClickKind::DropdownSelect)
            state.open_dropdown_id.clear();
        switch (region.kind) {
        case PanelClickKind::Close:
            settings_toggle(state, cfg, on_commit);
            return;
        case PanelClickKind::TabSelect:
            settings_commit_focused_field(state, cfg, on_commit);
            state.active_tab = static_cast<SettingsTab>(std::stoi(region.tag));
            settings_request_frame(state);
            return;
        case PanelClickKind::ToggleFlip:
            if (!visualizer_tab_handle_click(state, cfg, on_commit, region) &&
                !wallpaper_tab_handle_click(state, cfg, on_commit, region))
                displays_tab_handle_click(state, cfg, on_commit, region);
            return;
        case PanelClickKind::FieldFocus:
            settings_focus_field(
                state, cfg, on_commit,
                static_cast<SettingsFieldId>(std::stoi(region.tag)));
            settings_request_frame(state);
            return;
        case PanelClickKind::MonitorSelect:
            if (state.active_tab == SettingsTab::Displays)
                displays_tab_handle_click(state, cfg, on_commit, region);
            else
                wallpaper_tab_handle_click(state, cfg, on_commit, region);
            return;
        case PanelClickKind::RegionSelect:
        case PanelClickKind::AnimatedRegionSelect:
            wallpaper_tab_handle_click(state, cfg, on_commit, region);
            return;
        case PanelClickKind::WallpaperSelect:
        case PanelClickKind::AnimatedWallpaperSelect:
            wallpaper_tab_handle_click(state, cfg, on_commit, region);
            return;
        case PanelClickKind::DropdownToggle:
            state.open_dropdown_id =
                state.open_dropdown_id == region.tag ? "" : region.tag;
            settings_request_frame(state);
            return;
        case PanelClickKind::DropdownSelect:
            visualizer_tab_handle_click(state, cfg, on_commit, region) ||
                wallpaper_tab_handle_click(state, cfg, on_commit, region) ||
                displays_tab_handle_click(state, cfg, on_commit, region);
            state.open_dropdown_id.clear();
            settings_request_frame(state);
            return;
        default:
            return;
        }
    }

    klog("settings: click (%.0f,%.0f) hit no region (%zu checked, "
         "panel_rect=%.0f,%.0f %.0fx%.0f)",
         px, py, state.click_regions.size(),
         static_cast<double>(state.panel_rect.x),
         static_cast<double>(state.panel_rect.y),
         static_cast<double>(state.panel_rect.w),
         static_cast<double>(state.panel_rect.h));
    if (!hit(state.panel_rect, px, py)) {
        settings_toggle(state, cfg, on_commit);
        return;
    }
    if (!state.open_dropdown_id.empty()) {
        state.open_dropdown_id.clear();
        settings_request_frame(state);
    }
}

void settings_handle_key_event(SettingsState &state, const Config &cfg,
                               const SettingsCommitFn &on_commit,
                               const KeyEvent &event) {
    if (state.focused_field == SettingsFieldId::None) {
        if (event.kind == KeyKind::Escape) {
            klog("settings: escape with no field focused -> toggle close");
            settings_toggle(state, cfg, on_commit);
        }
        return;
    }
    switch (text_field_handle_key(state.field_buffer, event)) {
    case TextFieldResult::Changed:
        settings_request_frame(state);
        break;
    case TextFieldResult::Committed:
        klog("settings: field %d committed",
             static_cast<int>(state.focused_field));
        settings_commit_focused_field(state, cfg, on_commit);
        settings_request_frame(state);
        break;
    case TextFieldResult::Cancelled:
        klog("settings: field %d edit cancelled",
             static_cast<int>(state.focused_field));
        state.focused_field = SettingsFieldId::None;
        settings_request_frame(state);
        break;
    case TextFieldResult::None:
        break;
    }
}

using panel_chrome_detail::cached_icon;
using panel_chrome_detail::cached_text;

namespace {

struct SettingsTabDef {
    const char *label;
    const char *icon;
};

float draw_profile_block(SettingsState &state, Node *parent, int32_t scale,
                         float x, float y, float w) {
    float avatar_x = x;
    float avatar_y = y + kSettingsProfileTopPadding;
    node_add_rrect(parent, avatar_x, avatar_y, kSettingsProfileAvatarSize,
                   kSettingsProfileAvatarSize,
                   kSettingsProfileAvatarSize / 2.0f, 0.0f,
                   rgba(palette::text_alpha04), kPanelNoBorder);
    const Texture *avatar_icon = cached_icon(state.tcache, icon::user, scale);
    if (avatar_icon)
        node_add_texture(
            parent,
            avatar_x + (kSettingsProfileAvatarSize - avatar_icon->width) / 2.0f,
            avatar_y +
                (kSettingsProfileAvatarSize - avatar_icon->height) / 2.0f,
            *avatar_icon, rgba(palette::text));

    float text_x =
        avatar_x + kSettingsProfileAvatarSize + kSettingsProfileAvatarLabelGap;
    const Texture *name_tex =
        cached_text(state.tcache, user_info::username(), scale);
    const Texture *uptime_tex =
        cached_text(state.tcache, user_info::uptime_string(), scale);
    float info_h = (name_tex ? name_tex->height : 0) + kSettingsProfileLineGap +
                   (uptime_tex ? uptime_tex->height : 0);
    float text_y = avatar_y + (kSettingsProfileAvatarSize - info_h) / 2.0f;
    if (name_tex) {
        node_add_texture(parent, text_x, text_y, *name_tex,
                         rgba(palette::text));
        text_y += name_tex->height + kSettingsProfileLineGap;
    }
    if (uptime_tex)
        node_add_texture(parent, text_x, text_y, *uptime_tex,
                         rgba(palette::text_dim));

    float block_h = kSettingsProfileTopPadding + kSettingsProfileAvatarSize +
                    kSettingsProfileBottomPadding;
    node_add_rect(parent, x, y + block_h, w, 1.0f, rgba(palette::text_alpha11));
    return block_h + kSettingsProfileDividerGap;
}

void draw_nav_rail(SettingsState &state, Node *parent, int32_t scale, float x,
                   float y, float w, float h) {
    static const SettingsTabDef tabs[kSettingsTabCount] = {
        {"Wallpaper", icon::wallpaper},
        {"Displays", icon::device_desktop},
        {"Idle", icon::moon_stars},
        {"Visualizer", icon::music_note},
    };
    node_add_rrect(parent, x, y, w, h, metrics::radius_md, 0.0f,
                   rgba(palette::text_alpha04), kPanelNoBorder);
    float row_y = y + kSettingsRailPadding;
    for (int i = 0; i < kSettingsTabCount; ++i) {
        bool active = static_cast<int>(state.active_tab) == i;
        const float *row_color =
            active ? rgba(palette::accent) : rgba(palette::text_dim);
        node_add_rrect(parent, x, row_y, w, kSettingsRailItemHeight,
                       metrics::radius_sm, 0.0f,
                       active ? rgba(palette::accent_alpha19) : kPanelNoBorder,
                       kPanelNoBorder);

        float icon_x = x + kSettingsRailPadding;
        const Texture *icon_tex =
            cached_icon(state.tcache, tabs[i].icon, scale);
        if (icon_tex)
            node_add_texture(
                parent, icon_x,
                row_y + (kSettingsRailItemHeight - icon_tex->height) / 2.0f,
                *icon_tex, row_color);

        float label_x = icon_x + (icon_tex ? icon_tex->width : 0) +
                        kSettingsRailIconLabelGap;
        const Texture *label_tex =
            cached_text(state.tcache, tabs[i].label, scale);
        if (label_tex)
            node_add_texture(
                parent, label_x,
                row_y + (kSettingsRailItemHeight - label_tex->height) / 2.0f,
                *label_tex, row_color);

        state.click_regions.push_back({PanelClickKind::TabSelect,
                                       {x, row_y, w, kSettingsRailItemHeight},
                                       std::to_string(i)});
        row_y += kSettingsRailItemHeight + kSettingsRailItemGap;
    }
}

} // namespace

void draw_toggle_switch(SettingsState &state, Node *parent, float x, float y,
                        bool active, const char *tag) {
    node_add_rrect(parent, x, y, kSettingsToggleTrackWidth,
                   kSettingsToggleTrackHeight, kSettingsToggleTrackRadius, 0.0f,
                   active ? rgba(palette::accent) : rgba(palette::text_alpha11),
                   kPanelNoBorder);
    float knob_x = active
                       ? x + kSettingsToggleTrackWidth -
                             kSettingsToggleKnobSize - kSettingsToggleKnobInset
                       : x + kSettingsToggleKnobInset;
    float knob_y =
        y + (kSettingsToggleTrackHeight - kSettingsToggleKnobSize) / 2.0f;
    node_add_rrect(parent, knob_x, knob_y, kSettingsToggleKnobSize,
                   kSettingsToggleKnobSize, kSettingsToggleKnobRadius, 0.0f,
                   rgba(palette::text), kPanelNoBorder);
    state.click_regions.push_back(
        {PanelClickKind::ToggleFlip,
         {x, y, kSettingsToggleTrackWidth, kSettingsToggleTrackHeight},
         tag});
}

void draw_toggle_row(SettingsState &state, Node *parent, int32_t scale, float x,
                     float y, float w, const std::string &label, bool value,
                     const char *tag, bool tiled) {
    float row_h =
        tiled ? kSettingsToggleTileHeight : kSettingsToggleTrackHeight;
    float inset = tiled ? kSettingsToggleTileContentMargin : 0.0f;

    if (tiled)
        node_add_rrect(parent, x, y, w, row_h, kSettingsTileRadius,
                       kSettingsToggleTileBorderWidth,
                       rgba(palette::text_alpha04),
                       rgba(palette::text_alpha07));

    const Texture *label_tex = cached_text(state.tcache, label, scale);
    if (label_tex)
        node_add_texture(parent, x + inset,
                         y + (row_h - label_tex->height) / 2.0f, *label_tex,
                         tiled ? rgba(palette::text_alpha85)
                               : rgba(palette::text));

    float switch_x = x + w - inset - kSettingsToggleTrackWidth;
    float switch_y = y + (row_h - kSettingsToggleTrackHeight) / 2.0f;
    draw_toggle_switch(state, parent, switch_x, switch_y, value, tag);
}

void settings_paint(SettingsState &state, const Config &cfg,
                    const std::vector<std::string> &monitor_names,
                    const std::string &focused_monitor) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    state.monitor_names = monitor_names;
    auto sync_region_selection = [&](WallpaperSubtabState &sub,
                                     int (*count_fn)(const Config &,
                                                     const std::string &)) {
        if (monitor_names.size() == 1) {
            sub.selected_region = monitor_names[0];
        } else if (sub.selected_region.empty() ||
                   std::find(monitor_names.begin(), monitor_names.end(),
                             sub.selected_region) == monitor_names.end()) {
            sub.selected_region =
                !focused_monitor.empty() &&
                        std::find(monitor_names.begin(), monitor_names.end(),
                                  focused_monitor) != monitor_names.end()
                    ? focused_monitor
                : monitor_names.empty() ? ""
                                        : monitor_names[0];
        }
        int count = sub.selected_region.empty()
                        ? 1
                        : count_fn(cfg, sub.selected_region);
        sub.selected_column = std::clamp(sub.selected_column, 0, count - 1);
    };
    sync_region_selection(state.wallpaper_static,
                          wallpaper_service_column_count);
    sync_region_selection(state.wallpaper_animated,
                          wallpaper_service_animated_column_count);

    if (!state.displays_selected_monitor.empty() &&
        std::find(monitor_names.begin(), monitor_names.end(),
                  state.displays_selected_monitor) == monitor_names.end())
        state.displays_selected_monitor.clear();
    state.base.animations.tick(std::chrono::steady_clock::now());
    eglMakeCurrent(state.base.egl_display, state.base.egl_surface,
                   state.base.egl_surface, state.base.egl_context);
    int32_t scale = state.base.output_scale.scale;
    state.renderer->begin_frame(state.base.width, state.base.height, scale);
    state.renderer->set_opacity(state.base.opacity);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.scene.rebuild();
    state.click_regions.clear();
    Node *root = &state.scene.root;

    if (state.base.opacity > 0.0f) {
        float panel_w =
            std::min(static_cast<float>(state.base.width) - 80.0f, 920.0f);
        float panel_h =
            std::min(static_cast<float>(state.base.height) - 80.0f, 680.0f);
        float panel_x = (static_cast<float>(state.base.width) - panel_w) / 2.0f;
        float panel_y =
            (static_cast<float>(state.base.height) - panel_h) / 2.0f;
        state.panel_rect = {panel_x, panel_y, panel_w, panel_h};

        panel_draw_box(root, panel_x, panel_y, panel_w, panel_h,
                       metrics::border_thick);
        panel_draw_header(root, state.tcache, scale, "Settings", panel_x,
                          panel_y, panel_w, state.click_regions);

        float content_y = panel_y + kPanelPadding + kPanelHeaderHeight +
                          kPanelHeaderDividerGap;
        node_add_rect(root, panel_x + kPanelPadding, content_y,
                      panel_w - kPanelPadding * 2.0f, 1.0f,
                      rgba(palette::text_alpha11));
        content_y += 1.0f + kPanelContentGap;

        float rail_x = panel_x + kPanelPadding;
        float profile_h = draw_profile_block(state, root, scale, rail_x,
                                             content_y, kSettingsRailWidth);
        float rail_y = content_y + profile_h;
        float rail_h = panel_y + panel_h - kPanelPadding - rail_y;
        draw_nav_rail(state, root, scale, rail_x, rail_y, kSettingsRailWidth,
                      rail_h);

        float divider_x = rail_x + kSettingsRailWidth + kSettingsRailDividerGap;
        node_add_rect(root, divider_x, content_y, 1.0f, rail_h,
                      rgba(palette::text_alpha11));

        float label_x = divider_x + kSettingsRailDividerGap;
        float field_x = label_x + kSettingsLabelWidth;
        float y = content_y;

        switch (state.active_tab) {
        case SettingsTab::Wallpaper:
            wallpaper_tab_paint(state, root, scale, label_x, y, cfg);
            break;
        case SettingsTab::Displays: {
            float row_w = panel_x + panel_w - kPanelPadding - label_x;
            displays_tab_paint(state, root, scale, label_x, y, row_w, cfg);
            break;
        }
        case SettingsTab::Idle:
            idle_tab_paint(state, root, scale, label_x, field_x, y, cfg);
            break;
        case SettingsTab::Visualizer:
            visualizer_tab_paint(state, root, scale, label_x, y, cfg);
            break;
        }
    }

    state.scene.draw(*state.renderer);
    if (state.base.animations.hasActive())
        overlay_panel_request_frame(state.base);
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);
}

bool settings_point_is_clickable(const SettingsState &state, double px,
                                 double py) {
    auto hit = [](const Rect &r, double x, double y) {
        return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y &&
               y < r.y + r.h;
    };
    for (const PanelClickRegion &region : state.click_regions)
        if (hit(region.rect, px, py))
            return true;
    return false;
}

void settings_handle_scroll(SettingsState &state, double dy) {
    if (state.active_tab != SettingsTab::Wallpaper)
        return;
    wallpaper_tab_handle_scroll(state, dy);
}

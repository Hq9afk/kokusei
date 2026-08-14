#include "settings/settings.h"

#include "settings/tabs/displays_tab.h"
#include "settings/tabs/idle_tab.h"
#include "settings/tabs/wallpaper_tab.h"

#include "core/log.h"

#include <GLES2/gl2.h>

#include <algorithm>
#include <chrono>

std::string settings_detail_format_field(const Config &cfg,
                                         SettingsFieldId id) {
    switch (id) {
    case SettingsFieldId::WallpaperPath:
        return cfg.wallpaper_path;
    case SettingsFieldId::WallpaperDir:
        return cfg.wallpaper_dir;
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

namespace {

void apply_field_text(Config &cfg, SettingsFieldId id,
                      const std::string &text) {
    try {
        switch (id) {
        case SettingsFieldId::WallpaperPath:
            cfg.wallpaper_path = text;
            break;
        case SettingsFieldId::WallpaperDir:
            cfg.wallpaper_dir = text;
            break;
        case SettingsFieldId::IdleTimeout:
            cfg.idle_timeout_seconds =
                static_cast<uint32_t>(std::max(0, std::stoi(text)));
            break;
        case SettingsFieldId::IdleCommand:
            cfg.idle_command = text;
            break;
        case SettingsFieldId::IdleResumeCommand:
            cfg.idle_resume_command = text;
            break;
        default:
            break;
        }
    } catch (const std::exception &) {
        klog("settings: could not parse '%s' for field %d, keeping previous "
             "value",
             text.c_str(), static_cast<int>(id));
    }
}

} // namespace

bool settings_create_surface(SettingsState &state, wl_compositor *compositor,
                             zwlr_layer_shell_v1 *layer_shell,
                             wl_output *output) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-settings", output);
}

bool settings_init_egl(
    SettingsState &state, const Config &cfg, Renderer &renderer,
    EGLDisplay display, EGLConfig config, EGLContext context,
    std::function<std::vector<std::string>()> monitor_names_fn) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state, &cfg, monitor_names_fn] {
        settings_paint(state, cfg, monitor_names_fn());
    };
    state.wallpaper_picker.request_frame = [&state] {
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
    apply_field_text(updated, state.focused_field, state.field_buffer.text);
    on_commit(updated);
    if (state.focused_field == SettingsFieldId::WallpaperDir)
        wallpaper_picker_scan(state.wallpaper_picker, updated.wallpaper_dir);
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
    } else {
        state.active_tab = SettingsTab::Wallpaper;
        if (state.wallpaper_picker.dir != cfg.wallpaper_dir)
            wallpaper_picker_scan(state.wallpaper_picker, cfg.wallpaper_dir);
    }
    overlay_panel_toggle(state.base);
    settings_request_frame(state);
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
        switch (region.kind) {
        case PanelClickKind::Close:
            settings_toggle(state, cfg, on_commit);
            return;
        case PanelClickKind::TabSelect:
            settings_commit_focused_field(state, cfg, on_commit);
            state.active_tab = static_cast<SettingsTab>(std::stoi(region.tag));
            if (state.active_tab == SettingsTab::Wallpaper &&
                state.wallpaper_picker.dir != cfg.wallpaper_dir)
                wallpaper_picker_scan(state.wallpaper_picker,
                                      cfg.wallpaper_dir);
            settings_request_frame(state);
            return;
        case PanelClickKind::ToggleFlip:
            if (!wallpaper_tab_handle_click(state, cfg, on_commit, region))
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
        case PanelClickKind::WallpaperSelect:
            wallpaper_tab_handle_click(state, cfg, on_commit, region);
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
    if (!hit(state.panel_rect, px, py))
        settings_toggle(state, cfg, on_commit);
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

void draw_nav_rail(SettingsState &state, Node *parent, int32_t scale, float x,
                   float y, float w, float h) {
    static const SettingsTabDef tabs[kSettingsTabCount] = {
        {"Wallpaper", icon::wallpaper},
        {"Displays", icon::device_desktop},
        {"Idle", icon::moon_stars},
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

void settings_paint(SettingsState &state, const Config &cfg,
                    const std::vector<std::string> &monitor_names) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    state.monitor_names = monitor_names;
    if (monitor_names.size() == 1) {
        state.wallpaper_selected_monitor = monitor_names[0];
    } else if (state.wallpaper_selected_monitor.empty() ||
               std::find(monitor_names.begin(), monitor_names.end(),
                         state.wallpaper_selected_monitor) ==
                   monitor_names.end()) {
        state.wallpaper_selected_monitor =
            monitor_names.empty() ? "" : monitor_names[0];
    }
    {
        int count = state.wallpaper_selected_monitor.empty()
                        ? 1
                        : wallpaper_effective_column_count(
                              cfg, state.wallpaper_selected_monitor);
        state.wallpaper_selected_column =
            std::clamp(state.wallpaper_selected_column, 0, count - 1);
    }

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
        float rail_h = panel_y + panel_h - kPanelPadding - content_y;
        draw_nav_rail(state, root, scale, rail_x, content_y, kSettingsRailWidth,
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
        }
    }

    state.scene.draw(*state.renderer);
    if (state.base.animations.hasActive())
        overlay_panel_request_frame(state.base);
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);
}

void settings_handle_scroll(SettingsState &state, double dy) {
    if (state.active_tab != SettingsTab::Wallpaper)
        return;
    wallpaper_tab_handle_scroll(state, dy);
}

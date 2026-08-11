#pragma once

#include <algorithm>

#include "../render/panel_scroll.hpp"
#include "settings_state.hpp"

namespace settings_detail {

using panel_chrome_detail::cached_icon;
using panel_chrome_detail::cached_text;
using panel_chrome_detail::cached_text_clipped;

inline int wallpaper_grid_columns(float content_w) {
    return std::max(1, static_cast<int>((content_w + kSettingsWallpaperThumbGap) /
                                        (kSettingsWallpaperThumbSize +
                                         kSettingsWallpaperThumbGap)));
}

inline float wallpaper_grid_content_height(const WallpaperPickerState &picker,
                                           float content_w) {
    int cols = wallpaper_grid_columns(content_w);
    size_t rows = (picker.files.size() + cols - 1) / static_cast<size_t>(cols);
    if (rows == 0)
        return 0.0f;
    float cell = kSettingsWallpaperThumbSize + kSettingsWallpaperThumbGap;
    return static_cast<float>(rows) * cell - kSettingsWallpaperThumbGap;
}

inline void draw_field_row(SettingsState &state, Node *parent, int32_t scale,
                           float label_x, float y, float field_x, float field_w,
                           const std::string &label, SettingsFieldId id,
                           const Config &cfg) {
    const Texture *label_tex = cached_text(state.tcache, label, scale);
    if (label_tex)
        node_add_texture(parent, label_x,
                         y + (kSettingsFieldHeight - label_tex->height) / 2.0f,
                         *label_tex, rgba(palette::text));

    bool focused = state.focused_field == id;
    node_add_rrect(parent, field_x, y, field_w, kSettingsFieldHeight,
                   metrics::radius_sm, metrics::border_thin,
                   rgba(palette::field_bg),
                   focused ? rgba(palette::accent) : kPanelNoBorder);

    std::string display =
        focused ? state.field_buffer.text : format_field(cfg, id);
    if (id == SettingsFieldId::WallpaperPath && !focused)
        display = elide(display, 30);
    const Texture *value_tex = cached_text(state.tcache, display, scale);
    if (value_tex)
        node_add_texture(parent, field_x + 8,
                         y + (kSettingsFieldHeight - value_tex->height) / 2.0f,
                         *value_tex, rgba(palette::text));

    if (focused && state.field_buffer.cursor_blink_visible) {
        float cursor_x = field_x + 8 + (value_tex ? value_tex->width : 0) + 2;
        node_add_rect(parent, cursor_x, y + 5, 1.5f, kSettingsFieldHeight - 10,
                      rgba(palette::text));
    }

    state.click_regions.push_back({PanelClickKind::FieldFocus,
                                   {field_x, y, field_w, kSettingsFieldHeight},
                                   std::to_string(static_cast<int>(id))});
}

// Dedicated full-width directory bar (label + editable path + rescan
// button), matching keqing-shell's DirBar.qml, instead of the generic
// label-left/field-right draw_field_row idiom used for plain settings.
inline void draw_wallpaper_dirbar(SettingsState &state, Node *parent,
                                  int32_t scale, float x, float y, float w,
                                  const Config &cfg) {
    bool focused = state.focused_field == SettingsFieldId::WallpaperDir;
    node_add_rrect(parent, x, y, w, kSettingsDirBarHeight, metrics::radius_sm,
                   metrics::border_thin, rgba(palette::field_bg),
                   focused ? rgba(palette::accent) : kPanelNoBorder);

    const Texture *label_tex = cached_text(state.tcache, "Dir", scale);
    float input_x = x + kSettingsDirBarLabelMargin;
    if (label_tex) {
        node_add_texture(parent, input_x,
                         y + (kSettingsDirBarHeight - label_tex->height) / 2.0f,
                         *label_tex, rgba(palette::accent));
        input_x += label_tex->width;
    }
    input_x += kSettingsDirBarFieldMargin;

    float btn_x = x + w - kSettingsDirBarEdgeMargin - kSettingsDirBarButtonWidth;
    float input_w = btn_x - kSettingsDirBarEdgeMargin - input_x;

    std::string display =
        focused ? state.field_buffer.text : cfg.wallpaper_dir;
    if (!focused)
        display = elide(display, 40);
    const Texture *value_tex = cached_text(state.tcache, display, scale);
    if (value_tex)
        node_add_texture(parent, input_x,
                         y + (kSettingsDirBarHeight - value_tex->height) / 2.0f,
                         *value_tex, rgba(palette::text));
    if (focused && state.field_buffer.cursor_blink_visible) {
        float cursor_x = input_x + (value_tex ? value_tex->width : 0) + 2;
        node_add_rect(parent, cursor_x, y + 8, 1.5f, kSettingsDirBarHeight - 16,
                      rgba(palette::text));
    }
    state.click_regions.push_back(
        {PanelClickKind::FieldFocus, {input_x, y, input_w, kSettingsDirBarHeight},
         std::to_string(static_cast<int>(SettingsFieldId::WallpaperDir))});

    float btn_y = y + (kSettingsDirBarHeight - kSettingsDirBarButtonHeight) / 2.0f;
    node_add_rrect(parent, btn_x, btn_y, kSettingsDirBarButtonWidth,
                   kSettingsDirBarButtonHeight, metrics::radius_sm, 0.0f,
                   rgba(palette::text_alpha10), kPanelNoBorder);
    const Texture *btn_tex = cached_text(
        state.tcache, state.wallpaper_picker.scanning ? "\xE2\x80\xA6" : "Rescan",
        scale);
    if (btn_tex)
        node_add_texture(
            parent, btn_x + (kSettingsDirBarButtonWidth - btn_tex->width) / 2.0f,
            btn_y + (kSettingsDirBarButtonHeight - btn_tex->height) / 2.0f,
            *btn_tex, rgba(palette::text));
    state.click_regions.push_back(
        {PanelClickKind::ToggleFlip,
         {btn_x, btn_y, kSettingsDirBarButtonWidth, kSettingsDirBarButtonHeight},
         "wallpaperrescan"});
}

inline void draw_toggle_row(SettingsState &state, Node *parent, int32_t scale,
                            float label_x, float y, float toggle_x,
                            const std::string &label) {
    const Texture *label_tex = cached_text(state.tcache, label, scale);
    if (label_tex)
        node_add_texture(parent, label_x,
                         y + (kSettingsFieldHeight - label_tex->height) / 2.0f,
                         *label_tex, rgba(palette::text));

    toggle_draw(parent, toggle_x,
                y + (kSettingsFieldHeight - toggle_detail::kToggleHeight) /
                        2.0f,
                state.autohide_toggle);
    state.click_regions.push_back(
        {PanelClickKind::ToggleFlip,
         {toggle_x,
          y + (kSettingsFieldHeight - toggle_detail::kToggleHeight) / 2.0f,
          toggle_detail::kToggleWidth, toggle_detail::kToggleHeight},
         ""});
}

inline void draw_monitor_row(SettingsState &state, Node *parent, int32_t scale,
                             float x, float y) {
    float cx = x;
    for (const std::string &name : state.monitor_names) {
        const Texture *tex = cached_text(state.tcache, name, scale);
        float chip_w = (tex ? tex->width : 0) + 24.0f;
        bool active = name == state.wallpaper_selected_monitor;
        node_add_rrect(parent, cx, y, chip_w, kSettingsMonitorChipHeight,
                       metrics::radius_sm, metrics::border_thin,
                       active ? rgba(palette::accent_alpha20)
                              : rgba(palette::field_bg),
                       active ? rgba(palette::accent) : kPanelNoBorder);
        if (tex)
            node_add_texture(
                parent, cx + 12.0f,
                y + (kSettingsMonitorChipHeight - tex->height) / 2.0f, *tex,
                active ? rgba(palette::accent) : rgba(palette::text));
        state.click_regions.push_back(
            {PanelClickKind::MonitorSelect,
             {cx, y, chip_w, kSettingsMonitorChipHeight}, name});
        cx += chip_w + kSettingsMonitorChipGap;
    }
}

inline void draw_fill_mode_row(SettingsState &state, Node *parent,
                               int32_t scale, float x, float y, float w,
                               const Config &cfg) {
    std::string mode =
        wallpaper_effective_fill_mode(cfg, state.wallpaper_selected_monitor);
    static const char *kLabels[2] = {"Crop", "Fit"};
    static const char *kModes[2] = {"crop", "fit"};
    float widths[2];
    float pair_w = 0.0f;
    for (int i = 0; i < 2; ++i) {
        const Texture *tex = cached_text(state.tcache, kLabels[i], scale);
        widths[i] = (tex ? tex->width : 0) + 20.0f;
        pair_w += widths[i] + (i == 0 ? 6.0f : 0.0f);
    }

    float cx = x + w - pair_w;
    for (int i = 0; i < 2; ++i) {
        bool active = mode == kModes[i];
        const Texture *tex = cached_text(state.tcache, kLabels[i], scale);
        float bw = widths[i];
        node_add_rrect(parent, cx, y, bw, kSettingsFieldHeight,
                       metrics::radius_sm, metrics::border_thin,
                       active ? rgba(palette::accent_alpha20)
                              : rgba(palette::field_bg),
                       active ? rgba(palette::accent) : kPanelNoBorder);
        if (tex)
            node_add_texture(
                parent, cx + 10.0f,
                y + (kSettingsFieldHeight - tex->height) / 2.0f, *tex,
                active ? rgba(palette::accent) : rgba(palette::text));
        state.click_regions.push_back({PanelClickKind::ToggleFlip,
                                       {cx, y, bw, kSettingsFieldHeight},
                                       "fillmode"});
        cx += bw + 6.0f;
    }

    // Visible only once a wallpaper is actually assigned to the selected
    // monitor, matching keqing-shell's ControlRow "Remove" pill.
    if (!wallpaper_effective_path(cfg, state.wallpaper_selected_monitor)
             .empty()) {
        const Texture *tex = cached_text(state.tcache, "Remove", scale);
        float rw = (tex ? tex->width : 0) + 20.0f;
        float rx = x + (w - rw) / 2.0f;
        node_add_rrect(parent, rx, y, rw, kSettingsFieldHeight,
                       metrics::radius_sm, metrics::border_thin,
                       rgba(palette::field_bg), rgba(palette::critical));
        if (tex)
            node_add_texture(parent, rx + 10.0f,
                             y + (kSettingsFieldHeight - tex->height) / 2.0f,
                             *tex, rgba(palette::critical));
        state.click_regions.push_back({PanelClickKind::ToggleFlip,
                                       {rx, y, rw, kSettingsFieldHeight},
                                       "wallpaperremove"});
    }
}

// Renders only the rows overlapping the scrolled viewport, same visible-range
// approach launcher_draw.hpp uses for its row list - the grid can hold many
// more thumbnails than fit on screen.
inline void draw_wallpaper_grid(SettingsState &state, Node *parent,
                                int32_t scale, float x, float y, float w,
                                float h, const Config &cfg) {
    WallpaperPickerState &picker = state.wallpaper_picker;
    if (picker.scanning) {
        const Texture *t = cached_text(state.tcache, "Scanning\xE2\x80\xA6",
                                       scale);
        if (t)
            node_add_texture(parent, x, y, *t, rgba(palette::text_dim));
        return;
    }
    if (picker.files.empty()) {
        const Texture *t =
            cached_text(state.tcache, "No images found", scale);
        if (t)
            node_add_texture(parent, x, y, *t, rgba(palette::text_dim));
        return;
    }

    node_add_rrect(parent, x, y, w, h, metrics::radius_sm,
                   metrics::border_thin, kPanelNoBorder,
                   rgba(palette::accent));

    float inset_x = x + kSettingsWallpaperGridInset;
    float inset_y = y + kSettingsWallpaperGridInset;
    float inset_w = w - kSettingsWallpaperGridInset * 2.0f;
    float inset_h = h - kSettingsWallpaperGridInset * 2.0f;

    int cols = wallpaper_grid_columns(inset_w);
    float content_h = wallpaper_grid_content_height(picker, inset_w);
    float visible_h = std::min(inset_h, content_h);
    float cell = kSettingsWallpaperThumbSize + kSettingsWallpaperThumbGap;
    float row_w = cols * cell - kSettingsWallpaperThumbGap;
    inset_x += (inset_w - row_w) / 2.0f;
    std::string selected =
        wallpaper_effective_path(cfg, state.wallpaper_selected_monitor);

    Node *clip = node_add_group(parent, inset_x, inset_y, row_w, visible_h,
                                true);
    int total_rows =
        static_cast<int>((picker.files.size() + static_cast<size_t>(cols) - 1) /
                         static_cast<size_t>(cols));
    int first_row =
        std::clamp(static_cast<int>(state.wallpaper_scroll_offset / cell), 0,
                  std::max(0, total_rows - 1));
    int last_row = std::clamp(
        static_cast<int>((state.wallpaper_scroll_offset + visible_h) / cell),
        0, std::max(0, total_rows - 1));

    for (int row = first_row; row <= last_row; ++row) {
        for (int col = 0; col < cols; ++col) {
            size_t idx =
                static_cast<size_t>(row) * static_cast<size_t>(cols) + col;
            if (idx >= picker.files.size())
                break;
            const std::string &path = picker.files[idx];
            float cx = col * cell;
            float cy = row * cell - state.wallpaper_scroll_offset;
            bool active = path == selected;

            auto it = picker.thumbnails.find(path);
            if (it != picker.thumbnails.end()) {
                node_add_rect(clip, cx, cy, kSettingsWallpaperThumbSize,
                             kSettingsWallpaperThumbSize,
                             rgba(palette::field_bg));
                // Cover-fit + crop into the cell, same math as
                // wallpaper_paint() (wallpaper.hpp) - the decoded texture is
                // aspect-preserving, not pre-cropped to a square.
                const Texture &tex = it->second;
                Node *cell_clip = node_add_group(
                    clip, cx, cy, kSettingsWallpaperThumbSize,
                    kSettingsWallpaperThumbSize, true);
                float tscale =
                    std::max(kSettingsWallpaperThumbSize /
                                static_cast<float>(tex.width),
                            kSettingsWallpaperThumbSize /
                                static_cast<float>(tex.height));
                float draw_w = tex.width * tscale;
                float draw_h = tex.height * tscale;
                node_add_texture_rect(
                    cell_clip, (kSettingsWallpaperThumbSize - draw_w) / 2.0f,
                    (kSettingsWallpaperThumbSize - draw_h) / 2.0f, draw_w,
                    draw_h, tex, rgba(palette::text));
            } else {
                node_add_rrect(clip, cx, cy, kSettingsWallpaperThumbSize,
                               kSettingsWallpaperThumbSize,
                               kSettingsWallpaperThumbRadius, 0.0f,
                               rgba(palette::field_bg), kPanelNoBorder);
                wallpaper_picker_request_thumbnail(
                    picker, path,
                    static_cast<int>(kSettingsWallpaperThumbSize * scale),
                    state.base.egl_display, state.base.egl_surface,
                    state.base.egl_context);
            }

            std::string filename =
                std::filesystem::path(path).filename().string();
            const Texture *name_tex = cached_text_clipped(
                state.tcache, filename, scale,
                static_cast<int>(kSettingsWallpaperThumbSize -
                                 kSettingsWallpaperLabelPad));
            float label_h =
                (name_tex ? name_tex->height : 0) + kSettingsWallpaperLabelPad;
            node_add_rect(clip, cx,
                         cy + kSettingsWallpaperThumbSize - label_h,
                         kSettingsWallpaperThumbSize, label_h,
                         rgba(palette::overlay));
            if (name_tex)
                node_add_texture(
                    clip, cx + kSettingsWallpaperLabelPad / 2.0f,
                    cy + kSettingsWallpaperThumbSize -
                        (label_h + name_tex->height) / 2.0f,
                    *name_tex, rgba(palette::text));

            node_add_rrect(clip, cx, cy, kSettingsWallpaperThumbSize,
                           kSettingsWallpaperThumbSize,
                           kSettingsWallpaperThumbRadius,
                           active ? metrics::border_thick : 0.0f,
                           kPanelNoBorder,
                           active ? rgba(palette::accent) : kPanelNoBorder);
            state.click_regions.push_back(
                {PanelClickKind::WallpaperSelect,
                 {inset_x + cx, inset_y + cy, kSettingsWallpaperThumbSize,
                  kSettingsWallpaperThumbSize},
                 path});
        }
    }
}

struct SettingsTabDef {
    const char *label;
    const char *icon;
};

inline void draw_nav_rail(SettingsState &state, Node *parent, int32_t scale,
                          float x, float y, float w, float h) {
    static const SettingsTabDef tabs[kSettingsTabCount] = {
        {"Bar", icon::layout_navbar},
        {"Wallpaper", icon::wallpaper},
        {"Idle", icon::moon_stars},
    };
    node_add_rrect(parent, x, y, w, h, metrics::radius_md, 0.0f,
                   rgba(palette::text_alpha05), kPanelNoBorder);
    float row_y = y + kSettingsRailPadding;
    for (int i = 0; i < kSettingsTabCount; ++i) {
        bool active = static_cast<int>(state.active_tab) == i;
        const float *row_color =
            active ? rgba(palette::accent) : rgba(palette::text_dim);
        node_add_rrect(parent, x, row_y, w, kSettingsRailItemHeight,
                       metrics::radius_sm, 0.0f,
                       active ? rgba(palette::accent_alpha20) : kPanelNoBorder,
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

} // namespace settings_detail

inline void settings_paint(SettingsState &state, const Config &cfg,
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
        using namespace settings_detail;

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
                      rgba(palette::text_alpha10));
        content_y += 1.0f + kPanelContentGap;

        float rail_x = panel_x + kPanelPadding;
        float rail_h = panel_y + panel_h - kPanelPadding - content_y;
        draw_nav_rail(state, root, scale, rail_x, content_y,
                      kSettingsRailWidth, rail_h);

        float divider_x = rail_x + kSettingsRailWidth + kSettingsRailDividerGap;
        node_add_rect(root, divider_x, content_y, 1.0f, rail_h,
                      rgba(palette::text_alpha10));

        float label_x = divider_x + kSettingsRailDividerGap;
        float field_x = label_x + kSettingsLabelWidth;
        float y = content_y;

        switch (state.active_tab) {
        case SettingsTab::Bar:
            draw_toggle_row(state, root, scale, label_x, y, field_x,
                            "Autohide");
            y += kSettingsRowHeight;
            break;
        case SettingsTab::Wallpaper: {
            state.wallpaper_grid_width = panel_x + panel_w - kPanelPadding - label_x;
            if (state.monitor_names.size() > 1) {
                draw_monitor_row(state, root, scale, label_x, y);
                y += kSettingsMonitorChipHeight + kPanelRowGap;
            }
            draw_wallpaper_dirbar(state, root, scale, label_x, y,
                                  state.wallpaper_grid_width, cfg);
            y += kSettingsDirBarHeight + kPanelRowGap;
            draw_fill_mode_row(state, root, scale, label_x, y,
                               state.wallpaper_grid_width, cfg);
            y += kSettingsRowHeight;
            state.wallpaper_grid_height =
                panel_y + panel_h - kPanelPadding - y;
            draw_wallpaper_grid(state, root, scale, label_x, y,
                                state.wallpaper_grid_width,
                                state.wallpaper_grid_height, cfg);
            y += state.wallpaper_grid_height;
            break;
        }
        case SettingsTab::Idle:
            draw_field_row(state, root, scale, label_x, y, field_x,
                           kSettingsNumberFieldWidth, "Timeout (s)",
                           SettingsFieldId::IdleTimeout, cfg);
            y += kSettingsRowHeight;
            draw_field_row(state, root, scale, label_x, y, field_x,
                           kSettingsFieldWidth, "On idle",
                           SettingsFieldId::IdleCommand, cfg);
            y += kSettingsRowHeight;
            draw_field_row(state, root, scale, label_x, y, field_x,
                           kSettingsFieldWidth, "On resume",
                           SettingsFieldId::IdleResumeCommand, cfg);
            y += kSettingsRowHeight;
            break;
        }
    }

    state.scene.draw(*state.renderer);
    if (state.base.animations.hasActive())
        overlay_panel_request_frame(state.base);
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);
}

inline void settings_handle_scroll(SettingsState &state, double dy) {
    if (state.active_tab != SettingsTab::Wallpaper)
        return;
    float inset_w = state.wallpaper_grid_width -
                    kSettingsWallpaperGridInset * 2.0f;
    float inset_h = state.wallpaper_grid_height -
                    kSettingsWallpaperGridInset * 2.0f;
    float content_h = settings_detail::wallpaper_grid_content_height(
        state.wallpaper_picker, inset_w);
    state.wallpaper_scroll_offset = panel_clamp_scroll(
        state.wallpaper_scroll_offset, static_cast<float>(dy), content_h,
        inset_h);
    settings_request_frame(state);
}

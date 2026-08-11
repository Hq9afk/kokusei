#pragma once

#include <algorithm>

#include "settings_state.hpp"

namespace settings_detail {

using panel_chrome_detail::cached_icon;
using panel_chrome_detail::cached_text;

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

struct SettingsTabDef {
    const char *label;
    const char *icon;
};

inline void draw_nav_rail(SettingsState &state, Node *parent, int32_t scale,
                          float x, float y, float w) {
    static const SettingsTabDef tabs[kSettingsTabCount] = {
        {"Bar", icon::layout_navbar},
        {"Wallpaper", icon::wallpaper},
        {"Idle", icon::moon_stars},
    };
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

inline void settings_paint(SettingsState &state, const Config &cfg) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
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
                      kSettingsRailWidth);

        float divider_x = rail_x + kSettingsRailWidth + kSettingsRailDividerGap;
        node_add_rect(root, divider_x, content_y, 1.0f, rail_h,
                      rgba(palette::text_alpha10));

        float label_x = divider_x + kSettingsRailDividerGap;
        float field_x = label_x + kSettingsLabelWidth;
        float y = content_y;

        switch (state.active_tab) {
        case SettingsTab::Bar:
            draw_field_row(state, root, scale, label_x, y, field_x,
                           kSettingsNumberFieldWidth, "Height",
                           SettingsFieldId::BarHeight, cfg);
            y += kSettingsRowHeight;
            draw_field_row(state, root, scale, label_x, y, field_x,
                           kSettingsNumberFieldWidth, "Background R",
                           SettingsFieldId::BarBgR, cfg);
            y += kSettingsRowHeight;
            draw_field_row(state, root, scale, label_x, y, field_x,
                           kSettingsNumberFieldWidth, "Background G",
                           SettingsFieldId::BarBgG, cfg);
            y += kSettingsRowHeight;
            draw_field_row(state, root, scale, label_x, y, field_x,
                           kSettingsNumberFieldWidth, "Background B",
                           SettingsFieldId::BarBgB, cfg);
            y += kSettingsRowHeight;
            draw_field_row(state, root, scale, label_x, y, field_x,
                           kSettingsNumberFieldWidth, "Background A",
                           SettingsFieldId::BarBgA, cfg);
            y += kSettingsRowHeight;
            draw_toggle_row(state, root, scale, label_x, y, field_x,
                            "Autohide");
            y += kSettingsRowHeight;
            break;
        case SettingsTab::Wallpaper:
            draw_field_row(state, root, scale, label_x, y, field_x,
                           kSettingsFieldWidth, "Path",
                           SettingsFieldId::WallpaperPath, cfg);
            y += kSettingsRowHeight;
            break;
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

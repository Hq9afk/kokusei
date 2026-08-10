#pragma once

#include "tray_menu_state.hpp"
#include <algorithm>
#include <linux/input-event-codes.h>

namespace tray_menu_detail {

inline float menu_content_height(const std::vector<MenuEntry> &level) {
    float h = kTrayMenuRowHeight;
    for (const MenuEntry &e : level)
        if (e.visible)
            h += kTrayMenuRowHeight;
    return h;
}

inline float panel_chrome_top_offset() {
    return kPanelPadding + kPanelHeaderHeight + kPanelHeaderDividerGap + 1.0f +
          kPanelContentGap;
}

inline float panel_total_height(float content_h) {
    return panel_chrome_top_offset() + content_h + kPanelPadding;
}

}

inline void tray_menu_close(TrayMenuState &state) {
    if (!state.base.open)
        return;
    state.base.open = false;
    state.item_key.clear();
    state.menu_path.clear();
    overlay_panel_update_input_region(state.base);
    overlay_panel_request_frame(state.base);
}

inline void tray_menu_open(TrayMenuState &state, TrayState &tray,
                           const TrayItem &item, const Rect &anchor_cell,
                           int32_t screen_width) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    bool same_item = state.base.open && state.item_key == item.key();
    state.item_key = item.key();
    state.menu_path.clear();
    if (!same_item)
        tray_menu_request(tray, item, {});

    float x = std::clamp(anchor_cell.x, kPanelSideMargin,
                         static_cast<float>(screen_width) - kPanelWidth -
                             kPanelSideMargin);
    float y = anchor_cell.y + anchor_cell.h + 4.0f;
    zwlr_layer_surface_v1_set_margin(state.base.layer_surface,
                                     static_cast<int32_t>(y), 0, 0,
                                     static_cast<int32_t>(x));

    if (!state.base.open) {
        state.base.open = true;
        overlay_panel_update_input_region(state.base);
    }
    overlay_panel_request_frame(state.base);
}

inline void tray_menu_apply_size(TrayMenuState &state, int32_t height) {
    if (state.base.height == height)
        return;
    state.base.height = height;
    int32_t scale = state.base.output_scale.scale;
    zwlr_layer_surface_v1_set_size(state.base.layer_surface,
                                   static_cast<uint32_t>(kPanelWidth),
                                   static_cast<uint32_t>(height));
    if (state.base.egl_window)
        wl_egl_window_resize(state.base.egl_window,
                             static_cast<int32_t>(kPanelWidth) * scale,
                             height * scale, 0, 0);
}

inline void tray_menu_paint(TrayMenuState &state, TrayState &tray) {
    using namespace panel_chrome_detail;
    using namespace tray_menu_detail;

    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    eglMakeCurrent(state.base.egl_display, state.base.egl_surface,
                   state.base.egl_surface, state.base.egl_context);
    int32_t scale = state.base.output_scale.scale;
    state.click_regions.clear();
    state.panel_rect = {};
    state.scene.rebuild();

    if (!state.base.open) {
        state.renderer->begin_frame(state.base.width, state.base.height, scale);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        state.scene.draw(*state.renderer);
        eglSwapBuffers(state.base.egl_display, state.base.egl_surface);
        return;
    }

    std::vector<MenuEntry> *menu_level = tray_menu_detail::current_menu_level(tray, state);
    bool item_exists = false;
    for (const TrayItem &it : tray.items)
        if (it.key() == state.item_key)
            item_exists = true;
    if (!item_exists || !menu_level) {
        tray_menu_close(state);
        state.renderer->begin_frame(state.base.width, state.base.height, scale);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        state.scene.draw(*state.renderer);
        eglSwapBuffers(state.base.egl_display, state.base.egl_surface);
        return;
    }

    float content_h = menu_content_height(*menu_level);
    int32_t panel_h = static_cast<int32_t>(panel_total_height(content_h) + 0.5f);
    tray_menu_apply_size(state, panel_h);

    state.renderer->begin_frame(state.base.width, state.base.height, scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    Node *root = &state.scene.root;
    const float *white = rgba(palette::text);
    const float *dim = rgba(palette::text_dim);
    float panel_x = 0.0f;
    float panel_y = 0.0f;
    float panel_w = kPanelWidth;
    float panel_h_f = static_cast<float>(panel_h);
    state.panel_rect = {panel_x, panel_y, panel_w, panel_h_f};

    panel_draw_box(root, panel_x, panel_y, panel_w, panel_h_f);
    panel_draw_header(root, state.tcache, scale, "Menu", panel_x, panel_y,
                      panel_w, state.click_regions);

    float divider_y =
        panel_y + kPanelPadding + kPanelHeaderHeight + kPanelHeaderDividerGap;
    node_add_rect(root, panel_x + kPanelPadding, divider_y,
                 panel_w - 2 * kPanelPadding, 1.0f, rgba(palette::text_alpha06));

    float content_y = divider_y + 1.0f + kPanelContentGap;

    Rect back_rect = {panel_x + kPanelPadding, content_y, kTrayMenuRowHeight,
                      kTrayMenuRowHeight};
    const Texture *back_tex = cached_icon(state.tcache, icon::chevron_left, scale);
    if (back_tex)
        node_add_texture(
            root, back_rect.x + (back_rect.w - back_tex->width) / 2.0f,
            back_rect.y + (back_rect.h - back_tex->height) / 2.0f, *back_tex,
            white);
    state.click_regions.push_back({PanelClickKind::TrayMenuBack, back_rect, ""});

    float row_y = content_y + kTrayMenuRowHeight;
    for (const MenuEntry &entry : *menu_level) {
        if (!entry.visible)
            continue;
        Rect row_rect = {panel_x + kPanelPadding, row_y,
                         panel_w - 2 * kPanelPadding, kTrayMenuRowHeight};
        if (entry.is_separator) {
            node_add_rect(root, row_rect.x, row_y + kTrayMenuRowHeight / 2.0f,
                         row_rect.w, 1.0f, rgba(palette::text_alpha06));
            row_y += kTrayMenuRowHeight;
            continue;
        }
        const float *label_color = entry.enabled ? white : dim;
        float text_x = row_rect.x;
        if (entry.is_checkbox) {
            if (entry.checked) {
                const Texture *check_tex =
                    cached_icon(state.tcache, icon::check, scale);
                if (check_tex)
                    node_add_texture(
                        root, text_x,
                        row_y + (kTrayMenuRowHeight - check_tex->height) / 2.0f,
                        *check_tex, label_color);
            }
            text_x += 22.0f;
        }
        int label_max_w =
            static_cast<int>(row_rect.w - (text_x - row_rect.x) - 20.0f);
        const Texture *label_tex = cached_text_clipped(
            state.tcache, entry.label, scale, std::max(0, label_max_w));
        if (label_tex)
            node_add_texture(
                root, text_x,
                row_y + (kTrayMenuRowHeight - label_tex->height) / 2.0f,
                *label_tex, label_color);
        if (!entry.children.empty()) {
            const Texture *chevron_tex =
                cached_icon(state.tcache, icon::chevron_right, scale);
            if (chevron_tex)
                node_add_texture(
                    root, row_rect.x + row_rect.w - chevron_tex->width,
                    row_y + (kTrayMenuRowHeight - chevron_tex->height) / 2.0f,
                    *chevron_tex, dim);
        }
        if (entry.enabled)
            state.click_regions.push_back(
                {PanelClickKind::TrayMenuEntry, row_rect,
                 std::to_string(entry.id)});
        row_y += kTrayMenuRowHeight;
    }

    state.scene.draw(*state.renderer);
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);
}

inline void tray_menu_handle_click(TrayMenuState &state, TrayState &tray,
                                   double px, double py) {
    auto hit = [](const Rect &r, double x, double y) {
        return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    };

    for (const PanelClickRegion &region : state.click_regions) {
        if (!hit(region.rect, px, py))
            continue;
        switch (region.kind) {
        case PanelClickKind::Close:
            tray_menu_close(state);
            return;
        case PanelClickKind::TrayMenuBack:
            if (state.menu_path.empty())
                tray_menu_close(state);
            else
                state.menu_path.pop_back();
            return;
        case PanelClickKind::TrayMenuEntry: {
            int32_t id = std::stoi(region.tag);
            std::vector<MenuEntry> *level =
                tray_menu_detail::current_menu_level(tray, state);
            if (!level)
                return;
            for (const MenuEntry &entry : *level) {
                if (entry.id != id)
                    continue;
                if (!entry.children.empty()) {
                    state.menu_path.push_back(id);
                } else {
                    const TrayItem *item = nullptr;
                    for (const TrayItem &it : tray.items)
                        if (it.key() == state.item_key) {
                            item = &it;
                            break;
                        }
                    if (item)
                        tray_menu_event_clicked(tray, *item, id);
                    tray_menu_close(state);
                }
                return;
            }
            return;
        }
        default:
            return;
        }
    }
}

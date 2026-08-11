#pragma once

#include "tray_menu.hpp"
#include "tray_panel_state.hpp"
#include <algorithm>
#include <linux/input-event-codes.h>

namespace tray_panel_detail {

inline float grid_content_height(size_t item_count) {
    size_t count = std::max<size_t>(item_count, 1);
    size_t rows = (count + kTrayColumns - 1) / kTrayColumns;
    return static_cast<float>(rows) * kTrayCellSize +
           static_cast<float>(rows - 1) * kTrayGridGap;
}

inline float panel_chrome_top_offset() {
    return kPanelPadding + kPanelHeaderHeight + kPanelHeaderDividerGap + 1.0f +
           kPanelContentGap;
}

inline float panel_total_height(float content_h) {
    return panel_chrome_top_offset() + content_h + kPanelPadding;
}

}

inline void tray_panel_paint(TrayPanelState &state, TrayState &tray,
                             float pill_center_x, float bar_height,
                             float bar_top_margin) {
    using namespace panel_chrome_detail;
    using namespace tray_panel_detail;

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
    const float *white = rgba(palette::text);
    const float *dim = rgba(palette::text_dim);

    float panel_w = kTrayPanelWidth;
    float content_h = grid_content_height(tray.items.size());
    float panel_h = panel_total_height(content_h);

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

    panel_draw_box(root, panel_x, panel_y, panel_w, panel_h);
    panel_draw_header(root, state.tcache, scale, "Tray", panel_x, panel_y,
                      panel_w, state.click_regions);

    float divider_y =
        panel_y + kPanelPadding + kPanelHeaderHeight + kPanelHeaderDividerGap;
    node_add_rect(root, panel_x + kPanelPadding, divider_y,
                  panel_w - 2 * kPanelPadding, 1.0f,
                  rgba(palette::text_alpha06));

    float content_y = divider_y + 1.0f + kPanelContentGap;

    if (tray.items.empty()) {
        const Texture *t = cached_text(state.tcache, "No tray icons", scale);
        if (t)
            node_add_texture(root, panel_x + (panel_w - t->width) / 2.0f,
                             content_y, *t, dim);
    } else {
        for (size_t i = 0; i < tray.items.size(); ++i) {
            const TrayItem &item = tray.items[i];
            size_t col = i % kTrayColumns;
            size_t row = i / kTrayColumns;
            float cx = panel_x + kPanelPadding +
                       static_cast<float>(col) * (kTrayCellSize + kTrayGridGap);
            float cy = content_y +
                       static_cast<float>(row) * (kTrayCellSize + kTrayGridGap);
            Rect cell = {cx, cy, kTrayCellSize, kTrayCellSize};
            node_add_rrect(root, cell.x, cell.y, cell.w, cell.h, 8.0f, 0.0f,
                           rgba(palette::overlay), kPanelNoBorder);
            const Texture *icon_tex = item_icon_texture(state, item);
            if (!icon_tex)
                icon_tex = cached_icon(state.tcache, icon::apps, scale);
            if (icon_tex)
                node_add_texture(root,
                                 cell.x + (cell.w - icon_tex->width) / 2.0f,
                                 cell.y + (cell.h - icon_tex->height) / 2.0f,
                                 *icon_tex, white);
            state.click_regions.push_back(
                {PanelClickKind::TrayActivate, cell, item.key()});
        }
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

inline void tray_panel_handle_click(TrayPanelState &state, TrayState &tray,
                                    TrayMenuState &menu, double px, double py,
                                    uint32_t button) {
    auto hit = [](const Rect &r, double x, double y) {
        return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y &&
               y < r.y + r.h;
    };

    for (const PanelClickRegion &region : state.click_regions) {
        if (!hit(region.rect, px, py))
            continue;
        switch (region.kind) {
        case PanelClickKind::Close:
            tray_menu_close(menu);
            tray_panel_toggle(state);
            return;
        case PanelClickKind::TrayActivate: {
            const TrayItem *item = nullptr;
            for (const TrayItem &it : tray.items)
                if (it.key() == region.tag) {
                    item = &it;
                    break;
                }
            if (!item)
                return;
            if (button == BTN_RIGHT) {
                if (!item->has_menu)
                    return;
                tray_menu_open(menu, tray, *item, region.rect,
                               state.base.width);
            } else {
                tray_menu_close(menu);
                tray_activate(tray, *item, false);
            }
            return;
        }
        default:
            return;
        }
    }

    if (!hit(state.panel_rect, px, py)) {
        if (menu.base.open)
            tray_menu_close(menu);
        else
            tray_panel_toggle(state);
    }
}

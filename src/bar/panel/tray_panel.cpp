#include "bar/panel/tray_panel.h"

#include "render/icon.h"
#include "render/icons.h"
#include "render/image.h"
#include "render/node.h"
#include "render/palette.h"
#include "render/text.h"
#include "service/layer_surface.h"

#include <GLES2/gl2.h>
#include <algorithm>
#include <linux/input-event-codes.h>

bool tray_menu_create_surface(TrayMenuState &state, wl_compositor *compositor,
                              zwlr_layer_shell_v1 *layer_shell,
                              wl_output *output) {
    state.base.compositor = compositor;
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        .name_space = "kokusei-tray-menu",
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
        .width = static_cast<int32_t>(kPanelWidth),
        .height = static_cast<int32_t>(kPanelHeaderHeight),
        .empty_input_region = true,
    };
    state.base.layer_surface =
        layer_surface_create(state.base.surface, compositor, layer_shell, cfg,
                             &overlay_panel_listener, &state.base, output);
    if (!state.base.layer_surface)
        return false;

    state.base.output_scale.on_change = [&state](int32_t scale) {
        if (state.base.egl_window)
            wl_egl_window_resize(state.base.egl_window,
                                 state.base.width * scale,
                                 state.base.height * scale, 0, 0);
        if (state.base.frame_clock.surface)
            request_frame(state.base.frame_clock);
    };
    output_scale_watch(state.base.output_scale, state.base.surface);
    wl_surface_commit(state.base.surface);
    return true;
}

bool tray_menu_init_egl(TrayMenuState &state, Renderer &renderer,
                        TrayState &tray, EGLDisplay display, EGLConfig config,
                        EGLContext context) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state, &tray] {
        tray_menu_paint(state, tray);
    };
    return true;
}

void tray_menu_request_frame(TrayMenuState &state) {
    overlay_panel_request_frame(state.base);
}

namespace {

float menu_content_height(const std::vector<MenuEntry> &level) {
    float h = kTrayMenuRowHeight;
    for (const MenuEntry &e : level)
        if (e.visible)
            h += kTrayMenuRowHeight;
    return h;
}

float panel_chrome_top_offset() {
    return kPanelPadding + kPanelHeaderHeight + kPanelHeaderDividerGap + 1.0f +
           kPanelContentGap;
}

float panel_total_height(float content_h) {
    return panel_chrome_top_offset() + content_h + kPanelPadding;
}

std::vector<MenuEntry> *current_menu_level(TrayState &tray,
                                           TrayMenuState &state) {
    auto it = tray.menu_cache.find(state.item_key);
    if (it == tray.menu_cache.end())
        return nullptr;
    std::vector<MenuEntry> *level = &it->second;
    for (int32_t id : state.menu_path) {
        MenuEntry *found = nullptr;
        for (MenuEntry &entry : *level) {
            if (entry.id == id) {
                found = &entry;
                break;
            }
        }
        if (!found)
            return level;
        level = &found->children;
    }
    return level;
}

void tray_menu_apply_size(TrayMenuState &state, int32_t height) {
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

} // namespace

void tray_menu_close(TrayMenuState &state) {
    if (!state.base.open)
        return;
    state.base.open = false;
    state.item_key.clear();
    state.menu_path.clear();
    overlay_panel_update_input_region(state.base);
    overlay_panel_request_frame(state.base);
}

void tray_menu_open(TrayMenuState &state, TrayState &tray, const TrayItem &item,
                    const Rect &anchor_cell, int32_t screen_width) {
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

void tray_menu_paint(TrayMenuState &state, TrayState &tray) {
    using namespace panel_chrome_detail;

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

    std::vector<MenuEntry> *menu_level = current_menu_level(tray, state);
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
    int32_t panel_h =
        static_cast<int32_t>(panel_total_height(content_h) + 0.5f);
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
                  panel_w - 2 * kPanelPadding, 1.0f,
                  rgba(palette::text_alpha06));

    float content_y = divider_y + 1.0f + kPanelContentGap;

    Rect back_rect = {panel_x + kPanelPadding, content_y, kTrayMenuRowHeight,
                      kTrayMenuRowHeight};
    const Texture *back_tex =
        cached_icon(state.tcache, icon::chevron_left, scale);
    if (back_tex)
        node_add_texture(root,
                         back_rect.x + (back_rect.w - back_tex->width) / 2.0f,
                         back_rect.y + (back_rect.h - back_tex->height) / 2.0f,
                         *back_tex, white);
    state.click_regions.push_back(
        {PanelClickKind::TrayMenuBack, back_rect, ""});

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
            node_add_texture(root, text_x,
                             row_y + (kTrayMenuRowHeight - label_tex->height) /
                                         2.0f,
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
            state.click_regions.push_back({PanelClickKind::TrayMenuEntry,
                                           row_rect, std::to_string(entry.id)});
        row_y += kTrayMenuRowHeight;
    }

    state.scene.draw(*state.renderer);
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);
}

void tray_menu_handle_click(TrayMenuState &state, TrayState &tray, double px,
                            double py) {
    auto hit = [](const Rect &r, double x, double y) {
        return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y &&
               y < r.y + r.h;
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
            std::vector<MenuEntry> *level = current_menu_level(tray, state);
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

const Texture *tray_panel_detail_item_icon_texture(TrayPanelState &state,
                                                   const TrayItem &item) {
    std::string path = tray_item_icon_path(item);
    if (path.empty())
        return nullptr;
    auto it = state.icon_cache.find(path);
    if (it == state.icon_cache.end())
        it = state.icon_cache.emplace(path, load_image_texture(path)).first;
    return it->second.id ? &it->second : nullptr;
}

bool tray_panel_create_surface(TrayPanelState &state, wl_compositor *compositor,
                               zwlr_layer_shell_v1 *layer_shell,
                               wl_output *output) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-tray-panel", output);
}

bool tray_panel_init_egl(TrayPanelState &state, Renderer &renderer,
                         TrayState &tray, EGLDisplay display, EGLConfig config,
                         EGLContext context) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state, &tray] {
        tray_panel_paint(state, tray, state.pending_pill_center_x,
                         state.pending_bar_height,
                         state.pending_bar_top_margin);
    };
    return true;
}

void tray_panel_request_frame(TrayPanelState &state, float pill_center_x,
                              float bar_height, float bar_top_margin) {
    state.pending_pill_center_x = pill_center_x;
    state.pending_bar_height = bar_height;
    state.pending_bar_top_margin = bar_top_margin;
    overlay_panel_request_frame(state.base);
}

void tray_panel_toggle(TrayPanelState &state, float pill_center_x) {
    panel_lock_toggle(
        state.base, state.locked_center_x, pill_center_x,
        [&state] { state.visible_height = -1.0f; },
        [&state] {
            state.base.animations.animate(
                state.visible_height, 0.0f, kOverlayFadeMs,
                Easing::EaseOutCubic,
                [&state](float v) { state.visible_height = v; }, {},
                kPanelHeightAnimOwner);
        });
}

namespace {

float grid_content_height(size_t item_count) {
    size_t count = std::max<size_t>(item_count, 1);
    size_t rows = (count + kTrayColumns - 1) / kTrayColumns;
    return static_cast<float>(rows) * kTrayCellSize +
           static_cast<float>(rows - 1) * kTrayGridGap;
}

} // namespace

void tray_panel_paint(TrayPanelState &state, TrayState &tray,
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
            const Texture *icon_tex =
                tray_panel_detail_item_icon_texture(state, item);
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

void tray_panel_handle_click(TrayPanelState &state, TrayState &tray,
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

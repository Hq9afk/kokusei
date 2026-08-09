#pragma once

#include "launcher_state.hpp"

namespace launcher_detail {
inline std::string elide(const std::string &s) {
    if (s.size() <= kMaxRowChars)
        return s;
    size_t cut = kMaxRowChars - 1;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
        --cut;
    return s.substr(0, cut) + "…";
}

inline std::string home_relative(const std::string &path) {
    const char *home = getenv("HOME");
    if (home && *home) {
        std::string prefix(home);
        if (path.compare(0, prefix.size(), prefix) == 0) {
            if (path.size() == prefix.size())
                return "~";
            if (path[prefix.size()] == '/')
                return "~" + path.substr(prefix.size());
        }
    }
    return path;
}

inline const char *mode_icon(LauncherMode mode) {
    switch (mode) {
    case LauncherMode::Run:
        return icon::terminal;
    case LauncherMode::Google:
        return icon::brand_google;
    case LauncherMode::YouTube:
        return icon::brand_youtube;
    case LauncherMode::DuckDuckGo:
    case LauncherMode::Url:
        return icon::link;
    case LauncherMode::Drun:
    default:
        return icon::apps;
    }
}

struct Row {
    const char *icon;
    std::string label;
    std::string subtitle;
    const Texture *icon_tex = nullptr;
};

inline std::vector<Row> visible_rows(LauncherState &state, int &first) {
    std::vector<Row> rows;
    if (state.submenu.screen == SubmenuScreen::Search) {
        for (const DrunResult &r : state.results) {
            switch (r.kind) {
            case DrunResult::Kind::App: {
                Row row{icon::apps, r.app->name, ""};
                row.icon_tex =
                    launcher_icon_lookup(state, r.app->id, r.app->icon);
                rows.push_back(std::move(row));
                break;
            }
            case DrunResult::Kind::Dir:
                rows.push_back({icon::folder, r.file.name,
                                home_relative(dirname_of(r.file.path))});
                break;
            case DrunResult::Kind::File:
                rows.push_back({icon::edit, r.file.name,
                                home_relative(dirname_of(r.file.path))});
                break;
            }
        }
    } else {
        for (const SubmenuEntry &e : state.submenu.items) {
            std::string subtitle = e.action == SubmenuEntry::Action::None
                                       ? home_relative(dirname_of(e.path))
                                       : "";
            const char *row_icon =
                e.icon ? e.icon : (e.is_dir ? icon::folder : icon::edit);
            rows.push_back({row_icon, e.name, subtitle});
        }
    }

    first = 0;
    if (state.selected_index >= kLauncherMaxVisible)
        first = state.selected_index - kLauncherMaxVisible + 1;
    return rows;
}

inline const Texture *cached_text(TextureCache &cache, const std::string &s,
                                  int32_t scale) {
    if (s.empty())
        return nullptr;
    return cache.get("t" + std::to_string(scale) + ":" + s,
                     [&] { return rasterize_text(s, scale); });
}

inline const Texture *cached_text_small(TextureCache &cache,
                                        const std::string &s, int32_t scale) {
    if (s.empty())
        return nullptr;
    return cache.get("s" + std::to_string(scale) + ":" + s,
                     [&] { return rasterize_text_small(s, scale); });
}

inline const Texture *cached_icon(TextureCache &cache, const char *codepoint,
                                  int32_t scale) {
    return cache.get("i" + std::to_string(scale) + ":" + codepoint,
                     [&] { return rasterize_icon(codepoint, scale); });
}

constexpr float kLauncherListGap =
    kLauncherListTop - kLauncherSearchHeight - kLauncherPad;

inline int launcher_surface_height(int visible_rows) {
    float h = kLauncherPad * 2.0f + kLauncherSearchHeight;
    if (visible_rows > 0)
        h += kLauncherListGap + visible_rows * kLauncherRowHeight;
    return static_cast<int>(h);
}

}

inline void launcher_paint(LauncherState &state) {
    using namespace launcher_detail;
    if (state.egl_surface == EGL_NO_SURFACE)
        return;

    state.animations.tick(std::chrono::steady_clock::now());

    int first = 0;
    std::vector<Row> rows;
    if (state.open)
        rows = visible_rows(state, first);

    int visible_count =
        static_cast<int>(std::min<size_t>(rows.size(), kLauncherMaxVisible));
    int content_h = launcher_surface_height(visible_count);

    if (state.anim_height_target < 0.0f) {
        state.anim_height = static_cast<float>(content_h);
        state.anim_height_target = static_cast<float>(content_h);
    } else if (static_cast<float>(content_h) != state.anim_height_target) {
        state.anim_height_target = static_cast<float>(content_h);
        state.animations.animate(
            state.anim_height, state.anim_height_target, kLauncherHeightAnimMs,
            Easing::EaseInOutCubic,
            [&state](float v) { state.anim_height = v; }, {},
            kLauncherHeightOwner);
    }

    eglMakeCurrent(state.egl_display, state.egl_surface, state.egl_surface,
                   state.egl_context);
    int32_t scale = state.output_scale.scale;
    state.renderer->begin_frame(state.width, state.height, scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.scene.rebuild();
    Node *root = &state.scene.root;

    if (state.open) {
        const float *white = rgba(palette::text);
        const float *dim = rgba(palette::text_muted);

        float box_h = state.anim_height;
        float box_x =
            (static_cast<float>(state.width) - kLauncherSurfaceWidth) / 2.0f;
        float box_y = (static_cast<float>(state.height) - box_h) / 2.0f;
        state.box_rect = {box_x, box_y, kLauncherSurfaceWidth, box_h};

        node_add_rrect(root, box_x, box_y, kLauncherSurfaceWidth, box_h,
                       metrics::radius_md, kLauncherBorderWidth,
                       rgba(palette::overlay), rgba(palette::accent));

        float clip_inset = metrics::radius_md;
        Node *outer =
            node_add_group(root, box_x + clip_inset, box_y + clip_inset,
                           kLauncherSurfaceWidth - 2 * clip_inset,
                           box_h - 2 * clip_inset, true);
        auto orx = [&](float v) { return v - (box_x + clip_inset); };
        auto ory = [&](float v) { return v - (box_y + clip_inset); };

        constexpr float kTransparent[4] = {0, 0, 0, 0};
        float mode_box_x = box_x + kLauncherPad;
        float mode_box_w = kLauncherSearchHeight;
        node_add_rrect(outer, orx(mode_box_x), ory(box_y + kLauncherPad),
                       mode_box_w, kLauncherSearchHeight, metrics::radius_sm,
                       kLauncherBorderWidth, kTransparent,
                       rgba(palette::accent));
        const Texture *mode_tex =
            cached_icon(state.tcache, mode_icon(state.mode), scale);
        if (mode_tex) {
            node_add_texture(
                outer, orx(mode_box_x + (mode_box_w - mode_tex->width) / 2.0f),
                ory(box_y + kLauncherPad +
                    (kLauncherSearchHeight - mode_tex->height) / 2.0f),
                *mode_tex, white);
        }

        float field_box_x = mode_box_x + mode_box_w + kLauncherPad;
        float field_box_w =
            box_x + kLauncherSurfaceWidth - kLauncherPad - field_box_x;
        node_add_rrect(outer, orx(field_box_x), ory(box_y + kLauncherPad),
                       field_box_w, kLauncherSearchHeight, metrics::radius_sm,
                       kLauncherBorderWidth, kTransparent,
                       rgba(palette::accent));
        float text_x = field_box_x + kLauncherPad;
        const Texture *query_tex =
            cached_text(state.tcache, elide(state.query), scale);
        if (query_tex) {
            node_add_texture(
                outer, orx(text_x),
                ory(box_y + kLauncherPad +
                    (kLauncherSearchHeight - query_tex->height) / 2.0f),
                *query_tex, white);
        }

        float content_x = mode_box_x + mode_box_w + kLauncherBulletGap;
        float list_top = box_y + kLauncherListTop;
        float list_h = box_y + box_h - clip_inset - list_top;
        Node *list_clip =
            node_add_group(outer, orx(mode_box_x), ory(list_top),
                           kLauncherSurfaceWidth - 2 * kLauncherPad, list_h,
                           true);

        if (state.selected_index >= 0) {
            float highlight_target =
                static_cast<float>(state.selected_index) * kLauncherRowHeight;
            if (state.highlight_offset_target < 0.0f) {
                state.highlight_offset = highlight_target;
                state.highlight_offset_target = highlight_target;
            } else if (highlight_target != state.highlight_offset_target) {
                state.highlight_offset_target = highlight_target;
                state.animations.animate(
                    state.highlight_offset, state.highlight_offset_target,
                    kLauncherHighlightAnimMs, Easing::EaseOutCubic,
                    [&state](float v) { state.highlight_offset = v; }, {},
                    kLauncherHighlightOwner);
            }

            float scroll_target = static_cast<float>(first) * kLauncherRowHeight;
            if (state.scroll_offset_target < 0.0f) {
                state.scroll_offset = scroll_target;
                state.scroll_offset_target = scroll_target;
            } else if (scroll_target != state.scroll_offset_target) {
                state.scroll_offset_target = scroll_target;
                state.animations.animate(
                    state.scroll_offset, state.scroll_offset_target,
                    kLauncherHighlightAnimMs, Easing::EaseOutCubic,
                    [&state](float v) { state.scroll_offset = v; }, {},
                    kLauncherScrollOwner);
            }
        } else {
            state.highlight_offset_target = -1.0f;
            state.scroll_offset_target = -1.0f;
        }

        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            bool is_selected = i == state.selected_index;
            float y = static_cast<float>(i) * kLauncherRowHeight -
                      state.scroll_offset;

            Node *rowg =
                node_add_group(list_clip, 0, y,
                               kLauncherSurfaceWidth - 2 * kLauncherPad,
                               kLauncherRowHeight, true);
            auto lrx = [&](float v) { return v - mode_box_x; };
            auto lry = [&](float v) { return v - y; };

            float rowx = content_x + kLauncherPad;
            if (rows[i].icon_tex) {
                const Texture &tex = *rows[i].icon_tex;
                node_add_texture_rect(
                    rowg, lrx(rowx),
                    lry(y + (kLauncherRowHeight - kIconTargetSize) / 2.0f),
                    kIconTargetSize, kIconTargetSize, tex, white);
            } else {
                const Texture *row_icon =
                    cached_icon(state.tcache, rows[i].icon, scale);
                if (row_icon) {
                    node_add_texture(
                        rowg,
                        lrx(rowx + (kIconTargetSize - row_icon->width) / 2.0f),
                        lry(y + (kLauncherRowHeight - row_icon->height) / 2.0f),
                        *row_icon, is_selected ? white : dim);
                }
            }
            rowx += kIconTargetSize + kLauncherPad;
            const Texture *label =
                cached_text(state.tcache, elide(rows[i].label), scale);
            if (!rows[i].subtitle.empty()) {
                const Texture *subtitle = cached_text_small(
                    state.tcache, elide(rows[i].subtitle), scale);
                constexpr float kTwoLineTopPad = 5.0f;
                constexpr float kTwoLineBottomPad = 5.0f;
                if (label)
                    node_add_texture(rowg, lrx(rowx), lry(y + kTwoLineTopPad),
                                     *label, white);
                if (subtitle)
                    node_add_texture(rowg, lrx(rowx),
                                     lry(y + kLauncherRowHeight -
                                         subtitle->height - kTwoLineBottomPad),
                                     *subtitle, dim);
            } else if (label) {
                node_add_texture(
                    rowg, lrx(rowx),
                    lry(y + (kLauncherRowHeight - label->height) / 2.0f),
                    *label, white);
            }
        }

        for (int slot = 0; slot < kLauncherMaxVisible; ++slot) {
            const Texture &bullet = state.bullet_tex[slot];
            if (!bullet.id)
                continue;
            float slot_y = static_cast<float>(slot) * kLauncherRowHeight;
            node_add_texture_rect(
                list_clip, (mode_box_w - kLauncherBulletSize) / 2.0f,
                slot_y + (kLauncherRowHeight - kLauncherBulletSize) / 2.0f,
                kLauncherBulletSize, kLauncherBulletSize, bullet, white);
        }

        if (state.selected_index >= 0) {
            constexpr float kTransparent2[4] = {0, 0, 0, 0};
            node_add_rrect(
                list_clip, content_x - mode_box_x,
                state.highlight_offset - state.scroll_offset,
                box_x + kLauncherSurfaceWidth - kLauncherPad - content_x,
                kLauncherRowHeight, metrics::radius_sm,
                kLauncherHighlightBorderWidth, kTransparent2,
                rgba(palette::accent_alt));
        }
    }

    state.renderer->set_opacity(state.opacity);
    state.scene.draw(*state.renderer);
    state.renderer->set_opacity(1.0f);
    eglSwapBuffers(state.egl_display, state.egl_surface);

    if (state.animations.hasActive())
        launcher_request_frame(state);
}

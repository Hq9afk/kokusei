#pragma once

#include "../render/color_ops.hpp"
#include "logout_state.hpp"

#include <algorithm>

inline void logout_paint(LogoutState &state) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    auto now = std::chrono::steady_clock::now();
    state.base.animations.tick(now);
    eglMakeCurrent(state.base.egl_display, state.base.egl_surface,
                   state.base.egl_surface, state.base.egl_context);
    state.renderer->begin_frame(state.base.width, state.base.height,
                                state.base.output_scale.scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.scene.rebuild();

    std::deque<Color> blend;
    if (state.base.open) {
        float cx = static_cast<float>(state.base.width) / 2.0f;
        float cy = static_cast<float>(state.base.height) / 2.0f;

        for (int i = 0; i < kLogoutButtonCount; ++i) {
            float expand = state.button_expand[static_cast<size_t>(i)];
            float highlight_scale =
                1.0f + (kLogoutHighlightScale - 1.0f) *
                           state.button_highlight_scale[static_cast<size_t>(i)];

            Rect r = logout_detail::button_rect(i, cx, cy, expand);
            float w = r.w * highlight_scale;
            float h = r.h * highlight_scale;
            float x = r.x + (r.w - w) / 2.0f;
            float y = r.y + (r.h - h) / 2.0f;

            blend.push_back(with_alpha(palette::field_bg, expand));
            const Color &fill = blend.back();
            blend.push_back(with_alpha(
                lerp_color(
                    palette::accent, palette::accent_alt,
                    state.button_highlight_border[static_cast<size_t>(i)]),
                expand));
            const Color &border = blend.back();

            Node *btn = state.scene.root.claim_child();
            btn->kind = NodeKind::RoundedRect;
            btn->x = x;
            btn->y = y;
            btn->w = w;
            btn->h = h;
            btn->radius = kLogoutButtonCornerRadius * highlight_scale;
            btn->border_width = kLogoutBorderWidth;
            btn->fill = rgba(fill);
            btn->border = rgba(border);

            Texture &tex = state.glyph_tex[static_cast<size_t>(i)];
            if (!tex.id) {
                RasterizedText rt = rasterize_yujimai_glyph(
                    kLogoutActions[static_cast<size_t>(i)].glyph_utf8);
                if (rt.width > 0)
                    tex =
                        make_texture_rgba(rt.width, rt.height, rt.rgba.data());
            }
            if (tex.id) {
                blend.push_back(with_alpha(palette::text, expand));
                const Color &glyph_tint = blend.back();
                float cxw = x + w / 2.0f;
                float cyh = y + h / 2.0f;
                Node *glyph = state.scene.root.claim_child();
                glyph->kind = NodeKind::Texture;
                glyph->x = cxw - tex.width / 2.0f;
                glyph->y = cyh - tex.height / 2.0f;
                glyph->w = static_cast<float>(tex.width);
                glyph->h = static_cast<float>(tex.height);
                glyph->tex = &tex;
                glyph->tint = rgba(glyph_tint);
            }
        }

        float logo_size = kLogoutLogoSize * state.logo_scale;

        if (state.logo_tex.id) {
            float scale = logo_size / static_cast<float>(std::max(
                                          state.logo_tex.width,
                                          state.logo_tex.height));
            float w = static_cast<float>(state.logo_tex.width) * scale;
            float h = static_cast<float>(state.logo_tex.height) * scale;
            Node *tex = state.scene.root.claim_child();
            tex->kind = NodeKind::Texture;
            tex->tex = &state.logo_tex;
            tex->x = cx - w / 2.0f;
            tex->y = cy - h / 2.0f;
            tex->w = w;
            tex->h = h;
        }
    }

    state.renderer->set_opacity(state.base.opacity);
    state.scene.draw(*state.renderer);
    state.renderer->set_opacity(1.0f);
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);

    if (state.base.animations.hasActive())
        overlay_panel_request_frame(state.base);
}

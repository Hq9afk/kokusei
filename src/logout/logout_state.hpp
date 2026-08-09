#pragma once

#include "../core/log.hpp"
#include "../launcher/spawn.hpp"
#include "../render/node.hpp"
#include "../render/overlay_panel.hpp"
#include "../render/palette.hpp"
#include "../render/rect.hpp"
#include "../render/renderer.hpp"
#include "../render/scene.hpp"
#include "../render/text.hpp"
#include "../render/texture.hpp"
#include "../wayland/keyboard.hpp"
#include "../wayland/layer_surface.hpp"
#include "logout_config.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <cairo/cairo-ft.h>
#include <cairo/cairo.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>

struct LogoutState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    int selected_index = 0;
    bool opened_by_widget = false;

    bool input_ready = false;

    float logo_scale = 0.0f;
    std::array<float, kLogoutButtonCount> button_expand{};

    std::array<float, kLogoutButtonCount> button_highlight_scale{};
    std::array<float, kLogoutButtonCount> button_highlight_border{};

    std::array<Texture, kLogoutButtonCount> glyph_tex{};

    Texture logo_tex;
};

namespace logout_detail {

struct YujiMaiFont {
    FT_Face face = nullptr;
    cairo_font_face_t *cairo_face = nullptr;
};

inline YujiMaiFont &yujimai_font() {
    static YujiMaiFont font = [] {
        YujiMaiFont f;
        static FT_Library library;
        if (FT_Init_FreeType(&library)) {
            klog("logout: FT_Init_FreeType failed");
            return f;
        }
        const char *candidates[] = {
            KOKUSEI_YUJIMAI_FONT,
            "assets/fonts/YujiMai.ttf",
        };
        for (const char *path : candidates) {
            if (FT_New_Face(library, path, 0, &f.face) == 0) {
                klog("logout: loaded YujiMai from %s", path);
                break;
            }
            f.face = nullptr;
        }
        if (!f.face) {
            klog("logout: failed to load YujiMai.ttf");
            return f;
        }
        f.cairo_face = cairo_ft_font_face_create_for_ft_face(f.face, 0);
        return f;
    }();
    return font;
}

inline uint32_t decode_utf8_codepoint(const std::string &s) {
    if (s.empty())
        return 0;
    unsigned char c0 = static_cast<unsigned char>(s[0]);
    if (c0 < 0x80)
        return c0;
    if ((c0 & 0xE0) == 0xC0 && s.size() >= 2) {
        return static_cast<uint32_t>((c0 & 0x1F) << 6) | (s[1] & 0x3F);
    }
    if ((c0 & 0xF0) == 0xE0 && s.size() >= 3) {
        return (static_cast<uint32_t>(c0 & 0x0F) << 12) |
               (static_cast<uint32_t>(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
    if ((c0 & 0xF8) == 0xF0 && s.size() >= 4) {
        return (static_cast<uint32_t>(c0 & 0x07) << 18) |
               (static_cast<uint32_t>(s[1] & 0x3F) << 12) |
               (static_cast<uint32_t>(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }
    return 0;
}

inline Rect button_rect(int index, float center_x, float center_y,
                        float radius_fraction = 1.0f) {
    float angle =
        kLogoutStartAngle + kLogoutStepAngle * static_cast<float>(index);
    float radius = kLogoutButtonsRadius * radius_fraction;
    float x = center_x + radius * std::cos(angle) - kLogoutButtonSize / 2.0f;
    float y = center_y + radius * std::sin(angle) - kLogoutButtonSize / 2.0f;
    return {x, y, kLogoutButtonSize, kLogoutButtonSize};
}

inline uint64_t button_delay_owner(int i) {
    return 10 + static_cast<uint64_t>(i);
}
inline uint64_t button_expand_owner(int i) {
    return 20 + static_cast<uint64_t>(i);
}
inline uint64_t button_scale_owner(int i) {
    return 30 + static_cast<uint64_t>(i);
}
inline uint64_t button_border_owner(int i) {
    return 40 + static_cast<uint64_t>(i);
}

inline void schedule_after(AnimationManager &anim, float delay_ms,
                           uint64_t owner, std::function<void()> fn) {
    anim.animateTimer(
        0.0f, 1.0f, delay_ms, Easing::Linear, [](float) {}, std::move(fn),
        owner);
}

inline void cancel_open_close_tweens(LogoutState &state) {
    state.base.animations.cancelForOwner(kLogoutLogoOwner);
    state.base.animations.cancelForOwner(kLogoutInputReadyOwner);
    state.base.animations.cancelForOwner(kLogoutCloseChainOwner);
    for (int i = 0; i < kLogoutButtonCount; ++i) {
        state.base.animations.cancelForOwner(button_delay_owner(i));
        state.base.animations.cancelForOwner(button_expand_owner(i));
    }
}

inline void trigger_highlight(LogoutState &state, int from_index,
                              int to_index) {
    auto animate_pair = [&state](int i, float target) {
        if (i < 0 || i >= kLogoutButtonCount)
            return;
        size_t idx = static_cast<size_t>(i);
        state.base.animations.animate(
            state.button_highlight_scale[idx], target, kLogoutButtonScaleMs,
            Easing::EaseOutCubic,
            [&state, idx](float v) { state.button_highlight_scale[idx] = v; },
            {}, button_scale_owner(i));
        state.base.animations.animate(
            state.button_highlight_border[idx], target, kLogoutButtonBorderMs,
            Easing::EaseOutCubic,
            [&state, idx](float v) { state.button_highlight_border[idx] = v; },
            {}, button_border_owner(i));
    };
    animate_pair(from_index, 0.0f);
    animate_pair(to_index, 1.0f);
}

inline void start_button_stagger_in(LogoutState &state) {
    for (int i = 0; i < kLogoutButtonCount; ++i) {
        float delay = static_cast<float>(i) * kLogoutButtonStaggerMs;
        schedule_after(
            state.base.animations, delay, button_delay_owner(i), [&state, i] {
                state.base.animations.animate(
                    0.0f, 1.0f, kLogoutButtonExpandMs, Easing::Linear,
                    [&state, i](float v) {
                        state.button_expand[static_cast<size_t>(i)] = v;
                    },
                    {}, button_expand_owner(i));
            });
    }

    schedule_after(state.base.animations,
                   static_cast<float>(kLogoutButtonCount) *
                       kLogoutButtonStaggerMs,
                   kLogoutInputReadyOwner, [&state] {
                       state.input_ready = true;
                       trigger_highlight(state, -1, state.selected_index);
                   });
}

inline void start_open_sequence(LogoutState &state) {
    cancel_open_close_tweens(state);
    state.input_ready = false;
    state.logo_scale = 0.0f;
    for (int i = 0; i < kLogoutButtonCount; ++i)
        state.button_expand[static_cast<size_t>(i)] = 0.0f;

    state.base.animations.animate(
        0.0f, 1.0f, kLogoutLogoAnimMs, Easing::EaseOutBack,
        [&state](float v) { state.logo_scale = v; },
        [&state] { start_button_stagger_in(state); }, kLogoutLogoOwner);
}

inline void finish_close(LogoutState &state) {
    state.base.open = false;
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        state.base.layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    overlay_panel_update_input_region(state.base);
    wl_surface_commit(state.base.surface);
}

inline void start_close_sequence(LogoutState &state) {
    cancel_open_close_tweens(state);
    state.input_ready = false;
    trigger_highlight(state, state.selected_index, -1);

    for (int i = 0; i < kLogoutButtonCount; ++i) {
        int idx = kLogoutButtonCount - 1 - i;
        float delay = static_cast<float>(i) * kLogoutButtonStaggerMs;
        schedule_after(
            state.base.animations, delay, button_delay_owner(idx),
            [&state, idx] {
                state.base.animations.animate(
                    state.button_expand[static_cast<size_t>(idx)], 0.0f,
                    kLogoutButtonExpandMs, Easing::Linear,
                    [&state, idx](float v) {
                        state.button_expand[static_cast<size_t>(idx)] = v;
                    },
                    {}, button_expand_owner(idx));
            });
    }
    schedule_after(
        state.base.animations,
        static_cast<float>(kLogoutButtonCount) * kLogoutButtonStaggerMs,
        kLogoutCloseChainOwner, [&state] {
            state.base.animations.animate(
                state.logo_scale, 0.0f, kLogoutLogoAnimMs, Easing::EaseInBack,
                [&state](float v) { state.logo_scale = v; },
                [&state] { finish_close(state); }, kLogoutLogoOwner);
        });
}

inline void fast_hide(LogoutState &state) {
    cancel_open_close_tweens(state);
    for (int i = 0; i < kLogoutButtonCount; ++i) {
        size_t idx = static_cast<size_t>(i);
        state.button_expand[idx] = 0.0f;
        state.button_highlight_scale[idx] = 0.0f;
        state.button_highlight_border[idx] = 0.0f;
        state.base.animations.cancelForOwner(button_scale_owner(i));
        state.base.animations.cancelForOwner(button_border_owner(i));
    }
    state.logo_scale = 0.0f;
    state.input_ready = false;
    finish_close(state);
}

}

inline RasterizedText
rasterize_yujimai_glyph(const std::string &codepoint_utf8) {
    RasterizedText result;
    logout_detail::YujiMaiFont &font = logout_detail::yujimai_font();
    if (!font.cairo_face)
        return result;

    uint32_t codepoint = logout_detail::decode_utf8_codepoint(codepoint_utf8);
    FT_UInt glyph_index = FT_Get_Char_Index(font.face, codepoint);
    if (glyph_index == 0) {
        klog("logout: no glyph for codepoint U+%04X", codepoint);
        return result;
    }

    cairo_matrix_t font_matrix;
    cairo_matrix_init_scale(&font_matrix, kLogoutGlyphPx, kLogoutGlyphPx);
    cairo_matrix_t ctm;
    cairo_matrix_init_identity(&ctm);
    cairo_scaled_font_t *scaled_font = cairo_scaled_font_create(
        font.cairo_face, &font_matrix, &ctm, kokusei_font_options());

    cairo_glyph_t measure_glyph = {glyph_index, 0, 0};
    cairo_text_extents_t extents;
    cairo_scaled_font_glyph_extents(scaled_font, &measure_glyph, 1, &extents);

    int width = static_cast<int>(std::ceil(extents.width));
    int height = static_cast<int>(std::ceil(extents.height));
    if (width <= 0 || height <= 0) {
        cairo_scaled_font_destroy(scaled_font);
        return result;
    }

    cairo_surface_t *surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);
    cairo_set_scaled_font(cr, scaled_font);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_glyph_t draw_glyph = {glyph_index, -extents.x_bearing,
                                -extents.y_bearing};
    cairo_show_glyphs(cr, &draw_glyph, 1);
    cairo_surface_flush(surface);

    result = surface_to_rgba(surface, width, height);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    cairo_scaled_font_destroy(scaled_font);
    return result;
}

inline bool logout_create_surface(LogoutState &state, wl_compositor *compositor,
                                  zwlr_layer_shell_v1 *layer_shell) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-logout");
}

inline void logout_paint(LogoutState &state);

inline bool logout_init_egl(LogoutState &state, Renderer &renderer,
                            EGLDisplay display, EGLConfig config,
                            EGLContext context) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state] { logout_paint(state); };
    return true;
}

inline void logout_request_frame(LogoutState &state) {
    overlay_panel_request_frame(state.base);
}

inline void logout_toggle(LogoutState &state, bool by_widget = false) {
    if (!state.base.layer_surface || state.base.egl_surface == EGL_NO_SURFACE)
        return;

    bool opening = !state.base.open;
    if (opening) {

        state.selected_index = 0;
        state.base.open = true;
        state.base.opacity = 1.0f;
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            state.base.layer_surface,
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
        overlay_panel_update_input_region(state.base);
        wl_surface_commit(state.base.surface);
        state.opened_by_widget = by_widget;
        logout_detail::start_open_sequence(state);
    } else {
        logout_detail::start_close_sequence(state);
    }
    overlay_panel_request_frame(state.base);
}

inline void logout_execute(LogoutState &state, int index) {
    if (index < 0 || index >= kLogoutButtonCount)
        return;
    const char *cmd = kLogoutActions[static_cast<size_t>(index)].command;
    if (cmd && cmd[0] != '\0')
        spawn_detached(cmd);
    state.selected_index = 0;
    logout_detail::fast_hide(state);
    overlay_panel_request_frame(state.base);
}

inline void logout_handle_key_event(LogoutState &state, const KeyEvent &event) {

    if (!state.input_ready)
        return;
    switch (event.kind) {
    case KeyKind::Left: {
        int old = state.selected_index;
        state.selected_index = (state.selected_index + kLogoutButtonCount - 1) %
                               kLogoutButtonCount;
        logout_detail::trigger_highlight(state, old, state.selected_index);
        break;
    }
    case KeyKind::Right: {
        int old = state.selected_index;
        state.selected_index = (state.selected_index + 1) % kLogoutButtonCount;
        logout_detail::trigger_highlight(state, old, state.selected_index);
        break;
    }
    case KeyKind::Text:
        if (event.text.size() == 1 && event.text[0] >= '1' &&
            event.text[0] <= '8') {
            int old = state.selected_index;
            state.selected_index = event.text[0] - '1';
            if (old != state.selected_index)
                logout_detail::trigger_highlight(state, old,
                                                 state.selected_index);
        }
        break;
    case KeyKind::Enter:
        logout_execute(state, state.selected_index);
        break;
    case KeyKind::Escape:
        logout_toggle(state);
        break;
    default:
        break;
    }
}

inline void logout_handle_click(LogoutState &state, double px, double py) {
    float cx = static_cast<float>(state.base.width) / 2.0f;
    float cy = static_cast<float>(state.base.height) / 2.0f;
    for (int i = 0; i < kLogoutButtonCount; ++i) {
        Rect r = logout_detail::button_rect(i, cx, cy);
        if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) {

            if (state.input_ready)
                logout_execute(state, i);
            return;
        }
    }
    logout_toggle(state);
}

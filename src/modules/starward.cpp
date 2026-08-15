#include "modules/starward.h"

#include "app/monitor_output.h"
#include "app/wayland_state.h"
#include "core/log.h"
#include "modules/launcher.h"
#include "render/color_ops.h"
#include "render/node.h"
#include "service/layer_surface.h"

#include <GLES2/gl2.h>

#include <cairo/cairo-ft.h>
#include <cairo/cairo.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <deque>

Rect starward_detail_button_rect(int index, float center_x, float center_y,
                                 float radius_fraction) {
    float angle =
        kStarwardStartAngle + kStarwardStepAngle * static_cast<float>(index);
    float radius = kStarwardButtonsRadius * radius_fraction;
    float x = center_x + radius * std::cos(angle) - kStarwardButtonSize / 2.0f;
    float y = center_y + radius * std::sin(angle) - kStarwardButtonSize / 2.0f;
    return {x, y, kStarwardButtonSize, kStarwardButtonSize};
}

namespace {

struct YujiMaiFont {
    FT_Face face = nullptr;
    cairo_font_face_t *cairo_face = nullptr;
};

YujiMaiFont &yujimai_font() {
    static YujiMaiFont font = [] {
        YujiMaiFont f;
        static FT_Library library;
        if (FT_Init_FreeType(&library)) {
            klog("starward: FT_Init_FreeType failed");
            return f;
        }
        const char *candidates[] = {
            KOKUSEI_YUJIMAI_FONT,
            "assets/fonts/YujiMai.ttf",
        };
        for (const char *path : candidates) {
            if (FT_New_Face(library, path, 0, &f.face) == 0) {
                klog("starward: loaded YujiMai from %s", path);
                break;
            }
            f.face = nullptr;
        }
        if (!f.face) {
            klog("starward: failed to load YujiMai.ttf");
            return f;
        }
        f.cairo_face = cairo_ft_font_face_create_for_ft_face(f.face, 0);
        return f;
    }();
    return font;
}

uint32_t decode_utf8_codepoint(const std::string &s) {
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

uint64_t button_delay_owner(int i) { return 10 + static_cast<uint64_t>(i); }
uint64_t button_expand_owner(int i) { return 20 + static_cast<uint64_t>(i); }
uint64_t button_scale_owner(int i) { return 30 + static_cast<uint64_t>(i); }
uint64_t button_border_owner(int i) { return 40 + static_cast<uint64_t>(i); }

void schedule_after(AnimationManager &anim, float delay_ms, uint64_t owner,
                    std::function<void()> fn) {
    anim.animate(
        0.0f, 1.0f, delay_ms, Easing::Linear, [](float) {}, std::move(fn),
        owner);
}

void cancel_open_close_tweens(StarwardState &state) {
    state.base.animations.cancelForOwner(kStarwardLogoOwner);
    state.base.animations.cancelForOwner(kStarwardInputReadyOwner);
    state.base.animations.cancelForOwner(kStarwardCloseChainOwner);
    for (int i = 0; i < kStarwardButtonCount; ++i) {
        state.base.animations.cancelForOwner(button_delay_owner(i));
        state.base.animations.cancelForOwner(button_expand_owner(i));
    }
}

void set_button_highlight(StarwardState &state, int i, bool on) {
    if (i < 0 || i >= kStarwardButtonCount)
        return;
    size_t idx = static_cast<size_t>(i);
    float target = on ? 1.0f : 0.0f;
    state.base.animations.animate(
        state.button_highlight_scale[idx], target, kStarwardButtonScaleMs,
        Easing::EaseOutCubic,
        [&state, idx](float v) { state.button_highlight_scale[idx] = v; }, {},
        button_scale_owner(i));
    state.base.animations.animate(
        state.button_highlight_border[idx], target, kStarwardButtonBorderMs,
        Easing::EaseOutCubic,
        [&state, idx](float v) { state.button_highlight_border[idx] = v; }, {},
        button_border_owner(i));
}

bool is_highlighted(const StarwardState &state, int i) {
    return i == state.selected_index || i == state.hovered_index;
}

void update_highlight(StarwardState &state, int i) {
    set_button_highlight(state, i, is_highlighted(state, i));
}

void start_button_stagger_in(StarwardState &state) {
    for (int i = 0; i < kStarwardButtonCount; ++i) {
        float delay = static_cast<float>(i) * kStarwardButtonStaggerMs;
        schedule_after(
            state.base.animations, delay, button_delay_owner(i), [&state, i] {
                state.base.animations.animate(
                    0.0f, 1.0f, kStarwardButtonExpandMs, Easing::Linear,
                    [&state, i](float v) {
                        state.button_expand[static_cast<size_t>(i)] = v;
                    },
                    {}, button_expand_owner(i));
            });
    }

    schedule_after(state.base.animations,
                   static_cast<float>(kStarwardButtonCount) *
                       kStarwardButtonStaggerMs,
                   kStarwardInputReadyOwner, [&state] {
                       state.input_ready = true;
                       update_highlight(state, state.selected_index);
                   });
}

void start_open_sequence(StarwardState &state) {
    cancel_open_close_tweens(state);
    state.input_ready = false;
    state.logo_scale = 0.0f;
    for (int i = 0; i < kStarwardButtonCount; ++i)
        state.button_expand[static_cast<size_t>(i)] = 0.0f;

    state.base.animations.animate(
        0.0f, 1.0f, kStarwardLogoAnimMs, Easing::EaseOutBack,
        [&state](float v) { state.logo_scale = v; },
        [&state] { start_button_stagger_in(state); }, kStarwardLogoOwner);
}

void finish_close(StarwardState &state) {
    state.base.open = false;
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        state.base.layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    overlay_panel_update_input_region(state.base);
    wl_surface_commit(state.base.surface);
}

void start_close_sequence(StarwardState &state) {
    cancel_open_close_tweens(state);
    state.input_ready = false;
    update_highlight(state, state.selected_index);
    update_highlight(state, state.hovered_index);
    state.hovered_index = -1;

    for (int i = 0; i < kStarwardButtonCount; ++i) {
        int idx = kStarwardButtonCount - 1 - i;
        float delay = static_cast<float>(i) * kStarwardButtonStaggerMs;
        schedule_after(
            state.base.animations, delay, button_delay_owner(idx),
            [&state, idx] {
                state.base.animations.animate(
                    state.button_expand[static_cast<size_t>(idx)], 0.0f,
                    kStarwardButtonExpandMs, Easing::Linear,
                    [&state, idx](float v) {
                        state.button_expand[static_cast<size_t>(idx)] = v;
                    },
                    {}, button_expand_owner(idx));
            });
    }
    schedule_after(
        state.base.animations,
        static_cast<float>(kStarwardButtonCount) * kStarwardButtonStaggerMs,
        kStarwardCloseChainOwner, [&state] {
            state.base.animations.animate(
                state.logo_scale, 0.0f, kStarwardLogoAnimMs, Easing::EaseInBack,
                [&state](float v) { state.logo_scale = v; },
                [&state] { finish_close(state); }, kStarwardLogoOwner);
        });
}

void fast_hide(StarwardState &state) {
    cancel_open_close_tweens(state);
    for (int i = 0; i < kStarwardButtonCount; ++i) {
        size_t idx = static_cast<size_t>(i);
        state.button_expand[idx] = 0.0f;
        state.button_highlight_scale[idx] = 0.0f;
        state.button_highlight_border[idx] = 0.0f;
        state.base.animations.cancelForOwner(button_scale_owner(i));
        state.base.animations.cancelForOwner(button_border_owner(i));
    }
    state.hovered_index = -1;
    state.logo_scale = 0.0f;
    state.input_ready = false;
    finish_close(state);
}

} // namespace

RasterizedText rasterize_yujimai_glyph(const std::string &codepoint_utf8) {
    RasterizedText result;
    YujiMaiFont &font = yujimai_font();
    if (!font.cairo_face)
        return result;

    uint32_t codepoint = decode_utf8_codepoint(codepoint_utf8);
    FT_UInt glyph_index = FT_Get_Char_Index(font.face, codepoint);
    if (glyph_index == 0) {
        klog("starward: no glyph for codepoint U+%04X", codepoint);
        return result;
    }

    cairo_matrix_t font_matrix;
    cairo_matrix_init_scale(&font_matrix, kStarwardGlyphPx, kStarwardGlyphPx);
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

bool starward_create_surface(StarwardState &state, wl_compositor *compositor,
                             zwlr_layer_shell_v1 *layer_shell,
                             wl_output *output) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-starward", output);
}

bool starward_init_egl(StarwardState &state, Renderer &renderer,
                       EGLDisplay display, EGLConfig config,
                       EGLContext context) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state] { starward_paint(state); };
    return true;
}

void starward_retarget(StarwardState &state, wl_compositor *compositor,
                       zwlr_layer_shell_v1 *layer_shell, wl_display *display,
                       Renderer &renderer, EGLDisplay egl_display,
                       EGLConfig egl_config, EGLContext egl_context,
                       wl_output *target_output, const char *target_name) {
    wl_output *bound = overlay_panel_retarget(
        state.base, display, state.bound_output, target_output, target_name,
        [&](wl_output *out) {
            return starward_create_surface(state, compositor, layer_shell, out);
        },
        [&] {
            return starward_init_egl(state, renderer, egl_display, egl_config,
                                     egl_context);
        });
    if (bound)
        state.bound_output = bound;
}

void starward_request_frame(StarwardState &state) {
    overlay_panel_request_frame(state.base);
}

void starward_toggle(StarwardState &state, bool by_widget) {
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
        start_open_sequence(state);
    } else {
        start_close_sequence(state);
    }
    overlay_panel_request_frame(state.base);
}

std::vector<IpcHandler> starward_ipc_handlers(WaylandState &state) {
    return {
        {"starward",
         [&state] {
             StarwardState &starward = state.starward;
             if (!starward.base.open) {
                 MonitorOutput *target =
                     bar_detail::active_target_monitor(state);
                 if (target && target->output.wl != starward.bound_output)
                     starward_retarget(
                         starward, state.compositor, state.layer_shell,
                         state.display, state.renderer, state.egl_display,
                         state.egl_config, state.egl_context, target->output.wl,
                         target->output.name.c_str());
             }
             starward_toggle(starward);
         },
         "toggle the starward overlay"},
    };
}

void starward_execute(StarwardState &state, int index) {
    if (index < 0 || index >= kStarwardButtonCount)
        return;
    const char *cmd = kStarwardActions[static_cast<size_t>(index)].command;
    if (cmd && cmd[0] != '\0')
        spawn_detached(cmd);
    state.selected_index = 0;
    fast_hide(state);
    overlay_panel_request_frame(state.base);
}

void starward_handle_key_event(StarwardState &state, const KeyEvent &event) {

    if (!state.input_ready)
        return;
    switch (event.kind) {
    case KeyKind::Left: {
        int old = state.selected_index;
        state.selected_index =
            (state.selected_index + kStarwardButtonCount - 1) %
            kStarwardButtonCount;
        update_highlight(state, old);
        update_highlight(state, state.selected_index);
        break;
    }
    case KeyKind::Right: {
        int old = state.selected_index;
        state.selected_index =
            (state.selected_index + 1) % kStarwardButtonCount;
        update_highlight(state, old);
        update_highlight(state, state.selected_index);
        break;
    }
    case KeyKind::Text:
        if (event.text.size() == 1 && event.text[0] >= '1' &&
            event.text[0] <= '8') {
            int old = state.selected_index;
            state.selected_index = event.text[0] - '1';
            if (old != state.selected_index) {
                update_highlight(state, old);
                update_highlight(state, state.selected_index);
            }
        }
        break;
    case KeyKind::Enter:
        starward_execute(state, state.selected_index);
        break;
    case KeyKind::Escape:
        starward_toggle(state);
        break;
    default:
        break;
    }
}

void starward_handle_click(StarwardState &state, double px, double py) {
    float cx = static_cast<float>(state.base.width) / 2.0f;
    float cy = static_cast<float>(state.base.height) / 2.0f;
    for (int i = 0; i < kStarwardButtonCount; ++i) {
        Rect r = starward_detail_button_rect(i, cx, cy);
        if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) {

            if (state.input_ready)
                starward_execute(state, i);
            return;
        }
    }
    starward_toggle(state);
}

void starward_handle_hover(StarwardState &state, double px, double py) {
    if (!state.input_ready)
        return;
    float cx = static_cast<float>(state.base.width) / 2.0f;
    float cy = static_cast<float>(state.base.height) / 2.0f;
    int found = -1;
    for (int i = 0; i < kStarwardButtonCount; ++i) {
        Rect r = starward_detail_button_rect(i, cx, cy);
        if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) {
            found = i;
            break;
        }
    }
    if (found == state.hovered_index)
        return;
    int old = state.hovered_index;
    state.hovered_index = found;
    update_highlight(state, old);
    update_highlight(state, found);
}

void starward_clear_hover(StarwardState &state) {
    if (state.hovered_index == -1)
        return;
    int old = state.hovered_index;
    state.hovered_index = -1;
    update_highlight(state, old);
}

void starward_paint(StarwardState &state) {
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

        for (int i = 0; i < kStarwardButtonCount; ++i) {
            float expand = state.button_expand[static_cast<size_t>(i)];
            float highlight_scale =
                1.0f + (kStarwardHighlightScale - 1.0f) *
                           state.button_highlight_scale[static_cast<size_t>(i)];

            Rect r = starward_detail_button_rect(i, cx, cy, expand);
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
            btn->radius = kStarwardButtonCornerRadius * highlight_scale;
            btn->border_width = kStarwardBorderWidth;
            btn->fill = rgba(fill);
            btn->border = rgba(border);

            Texture &tex = state.glyph_tex[static_cast<size_t>(i)];
            if (!tex.id) {
                RasterizedText rt = rasterize_yujimai_glyph(
                    kStarwardActions[static_cast<size_t>(i)].glyph_utf8);
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

        float logo_size = kStarwardLogoSize * state.logo_scale;

        if (state.logo_tex.id) {
            float scale =
                logo_size / static_cast<float>(std::max(state.logo_tex.width,
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

#include "render/icon.h"

#include <cairo/cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "core/log.h"

#include <cmath>

namespace {

struct IconFont {
    FT_Face face = nullptr;
    cairo_font_face_t *cairo_face = nullptr;
};

IconFont &icon_font() {
    static IconFont font = [] {
        IconFont f;
        static FT_Library library;
        if (FT_Init_FreeType(&library)) {
            klog("icon: FT_Init_FreeType failed");
            return f;
        }
        const char *candidates[] = {
            KOKUSEI_FONT_DIR "/tabler-icons.ttf",
            "assets/fonts/tabler-icons.ttf",
        };
        for (const char *path : candidates) {
            if (FT_New_Face(library, path, 0, &f.face) == 0) {
                klog("icon: loaded tabler-icons from %s", path);
                break;
            }
            f.face = nullptr;
        }
        if (!f.face) {
            klog("icon: failed to load tabler-icons.ttf");
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

} // namespace

RasterizedText rasterize_icon(const std::string &codepoint_utf8,
                              int32_t scale) {
    RasterizedText result;
    IconFont &font = icon_font();
    if (!font.cairo_face)
        return result;

    uint32_t codepoint = decode_utf8_codepoint(codepoint_utf8);
    FT_UInt glyph_index = FT_Get_Char_Index(font.face, codepoint);
    if (glyph_index == 0) {
        klog("icon: no glyph for codepoint U+%04X", codepoint);
        return result;
    }

    scale = scale > 0 ? scale : 1;
    cairo_matrix_t font_matrix;
    cairo_matrix_init_scale(&font_matrix, KOKUSEI_ICON_PX * scale,
                            KOKUSEI_ICON_PX * scale);
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
    result.scale = scale;

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    cairo_scaled_font_destroy(scaled_font);
    return result;
}

#include <algorithm>

#include "render/text.h"

RasterizedText surface_to_rgba(cairo_surface_t *surface, int width,
                               int height) {
    RasterizedText result;
    const uint8_t *src = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    result.width = width;
    result.height = height;
    result.rgba.resize(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const uint8_t *row = src + y * stride;
        for (int x = 0; x < width; ++x) {
            uint8_t b = row[x * 4 + 0], g = row[x * 4 + 1], r = row[x * 4 + 2],
                    a = row[x * 4 + 3];
            if (a > 0 && a < 255) {
                r = static_cast<uint8_t>(std::min(255, (r * 255 + a / 2) / a));
                g = static_cast<uint8_t>(std::min(255, (g * 255 + a / 2) / a));
                b = static_cast<uint8_t>(std::min(255, (b * 255 + a / 2) / a));
            }
            uint8_t *out =
                &result.rgba[(static_cast<size_t>(y) * width + x) * 4];
            out[0] = r;
            out[1] = g;
            out[2] = b;
            out[3] = a;
        }
    }
    return result;
}

Texture make_texture_from_raster(const RasterizedText &raster, bool mipmapped) {
    if (raster.width <= 0 || raster.height <= 0)
        return Texture{};
    Texture tex = make_texture_rgba(raster.width, raster.height,
                                    raster.rgba.data(), mipmapped);
    tex.scale = raster.scale;
    return tex;
}

PangoFontDescription *kokusei_font_description() {
    static PangoFontDescription *desc =
        pango_font_description_from_string(KOKUSEI_FONT);
    return desc;
}

PangoFontDescription *kokusei_font_description_small() {
    static PangoFontDescription *desc =
        pango_font_description_from_string(KOKUSEI_FONT_SMALL);
    return desc;
}

PangoFontDescription *kokusei_font_description_large() {
    static PangoFontDescription *desc =
        pango_font_description_from_string(KOKUSEI_FONT_LARGE);
    return desc;
}

cairo_font_options_t *kokusei_font_options() {
    static cairo_font_options_t *options = [] {
        cairo_font_options_t *opts = cairo_font_options_create();
        cairo_font_options_set_antialias(opts, CAIRO_ANTIALIAS_GOOD);
        cairo_font_options_set_hint_style(opts, CAIRO_HINT_STYLE_SLIGHT);
        cairo_font_options_set_hint_metrics(opts, CAIRO_HINT_METRICS_ON);
        return opts;
    }();
    return options;
}

std::string elide(const std::string &s, size_t max_chars) {
    if (s.size() <= max_chars)
        return s;
    size_t cut = max_chars - 1;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
        --cut;
    return s.substr(0, cut) + "…";
}

void font_ascent_descent(PangoFontDescription *desc, int &ascent,
                         int &descent) {
    PangoFontMap *font_map = pango_cairo_font_map_get_default();
    PangoContext *context = pango_font_map_create_context(font_map);
    PangoFontMetrics *metrics =
        pango_context_get_metrics(context, desc, nullptr);
    ascent = pango_font_metrics_get_ascent(metrics) / PANGO_SCALE;
    descent = pango_font_metrics_get_descent(metrics) / PANGO_SCALE;
    pango_font_metrics_unref(metrics);
    g_object_unref(context);
}

RasterizedText rasterize_text_with(const std::string &text,
                                   PangoFontDescription *desc, int32_t scale,
                                   int max_width_px) {
    cairo_surface_t *measure_surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *measure_cr = cairo_create(measure_surface);
    cairo_set_font_options(measure_cr, kokusei_font_options());
    PangoLayout *measure_layout = pango_cairo_create_layout(measure_cr);
    pango_layout_set_font_description(measure_layout, desc);
    pango_layout_set_text(measure_layout, text.c_str(), -1);
    if (max_width_px > 0) {
        pango_layout_set_width(measure_layout, max_width_px * PANGO_SCALE);
        pango_layout_set_ellipsize(measure_layout, PANGO_ELLIPSIZE_END);
    }

    PangoRectangle ink_rect, logical_rect;
    pango_layout_get_pixel_extents(measure_layout, &ink_rect, &logical_rect);
    g_object_unref(measure_layout);
    cairo_destroy(measure_cr);
    cairo_surface_destroy(measure_surface);

    int ascent, descent;
    font_ascent_descent(desc, ascent, descent);

    int width = ink_rect.width, height = ascent + descent;
    RasterizedText result;
    if (width <= 0 || height <= 0) {
        return result;
    }
    scale = scale > 0 ? scale : 1;
    int pixel_width = width * scale, pixel_height = height * scale;

    cairo_surface_t *surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, pixel_width, pixel_height);
    cairo_t *cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);
    cairo_set_font_options(cr, kokusei_font_options());
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, text.c_str(), -1);
    if (max_width_px > 0) {
        pango_layout_set_width(layout, max_width_px * PANGO_SCALE);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    }

    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_move_to(cr, -ink_rect.x, 0);
    pango_cairo_show_layout(cr, layout);
    cairo_surface_flush(surface);

    result = surface_to_rgba(surface, pixel_width, pixel_height);
    result.scale = scale;

    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return result;
}

RasterizedText rasterize_text(const std::string &text, int32_t scale,
                              int max_width_px) {
    return rasterize_text_with(text, kokusei_font_description(), scale,
                               max_width_px);
}

RasterizedText rasterize_text_small(const std::string &text, int32_t scale,
                                    int max_width_px) {
    return rasterize_text_with(text, kokusei_font_description_small(), scale,
                               max_width_px);
}

RasterizedText rasterize_text_large(const std::string &text, int32_t scale,
                                    int max_width_px) {
    return rasterize_text_with(text, kokusei_font_description_large(), scale,
                               max_width_px);
}

#pragma once

#include "render/texture.h"

#include <cairo/cairo.h>
#include <cstdint>
#include <pango/pangocairo.h>
#include <string>
#include <vector>

inline constexpr const char *KOKUSEI_FONT = "ComicShannsMono Nerd Font 13";

inline constexpr const char *KOKUSEI_FONT_SMALL = "ComicShannsMono Nerd Font 9";

struct RasterizedText {
    int width = 0;
    int height = 0;
    int32_t scale = 1;
    std::vector<uint8_t> rgba;
};

RasterizedText surface_to_rgba(cairo_surface_t *surface, int width, int height);

Texture make_texture_from_raster(const RasterizedText &raster,
                                 bool mipmapped = false);

PangoFontDescription *kokusei_font_description();

PangoFontDescription *kokusei_font_description_small();

cairo_font_options_t *kokusei_font_options();

std::string elide(const std::string &s, size_t max_chars);

void font_ascent_descent(PangoFontDescription *desc, int &ascent, int &descent);

RasterizedText rasterize_text_with(const std::string &text,
                                   PangoFontDescription *desc,
                                   int32_t scale = 1, int max_width_px = 0);

RasterizedText rasterize_text(const std::string &text, int32_t scale = 1,
                              int max_width_px = 0);

RasterizedText rasterize_text_small(const std::string &text, int32_t scale = 1,
                                    int max_width_px = 0);

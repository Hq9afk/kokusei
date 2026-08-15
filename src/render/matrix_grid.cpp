#include "render/matrix_grid.h"

#include "config/matrix_config.h"
#include "render/text.h"

#include <cairo/cairo.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace {

std::mt19937 &rng() {
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

float random01() {
    static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng());
}

std::string utf8_encode(char32_t cp) {
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

void set_matrix_font(cairo_t *cr, bool bold) {
    cairo_select_font_face(cr, "Noto Sans CJK JP", CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD
                                : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, kMatrixFontPx);
}

void draw_glyph_centered(cairo_t *cr, char32_t glyph, float cell_x,
                         float cell_y, bool bold, const Color &color) {
    set_matrix_font(cr, bold);
    std::string utf8 = utf8_encode(glyph);
    cairo_text_extents_t extents;
    cairo_text_extents(cr, utf8.c_str(), &extents);
    float tx = cell_x + (kMatrixCellWidth - extents.width) / 2.0f -
              extents.x_bearing;
    float ty = cell_y + (kMatrixCellHeight - extents.height) / 2.0f -
              extents.y_bearing;
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    cairo_move_to(cr, std::round(tx), std::round(ty));
    cairo_show_text(cr, utf8.c_str());
}

} // namespace

char32_t MatrixGrid::random_glyph() const {
    const std::u32string &pool = matrix_glyph_pool();
    if (pool.empty())
        return U' ';
    size_t idx = static_cast<size_t>(random01() * static_cast<float>(pool.size()));
    if (idx >= pool.size())
        idx = pool.size() - 1;
    return pool[idx];
}

void MatrixGrid::rebuild(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);

    // Leave a gap between streams; filling every cell reads as too dense.
    column_count_ = std::max(1, static_cast<int>(width_ / (kMatrixCellWidth * 2)));
    row_count_ = std::max(1, static_cast<int>(height_ / kMatrixCellHeight));

    columns_.assign(static_cast<size_t>(column_count_), Column{});
    for (Column &c : columns_)
        c.drop = start_drop();

    float content_width = column_count_ * kMatrixCellWidth * 2 - kMatrixCellWidth;
    offset_x_ = (width_ - content_width) / 2.0f;
    offset_y_ = (height_ - row_count_ * kMatrixCellHeight) / 2.0f;

    stride_ = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width_);
    buffer_.assign(static_cast<size_t>(stride_) * static_cast<size_t>(height_), 0);
    texture_ = Texture{};
}

void MatrixGrid::decay() {
    int alpha_step = std::clamp(static_cast<int>(255.0f * kMatrixFadeAlpha), 1, 255);
    for (int y = 0; y < height_; ++y) {
        uint8_t *row = buffer_.data() + static_cast<size_t>(y) * stride_;
        for (int x = 0; x < width_; ++x) {
            uint8_t *px = row + x * 4;
            int old_alpha = px[3];
            if (old_alpha == 0)
                continue;
            int new_alpha = std::max(0, old_alpha - alpha_step);
            if (new_alpha == 0) {
                px[0] = px[1] = px[2] = px[3] = 0;
                continue;
            }
            px[0] = static_cast<uint8_t>(px[0] * new_alpha / old_alpha);
            px[1] = static_cast<uint8_t>(px[1] * new_alpha / old_alpha);
            px[2] = static_cast<uint8_t>(px[2] * new_alpha / old_alpha);
            px[3] = static_cast<uint8_t>(new_alpha);
        }
    }
}

void MatrixGrid::tick() {
    if (buffer_.empty() || columns_.empty())
        return;

    decay();

    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        buffer_.data(), CAIRO_FORMAT_ARGB32, width_, height_, stride_);
    cairo_t *cr = cairo_create(surface);
    cairo_font_options_t *opts = cairo_font_options_create();
    cairo_font_options_set_antialias(opts, CAIRO_ANTIALIAS_GRAY);
    cairo_set_font_options(cr, opts);
    cairo_font_options_destroy(opts);

    for (int c = 0; c < column_count_; ++c) {
        Column &col = columns_[static_cast<size_t>(c)];
        float x = offset_x_ + c * kMatrixCellWidth * 2.0f;

        if (col.last_head_valid) {
            float last_y = offset_y_ + col.last_head_drop * kMatrixCellHeight;
            draw_glyph_centered(cr, col.last_glyph, x, last_y, false,
                                kMatrixTailColor);
        }

        if (col.drop >= 0.0f) {
            float y = offset_y_ + col.drop * kMatrixCellHeight;
            bool bold = random01() < kMatrixBoldChance;
            char32_t glyph = random_glyph();
            draw_glyph_centered(cr, glyph, x, y, bold, kMatrixHeadColor);
            col.last_glyph = glyph;
            col.last_head_drop = col.drop;
            col.last_head_valid = true;
        } else {
            col.last_head_valid = false;
        }

        col.drop += 1.0f;

        if (col.drop * kMatrixCellHeight > static_cast<float>(height_) &&
            random01() < kMatrixResetChance) {
            col.drop = col.ever_reset
                          ? -(random01() * static_cast<float>(row_count_))
                          : start_drop();
            col.ever_reset = true;
            col.last_head_valid = false;
        }
    }

    cairo_destroy(cr);
    cairo_surface_flush(surface);

    RasterizedText raster = surface_to_rgba(surface, width_, height_);
    cairo_surface_destroy(surface);

    texture_ = make_texture_rgba(raster.width, raster.height, raster.rgba.data());
}

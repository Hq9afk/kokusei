#pragma once

#include "../core/log.hpp"
#include "texture.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <jpeglib.h>
#include <png.h>

inline unsigned char *decode_png(FILE *fp, int &width, int &height) {
    png_byte header[8];
    if (fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8))
        return nullptr;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                             nullptr, nullptr);
    if (!png)
        return nullptr;
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        return nullptr;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        return nullptr;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    width = static_cast<int>(png_get_image_width(png, info));
    height = static_cast<int>(png_get_image_height(png, info));
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16)
        png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    auto *data = new unsigned char[static_cast<size_t>(width) * height * 4];
    std::vector<png_bytep> rows(static_cast<size_t>(height));
    for (int y = 0; y < height; ++y)
        rows[static_cast<size_t>(y)] =
            data + static_cast<size_t>(y) * width * 4;
    png_read_image(png, rows.data());

    png_destroy_read_struct(&png, &info, nullptr);
    return data;
}

inline unsigned char *decode_jpeg(FILE *fp, int &width, int &height) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return nullptr;
    }

#ifdef JCS_EXTENSIONS
    cinfo.out_color_space = JCS_EXT_RGBA;
#else
    cinfo.out_color_space = JCS_RGB;
#endif
    jpeg_start_decompress(&cinfo);
    width = static_cast<int>(cinfo.output_width);
    height = static_cast<int>(cinfo.output_height);

    auto *data = new unsigned char[static_cast<size_t>(width) * height * 4];
#ifdef JCS_EXTENSIONS
    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char *row =
            data + static_cast<size_t>(cinfo.output_scanline) * width * 4;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
#else
    std::vector<unsigned char> row_buf(static_cast<size_t>(width) * 3);
    while (cinfo.output_scanline < cinfo.output_height) {
        int y = static_cast<int>(cinfo.output_scanline);
        unsigned char *row_ptr = row_buf.data();
        jpeg_read_scanlines(&cinfo, &row_ptr, 1);
        unsigned char *out = data + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            out[x * 4 + 0] = row_buf[static_cast<size_t>(x) * 3 + 0];
            out[x * 4 + 1] = row_buf[static_cast<size_t>(x) * 3 + 1];
            out[x * 4 + 2] = row_buf[static_cast<size_t>(x) * 3 + 2];
            out[x * 4 + 3] = 0xFF;
        }
    }
#endif
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return data;
}

inline unsigned char *load_image_decode(const std::string &path, int &width,
                                        int &height) {
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) {
        klog("image: failed to open '%s'", path.c_str());
        return nullptr;
    }

    unsigned char sig[8] = {0};
    size_t n = fread(sig, 1, 8, fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *data = nullptr;
    if (n >= 8 && !png_sig_cmp(sig, 0, 8))
        data = decode_png(fp, width, height);
    else if (n >= 2 && sig[0] == 0xFF && sig[1] == 0xD8)
        data = decode_jpeg(fp, width, height);

    fclose(fp);
    if (!data)
        klog("image: failed to load '%s'", path.c_str());
    return data;
}

inline Texture load_image_texture(const std::string &path) {
    int width = 0, height = 0;
    unsigned char *data = load_image_decode(path, width, height);
    if (!data)
        return Texture{};
    Texture tex = make_texture_rgba(width, height, data, true);
    delete[] data;
    return tex;
}


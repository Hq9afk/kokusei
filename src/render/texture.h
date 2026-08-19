#pragma once

#include <GLES2/gl2.h>
#include <cstdint>
#include <utility>

struct Texture {
    GLuint id = 0;
    int width = 0;
    int height = 0;
    int32_t scale = 1;

    Texture() = default;
    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;
    Texture(Texture &&other) noexcept { *this = std::move(other); }
    Texture &operator=(Texture &&other) noexcept {
        if (this != &other) {
            reset();
            id = other.id;
            width = other.width;
            height = other.height;
            scale = other.scale;
            other.id = 0;
        }
        return *this;
    }
    ~Texture() { reset(); }

    void reset() {
        if (id)
            glDeleteTextures(1, &id);
        id = 0;
    }
};

Texture make_texture_rgba(int width, int height, const uint8_t *rgba,
                          bool mipmapped = false, int stride_px = 0);

void update_texture_rgba(Texture &tex, int width, int height,
                         const uint8_t *rgba, bool mipmapped = false,
                         int stride_px = 0);

void texture_detect_caps();
bool texture_row_length_supported();

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

inline Texture make_texture_rgba(int width, int height, const uint8_t *rgba,
                                 bool mipmapped = false) {
    Texture tex;
    tex.width = width;
    tex.height = height;
    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
    if (mipmapped)
        glGenerateMipmap(GL_TEXTURE_2D);
    return tex;
}

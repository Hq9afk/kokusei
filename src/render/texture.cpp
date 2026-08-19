#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <atomic>
#include <cstring>

#include "render/texture.h"

namespace {

std::atomic<bool> g_row_length_supported{false};

void set_unpack_row_length(int stride_px) {
    if (stride_px > 0 && texture_row_length_supported())
        glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride_px);
}

void clear_unpack_row_length(int stride_px) {
    if (stride_px > 0 && texture_row_length_supported())
        glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
}

} // namespace

void texture_detect_caps() {
    const char *ext =
        reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    g_row_length_supported.store(
        ext && std::strstr(ext, "GL_EXT_unpack_subimage") != nullptr,
        std::memory_order_relaxed);
}

bool texture_row_length_supported() {
    return g_row_length_supported.load(std::memory_order_relaxed);
}

Texture make_texture_rgba(int width, int height, const uint8_t *rgba,
                          bool mipmapped, int stride_px) {
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
    set_unpack_row_length(stride_px);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
    clear_unpack_row_length(stride_px);
    if (mipmapped)
        glGenerateMipmap(GL_TEXTURE_2D);
    return tex;
}

void update_texture_rgba(Texture &tex, int width, int height,
                         const uint8_t *rgba, bool mipmapped, int stride_px) {
    if (tex.id && tex.width == width && tex.height == height) {
        glBindTexture(GL_TEXTURE_2D, tex.id);
        set_unpack_row_length(stride_px);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA,
                        GL_UNSIGNED_BYTE, rgba);
        clear_unpack_row_length(stride_px);
        if (mipmapped)
            glGenerateMipmap(GL_TEXTURE_2D);
        return;
    }
    tex = make_texture_rgba(width, height, rgba, mipmapped, stride_px);
}

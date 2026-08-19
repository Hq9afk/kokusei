#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <cstdint>
#include <utility>

// A dma-buf plane to import as an EGLImage: fd + layout, no pixel data.
struct DrmPlaneImport {
    int fd = -1;
    uint32_t fourcc = 0;
    uint64_t modifier = 0;
    int offset = 0;
    int pitch = 0;
    int width = 0;
    int height = 0;
};

// NV12 decoded frame backed by two dma-buf planes (Y, interleaved UV),
// imported without a CPU copy.
struct DrmFrameImport {
    DrmPlaneImport planes[2];
    int plane_count = 0;
    int width = 0;
    int height = 0;
};

// Two GL textures (Y as GL_R8, UV as GL_RG8-equivalent) bound directly to a
// VAAPI decode surface's dma-buf via EGLImage + glEGLImageTargetTexture2DOES.
// No pixel data ever touches the CPU.
struct VideoTexture {
    GLuint y_tex = 0;
    GLuint uv_tex = 0;
    EGLImageKHR y_image = EGL_NO_IMAGE_KHR;
    EGLImageKHR uv_image = EGL_NO_IMAGE_KHR;
    EGLDisplay display = EGL_NO_DISPLAY;
    int width = 0;
    int height = 0;

    VideoTexture() = default;
    VideoTexture(const VideoTexture &) = delete;
    VideoTexture &operator=(const VideoTexture &) = delete;
    VideoTexture(VideoTexture &&other) noexcept { *this = std::move(other); }
    VideoTexture &operator=(VideoTexture &&other) noexcept;
    ~VideoTexture() { reset(); }

    void reset();
};

// Detects EGL_EXT_image_dma_buf_import + GL_OES_EGL_image on the current
// context; call once with a GL context current (e.g. from Renderer::init).
void video_texture_detect_caps(EGLDisplay display);
bool video_texture_import_supported();

// Imports the frame's dma-buf planes into tex, replacing its previous
// EGLImages/textures. Returns false (tex left untouched) if the import
// fails, e.g. the driver rejects the modifier or plane format.
bool video_texture_import(VideoTexture &tex, EGLDisplay display,
                          const DrmFrameImport &frame);

#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <cstdint>
#include <utility>

struct DrmPlaneImport {
    int fd = -1;
    uint64_t modifier = 0;
    int offset = 0;
    int pitch = 0;
};

struct DrmFrameImport {
    DrmPlaneImport planes[2];
    int plane_count = 0;
    int width = 0;
    int height = 0;
};

struct VideoTexture {
    GLuint tex = 0;
    EGLImageKHR image = EGL_NO_IMAGE_KHR;
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

void video_texture_detect_caps(EGLDisplay display);
bool video_texture_import_supported();

bool video_texture_import(VideoTexture &tex, EGLDisplay display,
                          const DrmFrameImport &frame);

#include "render/video_texture.h"

#include "core/log.h"

#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>
#include <atomic>
#include <cstring>

namespace {

std::atomic<bool> g_import_supported{false};

PFNEGLCREATEIMAGEKHRPROC g_eglCreateImageKHR = nullptr;
PFNEGLDESTROYIMAGEKHRPROC g_eglDestroyImageKHR = nullptr;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC g_glEGLImageTargetTexture2DOES = nullptr;

GLuint make_plane_texture(EGLDisplay display, const DrmPlaneImport &plane,
                          EGLImageKHR &out_image) {
    EGLint attribs[] = {
        EGL_WIDTH,
        plane.width,
        EGL_HEIGHT,
        plane.height,
        EGL_LINUX_DRM_FOURCC_EXT,
        static_cast<EGLint>(plane.fourcc),
        EGL_DMA_BUF_PLANE0_FD_EXT,
        plane.fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT,
        plane.offset,
        EGL_DMA_BUF_PLANE0_PITCH_EXT,
        plane.pitch,
        EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
        static_cast<EGLint>(plane.modifier & 0xffffffff),
        EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
        static_cast<EGLint>(plane.modifier >> 32),
        EGL_NONE,
    };
    out_image = g_eglCreateImageKHR(display, EGL_NO_CONTEXT,
                                    EGL_LINUX_DMA_BUF_EXT, nullptr, attribs);
    if (out_image == EGL_NO_IMAGE_KHR)
        return 0;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D,
                                   static_cast<GLeglImageOES>(out_image));
    return tex;
}

} // namespace

void VideoTexture::reset() {
    if (y_tex)
        glDeleteTextures(1, &y_tex);
    if (uv_tex)
        glDeleteTextures(1, &uv_tex);
    y_tex = uv_tex = 0;
    if (y_image != EGL_NO_IMAGE_KHR && g_eglDestroyImageKHR)
        g_eglDestroyImageKHR(display, y_image);
    if (uv_image != EGL_NO_IMAGE_KHR && g_eglDestroyImageKHR)
        g_eglDestroyImageKHR(display, uv_image);
    y_image = uv_image = EGL_NO_IMAGE_KHR;
    display = EGL_NO_DISPLAY;
    width = height = 0;
}

VideoTexture &VideoTexture::operator=(VideoTexture &&other) noexcept {
    if (this != &other) {
        reset();
        y_tex = other.y_tex;
        uv_tex = other.uv_tex;
        y_image = other.y_image;
        uv_image = other.uv_image;
        display = other.display;
        width = other.width;
        height = other.height;
        other.y_tex = other.uv_tex = 0;
        other.y_image = other.uv_image = EGL_NO_IMAGE_KHR;
        other.display = EGL_NO_DISPLAY;
        other.width = other.height = 0;
    }
    return *this;
}

void video_texture_detect_caps(EGLDisplay display) {
    const char *egl_ext = eglQueryString(display, EGL_EXTENSIONS);
    const char *gl_ext = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    bool have_dma_buf = egl_ext && std::strstr(egl_ext, "EGL_EXT_image_dma_buf_import");
    bool have_image_base = egl_ext && std::strstr(egl_ext, "EGL_KHR_image_base");
    bool have_oes_image = gl_ext && std::strstr(gl_ext, "GL_OES_EGL_image");
    if (!have_dma_buf || !have_image_base || !have_oes_image) {
        g_import_supported.store(false, std::memory_order_relaxed);
        return;
    }
    g_eglCreateImageKHR = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
        eglGetProcAddress("eglCreateImageKHR"));
    g_eglDestroyImageKHR = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
        eglGetProcAddress("eglDestroyImageKHR"));
    g_glEGLImageTargetTexture2DOES =
        reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
            eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    bool ok = g_eglCreateImageKHR && g_eglDestroyImageKHR &&
             g_glEGLImageTargetTexture2DOES;
    if (!ok)
        klog("video_texture: dma-buf extensions advertised but proc "
             "addresses missing, disabling zero-copy import");
    g_import_supported.store(ok, std::memory_order_relaxed);
}

bool video_texture_import_supported() {
    return g_import_supported.load(std::memory_order_relaxed);
}

bool video_texture_import(VideoTexture &tex, EGLDisplay display,
                          const DrmFrameImport &frame) {
    if (!video_texture_import_supported() || frame.plane_count != 2)
        return false;

    EGLImageKHR y_image = EGL_NO_IMAGE_KHR;
    EGLImageKHR uv_image = EGL_NO_IMAGE_KHR;
    GLuint y_tex = make_plane_texture(display, frame.planes[0], y_image);
    GLuint uv_tex = 0;
    if (y_tex)
        uv_tex = make_plane_texture(display, frame.planes[1], uv_image);

    if (!y_tex || !uv_tex) {
        if (y_tex)
            glDeleteTextures(1, &y_tex);
        if (uv_tex)
            glDeleteTextures(1, &uv_tex);
        if (y_image != EGL_NO_IMAGE_KHR)
            g_eglDestroyImageKHR(display, y_image);
        if (uv_image != EGL_NO_IMAGE_KHR)
            g_eglDestroyImageKHR(display, uv_image);
        return false;
    }

    tex.reset();
    tex.y_tex = y_tex;
    tex.uv_tex = uv_tex;
    tex.y_image = y_image;
    tex.uv_image = uv_image;
    tex.display = display;
    tex.width = frame.width;
    tex.height = frame.height;
    return true;
}

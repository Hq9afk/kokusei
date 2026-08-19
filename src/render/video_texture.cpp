#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <atomic>
#include <cstring>
#include <drm_fourcc.h>

#include "core/log.h"

#include "render/video_texture.h"

namespace {

std::atomic<bool> g_import_supported{false};

PFNEGLCREATEIMAGEKHRPROC g_eglCreateImageKHR = nullptr;
PFNEGLDESTROYIMAGEKHRPROC g_eglDestroyImageKHR = nullptr;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC g_glEGLImageTargetTexture2DOES = nullptr;

} // namespace

void VideoTexture::reset() {
    if (tex)
        glDeleteTextures(1, &tex);
    tex = 0;
    if (image != EGL_NO_IMAGE_KHR && g_eglDestroyImageKHR)
        g_eglDestroyImageKHR(display, image);
    image = EGL_NO_IMAGE_KHR;
    display = EGL_NO_DISPLAY;
    width = height = 0;
}

VideoTexture &VideoTexture::operator=(VideoTexture &&other) noexcept {
    if (this != &other) {
        reset();
        tex = other.tex;
        image = other.image;
        display = other.display;
        width = other.width;
        height = other.height;
        other.tex = 0;
        other.image = EGL_NO_IMAGE_KHR;
        other.display = EGL_NO_DISPLAY;
        other.width = other.height = 0;
    }
    return *this;
}

void video_texture_detect_caps(EGLDisplay display) {
    const char *egl_ext = eglQueryString(display, EGL_EXTENSIONS);
    const char *gl_ext =
        reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    bool have_dma_buf =
        egl_ext && std::strstr(egl_ext, "EGL_EXT_image_dma_buf_import");
    bool have_image_base =
        egl_ext && std::strstr(egl_ext, "EGL_KHR_image_base");
    bool have_oes_image = gl_ext && std::strstr(gl_ext, "GL_OES_EGL_image");
    bool have_oes_external =
        gl_ext && std::strstr(gl_ext, "GL_OES_EGL_image_external");
    if (!have_dma_buf || !have_image_base || !have_oes_image ||
        !have_oes_external) {
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

    const DrmPlaneImport &y = frame.planes[0];
    const DrmPlaneImport &uv = frame.planes[1];

    EGLint attribs[27] = {
        EGL_WIDTH,
        frame.width,
        EGL_HEIGHT,
        frame.height,
        EGL_LINUX_DRM_FOURCC_EXT,
        static_cast<EGLint>(DRM_FORMAT_NV12),
        EGL_DMA_BUF_PLANE0_FD_EXT,
        y.fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT,
        y.offset,
        EGL_DMA_BUF_PLANE0_PITCH_EXT,
        y.pitch,
        EGL_DMA_BUF_PLANE1_FD_EXT,
        uv.fd,
        EGL_DMA_BUF_PLANE1_OFFSET_EXT,
        uv.offset,
        EGL_DMA_BUF_PLANE1_PITCH_EXT,
        uv.pitch,
    };
    int n = 18;
    if (y.modifier != DRM_FORMAT_MOD_INVALID) {
        attribs[n++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
        attribs[n++] = static_cast<EGLint>(y.modifier & 0xffffffff);
        attribs[n++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
        attribs[n++] = static_cast<EGLint>(y.modifier >> 32);
    }
    if (uv.modifier != DRM_FORMAT_MOD_INVALID) {
        attribs[n++] = EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT;
        attribs[n++] = static_cast<EGLint>(uv.modifier & 0xffffffff);
        attribs[n++] = EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT;
        attribs[n++] = static_cast<EGLint>(uv.modifier >> 32);
    }
    attribs[n++] = EGL_NONE;

    EGLImageKHR image = g_eglCreateImageKHR(
        display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attribs);
    if (image == EGL_NO_IMAGE_KHR) {
        klog("video_texture: combined nv12 plane import failed");
        return false;
    }

    GLuint gl_tex = 0;
    glGenTextures(1, &gl_tex);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, gl_tex);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,
                    GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,
                    GL_CLAMP_TO_EDGE);
    g_glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES,
                                   static_cast<GLeglImageOES>(image));

    tex.reset();
    tex.tex = gl_tex;
    tex.image = image;
    tex.display = display;
    tex.width = frame.width;
    tex.height = frame.height;
    return true;
}

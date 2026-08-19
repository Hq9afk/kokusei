#pragma once

#include <EGL/egl.h>
#include <chrono>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "config/osd_config.h"

#include "render/animation.h"
#include "render/renderer.h"
#include "render/scene.h"
#include "render/texture.h"

#include "service/frame_clock.h"
#include "service/output_scale.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

enum class OsdKind { Volume, Mic, Brightness };

struct OsdState {
    wl_surface *surface = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLDisplay egl_display = nullptr;
    EGLContext egl_context = nullptr;
    Renderer *renderer = nullptr;
    bool configured = false;
    OutputScale output_scale;
    FrameClock frame_clock;
    Scene scene;

    OsdKind kind = OsdKind::Volume;
    float level = 0.0f;
    bool muted = false;
    Texture icon_texture;
    Texture label_texture;
    bool visible = false;
    std::chrono::steady_clock::time_point hide_at;

    AnimationManager animations;
    float opacity = 0.0f;
    float bar_fill = 0.0f;
    float icon_color_t = 0.0f;
    std::chrono::steady_clock::time_point created_at =
        std::chrono::steady_clock::now();
};

bool osd_create_surface(OsdState &state, wl_compositor *compositor,
                        zwlr_layer_shell_v1 *layer_shell,
                        wl_output *output = nullptr);

bool osd_init_egl(OsdState &state, Renderer &renderer, EGLDisplay display,
                  EGLConfig config, EGLContext context);

void osd_request_frame(OsdState &state);

void osd_show(OsdState &state, OsdKind kind, float level, bool muted);

void osd_hide(OsdState &state);

struct BrightnessBackend {
    std::string device;
    int max = 0;
};

void brightness_init(BrightnessBackend &backend);

float brightness_get(const BrightnessBackend &backend);

int brightness_watch_init(const BrightnessBackend &backend);

bool brightness_watch_poll(int fd);

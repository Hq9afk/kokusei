#pragma once

#include "render/animation.h"
#include "render/palette.h"
#include "render/renderer.h"
#include "render/scene.h"
#include "render/texture.h"
#include "wayland/frame_clock.h"
#include "wayland/output_scale.h"
#include "notification/notification_config.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <EGL/egl.h>
#include <sdbus-c++/sdbus-c++.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

struct NotificationEntry {
    uint32_t id = 0;
    Texture app_name_texture;
    Texture summary_texture;
    Texture body_texture;
    uint8_t urgency = 1;
    int32_t timeout_ms = 5000;
    std::chrono::steady_clock::time_point expires_at;
    float height = 0.0f;
    float opacity = 0.0f;
    float slide_offset = kNotificationSlideOffset;
    bool exiting = false;
};

struct NotificationService {
    std::unique_ptr<sdbus::IConnection> bus;
    std::unique_ptr<sdbus::IObject> object;
    uint32_t next_id = 1;
    std::deque<NotificationEntry> entries;
    AnimationManager animations;
};

struct NotificationView {
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
};

float notification_detail_texture_height(const Texture &tex);

const Color &notification_detail_urgency_color(uint8_t urgency);

bool notification_view_create_surface(NotificationView &view,
                                      wl_compositor *compositor,
                                      zwlr_layer_shell_v1 *layer_shell,
                                      wl_output *output = nullptr);

bool notification_view_init_egl(NotificationView &view,
                                NotificationService &service,
                                Renderer &renderer, EGLDisplay display,
                                EGLConfig config, EGLContext context);

void notification_view_request_frame(NotificationView &view);

bool notification_sweep_expired(NotificationService &service);

void notification_push(NotificationService &service,
                       const std::string &app_name, const std::string &summary,
                       const std::string &body, int32_t expire_timeout_ms = -1,
                       uint32_t id = 0, uint8_t urgency = 1);

bool notification_init(NotificationService &service,
                       const std::function<void()> &on_repaint);

void notification_paint(NotificationView &view, NotificationService &service);

#include <GLES2/gl2.h>
#include <algorithm>
#include <filesystem>

#include "core/log.h"

#include "modules/idle_overlay.h"

#include "render/image.h"
#include "render/node.h"
#include "render/palette.h"

#include "service/layer_surface.h"

namespace {

constexpr const char *kIdleOverlayLayerNamespace = "kokusei-idle-overlay";

void idle_overlay_layer_surface_configure(void *data,
                                          zwlr_layer_surface_v1 *layer_surface,
                                          uint32_t serial, uint32_t width,
                                          uint32_t height) {
    auto *state = static_cast<IdleOverlayState *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    bool changed = state->width != static_cast<int32_t>(width) ||
                   state->height != static_cast<int32_t>(height);
    state->width = static_cast<int32_t>(width);
    state->height = static_cast<int32_t>(height);
    if (changed && state->egl_window) {
        int32_t scale = state->output_scale.scale;
        wl_egl_window_resize(state->egl_window, state->width * scale,
                             state->height * scale, 0, 0);
        if (state->frame_clock.surface)
            request_frame(state->frame_clock);
    }
    state->configured = true;
}

void idle_overlay_layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

constexpr zwlr_layer_surface_v1_listener idle_overlay_layer_surface_listener =
    {
        .configure = idle_overlay_layer_surface_configure,
        .closed = idle_overlay_layer_surface_closed,
};

void idle_overlay_bounce(IdleOverlayState &state, float dt) {
    if (dt <= 0.0f || dt > 0.5f || !state.logo_tex.id)
        return;
    float logo_w = static_cast<float>(state.logo_tex.width);
    float logo_h = static_cast<float>(state.logo_tex.height);
    state.logo_x += state.logo_vel_x * dt;
    state.logo_y += state.logo_vel_y * dt;
    float max_x = static_cast<float>(state.width) - logo_w;
    float max_y = static_cast<float>(state.height) - logo_h;
    if (state.logo_x < 0.0f) {
        state.logo_x = 0.0f;
        state.logo_vel_x = -state.logo_vel_x;
    } else if (state.logo_x > max_x) {
        state.logo_x = max_x;
        state.logo_vel_x = -state.logo_vel_x;
    }
    if (state.logo_y < 0.0f) {
        state.logo_y = 0.0f;
        state.logo_vel_y = -state.logo_vel_y;
    } else if (state.logo_y > max_y) {
        state.logo_y = max_y;
        state.logo_vel_y = -state.logo_vel_y;
    }
}

void idle_overlay_paint(IdleOverlayState &state) {
    if (state.egl_surface == EGL_NO_SURFACE)
        return;

    eglMakeCurrent(state.egl_display, state.egl_surface, state.egl_surface,
                   state.egl_context);
    state.renderer->begin_frame(state.width, state.height,
                                state.output_scale.scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    auto now = std::chrono::steady_clock::now();
    state.animations.tick(now);
    float dt = std::chrono::duration<float>(now - state.last_tick).count();
    state.last_tick = now;
    if (state.screensaver_active)
        idle_overlay_bounce(state, dt);

    if (state.ambient_opacity > 0.0f && state.draw_ambient) {
        state.scene.rebuild();
        state.draw_ambient(state.scene.root, static_cast<float>(state.width),
                           static_cast<float>(state.height));
        state.renderer->set_opacity(state.ambient_opacity);
        state.scene.draw(*state.renderer);
    }

    if (state.screensaver_opacity > 0.0f) {
        state.scene.rebuild();
        Color black{0.0f, 0.0f, 0.0f, state.screensaver_opacity};
        node_add_rect(&state.scene.root, 0, 0,
                      static_cast<float>(state.width),
                      static_cast<float>(state.height), rgba(black));
        Color white{1.0f, 1.0f, 1.0f, state.screensaver_opacity};
        if (state.logo_tex.id)
            node_add_texture(&state.scene.root, state.logo_x, state.logo_y,
                             state.logo_tex, rgba(white));
        state.renderer->set_opacity(1.0f);
        state.scene.draw(*state.renderer);
    }

    eglSwapBuffers(state.egl_display, state.egl_surface);

    if (state.animations.hasActive() || state.screensaver_active)
        request_frame(state.frame_clock);
}

} // namespace

bool idle_overlay_create_surface(IdleOverlayState &state,
                                 wl_compositor *compositor,
                                 zwlr_layer_shell_v1 *layer_shell,
                                 wl_output *output) {
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        .name_space = kIdleOverlayLayerNamespace,
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
        .empty_input_region = true,
    };
    state.layer_surface = layer_surface_create(
        state.surface, compositor, layer_shell, cfg,
        &idle_overlay_layer_surface_listener, &state, output);
    if (!state.layer_surface)
        return false;
    state.output_scale.on_change = [&state](int32_t scale) {
        if (state.egl_window)
            wl_egl_window_resize(state.egl_window, state.width * scale,
                                 state.height * scale, 0, 0);
        if (state.frame_clock.surface)
            request_frame(state.frame_clock);
    };
    output_scale_watch(state.output_scale, state.surface);
    wl_surface_commit(state.surface);
    return true;
}

bool idle_overlay_init_egl(IdleOverlayState &state, Renderer &renderer,
                           EGLDisplay display, EGLConfig config,
                           EGLContext context) {
    state.egl_display = display;
    state.egl_context = context;
    state.renderer = &renderer;
    int32_t scale = state.output_scale.scale;
    state.egl_window = wl_egl_window_create(
        state.surface, state.width * scale, state.height * scale);
    state.egl_surface = eglCreateWindowSurface(
        display, config,
        reinterpret_cast<EGLNativeWindowType>(state.egl_window), nullptr);
    if (state.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!eglMakeCurrent(display, state.egl_surface, state.egl_surface, context))
        return false;
    state.frame_clock.surface = state.surface;
    state.frame_clock.draw = [&state] { idle_overlay_paint(state); };

    const char *logo_candidates[] = {KOKUSEI_STARWARD_LOGO, "assets/logo.png"};
    std::string logo_path = logo_candidates[1];
    for (const char *candidate : logo_candidates) {
        if (std::filesystem::exists(candidate)) {
            logo_path = candidate;
            break;
        }
    }
    state.logo_tex = load_image_texture(logo_path);
    return true;
}

void idle_overlay_request_frame(IdleOverlayState &state) {
    if (state.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(state.frame_clock);
}

void idle_overlay_set_active(IdleOverlayState &state, bool ambient_active,
                             bool screensaver_active) {
    bool changed = false;
    bool ambient_shown = ambient_active && !screensaver_active;
    if (ambient_shown != state.ambient_active) {
        state.ambient_active = ambient_shown;
        state.animations.animate(
            state.ambient_opacity, ambient_shown ? 1.0f : 0.0f,
            kIdleOverlayFadeMs, Easing::EaseOutCubic,
            [&state](float v) { state.ambient_opacity = v; }, {},
            kIdleAmbientFadeOwner);
        changed = true;
    }
    if (screensaver_active != state.screensaver_active) {
        state.screensaver_active = screensaver_active;
        state.animations.animate(
            state.screensaver_opacity, screensaver_active ? 1.0f : 0.0f,
            kIdleOverlayFadeMs, Easing::EaseOutCubic,
            [&state](float v) { state.screensaver_opacity = v; }, {},
            kIdleScreensaverFadeOwner);
        changed = true;
    }
    // Redraws otherwise keep themselves alive (animations.hasActive() /
    // screensaver_active re-requesting each frame from idle_overlay_paint) -
    // only kick the loop off here on an actual state transition, not once a
    // second forever.
    if (changed)
        idle_overlay_request_frame(state);
}

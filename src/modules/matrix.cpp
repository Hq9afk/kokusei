#include "modules/matrix.h"

#include "app/wayland_state.h"
#include "render/node.h"
#include "render/overlay_panel.h"

#include <GLES2/gl2.h>

void matrix_request_frame(MatrixState &state) {
    toplevel_window_request_frame(state.base);
}

void matrix_toggle(MatrixState &state, WaylandState &app) {
    bool opening = !state.base.open;
    if (opening) {
        if (state.base.egl_surface == EGL_NO_SURFACE) {
            if (!toplevel_window_create_surface(
                    state.base, app.compositor, app.wm_base, "Matrix",
                    "kokusei-matrix", kMatrixDefaultWindowWidth,
                    kMatrixDefaultWindowHeight))
                return;
            while (!state.base.configured)
                wl_display_dispatch(app.display);
            if (!toplevel_window_init_egl(state.base, app.egl_display,
                                          app.egl_config, app.egl_context)) {
                toplevel_window_destroy_surface(state.base);
                return;
            }
            state.renderer = &app.renderer;
            state.base.frame_clock.draw = [&state] { matrix_paint(state); };
            state.base.on_close_request = [&state, &app] {
                matrix_toggle(state, app);
            };
        }
        state.base.open = true;
        state.grid.rebuild(state.base.width, state.base.height);
        state.grid_width = state.base.width;
        state.grid_height = state.base.height;
        state.last_tick = std::chrono::steady_clock::now();
    }

    if (opening) {
        state.base.animations.animate(
            state.base.opacity, 1.0f, kOverlayFadeMs, Easing::EaseOutCubic,
            [&state](float v) { state.base.opacity = v; }, {},
            kOverlayFadeOwner);
        toplevel_window_request_frame(state.base);
    } else {
        state.base.animations.cancelForOwner(kOverlayFadeOwner);
        state.base.open = false;
        toplevel_window_destroy_surface(state.base);
    }
}

void matrix_handle_key_event(MatrixState &state, WaylandState &app,
                             const KeyEvent &event) {
    if (event.kind == KeyKind::Escape)
        matrix_toggle(state, app);
}

std::vector<IpcHandler> matrix_ipc_handlers(WaylandState &state) {
    return {
        {"matrix", [&state] { matrix_toggle(state.matrix, state); },
         "toggle the matrix rain overlay"},
    };
}

void matrix_paint(MatrixState &state) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    auto now = std::chrono::steady_clock::now();
    state.base.animations.tick(now);

    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    eglMakeCurrent(state.base.egl_display, state.base.egl_surface,
                   state.base.egl_surface, state.base.egl_context);
    state.renderer->begin_frame(state.base.width, state.base.height,
                                state.base.output_scale.scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.scene.rebuild();

    float win_w = static_cast<float>(state.base.width);
    float win_h = static_cast<float>(state.base.height);

    node_add_rect(&state.scene.root, 0.0f, 0.0f, win_w, win_h,
                  rgba(kMatrixWindowBackground));

    if (state.base.width != state.grid_width ||
        state.base.height != state.grid_height) {
        state.grid.rebuild(state.base.width, state.base.height);
        state.grid_width = state.base.width;
        state.grid_height = state.base.height;
        state.last_tick = now;
    }

    float elapsed_ms =
        std::chrono::duration<float, std::milli>(now - state.last_tick).count();
    if (elapsed_ms >= kMatrixFallIntervalMs) {
        state.grid.tick();
        state.last_tick = now;
    }

    if (state.grid.ready()) {
        Node *tex = state.scene.root.claim_child();
        tex->kind = NodeKind::Texture;
        tex->x = 0.0f;
        tex->y = 0.0f;
        tex->w = win_w;
        tex->h = win_h;
        tex->tex = &state.grid.texture();
    }

    state.renderer->set_opacity(state.base.opacity);
    state.scene.draw(*state.renderer);
    state.renderer->set_opacity(1.0f);
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);

    if (state.base.open || state.base.animations.hasActive())
        toplevel_window_request_frame(state.base);
}

#include "modules/visualizer.h"

#include "app/wayland_state.h"
#include "render/color_ops.h"
#include "render/node.h"
#include "render/overlay_panel.h"

#include <GLES2/gl2.h>

#include <algorithm>
#include <cmath>

namespace {

void retarget_spectrum(VisualizerState &state, WaylandState &app) {
    uint32_t sink_id = app.pipewire.default_sink_id;
    auto it = app.pipewire.nodes.find(sink_id);
    std::string sink_name =
        it != app.pipewire.nodes.end() ? it->second.name : "";
    state.spectrum.setTargetNode(sink_id, sink_name);
}

} // namespace

void visualizer_request_frame(VisualizerState &state) {
    toplevel_window_request_frame(state.base);
}

void visualizer_toggle(VisualizerState &state, WaylandState &app) {
    bool opening = !state.base.open;
    if (opening) {
        if (state.base.egl_surface == EGL_NO_SURFACE) {
            if (!toplevel_window_create_surface(
                    state.base, app.compositor, app.wm_base, "Visualizer",
                    "kokusei-visualizer", kVisualizerDefaultWindowWidth,
                    kVisualizerDefaultWindowHeight))
                return;
            while (!state.base.configured)
                wl_display_dispatch(app.display);
            if (!toplevel_window_init_egl(state.base, app.egl_display,
                                          app.egl_config, app.egl_context)) {
                toplevel_window_destroy_surface(state.base);
                return;
            }
            state.renderer = &app.renderer;
            state.base.frame_clock.draw = [&state] { visualizer_paint(state); };
            if (!state.spectrum_ready)
                state.spectrum_ready = state.spectrum.init();
            state.base.on_close_request = [&state, &app] {
                visualizer_toggle(state, app);
            };
        }
        state.base.open = true;
        retarget_spectrum(state, app);
        state.last_frame = std::chrono::steady_clock::now();
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

void visualizer_handle_key_event(VisualizerState &state, WaylandState &app,
                                 const KeyEvent &event) {
    if (event.kind == KeyKind::Escape)
        visualizer_toggle(state, app);
}

std::vector<IpcHandler> visualizer_ipc_handlers(WaylandState &state) {
    return {
        {"visualizer", [&state] { visualizer_toggle(state.visualizer, state); },
         "toggle the audio visualizer overlay"},
    };
}

void visualizer_paint(VisualizerState &state) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    auto now = std::chrono::steady_clock::now();
    state.base.animations.tick(now);
    // Closing's on_complete (above) can destroy the surface synchronously
    // from inside this tick(), bail before touching now-dangling EGL handles.
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
                  rgba(kVisualizerWindowBackground));

    state.spectrum.processFrame();
    const std::vector<float> &target = state.spectrum.values();

    float elapsed_ms =
        std::chrono::duration<float, std::milli>(now - state.last_frame)
            .count();
    state.last_frame = now;
    // Exponential smoothing toward the latest FFT frame; a manual lerp
    // is simpler than driving kVisualizerBarCount AnimationManager
    // tweens for a target that moves every frame.
    float k = 1.0f - std::exp(-std::max(0.0f, elapsed_ms) /
                              kVisualizerBarsAnimDurationMs);

    float total_w = kVisualizerBarCount * kVisualizerBarWidth +
                    (kVisualizerBarCount - 1) * kVisualizerBarSpacing;
    float start_x = (win_w - total_w) / 2.0f;
    float baseline_y = win_h;
    Color bar_color = with_alpha(palette::accent, kVisualizerBarOpacity);

    for (int i = 0; i < kVisualizerBarCount; ++i) {
        float &v = state.display_values[static_cast<size_t>(i)];
        v += (target[static_cast<size_t>(i)] - v) * k;
        float bar_h = std::max(1.0f, v * kVisualizerBarHeightRatio * win_h);
        float bar_x =
            start_x + i * (kVisualizerBarWidth + kVisualizerBarSpacing);
        node_add_rrect(&state.scene.root, bar_x, baseline_y - bar_h,
                       kVisualizerBarWidth, bar_h, kVisualizerBarRadius, 0.0f,
                       rgba(bar_color), kNodeTransparent);
    }

    state.renderer->set_opacity(state.base.opacity);
    state.scene.draw(*state.renderer);
    state.renderer->set_opacity(1.0f);
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);

    if (state.base.open || state.base.animations.hasActive())
        toplevel_window_request_frame(state.base);
}

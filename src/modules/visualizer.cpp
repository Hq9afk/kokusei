#include "modules/visualizer.h"

#include "app/wayland_state.h"
#include "core/log.h"
#include "render/color_ops.h"
#include "render/node.h"
#include "render/overlay_panel.h"

#include <GLES2/gl2.h>

#include <algorithm>
#include <cmath>

namespace {

// Storage duration must outlive the Scene::draw() call that reads this
// pointer through Node::fill (Renderer::draw_rounded_rect dereferences it
// well after the node is built); a per-call local here would dangle by the
// time the scene is actually drawn.
static constexpr Color kBarColor =
    with_alpha(palette::accent, kVisualizerBarOpacity);

void draw_bars_frame(VisualizerState &state, const VisualizerFrame &f) {
    if (!state.thread_renderer_ready)
        state.thread_renderer_ready = state.thread_renderer.init();
    if (!state.thread_renderer_ready)
        return;

    state.thread_renderer.begin_frame(f.width, f.height, f.scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.scene.rebuild();

    float win_w = static_cast<float>(f.width);
    float win_h = static_cast<float>(f.height);

    node_add_rect(&state.scene.root, 0.0f, 0.0f, win_w, win_h,
                  rgba(kVisualizerWindowBackground));

    float k = 1.0f - std::exp(-std::max(0.0f, f.elapsed_ms) /
                              kVisualizerBarsAnimDurationMs);

    float total_w = kVisualizerBarCount * kVisualizerBarWidth +
                    (kVisualizerBarCount - 1) * kVisualizerBarSpacing;
    float start_x = (win_w - total_w) / 2.0f;
    float baseline_y = win_h;

    for (int i = 0; i < kVisualizerBarCount; ++i) {
        float &v = state.display_values[static_cast<size_t>(i)];
        v += (f.spectrum[static_cast<size_t>(i)] - v) * k;
        float bar_h = std::max(1.0f, v * kVisualizerBarHeightRatio * win_h);
        float bar_x =
            start_x + i * (kVisualizerBarWidth + kVisualizerBarSpacing);
        node_add_rrect(&state.scene.root, bar_x, baseline_y - bar_h,
                       kVisualizerBarWidth, bar_h, kVisualizerBarRadius, 0.0f,
                       rgba(kBarColor), kNodeTransparent);
    }

    state.thread_renderer.set_opacity(f.opacity);
    state.scene.draw(state.thread_renderer);
    state.thread_renderer.set_opacity(1.0f);
}

// Runs entirely on state.render_thread: state.render_context is current on
// state.base.egl_surface for the whole lifetime of this call. Owns
// state.base.egl_surface exclusively while running; the main thread must
// not also make it current on the shared main context (see
// visualizer_render_thread_submit / visualizer_render_thread_destroy).
void render_thread_main(VisualizerState *state) {
    if (!eglMakeCurrent(state->base.egl_display, state->base.egl_surface,
                        state->base.egl_surface, state->render_context)) {
        klog("visualizer: render thread eglMakeCurrent failed, eglGetError=0x%x",
             eglGetError());
        return;
    }
    glEnable(GL_BLEND);

    VisualizerRenderThreadState &ts = *state->thread_state;
    for (;;) {
        VisualizerFrame frame;
        {
            std::unique_lock<std::mutex> lock(ts.mutex);
            ts.cv.wait(lock, [&] { return ts.have_frame || ts.shutdown; });
            if (ts.shutdown)
                break;
            frame = std::move(ts.pending);
            ts.have_frame = false;
        }

        if (frame.ncs_shape) {
            ncs_visualizer_render(state->ncs, frame.width, frame.height,
                                  frame.scale, frame.time_seconds,
                                  frame.opacity, frame.spectrum_l,
                                  frame.spectrum_r);
        } else {
            draw_bars_frame(*state, frame);
        }
        eglSwapBuffers(state->base.egl_display, state->base.egl_surface);
    }

    ncs_visualizer_destroy_gl(state->ncs);
    eglMakeCurrent(state->base.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
}

// Fast, lock-only handoff: copies this frame's inputs and returns
// immediately. No GL is touched on the calling (main poll loop) thread.
// First call lazily creates a share-context EGLContext (sharing GL object
// namespace with the main context) and starts the dedicated render thread.
void visualizer_render_thread_submit(VisualizerState &state,
                                     EGLConfig egl_config, bool ncs_shape,
                                     float time_seconds, float elapsed_ms) {
    if (!state.thread_state) {
        static const EGLint kContextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                                 EGL_NONE};
        state.render_context =
            eglCreateContext(state.base.egl_display, egl_config,
                             state.base.egl_context, kContextAttribs);
        if (state.render_context == EGL_NO_CONTEXT)
            return;
        state.thread_state = std::make_unique<VisualizerRenderThreadState>();
        state.render_thread = std::thread(render_thread_main, &state);
    }

    VisualizerRenderThreadState &ts = *state.thread_state;
    std::lock_guard<std::mutex> lock(ts.mutex);
    ts.pending.ncs_shape = ncs_shape;
    ts.pending.width = state.base.width;
    ts.pending.height = state.base.height;
    ts.pending.scale = state.base.output_scale.scale;
    ts.pending.time_seconds = time_seconds;
    ts.pending.opacity = state.base.opacity;
    ts.pending.elapsed_ms = elapsed_ms;
    if (ncs_shape) {
        ts.pending.spectrum_l = state.spectrum.valuesLeft();
        ts.pending.spectrum_r = state.spectrum.valuesRight();
    } else {
        ts.pending.spectrum = state.spectrum.values();
    }
    ts.have_frame = true;
    ts.cv.notify_one();
}

// Stops render_thread (signals shutdown, joins), then destroys the ncs GL
// objects and the share-context EGLContext. Must be called from the
// poll-loop thread with the visualizer window still holding its surface
// (i.e. before toplevel_window_destroy_surface).
void visualizer_render_thread_destroy(VisualizerState &state) {
    if (state.thread_state) {
        {
            std::lock_guard<std::mutex> lock(state.thread_state->mutex);
            state.thread_state->shutdown = true;
        }
        state.thread_state->cv.notify_one();
    }
    if (state.render_thread.joinable())
        state.render_thread.join();
    if (state.render_context != EGL_NO_CONTEXT) {
        eglDestroyContext(state.base.egl_display, state.render_context);
        state.render_context = EGL_NO_CONTEXT;
    }
    state.thread_state.reset();
    state.thread_renderer_ready = false;
}

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
            state.base.frame_clock.draw = [&state, &app] {
                visualizer_paint(state, app.cfg, app.egl_config);
            };
            if (!state.spectrum_ready) {
                state.spectrum_ready = state.spectrum.init();
                state.start_time = std::chrono::steady_clock::now();
            }
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
        visualizer_render_thread_destroy(state);
        toplevel_window_destroy_surface(state.base);
    }
}

void visualizer_handle_key_event(VisualizerState &state, WaylandState &app,
                                 const KeyEvent &event) {
    if (event.kind == KeyKind::Escape)
        visualizer_toggle(state, app);
}

std::vector<IpcHandler> visualizer_ipc_handlers(VisualizerState &visualizer,
                                                WaylandState &state) {
    return {
        {"visualizer",
         [&visualizer, &state] { visualizer_toggle(visualizer, state); },
         "toggle the audio visualizer overlay"},
    };
}

void visualizer_paint(VisualizerState &state, const Config &cfg,
                      EGLConfig egl_config) {
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    auto now = std::chrono::steady_clock::now();
    state.base.animations.tick(now);

    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;

    state.spectrum.processFrame();

    bool ncs_shape = cfg.visualizer_shape == "ncs";
    float time_seconds =
        std::chrono::duration<float>(now - state.start_time).count();
    float elapsed_ms =
        std::chrono::duration<float, std::milli>(now - state.last_frame)
            .count();

    // Both shapes render on state.render_thread now (see
    // visualizer_render_thread_submit); this call only hands off this
    // frame's inputs and returns, it never touches GL itself.
    visualizer_render_thread_submit(state, egl_config, ncs_shape, time_seconds,
                                    elapsed_ms);

    state.last_frame = now;

    if (state.base.open || state.base.animations.hasActive())
        toplevel_window_request_frame(state.base);
}

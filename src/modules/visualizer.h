#pragma once

#include "app/config.h"
#include "app/ipc.h"
#include "render/toplevel_window.h"
#include "service/audio_spectrum.h"
#include "service/keyboard.h"
#include "visualizer/bar_visualizer.h"
#include "visualizer/sphere_visualizer.h"

#include <EGL/egl.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct WaylandState;

// One frame's worth of draw inputs, handed off from the poll-loop thread to
// the visualizer's dedicated render thread. Copied, not referenced: the
// poll loop's spectrum vectors are only valid for the duration of its call.
struct VisualizerFrame {
    bool sphere_shape = false;
    int width = 0;
    int height = 0;
    int32_t scale = 1;
    float time_seconds = 0.0f;
    float opacity = 1.0f;
    float elapsed_ms = 0.0f;
    std::vector<float> spectrum;
    std::vector<float> spectrum_l;
    std::vector<float> spectrum_r;
};

struct VisualizerRenderThreadState {
    std::mutex mutex;
    std::condition_variable cv;
    VisualizerFrame pending;
    bool have_frame = false;
    bool shutdown = false;
};

// VisualizerState owns one dedicated render thread covering both shapes
// (bar and sphere alike), matching noctalia's model of a single thread doing
// all GL/scene work for a visual, rather than kokusei's previous
// sphere-only-threaded special case. state.bar and state.sphere are touched
// exclusively by render_thread once it is running; the poll-loop thread only
// ever writes into thread_state's pending frame under its mutex.
struct VisualizerState {
    ToplevelWindowBase base;
    AudioSpectrum spectrum;
    BarVisualizerState bar;
    SphereVisualizerState sphere;
    std::chrono::steady_clock::time_point last_frame;
    std::chrono::steady_clock::time_point start_time;
    bool spectrum_ready = false;

    EGLContext render_context = EGL_NO_CONTEXT;
    std::thread render_thread;
    std::unique_ptr<VisualizerRenderThreadState> thread_state;
};

void visualizer_request_frame(VisualizerState &state);

void visualizer_toggle(VisualizerState &state, WaylandState &app);

void visualizer_handle_key_event(VisualizerState &state, WaylandState &app,
                                 const KeyEvent &event);

std::vector<IpcHandler> visualizer_ipc_handlers(VisualizerState &visualizer,
                                                WaylandState &state);

void visualizer_paint(VisualizerState &state, const Config &cfg,
                      EGLConfig egl_config);

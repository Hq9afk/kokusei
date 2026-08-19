#pragma once

#include <EGL/egl.h>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "app/config.h"
#include "app/ipc.h"

#include "render/toplevel_window.h"

#include "service/audio_spectrum.h"
#include "service/keyboard.h"

#include "visualizer/bar_visualizer.h"
#include "visualizer/sphere_visualizer.h"

struct WaylandState;

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

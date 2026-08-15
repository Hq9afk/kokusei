#pragma once

#include "app/ipc.h"
#include "config/visualizer_config.h"
#include "render/renderer.h"
#include "render/scene.h"
#include "render/toplevel_window.h"
#include "service/audio_spectrum.h"
#include "service/keyboard.h"

#include <chrono>
#include <vector>

struct WaylandState;

struct VisualizerState {
    ToplevelWindowBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    AudioSpectrum spectrum;
    std::vector<float> display_values = std::vector<float>(kVisualizerBarCount, 0.0f);
    std::chrono::steady_clock::time_point last_frame;
    bool spectrum_ready = false;
};

void visualizer_request_frame(VisualizerState &state);

void visualizer_toggle(VisualizerState &state, WaylandState &app);

void visualizer_handle_key_event(VisualizerState &state, WaylandState &app,
                                 const KeyEvent &event);

std::vector<IpcHandler> visualizer_ipc_handlers(WaylandState &state);

void visualizer_paint(VisualizerState &state);

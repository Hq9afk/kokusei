#pragma once

#include <cstdint>
#include <vector>

#include "render/renderer.h"
#include "render/scene.h"

struct BarVisualizerState {
    Renderer renderer;
    Scene scene;
    std::vector<float> display_values;
    bool ready = false;
};

int bar_visualizer_compute_bar_count(int width);

void bar_visualizer_render(BarVisualizerState &state, int width, int height,
                           int32_t scale, float opacity, float elapsed_ms,
                           const std::vector<float> &spectrum);

void bar_visualizer_destroy_gl(BarVisualizerState &state);

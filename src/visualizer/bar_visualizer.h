#pragma once

#include <cstdint>
#include <vector>

#include "config/visualizer_config.h"

#include "render/renderer.h"
#include "render/scene.h"

struct BarVisualizerState {
    Renderer renderer;
    Scene scene;
    std::vector<float> display_values =
        std::vector<float>(kVisualizerBarCount, 0.0f);
    bool ready = false;
};

void bar_visualizer_render(BarVisualizerState &state, int width, int height,
                           int32_t scale, float opacity, float elapsed_ms,
                           const std::vector<float> &spectrum);

void bar_visualizer_destroy_gl(BarVisualizerState &state);

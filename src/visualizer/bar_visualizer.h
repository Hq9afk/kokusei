#pragma once

#include "config/visualizer_config.h"
#include "render/renderer.h"
#include "render/scene.h"

#include <cstdint>
#include <vector>

// All GL objects below are created, used, and destroyed exclusively on the
// visualizer's dedicated render thread (see modules/visualizer.h); this
// struct owns no thread or EGL context of its own, only GL resource handles
// living in that thread's shared-namespace context.
struct BarVisualizerState {
    Renderer renderer;
    Scene scene;
    std::vector<float> display_values =
        std::vector<float>(kVisualizerBarCount, 0.0f);
    bool ready = false;
};

// Renders the bar shape into state.scene via state.renderer, against
// whichever EGL surface/context the caller (the visualizer's dedicated
// render thread) already made current. Lazily initializes state.renderer on
// first call.
void bar_visualizer_render(BarVisualizerState &state, int width, int height,
                           int32_t scale, float opacity, float elapsed_ms,
                           const std::vector<float> &spectrum);

// Deletes state.renderer's GL objects and resets state to defaults. Must be
// called from the thread whose context created them, with that context
// still current.
void bar_visualizer_destroy_gl(BarVisualizerState &state);

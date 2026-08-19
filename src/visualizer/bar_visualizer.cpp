#include "visualizer/bar_visualizer.h"

#include "render/color_ops.h"
#include "render/node.h"

#include <algorithm>
#include <cmath>

namespace {

// Storage duration must outlive the Scene::draw() call that reads this
// pointer through Node::fill (Renderer::draw_rounded_rect dereferences it
// well after the node is built); a per-call local here would dangle by the
// time the scene is actually drawn.
static constexpr Color kBarColor =
    with_alpha(palette::accent, kVisualizerBarOpacity);

} // namespace

void bar_visualizer_render(BarVisualizerState &state, int width, int height,
                           int32_t scale, float opacity, float elapsed_ms,
                           const std::vector<float> &spectrum) {
    if (!state.ready)
        state.ready = state.renderer.init();
    if (!state.ready)
        return;

    state.renderer.begin_frame(width, height, scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.scene.rebuild();

    float win_w = static_cast<float>(width);
    float win_h = static_cast<float>(height);

    node_add_rect(&state.scene.root, 0.0f, 0.0f, win_w, win_h,
                  rgba(kVisualizerWindowBackground));

    float k = 1.0f - std::exp(-std::max(0.0f, elapsed_ms) /
                              kVisualizerBarsAnimDurationMs);

    float total_w = kVisualizerBarCount * kVisualizerBarWidth +
                    (kVisualizerBarCount - 1) * kVisualizerBarSpacing;
    float start_x = (win_w - total_w) / 2.0f;
    float baseline_y = win_h;

    for (int i = 0; i < kVisualizerBarCount; ++i) {
        float &v = state.display_values[static_cast<size_t>(i)];
        v += (spectrum[static_cast<size_t>(i)] - v) * k;
        float bar_h = std::max(1.0f, v * kVisualizerBarHeightRatio * win_h);
        float bar_x =
            start_x + i * (kVisualizerBarWidth + kVisualizerBarSpacing);
        node_add_rrect(&state.scene.root, bar_x, baseline_y - bar_h,
                       kVisualizerBarWidth, bar_h, kVisualizerBarRadius, 0.0f,
                       rgba(kBarColor), kNodeTransparent);
    }

    state.renderer.set_opacity(opacity);
    state.scene.draw(state.renderer);
    state.renderer.set_opacity(1.0f);
}

void bar_visualizer_destroy_gl(BarVisualizerState &state) {
    state.renderer.destroy();
    state = BarVisualizerState{};
}

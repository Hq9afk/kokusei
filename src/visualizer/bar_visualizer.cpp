#include <algorithm>
#include <cmath>

#include "config/visualizer_config.h"

#include "render/color_ops.h"
#include "render/node.h"

#include "visualizer/bar_visualizer.h"

namespace {

static constexpr Color kBarColor =
    with_alpha(palette::accent, kVisualizerBarOpacity);

} // namespace

int bar_visualizer_compute_bar_count(int width) {
    return std::max(1, static_cast<int>(
                           (static_cast<float>(width) - kVisualizerBarSpacing) /
                           (kVisualizerBarWidth + kVisualizerBarSpacing)));
}

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

    state.display_values.resize(spectrum.size(), 0.0f);
    int bar_count = static_cast<int>(spectrum.size());

    float total_w = bar_count * kVisualizerBarWidth +
                    (bar_count - 1) * kVisualizerBarSpacing;
    float start_x = (win_w - total_w) / 2.0f;
    float baseline_y = win_h;

    for (int i = 0; i < bar_count; ++i) {
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

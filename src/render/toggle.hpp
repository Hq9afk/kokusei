#pragma once

#include "animation/animation.hpp"
#include "node.hpp"
#include "palette.hpp"

namespace toggle_detail {
constexpr float kToggleWidth = 40.0f;
constexpr float kToggleHeight = 22.0f;
constexpr float kToggleKnobPad = 2.0f;
constexpr float kToggleAnimMs = 120.0f;
} // namespace toggle_detail

struct ToggleState {
    bool on = false;
    float knob_t = 0.0f;
};

inline void toggle_set(ToggleState &t, AnimationManager &animations, bool on,
                       uint64_t owner) {
    if (t.on == on)
        return;
    t.on = on;
    animations.animate(
        t.knob_t, on ? 1.0f : 0.0f, toggle_detail::kToggleAnimMs,
        Easing::EaseOutCubic, [&t](float v) { t.knob_t = v; }, {}, owner);
}

inline void toggle_draw(Node *parent, float x, float y, const ToggleState &t) {
    using namespace toggle_detail;
    const Color &track = t.on ? palette::accent : palette::text_alpha10;
    node_add_rrect(parent, x, y, kToggleWidth, kToggleHeight,
                   kToggleHeight / 2.0f, 0.0f, rgba(track), rgba(track));
    float knob_size = kToggleHeight - kToggleKnobPad * 2.0f;
    float knob_x =
        x + kToggleKnobPad + (kToggleWidth - kToggleHeight) * t.knob_t;
    float knob_y = y + kToggleKnobPad;
    node_add_rrect(parent, knob_x, knob_y, knob_size, knob_size,
                   knob_size / 2.0f, 0.0f, rgba(palette::text),
                   rgba(palette::text));
}

inline bool toggle_hit_test(float x, float y, double px, double py) {
    return px >= x && px < x + toggle_detail::kToggleWidth && py >= y &&
           py < y + toggle_detail::kToggleHeight;
}

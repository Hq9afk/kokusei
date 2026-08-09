#pragma once

#include "../../render/icon.hpp"
#include "../../render/icons.hpp"
#include "../../system/pipewire.hpp"
#include "../panels/volume_panel/volume_panel_state.hpp"
#include "widget_capsule.hpp"

#include <chrono>
#include <cmath>
#include <string>

namespace bar_detail {

inline const char *volume_icon_glyph(const PipewireState &pw) {
    bool muted = false;
    float level = pipewire_sink_level(pw, muted);
    return volume_threshold_icon(muted, level);
}

inline std::string volume_label(const PipewireState &pw) {
    bool muted = false;
    float level = pipewire_sink_level(pw, muted);
    if (muted)
        return "muted";
    return std::to_string(static_cast<int>(std::lround(level * 100))) + "%";
}

inline Pill volume_pill(WaylandState &state) {
    const char *glyph = volume_icon_glyph(state.pipewire);
    if (glyph != state.volume_icon_glyph_cached) {
        state.volume_icon_texture = make_icon_texture(glyph);
        state.volume_icon_glyph_cached = glyph;
    }
    return Pill{PillId::Volume, &state.volume_icon_texture,
                volume_label(state.pipewire), nullptr, [&state] {
                    close_other_overlays(state, PillId::Volume);
                    // See bluetooth_widget.hpp's on_click: a click can land
                    // before the pill's own hover-expand tween has settled -
                    // snap it and force a repaint first so pill_center_x below
                    // reads the final expanded center, not a stale pre-snap
                    // one.
                    if (!state.volume_panel.base.open) {
                        update_pill_expand(state.capsule, state.animations,
                                           PillId::Volume, true, true);
                        bar_paint(state);
                    }
                    volume_panel_toggle(
                        state.volume_panel,
                        pill_center_x(state.capsule, PillId::Volume));
                }};
}

// `angleDelta.y > 0` in keqing-shell's VolumeWidget.qml means "scroll up" =
// louder; wl_pointer's vertical-scroll axis value carries the opposite
// sign (positive = away from the user), so a negative `dy` here is "up".
inline void volume_pill_handle_wheel(WaylandState &state, double dy) {
    bool sink_muted = false;
    float level = pipewire_sink_level(state.pipewire, sink_muted);
    float step = dy < 0 ? 0.05f : -0.05f;
    float next = std::clamp(level + step, 0.0f, 1.5f);
    if (state.pipewire.default_sink_id != 0)
        pipewire_set_node_volume(state.pipewire, state.pipewire.default_sink_id,
                                 next);
}

// Auto-expands the pill's label for `volumePeekMs` after the sink's
// volume/mute changes externally (media keys, pavucontrol), mirroring
// VolumeWidget.qml's Connections+Timer pair. Suppressed for the first
// `volumePeekReadyDelayMs` after startup so the initial state read doesn't
// itself trigger a peek.
inline void volume_pill_peek_tick(WaylandState &state) {
    auto now = std::chrono::steady_clock::now();
    if (!state.volume_peek_ready) {
        if (now - state.volume_peek_started_at >= kVolumePeekReadyDelayMs)
            state.volume_peek_ready = true;
        else
            return;
    }

    bool muted = false;
    float level = pipewire_sink_level(state.pipewire, muted);
    bool changed = state.volume_peek_last_level < 0.0f ||
                   std::abs(level - state.volume_peek_last_level) > 0.001f ||
                   muted != state.volume_peek_last_muted;
    state.volume_peek_last_level = level;
    state.volume_peek_last_muted = muted;
    if (!changed)
        return;

    state.volume_peek_active = true;
    state.volume_peek_deadline = now + kVolumePeekMs;
}

inline bool volume_pill_peek_expire(WaylandState &state) {
    if (!state.volume_peek_active)
        return false;
    if (std::chrono::steady_clock::now() < state.volume_peek_deadline)
        return false;
    state.volume_peek_active = false;
    return true;
}

} // namespace bar_detail

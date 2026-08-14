#include "bar/widget/volume_widget.h"
#include "bar/bar.h"
#include "bar/panel/volume_panel.h"

#include "render/icon.h"
#include "render/icons.h"
#include "service/pipewire.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

namespace {

const char *volume_icon_glyph(const PipewireState &pw) {
    bool muted = false;
    float level = pipewire_sink_level(pw, muted);
    return volume_threshold_icon(muted, level);
}

std::string volume_label(const PipewireState &pw) {
    bool muted = false;
    float level = pipewire_sink_level(pw, muted);
    if (muted)
        return "muted";
    return std::to_string(static_cast<int>(std::lround(level * 100))) + "%";
}

} // namespace

namespace bar_detail {

Pill volume_pill(MonitorOutput &mon) {
    const char *glyph = volume_icon_glyph(mon.app->pipewire);
    if (glyph != mon.volume_icon_glyph_cached) {
        mon.volume_icon_texture = make_icon_texture(glyph);
        mon.volume_icon_glyph_cached = glyph;
    }
    return Pill{PillId::Volume, &mon.volume_icon_texture,
                volume_label(mon.app->pipewire), nullptr, [&mon] {
                    close_other_overlays(mon, PillId::Volume);
                    if (!mon.volume_panel.base.open) {
                        update_pill_expand(mon.capsule, mon.animations,
                                           PillId::Volume, true, true);
                        bar_paint(mon);
                    }
                    volume_panel_toggle(
                        mon.volume_panel,
                        pill_center_x(mon.capsule, PillId::Volume));
                }};
}

void volume_pill_handle_wheel(MonitorOutput &mon, double dy) {
    PipewireState &pw = mon.app->pipewire;
    bool sink_muted = false;
    float level = pipewire_sink_level(pw, sink_muted);
    float step = dy < 0 ? 0.05f : -0.05f;
    float next = std::clamp(level + step, 0.0f, 1.5f);
    if (pw.default_sink_id != 0)
        pipewire_set_node_volume(pw, pw.default_sink_id, next);
}

void volume_pill_peek_tick(MonitorOutput &mon) {
    auto now = std::chrono::steady_clock::now();
    if (!mon.volume_peek_ready) {
        if (now - mon.volume_peek_started_at >= kVolumePeekReadyDelayMs)
            mon.volume_peek_ready = true;
        else
            return;
    }

    bool muted = false;
    float level = pipewire_sink_level(mon.app->pipewire, muted);
    bool changed = mon.volume_peek_last_level < 0.0f ||
                   std::abs(level - mon.volume_peek_last_level) > 0.001f ||
                   muted != mon.volume_peek_last_muted;
    mon.volume_peek_last_level = level;
    mon.volume_peek_last_muted = muted;
    if (!changed)
        return;

    mon.volume_peek_active = true;
    mon.volume_peek_deadline = now + kVolumePeekMs;
}

bool volume_pill_peek_expire(MonitorOutput &mon) {
    if (!mon.volume_peek_active)
        return false;
    if (std::chrono::steady_clock::now() < mon.volume_peek_deadline)
        return false;
    mon.volume_peek_active = false;
    return true;
}

} // namespace bar_detail

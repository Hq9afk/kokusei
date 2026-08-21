#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

#include "bar/panel/volume_panel.h"

#include "bar/widget/volume_widget.h"

#include "modules/bar.h"

#include "render/icon.h"
#include "render/icons.h"

#include "service/pipewire.h"

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
    BarPerMonitorState &bs = bar_state(mon);
    const char *glyph = volume_icon_glyph(mon.app->pipewire);
    if (glyph != bs.volume_icon_glyph_cached) {
        bs.volume_icon_texture = make_icon_texture(glyph);
        bs.volume_icon_glyph_cached = glyph;
    }
    return Pill{PillId::Volume, &bs.volume_icon_texture,
                volume_label(mon.app->pipewire), nullptr, [&mon, &bs] {
                    close_other_overlays(mon, PillId::Volume);
                    if (!bs.volume_panel.base.open) {
                        update_pill_expand(bs.capsule, mon.animations,
                                           PillId::Volume, true, true);
                        bar_paint(mon);
                        overlay_panel_ensure(
                            bs.volume_panel.base, mon.app->display,
                            [&] {
                                return volume_panel_create_surface(
                                    bs.volume_panel, mon.app->compositor,
                                    mon.app->layer_shell, mon.output.wl);
                            },
                            [&] {
                                return volume_panel_init_egl(
                                    bs.volume_panel, mon.app->renderer,
                                    mon.app->pipewire, mon.app->egl_display,
                                    mon.app->egl_config, mon.app->egl_context);
                            });
                        bar_detail::rest_egl_current(*mon.app);
                    }
                    volume_panel_toggle(
                        bs.volume_panel,
                        pill_center_x(bs.capsule, PillId::Volume));
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
    BarPerMonitorState &bs = bar_state(mon);
    auto now = std::chrono::steady_clock::now();
    if (!bs.volume_peek_ready) {
        if (now - bs.volume_peek_started_at >= kVolumePeekReadyDelayMs)
            bs.volume_peek_ready = true;
        else
            return;
    }

    bool muted = false;
    float level = pipewire_sink_level(mon.app->pipewire, muted);
    bool changed = bs.volume_peek_last_level < 0.0f ||
                   std::abs(level - bs.volume_peek_last_level) > 0.001f ||
                   muted != bs.volume_peek_last_muted;
    bs.volume_peek_last_level = level;
    bs.volume_peek_last_muted = muted;
    if (!changed)
        return;

    bs.volume_peek_active = true;
    bs.volume_peek_deadline = now + kVolumePeekMs;
}

bool volume_pill_peek_expire(MonitorOutput &mon) {
    BarPerMonitorState &bs = bar_state(mon);
    if (!bs.volume_peek_active)
        return false;
    if (std::chrono::steady_clock::now() < bs.volume_peek_deadline)
        return false;
    bs.volume_peek_active = false;
    return true;
}

} // namespace bar_detail

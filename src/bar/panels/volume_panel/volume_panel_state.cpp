#include "volume_panel_state.h"

#include "../../../render/panel_scroll.h"
#include "../../../wayland/layer_surface.h"
#include "volume_panel.h"

#include <algorithm>

namespace volume_panel_detail {

std::vector<PanelRow> build_rows(const PipewireState &pw) {
    std::vector<PanelRow> rows;
    rows.push_back({RowKind::OutputLabel, kVolumeLabelRowHeight});
    rows.push_back({RowKind::OutputSlider, kVolumeRowHeight});
    rows.push_back({RowKind::InputLabel, kVolumeLabelRowHeight});
    rows.push_back({RowKind::InputSlider, kVolumeRowHeight});
    rows.push_back({RowKind::Divider, kVolumeDividerRowHeight});

    rows.push_back({RowKind::AppsHeader, kVolumeSectionHeaderHeight});
    for (const PwNodeEntry *stream : pipewire_streams(pw, true))
        rows.push_back({RowKind::AppRow, kVolumeAppRowHeight, stream});

    rows.push_back({RowKind::Divider, kVolumeDividerRowHeight});

    rows.push_back({RowKind::OutputDeviceHeader, kVolumeSectionHeaderHeight});
    for (const PwNodeEntry *sink : pipewire_sinks(pw))
        rows.push_back(
            {RowKind::OutputDeviceRow, kVolumeDeviceRowHeight, sink});

    rows.push_back({RowKind::InputDeviceHeader, kVolumeSectionHeaderHeight});
    for (const PwNodeEntry *source : pipewire_sources(pw))
        rows.push_back(
            {RowKind::InputDeviceRow, kVolumeDeviceRowHeight, source});

    rows.push_back({RowKind::Spacer, kPanelTrailingSpacerHeight});
    return rows;
}

float content_height(const std::vector<PanelRow> &rows) {
    float h = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i > 0)
            h += kPanelListSpacing;
        h += rows[i].height;
    }
    return h;
}

float panel_height(const std::vector<PanelRow> &rows) {
    float h = kPanelPadding + kPanelHeaderHeight + kPanelHeaderDividerGap +
              1.0f + kPanelContentGap + content_height(rows) + kPanelPadding;
    return std::min(kPanelMaxHeight, h);
}

void draw_slider(Node *clip, std::vector<PanelClickRegion> &regions,
                 Rect rect_local, Rect rect_absolute, float track_height,
                 float value01, bool dimmed, const std::string &tag) {
    float track_y = rect_local.y + (rect_local.h - track_height) / 2.0f;
    node_add_rrect(clip, rect_local.x, track_y, rect_local.w, track_height,
                   track_height / 2.0f, 0.0f, rgba(palette::text_alpha11),
                   kPanelNoBorder);
    float fill_w = rect_local.w * std::clamp(value01, 0.0f, 1.0f);
    if (fill_w > 0.0f)
        node_add_rrect(clip, rect_local.x, track_y, fill_w, track_height,
                       track_height / 2.0f, 0.0f,
                       dimmed ? rgba(palette::text_muted)
                              : rgba(palette::accent),
                       kPanelNoBorder);
    regions.push_back({PanelClickKind::SliderDrag, rect_absolute, tag});
}

namespace {

uint32_t resolve_tag_id(const PipewireState &pw, const std::string &tag) {
    if (tag == "sink")
        return pw.default_sink_id;
    if (tag == "source")
        return pw.default_source_id;
    if (tag.rfind("stream:", 0) == 0)
        return static_cast<uint32_t>(std::stoul(tag.substr(7)));
    return 0;
}

} // namespace

} // namespace volume_panel_detail

bool volume_panel_create_surface(VolumePanelState &state,
                                 wl_compositor *compositor,
                                 zwlr_layer_shell_v1 *layer_shell,
                                 wl_output *output) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-volume-panel", output);
}

bool volume_panel_init_egl(VolumePanelState &state, Renderer &renderer,
                           PipewireState &pw, EGLDisplay display,
                           EGLConfig config, EGLContext context) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state, &pw] {
        volume_panel_paint(state, pw, state.pending_pill_center_x,
                           state.pending_bar_height,
                           state.pending_bar_top_margin);
    };
    return true;
}

void volume_panel_request_frame(VolumePanelState &state, float pill_center_x,
                                float bar_height, float bar_top_margin) {
    state.pending_pill_center_x = pill_center_x;
    state.pending_bar_height = bar_height;
    state.pending_bar_top_margin = bar_top_margin;
    overlay_panel_request_frame(state.base);
}

void volume_panel_toggle(VolumePanelState &state, float pill_center_x) {
    panel_lock_toggle(
        state.base, state.locked_center_x, pill_center_x,
        [&state] { state.visible_height = -1.0f; },
        [&state] {
            state.scroll_offset = 0.0f;
            state.dragging.reset();
            state.selected_slider_tag.clear();
            state.base.animations.animate(
                state.visible_height, 0.0f, kOverlayFadeMs,
                Easing::EaseOutCubic,
                [&state](float v) { state.visible_height = v; }, {},
                kPanelHeightAnimOwner);
        });
}

void volume_panel_handle_scroll(VolumePanelState &state,
                                const PipewireState &pw, double dy) {
    state.scroll_offset = panel_clamp_scroll(
        state.scroll_offset, static_cast<float>(dy),
        volume_panel_detail::content_height(volume_panel_detail::build_rows(pw)),
        state.visible_content_height);
}

void volume_panel_handle_pointer_move(VolumePanelState &state,
                                      PipewireState &pw, double px) {
    if (!state.dragging)
        return;
    const DraggedSlider &drag = *state.dragging;
    float value01 =
        drag.rect.w > 0.0f
            ? std::clamp(static_cast<float>(px - drag.rect.x) / drag.rect.w,
                         0.0f, 1.0f)
            : 0.0f;
    uint32_t id = volume_panel_detail::resolve_tag_id(pw, drag.tag);
    if (id != 0)
        pipewire_set_node_volume(pw, id, value01);
}

void volume_panel_handle_click(VolumePanelState &state, PipewireState &pw,
                               double px, double py) {
    auto hit = [](const Rect &r, double x, double y) {
        return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y &&
               y < r.y + r.h;
    };

    for (const PanelClickRegion &region : state.click_regions) {
        if (!hit(region.rect, px, py))
            continue;
        switch (region.kind) {
        case PanelClickKind::Close:
            volume_panel_toggle(state);
            return;
        case PanelClickKind::SliderDrag: {
            state.dragging = DraggedSlider{region.tag, region.rect};
            state.selected_slider_tag = region.tag;
            volume_panel_handle_pointer_move(state, pw, px);
            return;
        }
        case PanelClickKind::MuteToggle: {
            uint32_t id = volume_panel_detail::resolve_tag_id(pw, region.tag);
            if (id != 0) {
                auto it = pw.nodes.find(id);
                if (it != pw.nodes.end())
                    pipewire_set_node_muted(pw, id, !it->second.muted);
            }
            return;
        }
        case PanelClickKind::DeviceSelect: {
            uint32_t id = static_cast<uint32_t>(std::stoul(region.tag));
            pipewire_set_default(pw, id);
            return;
        }
        default:
            return;
        }
    }

    if (!hit(state.panel_rect, px, py))
        volume_panel_toggle(state);
}

void volume_panel_handle_key_event(VolumePanelState &state, PipewireState &pw,
                                   const KeyEvent &event) {
    switch (event.kind) {
    case KeyKind::Escape:
        volume_panel_toggle(state);
        break;
    case KeyKind::Left:
    case KeyKind::Right: {
        if (state.selected_slider_tag.empty())
            break;
        uint32_t id = volume_panel_detail::resolve_tag_id(
            pw, state.selected_slider_tag);
        if (id == 0)
            break;
        auto it = pw.nodes.find(id);
        if (it == pw.nodes.end())
            break;
        float step = event.kind == KeyKind::Right ? 0.01f : -0.01f;
        pipewire_set_node_volume(
            pw, id, std::clamp(it->second.level + step, 0.0f, 1.0f));
        break;
    }
    default:
        break;
    }
}

#pragma once

#include "../../../render/icon.hpp"
#include "../../../render/icons.hpp"
#include "../../../render/node.hpp"
#include "../../../render/overlay_panel.hpp"
#include "../../../render/palette.hpp"
#include "../../../render/panel_chrome.hpp"
#include "../../../render/panel_scroll.hpp"
#include "../../../render/rect.hpp"
#include "../../../render/renderer.hpp"
#include "../../../render/scene.hpp"
#include "../../../render/text.hpp"
#include "../../../render/texture.hpp"
#include "../../../render/texture_cache.hpp"
#include "../../../system/pipewire.hpp"
#include "../../../wayland/keyboard.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

constexpr float kVolumeLabelRowHeight = 20.0f;
constexpr float kVolumeRowHeight = 24.0f;
constexpr float kVolumeSliderHeight = 20.0f;
constexpr float kVolumeSliderTrackHeight = 6.0f;
constexpr float kVolumeAppSliderHeight = 16.0f;
constexpr float kVolumeAppSliderTrackHeight = 5.0f;
constexpr float kVolumePercentLabelWidth = 40.0f;
constexpr float kVolumeSliderRightGap = 8.0f;
constexpr float kVolumeLabelWidthCap = 180.0f;
constexpr float kVolumeSectionHeaderPad = 6.0f;
constexpr float kVolumeAppListTopGap = 4.0f;
constexpr float kVolumeAppRowBottomPad = 8.0f;
constexpr float kVolumeDeviceRowBottomPad = 6.0f;
constexpr float kVolumeDeviceIndicatorSize = 14.0f;
constexpr float kVolumeDeviceIndicatorRadius = 7.0f;
constexpr float kVolumeDeviceIndicatorDotSize = 6.0f;
constexpr float kVolumeDeviceIndicatorDotRadius = 3.0f;
constexpr std::chrono::milliseconds kVolumePeekMs{2000};
constexpr std::chrono::milliseconds kVolumePeekReadyDelayMs{1000};

constexpr float kVolumeTextRowHeight = 18.0f;
constexpr float kVolumeAppRowHeight =
    kVolumeTextRowHeight + kVolumeAppListTopGap + kVolumeAppSliderHeight +
    kVolumeAppRowBottomPad;
constexpr float kVolumeDeviceRowHeight =
    kVolumeTextRowHeight + kVolumeDeviceRowBottomPad;
constexpr float kVolumeSectionHeaderHeight =
    kVolumeTextRowHeight + kVolumeSectionHeaderPad;
constexpr float kVolumeDividerRowHeight = 1.0f;

inline const char *volume_threshold_icon(bool muted, float level) {
    if (muted)
        return icon::volume_mute;
    if (level < 0.01f)
        return icon::volume_empty;
    if (level < 0.5f)
        return icon::volume_low;
    return icon::volume_high;
}

struct DraggedSlider {
    std::string tag;
    Rect rect;
};

struct VolumePanelState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    TextureCache tcache;

    Rect panel_rect;
    std::vector<PanelClickRegion> click_regions;
    float locked_center_x = -1.0f;
    float visible_height = -1.0f;
    float scroll_offset = 0.0f;
    float visible_content_height = 0.0f;
    std::optional<DraggedSlider> dragging;
    std::string selected_slider_tag;

    float pending_pill_center_x = 0.0f;
    float pending_bar_height = 0.0f;
    float pending_bar_top_margin = 0.0f;
};

namespace volume_panel_detail {

using panel_chrome_detail::cached_icon;
using panel_chrome_detail::cached_text;
using panel_chrome_detail::cached_text_clipped;

enum class RowKind {
    OutputLabel,
    OutputSlider,
    InputLabel,
    InputSlider,
    Divider,
    AppsHeader,
    AppRow,
    OutputDeviceHeader,
    OutputDeviceRow,
    InputDeviceHeader,
    InputDeviceRow,
    Spacer,
};

struct PanelRow {
    RowKind kind;
    float height;
    const PwNodeEntry *entry = nullptr;
};

inline std::vector<PanelRow> build_rows(const PipewireState &pw) {
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

inline float content_height(const std::vector<PanelRow> &rows) {
    float h = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i > 0)
            h += kPanelListSpacing;
        h += rows[i].height;
    }
    return h;
}

inline float panel_height(const std::vector<PanelRow> &rows) {
    float h = kPanelPadding + kPanelHeaderHeight + kPanelHeaderDividerGap +
              1.0f + kPanelContentGap + content_height(rows) + kPanelPadding;
    return std::min(kPanelMaxHeight, h);
}

// Slider track + fill + registers a drag-target click region. `rect` is the
// full clickable slider area (kVolumeSliderHeight tall); the visual track is
// drawn thinner and vertically centered inside it, same convention as OSD's
// own level bar.
inline void draw_slider(Node *clip, std::vector<PanelClickRegion> &regions,
                        Rect rect_local, Rect rect_absolute, float track_height,
                        float value01, bool dimmed, const std::string &tag) {
    float track_y = rect_local.y + (rect_local.h - track_height) / 2.0f;
    node_add_rrect(clip, rect_local.x, track_y, rect_local.w, track_height,
                   track_height / 2.0f, 0.0f, rgba(palette::text_alpha10),
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

} // namespace volume_panel_detail

inline bool volume_panel_create_surface(VolumePanelState &state,
                                        wl_compositor *compositor,
                                        zwlr_layer_shell_v1 *layer_shell) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-volume-panel");
}

inline void volume_panel_paint(VolumePanelState &state, PipewireState &pw,
                               float pill_center_x, float bar_height,
                               float bar_top_margin);

inline bool volume_panel_init_egl(VolumePanelState &state, Renderer &renderer,
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

inline void volume_panel_request_frame(VolumePanelState &state,
                                       float pill_center_x, float bar_height,
                                       float bar_top_margin) {
    state.pending_pill_center_x = pill_center_x;
    state.pending_bar_height = bar_height;
    state.pending_bar_top_margin = bar_top_margin;
    overlay_panel_request_frame(state.base);
}

inline void volume_panel_toggle(VolumePanelState &state,
                                float pill_center_x = -1.0f) {
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

inline void volume_panel_handle_scroll(VolumePanelState &state,
                                       const PipewireState &pw, double dy) {
    state.scroll_offset =
        panel_clamp_scroll(state.scroll_offset, static_cast<float>(dy),
                           volume_panel_detail::content_height(
                               volume_panel_detail::build_rows(pw)),
                           state.visible_content_height);
}

namespace volume_panel_detail {

inline uint32_t resolve_tag_id(const PipewireState &pw,
                               const std::string &tag) {
    if (tag == "sink")
        return pw.default_sink_id;
    if (tag == "source")
        return pw.default_source_id;
    if (tag.rfind("stream:", 0) == 0)
        return static_cast<uint32_t>(std::stoul(tag.substr(7)));
    return 0;
}

} // namespace volume_panel_detail

inline void volume_panel_handle_pointer_move(VolumePanelState &state,
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

inline void volume_panel_handle_click(VolumePanelState &state,
                                      PipewireState &pw, double px, double py) {
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

inline void volume_panel_handle_key_event(VolumePanelState &state,
                                          PipewireState &pw,
                                          const KeyEvent &event) {
    switch (event.kind) {
    case KeyKind::Escape:
        volume_panel_toggle(state);
        break;
    case KeyKind::Left:
    case KeyKind::Right: {
        if (state.selected_slider_tag.empty())
            break;
        uint32_t id =
            volume_panel_detail::resolve_tag_id(pw, state.selected_slider_tag);
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

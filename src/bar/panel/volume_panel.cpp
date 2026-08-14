#include "bar/panel/volume_panel.h"

#include "render/icon.h"
#include "render/palette.h"
#include "render/panel_scroll.h"
#include "render/text.h"
#include "wayland/layer_surface.h"
#include <GLES2/gl2.h>
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
    state.scroll_offset =
        panel_clamp_scroll(state.scroll_offset, static_cast<float>(dy),
                           volume_panel_detail::content_height(
                               volume_panel_detail::build_rows(pw)),
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

using namespace volume_panel_detail;

void volume_panel_paint(VolumePanelState &state, PipewireState &pw,
                        float pill_center_x, float bar_height,
                        float bar_top_margin) {
    using namespace panel_chrome_detail;
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    state.base.animations.tick(std::chrono::steady_clock::now());
    eglMakeCurrent(state.base.egl_display, state.base.egl_surface,
                   state.base.egl_surface, state.base.egl_context);
    int32_t scale = state.base.output_scale.scale;
    state.renderer->begin_frame(state.base.width, state.base.height, scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.click_regions.clear();
    state.panel_rect = {};
    state.scene.rebuild();

    if (!state.base.open) {
        state.scene.draw(*state.renderer);
        eglSwapBuffers(state.base.egl_display, state.base.egl_surface);
        return;
    }

    Node *root = &state.scene.root;

    std::vector<PanelRow> rows = build_rows(pw);
    float panel_w = kPanelWidth;
    float panel_h = panel_height(rows);
    if (state.locked_center_x < 0.0f)
        state.locked_center_x = pill_center_x;
    if (state.visible_height < 0.0f) {
        state.visible_height = 0.0f;
        state.base.animations.animate(
            state.visible_height, panel_h, kOverlayFadeMs, Easing::EaseOutCubic,
            [&state](float v) { state.visible_height = v; }, {},
            kPanelHeightAnimOwner);
    }
    float panel_x = std::clamp(
        state.locked_center_x - panel_w / 2.0f, kPanelSideMargin,
        static_cast<float>(state.base.width) - panel_w - kPanelSideMargin);
    float panel_y = bar_height + bar_top_margin + kPanelGap;
    state.panel_rect = {panel_x, panel_y, panel_w, panel_h};

    const float *white = rgba(palette::text);
    const float *dim = rgba(palette::text_dim);

    panel_draw_box(root, panel_x, panel_y, panel_w, panel_h);
    float header_y = panel_y + kPanelPadding;
    panel_draw_header(root, state.tcache, scale, "Volume", panel_x, panel_y,
                      panel_w, state.click_regions);

    float divider_y = header_y + kPanelHeaderHeight + kPanelHeaderDividerGap;
    node_add_rect(root, panel_x + kPanelPadding, divider_y,
                  panel_w - 2 * kPanelPadding, 1.0f,
                  rgba(palette::text_alpha06));

    PanelScrollRegion region =
        panel_scroll_region(panel_x, panel_y, panel_w, panel_h);
    float content_x = region.content_x;
    float content_w = region.content_w;
    float content_top = region.content_top;
    float content_bottom = region.content_bottom;

    state.visible_content_height = std::max(0.0f, content_bottom - content_top);

    Node *scroll_clip =
        node_add_group(root, panel_x, content_top, panel_w,
                       std::max(0.0f, content_bottom - content_top), true);

    auto rx = [&](float v) { return v - panel_x; };
    auto ry = [&](float v) { return v - content_top; };

    bool sink_muted = false;
    float sink_level = pipewire_sink_level(pw, sink_muted);
    bool source_muted = false;
    float source_level = pipewire_source_level(pw, source_muted);
    auto sink_it = pw.nodes.find(pw.default_sink_id);
    auto source_it = pw.nodes.find(pw.default_source_id);
    const std::string sink_desc = sink_it != pw.nodes.end()
                                      ? (sink_it->second.description.empty()
                                             ? sink_it->second.name
                                             : sink_it->second.description)
                                      : "";
    const std::string source_desc = source_it != pw.nodes.end()
                                        ? (source_it->second.description.empty()
                                               ? source_it->second.name
                                               : source_it->second.description)
                                        : "";

    float y = content_top - state.scroll_offset;
    for (size_t i = 0; i < rows.size(); ++i) {
        const PanelRow &row = rows[i];
        if (i > 0)
            y += kPanelListSpacing;
        Node *clip = scroll_clip;
        float row_h = row.height;
        bool row_visible = y + row_h > content_top && y < content_bottom;
        if (!row_visible) {
            y += row_h;
            continue;
        }

        auto draw_label_row = [&](const char *title, const std::string &desc) {
            const Texture *title_tex = cached_text(state.tcache, title, scale);
            float title_w = 0.0f;
            if (title_tex) {
                node_add_texture(clip, rx(content_x),
                                 ry(y + (row_h - title_tex->height) / 2.0f),
                                 *title_tex, white);
                title_w = static_cast<float>(title_tex->width) + kPanelTightGap;
            }
            if (!desc.empty()) {
                std::string full = "\xE2\x80\x94 " + desc;
                int max_w = static_cast<int>(kVolumeLabelWidthCap);
                const Texture *desc_tex =
                    cached_text_clipped(state.tcache, full, scale, max_w);
                if (desc_tex)
                    node_add_texture(clip, rx(content_x + title_w),
                                     ry(y + (row_h - desc_tex->height) / 2.0f),
                                     *desc_tex, dim);
            }
        };

        auto draw_slider_row = [&](float level, bool muted,
                                   const std::string &tag) {
            float mute_x = content_x + content_w - kPanelActionButtonSize;
            Rect mute_rect = {mute_x,
                              y + (row_h - kPanelActionButtonSize) / 2.0f,
                              kPanelActionButtonSize, kPanelActionButtonSize};
            node_add_rrect(clip, rx(mute_rect.x), ry(mute_rect.y), mute_rect.w,
                           mute_rect.h, kPanelActionButtonSize / 2.0f, 0.0f,
                           rgba(palette::overlay), kPanelNoBorder);
            const Texture *mute_icon = cached_icon(
                state.tcache, volume_threshold_icon(muted, level), scale);
            if (mute_icon)
                node_add_texture(
                    clip,
                    rx(mute_rect.x + (mute_rect.w - mute_icon->width) / 2.0f),
                    ry(mute_rect.y + (mute_rect.h - mute_icon->height) / 2.0f),
                    *mute_icon, white);
            state.click_regions.push_back(
                {PanelClickKind::MuteToggle, mute_rect, tag});

            float pct_x = mute_rect.x - kPanelRowGap - kVolumePercentLabelWidth;
            std::string pct_str = muted ? "muted"
                                        : std::to_string(static_cast<int>(
                                              std::lround(level * 100))) +
                                              "%";
            const Texture *pct_tex = cached_text(state.tcache, pct_str, scale);
            if (pct_tex)
                node_add_texture(
                    clip, rx(pct_x + kVolumePercentLabelWidth - pct_tex->width),
                    ry(y + (row_h - pct_tex->height) / 2.0f), *pct_tex, dim);

            float slider_right = pct_x - kVolumeSliderRightGap;
            Rect slider_rect = {content_x, y, slider_right - content_x, row_h};
            Rect slider_local = {rx(slider_rect.x), ry(slider_rect.y),
                                 slider_rect.w, slider_rect.h};
            draw_slider(clip, state.click_regions, slider_local, slider_rect,
                        kVolumeSliderTrackHeight, muted ? 0.0f : level, muted,
                        tag);
        };

        switch (row.kind) {
        case RowKind::OutputLabel:
            draw_label_row("Output", sink_desc);
            break;
        case RowKind::OutputSlider:
            draw_slider_row(sink_level, sink_muted, "sink");
            break;
        case RowKind::InputLabel:
            draw_label_row("Input", source_desc);
            break;
        case RowKind::InputSlider:
            draw_slider_row(source_level, source_muted, "source");
            break;
        case RowKind::Divider:
            node_add_rect(clip, rx(content_x), ry(y), content_w, 1.0f,
                          rgba(palette::text_alpha06));
            break;
        case RowKind::AppsHeader:
        case RowKind::OutputDeviceHeader:
        case RowKind::InputDeviceHeader: {
            const char *label = row.kind == RowKind::AppsHeader ? "Applications"
                                : row.kind == RowKind::OutputDeviceHeader
                                    ? "Output Device"
                                    : "Input Device";
            const Texture *t = cached_text(state.tcache, label, scale);
            if (t)
                node_add_texture(clip, rx(content_x), ry(y + row_h - t->height),
                                 *t, white);
            break;
        }
        case RowKind::AppRow: {
            const PwNodeEntry &entry = *row.entry;
            std::string name = !entry.app_name.empty()      ? entry.app_name
                               : !entry.description.empty() ? entry.description
                                                            : entry.name;
            if (name.empty())
                name = "Unknown";

            float mute_x = content_x + content_w - kPanelActionButtonSize;
            Rect mute_rect = {mute_x, y, kPanelActionButtonSize,
                              kPanelActionButtonSize};
            node_add_rrect(clip, rx(mute_rect.x), ry(mute_rect.y), mute_rect.w,
                           mute_rect.h, kPanelActionButtonSize / 2.0f, 0.0f,
                           rgba(palette::overlay), kPanelNoBorder);
            const Texture *mute_icon = cached_icon(
                state.tcache, volume_threshold_icon(entry.muted, entry.level),
                scale);
            if (mute_icon)
                node_add_texture(
                    clip,
                    rx(mute_rect.x + (mute_rect.w - mute_icon->width) / 2.0f),
                    ry(mute_rect.y + (mute_rect.h - mute_icon->height) / 2.0f),
                    *mute_icon, white);
            std::string tag = "stream:" + std::to_string(entry.id);
            state.click_regions.push_back(
                {PanelClickKind::MuteToggle, mute_rect, tag});

            float pct_x = mute_rect.x - kPanelRowGap - kVolumePercentLabelWidth;
            std::string pct_str = entry.muted
                                      ? "muted"
                                      : std::to_string(static_cast<int>(
                                            std::lround(entry.level * 100))) +
                                            "%";
            const Texture *pct_tex = cached_text(state.tcache, pct_str, scale);
            if (pct_tex)
                node_add_texture(
                    clip, rx(pct_x + kVolumePercentLabelWidth - pct_tex->width),
                    ry(y), *pct_tex, dim);

            float name_right = pct_x - kPanelRowGap;
            int name_w_px =
                static_cast<int>(std::max(0.0f, name_right - content_x));
            const Texture *name_tex =
                cached_text_clipped(state.tcache, name, scale, name_w_px);
            if (name_tex)
                node_add_texture(clip, rx(content_x), ry(y), *name_tex, white);

            float slider_y = y + kVolumeTextRowHeight + kVolumeAppListTopGap;
            Rect slider_rect = {content_x, slider_y, name_right - content_x,
                                kVolumeAppSliderHeight};
            Rect slider_local = {rx(slider_rect.x), ry(slider_rect.y),
                                 slider_rect.w, slider_rect.h};
            draw_slider(clip, state.click_regions, slider_local, slider_rect,
                        kVolumeAppSliderTrackHeight,
                        entry.muted ? 0.0f : entry.level, entry.muted, tag);
            break;
        }
        case RowKind::OutputDeviceRow:
        case RowKind::InputDeviceRow: {
            const PwNodeEntry &entry = *row.entry;
            bool is_default = entry.id == (row.kind == RowKind::OutputDeviceRow
                                               ? pw.default_sink_id
                                               : pw.default_source_id);
            std::string label =
                !entry.description.empty() ? entry.description : entry.name;
            if (label.empty())
                label = "Unknown";

            float dot_y = y + (row_h - kVolumeDeviceIndicatorSize) / 2.0f;
            node_add_rrect(
                clip, rx(content_x), ry(dot_y), kVolumeDeviceIndicatorSize,
                kVolumeDeviceIndicatorSize, kVolumeDeviceIndicatorRadius, 0.0f,
                is_default ? rgba(palette::accent)
                           : rgba(palette::text_alpha20),
                kPanelNoBorder);
            if (is_default) {
                float inner_off = (kVolumeDeviceIndicatorSize -
                                   kVolumeDeviceIndicatorDotSize) /
                                  2.0f;
                node_add_rrect(clip, rx(content_x + inner_off),
                               ry(dot_y + inner_off),
                               kVolumeDeviceIndicatorDotSize,
                               kVolumeDeviceIndicatorDotSize,
                               kVolumeDeviceIndicatorDotRadius, 0.0f, white,
                               kPanelNoBorder);
            }

            float text_x =
                content_x + kVolumeDeviceIndicatorSize + kPanelRowIconGap;
            int text_w_px = static_cast<int>(
                std::max(0.0f, content_x + content_w - text_x));
            const Texture *label_tex =
                cached_text_clipped(state.tcache, label, scale, text_w_px);
            if (label_tex)
                node_add_texture(clip, rx(text_x),
                                 ry(y + (row_h - label_tex->height) / 2.0f),
                                 *label_tex,
                                 is_default ? rgba(palette::accent) : white);

            Rect row_rect = {content_x, y, content_w, row_h};
            state.click_regions.push_back({PanelClickKind::DeviceSelect,
                                           row_rect, std::to_string(entry.id)});
            break;
        }
        case RowKind::Spacer:
            break;
        }
        y += row_h;
    }

    if (state.base.animations.hasActive()) {
        ScopedClip clip(*state.renderer, panel_x, panel_y, panel_w,
                        std::max(0.0f, state.visible_height));
        state.scene.draw(*state.renderer);
    } else {
        state.scene.draw(*state.renderer);
    }
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);

    if (state.base.animations.hasActive())
        overlay_panel_request_frame(state.base);
}

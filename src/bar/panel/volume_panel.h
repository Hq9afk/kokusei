#pragma once

#include "render/node.h"
#include "render/overlay_panel.h"
#include "render/panel_chrome.h"
#include "render/rect.h"
#include "render/renderer.h"
#include "render/scene.h"
#include "render/texture_cache.h"
#include "system/pipewire.h"
#include "wayland/keyboard.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <EGL/egl.h>
#include <chrono>
#include <optional>
#include <string>
#include <vector>
#include <wayland-client.h>

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

// Shared with volume_panel.cpp's paint function.

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

std::vector<PanelRow> build_rows(const PipewireState &pw);

float content_height(const std::vector<PanelRow> &rows);

float panel_height(const std::vector<PanelRow> &rows);

void draw_slider(Node *clip, std::vector<PanelClickRegion> &regions,
                 Rect rect_local, Rect rect_absolute, float track_height,
                 float value01, bool dimmed, const std::string &tag);

} // namespace volume_panel_detail

bool volume_panel_create_surface(VolumePanelState &state,
                                 wl_compositor *compositor,
                                 zwlr_layer_shell_v1 *layer_shell,
                                 wl_output *output = nullptr);

bool volume_panel_init_egl(VolumePanelState &state, Renderer &renderer,
                           PipewireState &pw, EGLDisplay display,
                           EGLConfig config, EGLContext context);

void volume_panel_request_frame(VolumePanelState &state, float pill_center_x,
                                float bar_height, float bar_top_margin);

void volume_panel_toggle(VolumePanelState &state, float pill_center_x = -1.0f);

void volume_panel_handle_scroll(VolumePanelState &state,
                                const PipewireState &pw, double dy);

void volume_panel_handle_pointer_move(VolumePanelState &state,
                                      PipewireState &pw, double px);

void volume_panel_handle_click(VolumePanelState &state, PipewireState &pw,
                               double px, double py);

void volume_panel_handle_key_event(VolumePanelState &state, PipewireState &pw,
                                   const KeyEvent &event);

void volume_panel_paint(VolumePanelState &state, PipewireState &pw,
                        float pill_center_x, float bar_height,
                        float bar_top_margin);

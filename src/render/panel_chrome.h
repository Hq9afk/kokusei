#pragma once

#include <string>
#include <vector>

#include "render/icon.h"
#include "render/icons.h"
#include "render/node.h"
#include "render/palette.h"
#include "render/rect.h"
#include "render/renderer.h"
#include "render/text.h"
#include "render/texture.h"
#include "render/texture_cache.h"

constexpr float kPanelWidth = 320.0f;
constexpr float kPanelPadding = 20.0f;
constexpr float kPanelHeaderHeight = 32.0f;
constexpr float kPanelHeaderDividerGap = 4.0f;
constexpr float kPanelContentGap = 8.0f;
constexpr float kPanelGap = 8.0f;
constexpr float kPanelMaxHeight = 520.0f;
constexpr float kPanelActionButtonSize = 22.0f;
constexpr float kPanelListSpacing = 5.0f;
constexpr float kPanelRowGap = 6.0f;
constexpr float kPanelRowIconGap = 8.0f;
constexpr float kPanelRowActionGap = 4.0f;
constexpr float kPanelTightGap = 4.0f;
constexpr float kPanelDeviceRowHeight = 50.0f;
constexpr float kPanelDialogButtonPaddingH = 16.0f;
constexpr float kPanelConfirmButtonSize = 28.0f;
constexpr float kPanelSubPanelRowHeight = 28.0f;
constexpr float kPanelSubPanelTopMargin = 6.0f;
constexpr float kPanelDialogSpacerHeight = 14.0f;
constexpr float kPanelTrailingSpacerHeight = 4.0f;
constexpr float kPanelSubLabelHeight = 20.0f;
constexpr float kPanelSideMargin = 20.0f;

inline constexpr float kPanelNoBorder[4] = {0, 0, 0, 0};

enum class PanelClickKind {
    Close,
    HeaderToggle,
    HeaderAction,
    ErrorClose,
    RowConnect,
    RowForget,
    SubClose,
    SubConfirm,
    SubCancel,
    SliderDrag,
    MuteToggle,
    DeviceSelect,
    TrayActivate,
    TrayOpenMenu,
    TrayMenuBack,
    TrayMenuEntry,
    TabSelect,
    ToggleFlip,
    FieldFocus,
    WallpaperSelect,
    MonitorSelect,
    RegionSelect,
    AnimatedWallpaperSelect,
    AnimatedRegionSelect,
    MediaPlayPause,
    MediaNext,
    MediaPrevious,
    ProfileSettings,
    DropdownToggle,
    DropdownSelect,
};
struct PanelClickRegion {
    PanelClickKind kind;
    Rect rect;
    std::string tag;
};

struct PanelDropdownOption {
    std::string label;
    std::string value;
};

namespace panel_chrome_detail {

const Texture *cached_text(TextureCache &cache, const std::string &s,
                           int32_t scale);

const Texture *cached_icon(TextureCache &cache, const char *codepoint,
                           int32_t scale);

const Texture *cached_text_clipped(TextureCache &cache, const std::string &s,
                                   int32_t scale, int max_width_px);

const Texture *cached_text_large(TextureCache &cache, const std::string &s,
                                 int32_t scale);

} // namespace panel_chrome_detail

Node *panel_draw_box(Node *parent, float x, float y, float w, float h,
                     float border_width = metrics::border_thin);

float panel_draw_header(Node *parent, TextureCache &cache, int32_t scale,
                        const std::string &title, float panel_x, float panel_y,
                        float panel_w,
                        std::vector<PanelClickRegion> &click_regions);

void panel_draw_row_text(Node *tclip, const Texture *name_tex,
                         const Texture *sub_tex, float row_h,
                         const float *name_color, const float *sub_color);

float panel_confirm_subpanel_height();

float panel_draw_subpanel_top(Node *parent, TextureCache &cache, int32_t scale,
                              const std::string &top_label, float panel_x,
                              float sub_y, float panel_w, float sub_h,
                              std::vector<PanelClickRegion> &click_regions);

void panel_draw_confirm_subpanel(Node *parent, TextureCache &cache,
                                 int32_t scale, const std::string &top_label,
                                 const std::string &prompt,
                                 const std::string &confirm_label,
                                 float panel_x, float sub_y, float panel_w,
                                 std::vector<PanelClickRegion> &click_regions);

float panel_draw_dropdown(Node *parent, TextureCache &cache, int32_t scale,
                          float x, float y, float row_w,
                          const std::string &label,
                          const std::string &active_value,
                          const std::vector<PanelDropdownOption> &options,
                          const std::string &dropdown_id,
                          const std::string &open_dropdown_id,
                          std::vector<PanelClickRegion> &click_regions);

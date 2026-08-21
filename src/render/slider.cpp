#include <algorithm>

#include "render/palette.h"
#include "render/slider.h"

namespace {
constexpr float kSliderThumbSize = 12.0f;
} // namespace

void draw_slider_track(Node *clip, std::vector<PanelClickRegion> &regions,
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

    float thumb_x = std::clamp(fill_w - kSliderThumbSize / 2.0f, 0.0f,
                               rect_local.w - kSliderThumbSize);
    float thumb_y = rect_local.y + (rect_local.h - kSliderThumbSize) / 2.0f;
    node_add_rrect(clip, rect_local.x + thumb_x, thumb_y, kSliderThumbSize,
                   kSliderThumbSize, kSliderThumbSize / 2.0f, 0.0f,
                   rgba(palette::text), kPanelNoBorder);

    regions.push_back({PanelClickKind::SliderDrag, rect_absolute, tag});
}

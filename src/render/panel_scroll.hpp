#pragma once

#include "panel_chrome.hpp"

#include <algorithm>

struct PanelScrollRegion {
    float content_x = 0.0f;
    float content_w = 0.0f;
    float content_top = 0.0f;
    float content_bottom = 0.0f;
    float visible_height = 0.0f;
};

inline PanelScrollRegion panel_scroll_region(float panel_x, float panel_y,
                                             float panel_w,
                                             float panel_h) noexcept {
    float header_y = panel_y + kPanelPadding;
    float divider_y = header_y + kPanelHeaderHeight + kPanelHeaderDividerGap;

    PanelScrollRegion region;
    region.content_x = panel_x + kPanelPadding;
    region.content_w = panel_w - 2 * kPanelPadding;
    region.content_top = divider_y + 1.0f + kPanelContentGap;
    region.content_bottom = panel_y + panel_h - kPanelPadding;
    region.visible_height = region.content_bottom - region.content_top;
    return region;
}

inline float panel_clamp_scroll(float scroll_offset, float dy,
                                float content_height,
                                float visible_height) noexcept {
    float max_scroll = std::max(0.0f, content_height - visible_height);
    return std::clamp(scroll_offset + dy, 0.0f, max_scroll);
}


#pragma once

#include "panel_chrome.h"

struct PanelScrollRegion {
    float content_x = 0.0f;
    float content_w = 0.0f;
    float content_top = 0.0f;
    float content_bottom = 0.0f;
    float visible_height = 0.0f;
};

PanelScrollRegion panel_scroll_region(float panel_x, float panel_y,
                                      float panel_w, float panel_h) noexcept;

float panel_clamp_scroll(float scroll_offset, float dy, float content_height,
                         float visible_height) noexcept;

#pragma once

#include <string>
#include <vector>

#include "render/node.h"
#include "render/panel_chrome.h"
#include "render/rect.h"

void draw_slider_track(Node *clip, std::vector<PanelClickRegion> &regions,
                       Rect rect_local, Rect rect_absolute, float track_height,
                       float value01, bool dimmed, const std::string &tag);

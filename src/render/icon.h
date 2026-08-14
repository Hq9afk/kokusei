#pragma once

#include "render/text.h"

#include <cstdint>
#include <string>

inline constexpr int KOKUSEI_ICON_PX = 18;

RasterizedText rasterize_icon(const std::string &codepoint_utf8,
                              int32_t scale = 1);

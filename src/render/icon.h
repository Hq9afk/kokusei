#pragma once

#include <cstdint>
#include <string>

#include "render/text.h"

inline constexpr int KOKUSEI_ICON_PX = 18;

RasterizedText rasterize_icon(const std::string &codepoint_utf8,
                              int32_t scale = 1);

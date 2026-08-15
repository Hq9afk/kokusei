#pragma once

#include <chrono>
#include <cstdint>

namespace bar_detail {
constexpr int32_t kBarHeight = 35;

constexpr float kPillPad = 10.0f;
constexpr float kCapsuleGap = 10.0f;
constexpr float kPillExpandMs = 150.0f;
constexpr auto kPillCloseLingerMs = std::chrono::milliseconds(80);

constexpr int32_t kBarTopMargin = 10;

constexpr int32_t kAutoHideStripPx = 1;
constexpr float kAutoHideRevealMs = 150.0f;
constexpr float kAutoHideHideMs = 150.0f;
constexpr uint64_t kAutoHideAnimOwner = 1000;
} // namespace bar_detail

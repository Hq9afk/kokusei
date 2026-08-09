#pragma once

#include <cstddef>
#include <cstdint>

constexpr float kLauncherPad = 12.0f;
constexpr float kLauncherBorderWidth = 2.0f;
constexpr float kLauncherHighlightBorderWidth = 2.0f;
constexpr float kLauncherBulletSize = 25.0f;
constexpr float kLauncherBulletGap = kLauncherPad;
constexpr float kLauncherSearchHeight = 44.0f;
constexpr float kLauncherRowHeight = 40.0f;
constexpr float kLauncherListTop = 68.0f;
constexpr int kLauncherSurfaceWidth = 700;
constexpr int kLauncherMaxVisible = 6;
constexpr int kLauncherSearchDebounceMs = 120;
constexpr int kLauncherKillGraceMs = 50;
constexpr int kLauncherKillCheckMs = 5;
constexpr int kLauncherMaxResults = 20;
constexpr float kLauncherHeightAnimMs = 200.0f;
constexpr float kLauncherHighlightAnimMs = 140.0f;
constexpr uint64_t kLauncherHeightOwner = 100;
constexpr uint64_t kLauncherHighlightOwner = 101;
constexpr uint64_t kLauncherScrollOwner = 102;

namespace launcher_detail {
constexpr size_t kMaxRowChars = 74;
} // namespace launcher_detail

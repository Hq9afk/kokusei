#pragma once

#include <cstddef>
#include <cstdint>

constexpr float kLauncherPad = 12.0f;
constexpr float kLauncherBorderWidth = 2.0f;
constexpr float kLauncherHighlightBorderWidth = 2.0f;
constexpr float kLauncherBulletSize = 25.0f;
constexpr float kLauncherBulletGap = kLauncherPad;
constexpr float kLauncherSearchHeight = 44.0f;
constexpr float kLauncherRowHeight = 44.0f;
constexpr float kLauncherRowSpacing = 10.0f;
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

constexpr float kLauncherQueryScaleMs = 200.0f;
constexpr float kLauncherQuerySlideMs = 80.0f;
constexpr float kLauncherQuerySlideOffsetPx = 8.0f;
constexpr uint64_t kLauncherQueryCharOwnerBase = 1000;
constexpr size_t kLauncherQueryCharMax = 256;

enum class QueryCharProp : uint64_t { Scale = 0, Slide = 1 };

inline uint64_t launcher_query_char_owner(size_t index, QueryCharProp prop) {
    return kLauncherQueryCharOwnerBase + (index % kLauncherQueryCharMax) * 2 +
           static_cast<uint64_t>(prop);
}

namespace launcher_detail {
constexpr size_t kMaxRowChars = 74;
}

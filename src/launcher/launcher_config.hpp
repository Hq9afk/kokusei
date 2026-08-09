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

// Query text entrance animation, one character at a time, ported from
// keqing-shell's PasswordInput.qml (spring scale-pop + slide-in per new
// character; no opacity tween here, see launcher_query_char_push for why).
// Each live character runs two simultaneous tweens (scale/slide), each
// needs its own AnimationManager owner since animate() cancels any prior
// entry sharing its owner, so a character's own two tweens would cancel
// each other if they shared one id. Owner id for character index i,
// property p (0=scale, 1=slide) is kLauncherQueryCharOwnerBase + i * 2 + p;
// the range reserved here must stay clear of every other fixed owner id
// above.
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
} // namespace launcher_detail

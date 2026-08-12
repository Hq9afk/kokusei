#pragma once

#include <chrono>
#include <cstdint>

constexpr int kOsdSurfaceWidth = 280;
constexpr int kOsdSurfaceHeight = 50;
constexpr std::chrono::milliseconds kOsdVisibleFor{2000};
constexpr int kOsdContentMargin = 14;
constexpr int kOsdBarMargin = 10;
constexpr int kOsdLabelWidth = 44;
constexpr float kOsdAnimNormal = 220.0f;
constexpr float kOsdAnimFast = 150.0f;
constexpr std::chrono::milliseconds kOsdReadyDelay{1000};

constexpr uint64_t kOsdOwnerOpacity = 1;
constexpr uint64_t kOsdOwnerBarFill = 2;
constexpr uint64_t kOsdOwnerIconColor = 3;

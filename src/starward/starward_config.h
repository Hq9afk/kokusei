#pragma once

#include <array>
#include <cmath>
#include <cstdint>

constexpr int kStarwardButtonCount = 8;
constexpr float kStarwardButtonSize = 110.0f;
constexpr float kStarwardButtonsRadius = 300.0f;
constexpr float kStarwardButtonCornerRadius = kStarwardButtonSize / 5.0f;
constexpr float kStarwardLogoSize = 250.0f;
constexpr float kStarwardBorderWidth = 5.0f;

constexpr float kStarwardGlyphPx = kStarwardButtonSize / 2.0f;

constexpr float kStarwardStartAngle = -static_cast<float>(M_PI) / 2.0f;
constexpr float kStarwardStepAngle =
    2.0f * static_cast<float>(M_PI) / kStarwardButtonCount;

constexpr float kStarwardLogoAnimMs = 600.0f;
constexpr float kStarwardButtonStaggerMs = 100.0f;
constexpr float kStarwardButtonExpandMs = 150.0f;
constexpr float kStarwardButtonScaleMs = 140.0f;
constexpr float kStarwardButtonBorderMs = 160.0f;
constexpr float kStarwardHighlightScale = 1.05f;

constexpr uint64_t kStarwardLogoOwner = 1;
constexpr uint64_t kStarwardInputReadyOwner = 2;
constexpr uint64_t kStarwardCloseChainOwner = 3;

struct StarwardAction {
    const char *glyph_utf8;
    const char *command;
};

inline constexpr std::array<StarwardAction, kStarwardButtonCount> kStarwardActions = {
    {
        {"劍", "systemctl poweroff"},
        {"光", "systemctl reboot"},
        {"如", "kokusei lock"},
        {"我", "systemctl reboot --firmware-setup"},
        {"斬", ""},
        {"盡", ""},
        {"蕪", ""},
        {"雜", ""},
    }};

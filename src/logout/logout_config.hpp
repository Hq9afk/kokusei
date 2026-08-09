#pragma once

#include <array>
#include <cmath>
#include <cstdint>

constexpr int kLogoutButtonCount = 8;
constexpr float kLogoutButtonSize = 110.0f;
constexpr float kLogoutButtonsRadius = 300.0f;
constexpr float kLogoutButtonCornerRadius = kLogoutButtonSize / 5.0f;
constexpr float kLogoutLogoSize = 250.0f;
constexpr float kLogoutBorderWidth = 5.0f;

constexpr float kLogoutGlyphPx = kLogoutButtonSize / 2.0f;

constexpr float kLogoutStartAngle = -static_cast<float>(M_PI) / 2.0f;
constexpr float kLogoutStepAngle =
    2.0f * static_cast<float>(M_PI) / kLogoutButtonCount;

constexpr float kLogoutLogoAnimMs = 600.0f;
constexpr float kLogoutButtonStaggerMs = 100.0f;
constexpr float kLogoutButtonExpandMs = 150.0f;
constexpr float kLogoutButtonScaleMs = 140.0f;
constexpr float kLogoutButtonBorderMs = 160.0f;
constexpr float kLogoutHighlightScale = 1.05f;

constexpr uint64_t kLogoutLogoOwner = 1;
constexpr uint64_t kLogoutInputReadyOwner = 2;
constexpr uint64_t kLogoutCloseChainOwner = 3;

struct LogoutAction {
    const char *glyph_utf8;
    const char *command;
};

inline constexpr std::array<LogoutAction, kLogoutButtonCount> kLogoutActions = {
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

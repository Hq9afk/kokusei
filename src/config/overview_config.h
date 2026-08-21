#pragma once

// 1:1 from keqing-shell's OverviewConfig.qml.

// Animation
constexpr int kOverviewAnimEnterMs = 400;
constexpr int kOverviewAnimFastMs = 200;

// Layout
constexpr float kOverviewBackgroundBorderWidth = 1.0f;

// Colors
constexpr float kOverviewBackgroundOpacity = 1.0f;
constexpr float kOverviewBackgroundPadding = 10.0f;
constexpr int kOverviewColumns = 5;
constexpr float kOverviewElevationMargin = 10.0f;

// Timing
constexpr int kOverviewFocusGrabDelayMs = 150;
constexpr float kOverviewFocusedIndicatorBorderWidth = 2.0f;

// Window
constexpr float kOverviewIconToWindowRatio = 0.25f;
constexpr float kOverviewOtherMonitorOpacity = 0.4f;
constexpr int kOverviewRaceDelayMs = 150;
constexpr int kOverviewRows = 2;
constexpr float kOverviewScale = 0.15f;
constexpr float kOverviewScreenRounding = 23.0f;

// Shadow
constexpr float kOverviewShadowBlurFactor = 0.9f;
constexpr float kOverviewShadowOffsetX = 0.0f;
constexpr float kOverviewShadowOffsetY = 1.0f;
constexpr float kOverviewShadowRadius = 20.0f;
constexpr float kOverviewShadowSpread = 1.0f;

// Window preview
constexpr float kOverviewWindowDraggingZ = 99999.0f;
constexpr float kOverviewWindowPreviewBorderWidth = 1.0f;
constexpr float kOverviewWindowRounding = 18.0f;

// Workspace
constexpr float kOverviewWorkspaceBorderWidth = 2.0f;
constexpr float kOverviewWorkspaceNumberBaseSize = 250.0f;
constexpr float kOverviewWorkspaceNumberTextFade = 0.8f;
constexpr float kOverviewWorkspaceSpacing = 5.0f;

// Live-capture throttle: not part of the QML source (its ScreencopyView
// recaptures every frame). kokusei throttles hyprland-toplevel-export-v1
// requests instead of recapturing per-frame; see local/plan/overview-module.md.
constexpr int kOverviewCaptureIntervalMs = 250;

#pragma once

enum class SettingsFieldId {
    None,
    WallpaperPath,
    WallpaperDir,
    WallpaperAnimatedDir,
    IdleTimeout,
    IdleCommand,
    IdleResumeCommand,
};

constexpr float kSettingsRailWidth = 140.0f;
constexpr float kSettingsRailItemHeight = 36.0f;
constexpr float kSettingsRailItemGap = 4.0f;
constexpr float kSettingsRailIconLabelGap = 10.0f;
constexpr float kSettingsRailPadding = 10.0f;
constexpr float kSettingsRailDividerGap = 16.0f;
constexpr float kSettingsRowHeight = 40.0f;
constexpr float kSettingsRowGap = 10.0f;
constexpr float kSettingsLabelWidth = 170.0f;
constexpr float kSettingsFieldHeight = 28.0f;
constexpr float kSettingsFieldWidth = 240.0f;
constexpr float kSettingsNumberFieldWidth = 72.0f;

constexpr uint64_t kSettingsAutohideToggleOwner = 3;
constexpr uint64_t kSettingsFillModeToggleOwner = 4;

constexpr float kSettingsWallpaperThumbSize = 115.0f;
constexpr float kSettingsWallpaperThumbGap = 15.0f;
constexpr int kSettingsWallpaperGridColumns = 5;
constexpr float kSettingsWallpaperThumbRadius = 8.0f;
constexpr float kSettingsWallpaperLabelPad = 6.0f;
constexpr float kSettingsWallpaperGridInset = 5.0f;

constexpr float kSettingsWallpaperScrollSpeed = 3.0f;
constexpr float kSettingsMonitorChipHeight = 35.0f;
constexpr float kSettingsMonitorChipGap = 6.0f;

constexpr float kSettingsColumnStepperButtonSize = 28.0f;

constexpr float kSettingsDirBarHeight = 40.0f;
constexpr float kSettingsDirBarLabelMargin = 14.0f;
constexpr float kSettingsDirBarFieldMargin = 10.0f;
constexpr float kSettingsDirBarEdgeMargin = 8.0f;
constexpr float kSettingsDirBarButtonWidth = 72.0f;
constexpr float kSettingsDirBarButtonHeight = 28.0f;

constexpr float kSettingsScreenSelectorHeight = 35.0f;
constexpr float kSettingsScreenSelectorSpacing = 6.0f;
constexpr float kSettingsSelectorBorderWidth = 2.0f;
constexpr float kSettingsTileRadius = 6.0f;

constexpr float kSettingsToggleTileHeight = 48.0f;
constexpr float kSettingsToggleTileBorderWidth = 1.0f;
constexpr float kSettingsToggleTileContentMargin = 12.0f;
constexpr float kSettingsToggleTileContentSpacing = 10.0f;
constexpr float kSettingsGroupSpacingSm = 8.0f;

constexpr float kSettingsToggleTrackWidth = 36.0f;
constexpr float kSettingsToggleTrackHeight = 20.0f;
constexpr float kSettingsToggleTrackRadius = 10.0f;
constexpr float kSettingsToggleKnobSize = 14.0f;
constexpr float kSettingsToggleKnobRadius = 7.0f;
constexpr float kSettingsToggleKnobInset = 3.0f;

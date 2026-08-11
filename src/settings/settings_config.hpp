#pragma once

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

// 1/2 are reserved by render/overlay_panel.hpp's kOverlayFadeOwner/
// kPanelHeightAnimOwner on this same SettingsState::base.animations manager.
constexpr uint64_t kSettingsAutohideToggleOwner = 3;

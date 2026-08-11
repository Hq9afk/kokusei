#pragma once

constexpr float kSettingsTabBarHeight = 32.0f;
constexpr float kSettingsTabGap = 8.0f;
constexpr float kSettingsRowHeight = 40.0f;
constexpr float kSettingsRowGap = 10.0f;
constexpr float kSettingsLabelWidth = 170.0f;
constexpr float kSettingsFieldHeight = 28.0f;
constexpr float kSettingsFieldWidth = 240.0f;
constexpr float kSettingsNumberFieldWidth = 72.0f;
constexpr float kSettingsSectionTopGap = 20.0f;

// 1/2 are reserved by render/overlay_panel.hpp's kOverlayFadeOwner/
// kPanelHeightAnimOwner on this same SettingsState::base.animations manager.
constexpr uint64_t kSettingsAutohideToggleOwner = 3;

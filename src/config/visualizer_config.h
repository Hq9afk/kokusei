#pragma once

#include "render/palette.h"

constexpr float kVisualizerBarOpacity = 0.6f;
constexpr float kVisualizerBarHeightRatio = 0.7f;
constexpr float kVisualizerBarRadius = 3.0f;
constexpr float kVisualizerBarSpacing = 7.0f;
constexpr float kVisualizerBarWidth = 10.0f;
constexpr int kVisualizerBarCount = 100;
constexpr float kVisualizerBarsAnimDurationMs = 60.0f;

constexpr int kVisualizerDefaultWindowWidth = 480;
constexpr int kVisualizerDefaultWindowHeight = 350;
inline constexpr Color kVisualizerWindowBackground = {0.0f, 0.0f, 0.0f, 0.7f};

constexpr int kSpectrumFftSize = 4096;
constexpr int kSpectrumIdleThreshold = 30;
constexpr int kSpectrumLowerCutoffHz = 50;
constexpr int kSpectrumUpperCutoffHz = 12000;
constexpr float kSpectrumNoiseReduction = 0.77f;
constexpr bool kSpectrumSmoothing = true;

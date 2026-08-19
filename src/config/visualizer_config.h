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

inline constexpr Color kVisualizerSphereColor = {1.0f, 0.8353f, 0.0784f, 1.0f};
inline constexpr Color kVisualizerSphereGlowColor = {1.0f, 0.6667f, 0.0392f,
                                                     1.0f};
constexpr float kVisualizerSphereParticleStrength = 0.05f;
constexpr float kVisualizerSphereColorIntensityAddStrength = 0.38f;
constexpr float kVisualizerSphereGlowSize = 18.0f;
constexpr float kVisualizerSphereGlowIntensity = 1.0f;
constexpr float kVisualizerSphereGlowScale = 0.5f;
constexpr float kVisualizerSphereGlowDirections = 16.0f;
constexpr float kVisualizerSphereGlowQuality = 6.0f;

constexpr float kVisualizerSphereFieldSizeRatio = 1.0f;
constexpr float kVisualizerSphereNoiseAmplitude = 0.08f;
constexpr float kVisualizerSphereNoiseFrequency = 3.2f;
constexpr float kVisualizerSphereNoiseAudioMultiplier = 0.05f;
constexpr float kVisualizerSphereFlowX = 0.06f;
constexpr float kVisualizerSphereFlowY = 0.12f;
constexpr float kVisualizerSphereFlowZ = 0.03f;
constexpr float kVisualizerSphereRadius = 0.1875f;
constexpr float kVisualizerSphereRadiusAudioMultiplier = 0.05f;
constexpr float kVisualizerSphereProjectionFactor = 1.0f;

constexpr int kVisualizerSphereParticleGridSize = 96;
constexpr float kVisualizerSpherePointSize = 5.0f;

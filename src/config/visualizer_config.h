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

// Ported from ncs-spectrum-glava's ncs.glsl #define block (active values only).
inline constexpr Color kVisualizerNcsColor = {0.0118f, 0.1412f, 0.3412f, 1.0f};
// Per-particle effect strength at full window opacity; multiplied by the
// window's fade-in opacity, not a replacement for it.
constexpr float kVisualizerNcsParticleStrength = 0.05f;
constexpr float kVisualizerNcsColorIntensityAddStrength = 0.2f;
constexpr float kVisualizerNcsGlowSize = 10.0f;
constexpr float kVisualizerNcsGlowIntensity = 0.5f;
// The glow ping-pong pass runs at this fraction of output resolution; its
// 96-tap blur is the dominant per-frame GPU cost, cheap enough at half res
// to not stall kokusei's single poll loop, and GL_LINEAR upscales it for
// free in the composite pass.
constexpr float kVisualizerNcsGlowScale = 0.5f;
constexpr float kVisualizerNcsGlowDirections = 16.0f;
constexpr float kVisualizerNcsGlowQuality = 6.0f;
constexpr float kVisualizerNcsRadiusAudioMultiplier = 200.0f;
constexpr float kVisualizerNcsFractalAudioMixing = 0.50f;
constexpr float kVisualizerNcsFractalAudioMultiplier = 9.0f;
constexpr float kVisualizerNcsOctaveMultiplier = 0.25f;
constexpr float kVisualizerNcsOctaveScale = 1.0f;
constexpr int kVisualizerNcsComplexity = 3;
constexpr float kVisualizerNcsFScale = 4.6f;
constexpr float kVisualizerNcsGamma = 1.0f;
constexpr float kVisualizerNcsMinVal = -5.0f;
constexpr float kVisualizerNcsMaxVal = 5.0f;
constexpr float kVisualizerNcsDisplaceX = 110.0f;
constexpr float kVisualizerNcsDisplaceY = 95.0f;
constexpr float kVisualizerNcsDisplaceZ = 115.0f;
constexpr float kVisualizerNcsFlowX = 0.0f;
constexpr float kVisualizerNcsFlowY = 0.033f;
constexpr float kVisualizerNcsFlowZ = 0.0f;
constexpr float kVisualizerNcsFlowEvolution = 0.015f;
constexpr float kVisualizerNcsSphereRadius = 275.0f;
constexpr float kVisualizerNcsFeather = 0.45f;

// New for this port: point-sprite particle draw has no direct #define
// equivalent in the reference (which scatter-writes per screen pixel).
constexpr int kVisualizerNcsParticleGridSize = 200;
constexpr float kVisualizerNcsPointSize = 6.0f;

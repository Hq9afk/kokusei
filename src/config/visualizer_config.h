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

// Ported from ~/references/ncs4au/src/NCS4AU.obj (an AviUtl custom-object
// Lua script: a flat grid of points, displaced by 3D Perlin noise, projected
// onto a sphere with the back hemisphere folded to face the camera). Color
// re-derived from local/THUMBNAIL.png's gold/yellow target look, replacing
// the previous cyan/dark-blue pair sourced from wayves's
// starter-configs/ncs.glsl (a different, now-disregarded reference).
inline constexpr Color kVisualizerSphereColor = {1.0f, 0.8353f, 0.0784f, 1.0f};
inline constexpr Color kVisualizerSphereGlowColor = {1.0f, 0.6667f, 0.0392f,
                                                     1.0f};
// Per-particle effect strength at full window opacity; multiplied by the
// window's fade-in opacity, not a replacement for it.
constexpr float kVisualizerSphereParticleStrength = 0.05f;
constexpr float kVisualizerSphereColorIntensityAddStrength = 0.38f;
constexpr float kVisualizerSphereGlowSize = 18.0f;
constexpr float kVisualizerSphereGlowIntensity = 1.0f;
// The glow ping-pong pass runs at this fraction of output resolution; its
// 96-tap blur is the dominant per-frame GPU cost, cheap enough at half res
// to not stall kokusei's single poll loop, and GL_LINEAR upscales it for
// free in the composite pass.
constexpr float kVisualizerSphereGlowScale = 0.5f;
constexpr float kVisualizerSphereGlowDirections = 16.0f;
constexpr float kVisualizerSphereGlowQuality = 6.0f;

// Ratio of window width; multiplied by the window's pixel width at upload
// time so the effect scales if the visualizer window is resized. ncs4au's
// flat field is 800x800 AviUtl units against its own canvas; this keeps the
// same "field roughly spans the window" proportion.
constexpr float kVisualizerSphereFieldSizeRatio = 1.0f;
// Ratio of window width. ncs4au's default noiseAmplitude (obj.track1) is
// 64 against an 800-wide field, ~0.08 of it.
constexpr float kVisualizerSphereNoiseAmplitude = 0.08f;
// Unitless; multiplies the [-0.5, 0.5]-normalized grid position before
// sampling noise, independent of window size.
constexpr float kVisualizerSphereNoiseFrequency = 3.2f;
// Ratio of window width; added to the noise amplitude, scaled by the
// spectrum's bass-band level (see sphere_particle.h's bassLevel()).
constexpr float kVisualizerSphereNoiseAudioMultiplier = 0.05f;
// Per-axis noise flow speed (unitless, multiplies u_time). ncs4au only
// animates X/Y by default (noiseFlowSpeed track) and leaves Z static unless
// an AviUtl user keyframes it; this port drives Z too, at a slower rate,
// since it runs continuously rather than as an edited video clip.
constexpr float kVisualizerSphereFlowX = 0.06f;
constexpr float kVisualizerSphereFlowY = 0.12f;
constexpr float kVisualizerSphereFlowZ = 0.03f;
// Ratio of window width. ncs4au's default sphereRadius (obj.track0) is 600
// against an 800-wide field, ~0.375 of it, halved in the script's own
// SphereProjector.new() call (sphereRadius * 0.5).
constexpr float kVisualizerSphereRadius = 0.1875f;
// Ratio of window width; added to the sphere radius, scaled by the
// spectrum's bass-band level. See kVisualizerSphereNoiseAudioMultiplier.
constexpr float kVisualizerSphereRadiusAudioMultiplier = 0.05f;
// Lerp factor between the flat noise-displaced grid and its sphere
// projection; ncs4au defaults isSpherical/projectionFactor to fully on.
constexpr float kVisualizerSphereProjectionFactor = 1.0f;

// New for this port: point-sprite particle draw has no direct #define
// equivalent in the reference (which draws one obj.load/obj.draw circle per
// grid point). ncs4au's own default resolution is 64 (4096 points); kept
// higher here for a denser silhouette under this port's additive glow.
constexpr int kVisualizerSphereParticleGridSize = 96;
constexpr float kVisualizerSpherePointSize = 5.0f;

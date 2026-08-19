#pragma once

constexpr const char *kSphereFullscreenVs = R"(
attribute vec2 a_pos;
varying vec2 v_uv;
void main() {
    v_uv = a_pos;
    gl_Position = vec4(a_pos * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char *kSphereGlowFs = R"(
precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_prev;
uniform sampler2D u_current;
uniform vec2 u_screen;
uniform vec3 u_glow_color;
uniform float u_glow_size;
uniform float u_glow_intensity;
uniform float u_particle_strength;
uniform float u_color_intensity_add_strength;

const float kTwoPi = 6.28318530718;
const float kGlowDirections = 16.0;
const float kGlowQuality = 6.0;

float glowDecay(float value, float strength, float dist) {
    return dist + dist / pow(max(value, 0.0001), strength);
}

void main() {
    vec2 radius = u_glow_size / u_screen;
    vec4 color = vec4(0.0);
    vec4 prevColor = texture2D(u_prev, v_uv);

    for (float d = 0.0; d < kTwoPi; d += kTwoPi / kGlowDirections) {
        for (float i = 1.0 / kGlowQuality; i <= 1.0; i += 1.0 / kGlowQuality) {
            vec2 coords = v_uv + radius * i * vec2(cos(d), sin(d));
            if (coords.x > 0.0 && coords.x < 1.0 && coords.y > 0.0 && coords.y < 1.0)
                color += texture2D(u_prev, coords);
        }
    }
    color /= (kGlowQuality * kGlowDirections);

    vec3 outc = u_glow_color * u_glow_intensity * color.a;
    if (prevColor.a != 0.0)
        outc += prevColor.rgb;
    outc *= glowDecay(length(outc), 0.5, 0.92);

    vec4 currentRaw = texture2D(u_current, v_uv);
    float overlap = currentRaw.a / max(u_particle_strength, 0.0001);
    float intensity = max(0.0, pow(overlap, u_color_intensity_add_strength) -
                                   u_color_intensity_add_strength) *
                       (1.0 - pow(1.0 - u_particle_strength, overlap));
    vec4 currentColor = vec4(u_glow_color * intensity, intensity);

    outc += currentColor.rgb;
    float outa = max(mix(color.a, prevColor.a, 0.5), currentColor.a);

    gl_FragColor = vec4(outc, outa);
}
)";

constexpr const char *kSphereCompositeFs = R"(
precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_opacity;
void main() {
    vec4 c = texture2D(u_tex, v_uv);
    gl_FragColor = vec4(c.rgb, c.a) * u_opacity;
}
)";

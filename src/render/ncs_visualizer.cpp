#include "render/ncs_visualizer.h"

#include "config/visualizer_config.h"
#include "render/gl.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

// Vertex shader: ports the particle-displacement math from
// ncs-spectrum-glava's ncs/1.frag (cnoise/fbm3/setAudio + the pull-to-circle
// logic), operating on the particle's own grid seed instead of gl_FragCoord,
// and driving stereo reactivity by mixing audio_l/audio_r per the particle's
// horizontal position instead of the reference's audio_r-only read. The
// per-pixel scatter loop that wrote into an atomic depth image is dropped
// entirely: each particle is a real vertex and draws its own point sprite.
constexpr const char *kParticleVs = R"(
attribute vec2 a_seed;

uniform vec2 u_screen;
uniform float u_time;
uniform sampler2D u_audio_l;
uniform sampler2D u_audio_r;
uniform float u_point_size;

uniform float u_radius_audio_multiplier;
uniform float u_fractal_audio_mixing;
uniform float u_fractal_audio_multiplier;
uniform float u_octave_multiplier;
uniform float u_octave_scale;
uniform float u_fscale;
uniform float u_gamma;
uniform float u_min_val;
uniform float u_max_val;
uniform float u_displace_x;
uniform float u_displace_y;
uniform float u_displace_z;
uniform float u_flow_x;
uniform float u_flow_y;
uniform float u_flow_z;
uniform float u_flow_evolution;
uniform float u_sphere_radius;
uniform float u_feather;

const int kComplexity = 3;

float gFinalAudio = 0.0;

vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289(((x * 34.0) + 1.0) * x); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }
vec4 fadeCurve(vec4 t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

float cnoise(vec4 P, vec4 rep) {
    vec4 Pi0 = mod(floor(P), rep);
    vec4 Pi1 = mod(Pi0 + 1.0, rep);
    vec4 Pf0 = fract(P);
    vec4 Pf1 = Pf0 - 1.0;
    vec4 ix = vec4(Pi0.x, Pi1.x, Pi0.x, Pi1.x);
    vec4 iy = vec4(Pi0.yy, Pi1.yy);
    vec4 iz0 = vec4(Pi0.zzzz);
    vec4 iz1 = vec4(Pi1.zzzz);
    vec4 iw0 = vec4(Pi0.wwww);
    vec4 iw1 = vec4(Pi1.wwww);

    vec4 ixy = permute(permute(ix) + iy);
    vec4 ixy0 = permute(ixy + iz0);
    vec4 ixy1 = permute(ixy + iz1);
    vec4 ixy00 = permute(ixy0 + iw0);
    vec4 ixy01 = permute(ixy0 + iw1);
    vec4 ixy10 = permute(ixy1 + iw0);
    vec4 ixy11 = permute(ixy1 + iw1);

    vec4 gx00 = ixy00 / 7.0;
    vec4 gy00 = floor(gx00) / 7.0;
    vec4 gz00 = floor(gy00) / 6.0;
    gx00 = fract(gx00) - 0.5;
    gy00 = fract(gy00) - 0.5;
    gz00 = fract(gz00) - 0.5;
    vec4 gw00 = vec4(0.75) - abs(gx00) - abs(gy00) - abs(gz00);
    vec4 sw00 = step(gw00, vec4(0.0));
    gx00 -= sw00 * (step(0.0, gx00) - 0.5);
    gy00 -= sw00 * (step(0.0, gy00) - 0.5);

    vec4 gx01 = ixy01 / 7.0;
    vec4 gy01 = floor(gx01) / 7.0;
    vec4 gz01 = floor(gy01) / 6.0;
    gx01 = fract(gx01) - 0.5;
    gy01 = fract(gy01) - 0.5;
    gz01 = fract(gz01) - 0.5;
    vec4 gw01 = vec4(0.75) - abs(gx01) - abs(gy01) - abs(gz01);
    vec4 sw01 = step(gw01, vec4(0.0));
    gx01 -= sw01 * (step(0.0, gx01) - 0.5);
    gy01 -= sw01 * (step(0.0, gy01) - 0.5);

    vec4 gx10 = ixy10 / 7.0;
    vec4 gy10 = floor(gx10) / 7.0;
    vec4 gz10 = floor(gy10) / 6.0;
    gx10 = fract(gx10) - 0.5;
    gy10 = fract(gy10) - 0.5;
    gz10 = fract(gz10) - 0.5;
    vec4 gw10 = vec4(0.75) - abs(gx10) - abs(gy10) - abs(gz10);
    vec4 sw10 = step(gw10, vec4(0.0));
    gx10 -= sw10 * (step(0.0, gx10) - 0.5);
    gy10 -= sw10 * (step(0.0, gy10) - 0.5);

    vec4 gx11 = ixy11 / 7.0;
    vec4 gy11 = floor(gx11) / 7.0;
    vec4 gz11 = floor(gy11) / 6.0;
    gx11 = fract(gx11) - 0.5;
    gy11 = fract(gy11) - 0.5;
    gz11 = fract(gz11) - 0.5;
    vec4 gw11 = vec4(0.75) - abs(gx11) - abs(gy11) - abs(gz11);
    vec4 sw11 = step(gw11, vec4(0.0));
    gx11 -= sw11 * (step(0.0, gx11) - 0.5);
    gy11 -= sw11 * (step(0.0, gy11) - 0.5);

    vec4 g0000 = vec4(gx00.x, gy00.x, gz00.x, gw00.x);
    vec4 g1000 = vec4(gx00.y, gy00.y, gz00.y, gw00.y);
    vec4 g0100 = vec4(gx00.z, gy00.z, gz00.z, gw00.z);
    vec4 g1100 = vec4(gx00.w, gy00.w, gz00.w, gw00.w);
    vec4 g0010 = vec4(gx10.x, gy10.x, gz10.x, gw10.x);
    vec4 g1010 = vec4(gx10.y, gy10.y, gz10.y, gw10.y);
    vec4 g0110 = vec4(gx10.z, gy10.z, gz10.z, gw10.z);
    vec4 g1110 = vec4(gx10.w, gy10.w, gz10.w, gw10.w);
    vec4 g0001 = vec4(gx01.x, gy01.x, gz01.x, gw01.x);
    vec4 g1001 = vec4(gx01.y, gy01.y, gz01.y, gw01.y);
    vec4 g0101 = vec4(gx01.z, gy01.z, gz01.z, gw01.z);
    vec4 g1101 = vec4(gx01.w, gy01.w, gz01.w, gw01.w);
    vec4 g0011 = vec4(gx11.x, gy11.x, gz11.x, gw11.x);
    vec4 g1011 = vec4(gx11.y, gy11.y, gz11.y, gw11.y);
    vec4 g0111 = vec4(gx11.z, gy11.z, gz11.z, gw11.z);
    vec4 g1111 = vec4(gx11.w, gy11.w, gz11.w, gw11.w);

    vec4 norm00 = taylorInvSqrt(vec4(dot(g0000, g0000), dot(g0100, g0100), dot(g1000, g1000), dot(g1100, g1100)));
    g0000 *= norm00.x; g0100 *= norm00.y; g1000 *= norm00.z; g1100 *= norm00.w;

    vec4 norm01 = taylorInvSqrt(vec4(dot(g0001, g0001), dot(g0101, g0101), dot(g1001, g1001), dot(g1101, g1101)));
    g0001 *= norm01.x; g0101 *= norm01.y; g1001 *= norm01.z; g1101 *= norm01.w;

    vec4 norm10 = taylorInvSqrt(vec4(dot(g0010, g0010), dot(g0110, g0110), dot(g1010, g1010), dot(g1110, g1110)));
    g0010 *= norm10.x; g0110 *= norm10.y; g1010 *= norm10.z; g1110 *= norm10.w;

    vec4 norm11 = taylorInvSqrt(vec4(dot(g0011, g0011), dot(g0111, g0111), dot(g1011, g1011), dot(g1111, g1111)));
    g0011 *= norm11.x; g0111 *= norm11.y; g1011 *= norm11.z; g1111 *= norm11.w;

    float n0000 = dot(g0000, Pf0);
    float n1000 = dot(g1000, vec4(Pf1.x, Pf0.yzw));
    float n0100 = dot(g0100, vec4(Pf0.x, Pf1.y, Pf0.zw));
    float n1100 = dot(g1100, vec4(Pf1.xy, Pf0.zw));
    float n0010 = dot(g0010, vec4(Pf0.xy, Pf1.z, Pf0.w));
    float n1010 = dot(g1010, vec4(Pf1.x, Pf0.y, Pf1.z, Pf0.w));
    float n0110 = dot(g0110, vec4(Pf0.x, Pf1.yz, Pf0.w));
    float n1110 = dot(g1110, vec4(Pf1.xyz, Pf0.w));
    float n0001 = dot(g0001, vec4(Pf0.xyz, Pf1.w));
    float n1001 = dot(g1001, vec4(Pf1.x, Pf0.yz, Pf1.w));
    float n0101 = dot(g0101, vec4(Pf0.x, Pf1.y, Pf0.z, Pf1.w));
    float n1101 = dot(g1101, vec4(Pf1.xy, Pf0.z, Pf1.w));
    float n0011 = dot(g0011, vec4(Pf0.xy, Pf1.zw));
    float n1011 = dot(g1011, vec4(Pf1.x, Pf0.y, Pf1.zw));
    float n0111 = dot(g0111, vec4(Pf0.x, Pf1.yzw));
    float n1111 = dot(g1111, Pf1);

    vec4 fade_xyzw = fadeCurve(Pf0);
    vec4 n_0w = mix(vec4(n0000, n1000, n0100, n1100), vec4(n0001, n1001, n0101, n1101), fade_xyzw.w);
    vec4 n_1w = mix(vec4(n0010, n1010, n0110, n1110), vec4(n0011, n1011, n0111, n1111), fade_xyzw.w);
    vec4 n_zw = mix(n_0w, n_1w, fade_xyzw.z);
    vec2 n_yzw = mix(n_zw.xy, n_zw.zw, fade_xyzw.y);
    float n_xyzw = mix(n_yzw.x, n_yzw.y, fade_xyzw.x);
    return 2.2 * n_xyzw;
}

float octaveNoise(vec4 p, vec4 flow) {
    float total = 0.0;
    float frequency = 1.0;
    float amplitude = 1.0;
    float value = 0.0;
    for (int i = 0; i < kComplexity; i += 1) {
        value += cnoise((p + flow * u_time) * frequency, vec4(0.0)) * amplitude;
        total += amplitude;
        amplitude *= u_octave_multiplier;
        frequency *= u_octave_scale;
    }
    return value / total;
}

float fbm3(vec4 p, float disp, vec4 flow) {
    vec4 centered = p - vec4(u_screen * 0.5, 0.0, 0.0);
    float oN = gFinalAudio * octaveNoise(u_fscale * centered / u_screen.x, flow);
    oN = clamp(oN, u_min_val, u_max_val);
    oN = oN >= 0.0 ? pow(oN, u_gamma) : -pow(-oN, u_gamma);
    return disp * oN;
}

float sampleAudio(sampler2D tex, float u) {
    return texture2D(tex, vec2(u, 0.5)).r;
}

float computeFinalAudio(sampler2D tex) {
    float audios[8];
    audios[0] = max(sampleAudio(tex, 0.20), sampleAudio(tex, 0.25));
    audios[1] = max(sampleAudio(tex, 0.30), sampleAudio(tex, 0.35));
    audios[2] = max(sampleAudio(tex, 0.40), sampleAudio(tex, 0.45));
    audios[3] = max(sampleAudio(tex, 0.50), sampleAudio(tex, 0.55));
    audios[4] = max(sampleAudio(tex, 0.60), sampleAudio(tex, 0.65));
    audios[5] = max(sampleAudio(tex, 0.70), sampleAudio(tex, 0.75));
    audios[6] = max(sampleAudio(tex, 0.80), sampleAudio(tex, 0.85));
    audios[7] = max(sampleAudio(tex, 0.90), sampleAudio(tex, 0.95));

    float temp;
    temp = max(audios[0], audios[2]); audios[0] = min(audios[0], audios[2]); audios[2] = temp;
    temp = max(audios[1], audios[3]); audios[1] = min(audios[1], audios[3]); audios[3] = temp;
    temp = max(audios[4], audios[6]); audios[4] = min(audios[4], audios[6]); audios[6] = temp;
    temp = max(audios[5], audios[7]); audios[5] = min(audios[5], audios[7]); audios[7] = temp;
    temp = max(audios[0], audios[4]); audios[0] = min(audios[0], audios[4]); audios[4] = temp;
    temp = max(audios[1], audios[5]); audios[1] = min(audios[1], audios[5]); audios[5] = temp;
    temp = max(audios[2], audios[6]); audios[2] = min(audios[2], audios[6]); audios[6] = temp;
    temp = max(audios[3], audios[7]); audios[3] = min(audios[3], audios[7]); audios[7] = temp;
    temp = max(audios[0], audios[1]); audios[0] = min(audios[0], audios[1]); audios[1] = temp;
    temp = max(audios[2], audios[3]); audios[2] = min(audios[2], audios[3]); audios[3] = temp;
    temp = max(audios[4], audios[5]); audios[4] = min(audios[4], audios[5]); audios[5] = temp;
    temp = max(audios[6], audios[7]); audios[6] = min(audios[6], audios[7]); audios[7] = temp;
    temp = max(audios[2], audios[4]); audios[2] = min(audios[2], audios[4]); audios[4] = temp;
    temp = max(audios[3], audios[5]); audios[3] = min(audios[3], audios[5]); audios[5] = temp;
    temp = max(audios[1], audios[4]); audios[1] = min(audios[1], audios[4]); audios[4] = temp;
    temp = max(audios[3], audios[6]); audios[3] = min(audios[3], audios[6]); audios[6] = temp;
    temp = max(audios[1], audios[2]); audios[1] = min(audios[1], audios[2]); audios[2] = temp;
    temp = max(audios[3], audios[4]); audios[3] = min(audios[3], audios[4]); audios[4] = temp;
    temp = max(audios[5], audios[6]); audios[5] = min(audios[5], audios[6]); audios[6] = temp;

    return u_fractal_audio_multiplier * mix(
        mix(audios[7] * audios[6] - audios[1] * audios[0], audios[7] * audios[6], audios[5]),
        mix(audios[6] * mix(audios[7] - audios[0], audios[6] - audios[3], audios[7] * audios[6]) -
                pow(audios[1] * audios[0], 1.05),
            audios[7] * audios[6], audios[5] * audios[4]),
        u_fractal_audio_mixing);
}

void main() {
    float audioRadiusL = max(sampleAudio(u_audio_l, 0.10), sampleAudio(u_audio_l, 0.15));
    float audioRadiusR = max(sampleAudio(u_audio_r, 0.10), sampleAudio(u_audio_r, 0.15));
    float audioRadius = mix(audioRadiusL, audioRadiusR, a_seed.x);

    gFinalAudio = mix(computeFinalAudio(u_audio_l), computeFinalAudio(u_audio_r), a_seed.x);

    vec3 particleCoords = vec3(a_seed * u_screen, 0.0);
    vec4 old = vec4(particleCoords, 0.0);

    particleCoords.xyz += vec3(
        fbm3(old.xyzw, u_displace_x, vec4(u_flow_x, u_flow_y, u_flow_z, u_flow_evolution)),
        fbm3(old.yzxw, u_displace_y, vec4(u_flow_y, u_flow_z, u_flow_x, u_flow_evolution)),
        fbm3(old.zxyw, u_displace_z, vec4(u_flow_z, u_flow_x, u_flow_y, u_flow_evolution)));

    float radius = min(u_sphere_radius + u_radius_audio_multiplier * abs(audioRadius), u_screen.x);
    vec3 centerCoords = vec3(u_screen * 0.5, 0.0);
    vec3 fromCenter = particleCoords - centerCoords;
    float distFromCenter = length(fromCenter);
    if (distFromCenter <= radius && distFromCenter > 0.0001) {
        vec3 newPos = centerCoords + radius * normalize(fromCenter);
        float diff = length(newPos - particleCoords);
        diff *= clamp(smoothstep(0.0, u_feather * radius, diff), 0.0, 1.0);
        particleCoords += diff * normalize(fromCenter);
    }

    vec2 displayCoords = (particleCoords.xy + centerCoords.xy) * 0.5;
    vec2 ndc = vec2(displayCoords.x / u_screen.x * 2.0 - 1.0,
                    1.0 - displayCoords.y / u_screen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    gl_PointSize = u_point_size;
}
)";

// Fragment shader: soft round point sprite, drawn additively so overlapping
// particles brighten (the point-sprite analog of the reference's
// atomic-depth-count resolve in 2.frag).
constexpr const char *kParticleFs = R"(
precision mediump float;
uniform vec3 u_color;
uniform float u_opacity;
void main() {
    vec2 d = gl_PointCoord - vec2(0.5);
    float dist = length(d) * 2.0;
    float alpha = (1.0 - smoothstep(0.0, 1.0, dist)) * u_opacity;
    gl_FragColor = vec4(u_color * alpha, alpha);
}
)";

constexpr const char *kFullscreenVs = R"(
attribute vec2 a_pos;
varying vec2 v_uv;
void main() {
    v_uv = a_pos;
    gl_Position = vec4(a_pos * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Ports ncs/3.frag's directional multi-tap blur + temporal feedback, plus
// resolves this frame's fresh accumulation texture through ncs/2.frag's
// depth-compression curve (colorIntensityAddStrength): accum_tex.a is this
// pixel's summed particle-overlap "depth" (the additive-blend analog of
// 2.frag's atomicAdd'd depthImage), divided by u_particle_strength to
// approximate an overlap count, then the same pow/geometric-opacity formula
// suppresses low-overlap pixels near zero so only pixels where the
// sphere-pull math actually converges many particles (the ring) light up.
constexpr const char *kGlowFs = R"(
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

constexpr const char *kCompositeFs = R"(
precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_opacity;
void main() {
    vec4 c = texture2D(u_tex, v_uv);
    gl_FragColor = vec4(c.rgb, c.a) * u_opacity;
}
)";

GLuint make_fbo_texture(int width, int height) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    return tex;
}

GLuint make_fbo(GLuint tex) {
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           tex, 0);
    return fbo;
}

GLuint make_audio_texture() {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, kVisualizerBarCount, 1, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);
    return tex;
}

void upload_audio_texture(GLuint tex, const std::vector<float> &spectrum) {
    static thread_local std::vector<uint8_t> bytes;
    bytes.resize(kVisualizerBarCount);
    for (int i = 0; i < kVisualizerBarCount; ++i) {
        float v = i < static_cast<int>(spectrum.size())
                      ? spectrum[static_cast<size_t>(i)]
                      : 0.0f;
        bytes[static_cast<size_t>(i)] =
            static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kVisualizerBarCount, 1,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE, bytes.data());
}

void build_particle_grid(NcsVisualizerState &state) {
    constexpr int n = kVisualizerNcsParticleGridSize;
    std::vector<float> seeds;
    seeds.reserve(static_cast<size_t>(n) * static_cast<size_t>(n) * 2);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            seeds.push_back((static_cast<float>(x) + 0.5f) /
                            static_cast<float>(n));
            seeds.push_back((static_cast<float>(y) + 0.5f) /
                            static_cast<float>(n));
        }
    }
    state.particle_count = n * n;
    glGenBuffers(1, &state.particle_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, state.particle_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(seeds.size() * sizeof(float)),
                 seeds.data(), GL_STATIC_DRAW);
}

void resize_fbos(NcsVisualizerState &state, int px_width, int px_height) {
    if (state.accum_tex)
        glDeleteTextures(1, &state.accum_tex);
    if (state.accum_fbo)
        glDeleteFramebuffers(1, &state.accum_fbo);
    for (int i = 0; i < 2; ++i) {
        if (state.ping_tex[i])
            glDeleteTextures(1, &state.ping_tex[i]);
        if (state.ping_fbo[i])
            glDeleteFramebuffers(1, &state.ping_fbo[i]);
    }

    int glow_width = std::max(1, static_cast<int>(static_cast<float>(px_width) *
                                                  kVisualizerNcsGlowScale));
    int glow_height =
        std::max(1, static_cast<int>(static_cast<float>(px_height) *
                                     kVisualizerNcsGlowScale));

    state.accum_tex = make_fbo_texture(px_width, px_height);
    state.accum_fbo = make_fbo(state.accum_tex);
    for (int i = 0; i < 2; ++i) {
        state.ping_tex[i] = make_fbo_texture(glow_width, glow_height);
        state.ping_fbo[i] = make_fbo(state.ping_tex[i]);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    state.ping_read = 0;
    state.fbo_width = px_width;
    state.fbo_height = px_height;
    state.glow_width = glow_width;
    state.glow_height = glow_height;
}

bool ensure_ready(NcsVisualizerState &state) {
    if (state.ready)
        return true;
    state.particle_program = gl_compile_program(kParticleVs, kParticleFs);
    state.glow_program = gl_compile_program(kFullscreenVs, kGlowFs);
    state.composite_program = gl_compile_program(kFullscreenVs, kCompositeFs);
    if (!state.particle_program || !state.glow_program ||
        !state.composite_program)
        return false;
    build_particle_grid(state);
    state.audio_tex_l = make_audio_texture();
    state.audio_tex_r = make_audio_texture();
    static const float kQuad[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glGenBuffers(1, &state.quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, state.quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    state.ready = true;
    return true;
}

struct NcsFrameInput {
    int width = 0;
    int height = 0;
    int32_t scale = 1;
    float time_seconds = 0.0f;
    float opacity = 1.0f;
    const std::vector<float> &spectrum_l;
    const std::vector<float> &spectrum_r;
};

// Runs against whatever EGL surface/context the caller (the visualizer's
// dedicated render thread) already made current.
void render_frame(NcsVisualizerState &state, const NcsFrameInput &f) {
    if (f.width <= 0 || f.height <= 0 || !ensure_ready(state))
        return;

    int px_width = std::max(1, f.width * std::max(1, f.scale));
    int px_height = std::max(1, f.height * std::max(1, f.scale));
    if (px_width != state.fbo_width || px_height != state.fbo_height)
        resize_fbos(state, px_width, px_height);

    upload_audio_texture(state.audio_tex_l, f.spectrum_l);
    upload_audio_texture(state.audio_tex_r, f.spectrum_r);

    // --- Particle pass: additive splats into the accumulation FBO. ---
    glBindFramebuffer(GL_FRAMEBUFFER, state.accum_fbo);
    glViewport(0, 0, px_width, px_height);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlendFunc(GL_ONE, GL_ONE);

    glUseProgram(state.particle_program);
    float screen[2] = {static_cast<float>(f.width), static_cast<float>(f.height)};
    glUniform2fv(glGetUniformLocation(state.particle_program, "u_screen"), 1,
                 screen);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_time"),
                f.time_seconds);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_point_size"),
                kVisualizerNcsPointSize *
                    static_cast<float>(std::max(1, f.scale)));
    glUniform1f(glGetUniformLocation(state.particle_program,
                                     "u_radius_audio_multiplier"),
                kVisualizerNcsRadiusAudioMultiplier);
    glUniform1f(
        glGetUniformLocation(state.particle_program, "u_fractal_audio_mixing"),
        kVisualizerNcsFractalAudioMixing);
    glUniform1f(glGetUniformLocation(state.particle_program,
                                     "u_fractal_audio_multiplier"),
                kVisualizerNcsFractalAudioMultiplier);
    glUniform1f(
        glGetUniformLocation(state.particle_program, "u_octave_multiplier"),
        kVisualizerNcsOctaveMultiplier);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_octave_scale"),
                kVisualizerNcsOctaveScale);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_fscale"),
                kVisualizerNcsFScale);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_gamma"),
                kVisualizerNcsGamma);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_min_val"),
                kVisualizerNcsMinVal);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_max_val"),
                kVisualizerNcsMaxVal);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_displace_x"),
                kVisualizerNcsDisplaceX);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_displace_y"),
                kVisualizerNcsDisplaceY);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_displace_z"),
                kVisualizerNcsDisplaceZ);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_flow_x"),
                kVisualizerNcsFlowX);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_flow_y"),
                kVisualizerNcsFlowY);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_flow_z"),
                kVisualizerNcsFlowZ);
    glUniform1f(
        glGetUniformLocation(state.particle_program, "u_flow_evolution"),
        kVisualizerNcsFlowEvolution);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_sphere_radius"),
                kVisualizerNcsSphereRadius);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_feather"),
                kVisualizerNcsFeather);
    glUniform3f(glGetUniformLocation(state.particle_program, "u_color"),
                kVisualizerNcsColor.r, kVisualizerNcsColor.g,
                kVisualizerNcsColor.b);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_opacity"),
                kVisualizerNcsParticleStrength * f.opacity);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, state.audio_tex_l);
    glUniform1i(glGetUniformLocation(state.particle_program, "u_audio_l"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, state.audio_tex_r);
    glUniform1i(glGetUniformLocation(state.particle_program, "u_audio_r"), 1);

    glBindBuffer(GL_ARRAY_BUFFER, state.particle_vbo);
    GLint seed_loc = glGetAttribLocation(state.particle_program, "a_seed");
    glEnableVertexAttribArray(seed_loc);
    glVertexAttribPointer(seed_loc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_POINTS, 0, state.particle_count);
    glDisableVertexAttribArray(seed_loc);

    // --- Glow pass: blur+feedback of last frame, plus this frame's splats,
    // written into the other ping-pong texture, at reduced resolution since
    // the 96-tap blur is the dominant per-frame cost. ---
    int write_idx = 1 - state.ping_read;
    glBindFramebuffer(GL_FRAMEBUFFER, state.ping_fbo[write_idx]);
    glViewport(0, 0, state.glow_width, state.glow_height);
    glBlendFunc(GL_ONE, GL_ZERO);

    glUseProgram(state.glow_program);
    glUniform2fv(glGetUniformLocation(state.glow_program, "u_screen"), 1,
                 screen);
    glUniform3f(glGetUniformLocation(state.glow_program, "u_glow_color"),
                kVisualizerNcsColor.r, kVisualizerNcsColor.g,
                kVisualizerNcsColor.b);
    glUniform1f(glGetUniformLocation(state.glow_program, "u_glow_size"),
                kVisualizerNcsGlowSize);
    glUniform1f(glGetUniformLocation(state.glow_program, "u_glow_intensity"),
                kVisualizerNcsGlowIntensity);
    glUniform1f(
        glGetUniformLocation(state.glow_program, "u_particle_strength"),
        kVisualizerNcsParticleStrength);
    glUniform1f(glGetUniformLocation(state.glow_program,
                                     "u_color_intensity_add_strength"),
                kVisualizerNcsColorIntensityAddStrength);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, state.ping_tex[state.ping_read]);
    glUniform1i(glGetUniformLocation(state.glow_program, "u_prev"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, state.accum_tex);
    glUniform1i(glGetUniformLocation(state.glow_program, "u_current"), 1);

    glBindBuffer(GL_ARRAY_BUFFER, state.quad_vbo);
    GLint pos_loc = glGetAttribLocation(state.glow_program, "a_pos");
    glEnableVertexAttribArray(pos_loc);
    glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(pos_loc);

    state.ping_read = write_idx;

    // --- Composite: clear the real target to the visualizer's window
    // background, scaled by this frame's fade opacity so it fades in at the
    // same rate as the bars shape's background, then draw the resulting
    // glow texture on top. ---
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, px_width, px_height);
    glDisable(GL_BLEND);
    glClearColor(kVisualizerWindowBackground.r, kVisualizerWindowBackground.g,
                kVisualizerWindowBackground.b,
                kVisualizerWindowBackground.a * f.opacity);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(state.composite_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, state.ping_tex[state.ping_read]);
    glUniform1i(glGetUniformLocation(state.composite_program, "u_tex"), 0);
    glUniform1f(glGetUniformLocation(state.composite_program, "u_opacity"),
                f.opacity);

    glBindBuffer(GL_ARRAY_BUFFER, state.quad_vbo);
    pos_loc = glGetAttribLocation(state.composite_program, "a_pos");
    glEnableVertexAttribArray(pos_loc);
    glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(pos_loc);
}

void destroy_gl_objects(NcsVisualizerState &state) {
    if (state.particle_vbo)
        glDeleteBuffers(1, &state.particle_vbo);
    if (state.quad_vbo)
        glDeleteBuffers(1, &state.quad_vbo);
    if (state.audio_tex_l)
        glDeleteTextures(1, &state.audio_tex_l);
    if (state.audio_tex_r)
        glDeleteTextures(1, &state.audio_tex_r);
    if (state.accum_tex)
        glDeleteTextures(1, &state.accum_tex);
    if (state.accum_fbo)
        glDeleteFramebuffers(1, &state.accum_fbo);
    for (int i = 0; i < 2; ++i) {
        if (state.ping_tex[i])
            glDeleteTextures(1, &state.ping_tex[i]);
        if (state.ping_fbo[i])
            glDeleteFramebuffers(1, &state.ping_fbo[i]);
    }
    if (state.particle_program)
        glDeleteProgram(state.particle_program);
    if (state.glow_program)
        glDeleteProgram(state.glow_program);
    if (state.composite_program)
        glDeleteProgram(state.composite_program);
}

} // namespace

void ncs_visualizer_render(NcsVisualizerState &state, int width, int height,
                           int32_t scale, float time_seconds, float opacity,
                           const std::vector<float> &spectrum_l,
                           const std::vector<float> &spectrum_r) {
    if (width <= 0 || height <= 0)
        return;
    render_frame(state, {width, height, scale, time_seconds, opacity,
                         spectrum_l, spectrum_r});
}

void ncs_visualizer_destroy_gl(NcsVisualizerState &state) {
    destroy_gl_objects(state);
    state = NcsVisualizerState{};
}

#include <algorithm>
#include <cstdint>
#include <vector>

#include "config/visualizer_config.h"

#include "render/gl.h"

#include "shaders/sphere_particle.h"
#include "shaders/sphere_post.h"

#include "visualizer/sphere_visualizer.h"

namespace {

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

void build_particle_grid(SphereVisualizerState &state) {
    constexpr int n = kVisualizerSphereParticleGridSize;
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

void resize_fbos(SphereVisualizerState &state, int px_width, int px_height) {
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
                                                  kVisualizerSphereGlowScale));
    int glow_height =
        std::max(1, static_cast<int>(static_cast<float>(px_height) *
                                     kVisualizerSphereGlowScale));

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

bool ensure_ready(SphereVisualizerState &state) {
    if (state.ready)
        return true;
    state.particle_program =
        gl_compile_program(kSphereParticleVs, kSphereParticleFs);
    state.glow_program = gl_compile_program(kSphereFullscreenVs, kSphereGlowFs);
    state.composite_program =
        gl_compile_program(kSphereFullscreenVs, kSphereCompositeFs);
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

struct SphereFrameInput {
    int width = 0;
    int height = 0;
    int32_t scale = 1;
    float time_seconds = 0.0f;
    float opacity = 1.0f;
    const std::vector<float> &spectrum_l;
    const std::vector<float> &spectrum_r;
};

void render_frame(SphereVisualizerState &state, const SphereFrameInput &f) {
    if (f.width <= 0 || f.height <= 0 || !ensure_ready(state))
        return;

    int px_width = std::max(1, f.width * std::max(1, f.scale));
    int px_height = std::max(1, f.height * std::max(1, f.scale));
    if (px_width != state.fbo_width || px_height != state.fbo_height)
        resize_fbos(state, px_width, px_height);

    upload_audio_texture(state.audio_tex_l, f.spectrum_l);
    upload_audio_texture(state.audio_tex_r, f.spectrum_r);

    glBindFramebuffer(GL_FRAMEBUFFER, state.accum_fbo);
    glViewport(0, 0, px_width, px_height);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlendFunc(GL_ONE, GL_ONE);

    glUseProgram(state.particle_program);
    float screen[2] = {static_cast<float>(f.width),
                       static_cast<float>(f.height)};
    glUniform2fv(glGetUniformLocation(state.particle_program, "u_screen"), 1,
                 screen);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_time"),
                f.time_seconds);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_point_size"),
                kVisualizerSpherePointSize *
                    static_cast<float>(std::max(1, f.scale)));
    glUniform1f(glGetUniformLocation(state.particle_program,
                                     "u_radius_audio_multiplier"),
                kVisualizerSphereRadiusAudioMultiplier * screen[0]);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_field_size"),
                kVisualizerSphereFieldSizeRatio * screen[0]);
    glUniform1f(
        glGetUniformLocation(state.particle_program, "u_noise_amplitude"),
        kVisualizerSphereNoiseAmplitude * screen[0]);
    glUniform1f(
        glGetUniformLocation(state.particle_program, "u_noise_frequency"),
        kVisualizerSphereNoiseFrequency);
    glUniform1f(glGetUniformLocation(state.particle_program,
                                     "u_noise_audio_multiplier"),
                kVisualizerSphereNoiseAudioMultiplier * screen[0]);
    glUniform3f(glGetUniformLocation(state.particle_program, "u_noise_flow"),
                kVisualizerSphereFlowX, kVisualizerSphereFlowY,
                kVisualizerSphereFlowZ);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_sphere_radius"),
                kVisualizerSphereRadius * screen[0]);
    glUniform1f(
        glGetUniformLocation(state.particle_program, "u_projection_factor"),
        kVisualizerSphereProjectionFactor);
    glUniform3f(glGetUniformLocation(state.particle_program, "u_color"),
                kVisualizerSphereColor.r, kVisualizerSphereColor.g,
                kVisualizerSphereColor.b);
    glUniform1f(glGetUniformLocation(state.particle_program, "u_opacity"),
                kVisualizerSphereParticleStrength * f.opacity);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, state.audio_tex_l);
    glUniform1i(glGetUniformLocation(state.particle_program, "u_audio_l"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, state.audio_tex_r);
    glUniform1i(glGetUniformLocation(state.particle_program, "u_audio_r"), 1);

    glBindBuffer(GL_ARRAY_BUFFER, state.particle_vbo);
    GLint grid_loc = glGetAttribLocation(state.particle_program, "a_grid");
    glEnableVertexAttribArray(grid_loc);
    glVertexAttribPointer(grid_loc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_POINTS, 0, state.particle_count);
    glDisableVertexAttribArray(grid_loc);

    int write_idx = 1 - state.ping_read;
    glBindFramebuffer(GL_FRAMEBUFFER, state.ping_fbo[write_idx]);
    glViewport(0, 0, state.glow_width, state.glow_height);
    glBlendFunc(GL_ONE, GL_ZERO);

    glUseProgram(state.glow_program);
    glUniform2fv(glGetUniformLocation(state.glow_program, "u_screen"), 1,
                 screen);
    glUniform3f(glGetUniformLocation(state.glow_program, "u_glow_color"),
                kVisualizerSphereGlowColor.r, kVisualizerSphereGlowColor.g,
                kVisualizerSphereGlowColor.b);
    glUniform1f(glGetUniformLocation(state.glow_program, "u_glow_size"),
                kVisualizerSphereGlowSize);
    glUniform1f(glGetUniformLocation(state.glow_program, "u_glow_intensity"),
                kVisualizerSphereGlowIntensity);
    glUniform1f(glGetUniformLocation(state.glow_program, "u_particle_strength"),
                kVisualizerSphereParticleStrength);
    glUniform1f(glGetUniformLocation(state.glow_program,
                                     "u_color_intensity_add_strength"),
                kVisualizerSphereColorIntensityAddStrength);

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

void destroy_gl_objects(SphereVisualizerState &state) {
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

void sphere_visualizer_render(SphereVisualizerState &state, int width,
                              int height, int32_t scale, float time_seconds,
                              float opacity,
                              const std::vector<float> &spectrum_l,
                              const std::vector<float> &spectrum_r) {
    if (width <= 0 || height <= 0)
        return;
    render_frame(state, {width, height, scale, time_seconds, opacity,
                         spectrum_l, spectrum_r});
}

void sphere_visualizer_destroy_gl(SphereVisualizerState &state) {
    destroy_gl_objects(state);
    state = SphereVisualizerState{};
}

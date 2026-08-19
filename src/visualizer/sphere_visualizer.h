#pragma once

#include <GLES2/gl2.h>
#include <cstdint>
#include <vector>

struct SphereVisualizerState {
    GLuint particle_program = 0;
    GLuint glow_program = 0;
    GLuint composite_program = 0;

    GLuint particle_vbo = 0;
    int particle_count = 0;
    GLuint quad_vbo = 0;

    GLuint audio_tex_l = 0;
    GLuint audio_tex_r = 0;

    GLuint accum_fbo = 0;
    GLuint accum_tex = 0;

    GLuint ping_fbo[2] = {0, 0};
    GLuint ping_tex[2] = {0, 0};
    int ping_read = 0;

    int fbo_width = 0;
    int fbo_height = 0;
    int glow_width = 0;
    int glow_height = 0;

    bool ready = false;
};

void sphere_visualizer_render(SphereVisualizerState &state, int width,
                              int height, int32_t scale, float time_seconds,
                              float opacity,
                              const std::vector<float> &spectrum_l,
                              const std::vector<float> &spectrum_r);

void sphere_visualizer_destroy_gl(SphereVisualizerState &state);

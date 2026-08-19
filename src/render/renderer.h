#pragma once

#include <GLES2/gl2.h>
#include <cstdint>
#include <vector>

#include "render/texture.h"

class Renderer {
  public:
    bool init();

    void destroy();

    void begin_frame(int logical_width, int logical_height, int32_t scale = 1);

    void set_clip(float x, float y, float w, float h);

    int32_t scale() const { return scale_; }

    void set_opacity(float a) { opacity_ = a; }

    void clear_clip();

    void draw_rect(float x, float y, float w, float h, const float color[4]);

    void draw_rounded_rect(float x, float y, float w, float h, float radius,
                           float border_width, const float fill[4],
                           const float border[4]);

    void draw_texture(float x, float y, const Texture &tex,
                      const float tint[4]);

    void draw_texture_rect(float x, float y, float w, float h,
                           const Texture &tex, const float tint[4]);

  private:
    struct ClipRect {
        float x0, y0, x1, y1;
    };

    void apply_clip(const ClipRect &r);

    void set_common_uniforms(GLuint program, float x, float y, float w,
                             float h);

    void draw_quad(GLuint program);

    GLuint rect_program_ = 0;
    GLuint tex_program_ = 0;
    GLuint rrect_program_ = 0;
    GLuint quad_vbo_ = 0;
    float viewport_[2] = {0, 0};
    int32_t scale_ = 1;
    float opacity_ = 1.0f;
    std::vector<ClipRect> clip_stack_;
};

class ScopedClip {
  public:
    ScopedClip(Renderer &renderer, float x, float y, float w, float h)
        : renderer_(renderer) {
        renderer_.set_clip(x, y, w, h);
    }
    ~ScopedClip() { renderer_.clear_clip(); }
    ScopedClip(const ScopedClip &) = delete;
    ScopedClip &operator=(const ScopedClip &) = delete;

  private:
    Renderer &renderer_;
};

#pragma once

#include "gl.hpp"
#include "texture.hpp"
#include <GLES2/gl2.h>
#include <algorithm>
#include <cmath>
#include <vector>

class Renderer {
  public:
    bool init() {
        static const char *quad_vs = R"(
            attribute vec2 a_pos;
            uniform vec2 u_viewport;
            uniform vec4 u_rect; // x, y, w, h in pixels
            varying vec2 v_uv;
            void main() {
                v_uv = a_pos;
                vec2 px = u_rect.xy + a_pos * u_rect.zw;
                vec2 ndc = vec2(px.x / u_viewport.x * 2.0 - 1.0,
                                 1.0 - px.y / u_viewport.y * 2.0);
                gl_Position = vec4(ndc, 0.0, 1.0);
            }
        )";
        static const char *rect_fs = R"(
            precision mediump float;
            uniform vec4 u_color;
            void main() { gl_FragColor = u_color; }
        )";
        static const char *tex_fs = R"(
            precision mediump float;
            varying vec2 v_uv;
            uniform sampler2D u_tex;
            uniform vec4 u_color;
            void main() { gl_FragColor = texture2D(u_tex, v_uv) * u_color; }
        )";

        static const char *rrect_fs = R"(
            precision mediump float;
            varying vec2 v_uv;
            uniform vec2 u_size;
            uniform float u_radius;
            uniform float u_border_width;
            uniform vec4 u_fill_color;
            uniform vec4 u_border_color;
            void main() {
                vec2 half_size = u_size * 0.5;
                vec2 p = (v_uv - 0.5) * u_size;
                vec2 b = half_size - u_radius;
                float d = length(max(abs(p) - b, 0.0)) - u_radius;
                float alpha = 1.0 - smoothstep(-0.5, 0.5, d);
                vec4 color = d <= -u_border_width ? u_fill_color : u_border_color;
                gl_FragColor = color * alpha;
            }
        )";

        rect_program_ = gl_compile_program(quad_vs, rect_fs);
        tex_program_ = gl_compile_program(quad_vs, tex_fs);
        rrect_program_ = gl_compile_program(quad_vs, rrect_fs);
        if (!rect_program_ || !tex_program_ || !rrect_program_)
            return false;

        static const float quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
        glGenBuffers(1, &quad_vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        return true;
    }

    void begin_frame(int logical_width, int logical_height, int32_t scale = 1) {
        viewport_[0] = static_cast<float>(logical_width);
        viewport_[1] = static_cast<float>(logical_height);
        scale_ = scale > 0 ? scale : 1;
        glViewport(0, 0, logical_width * scale_, logical_height * scale_);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    void set_clip(float x, float y, float w, float h) {
        float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
        if (!clip_stack_.empty()) {
            const ClipRect &p = clip_stack_.back();
            x0 = std::max(x0, p.x0);
            y0 = std::max(y0, p.y0);
            x1 = std::min(x1, p.x1);
            y1 = std::min(y1, p.y1);
        }
        clip_stack_.push_back({x0, y0, x1, y1});
        apply_clip(clip_stack_.back());
    }

    int32_t scale() const { return scale_; }

    void set_opacity(float a) { opacity_ = a; }

    void clear_clip() {
        clip_stack_.pop_back();
        if (clip_stack_.empty())
            glDisable(GL_SCISSOR_TEST);
        else
            apply_clip(clip_stack_.back());
    }

    void draw_rect(float x, float y, float w, float h, const float color[4]) {
        glUseProgram(rect_program_);
        set_common_uniforms(rect_program_, x, y, w, h);
        float c[4] = {color[0], color[1], color[2], color[3] * opacity_};
        glUniform4fv(glGetUniformLocation(rect_program_, "u_color"), 1, c);
        draw_quad(rect_program_);
    }

    void draw_rounded_rect(float x, float y, float w, float h, float radius,
                           float border_width, const float fill[4],
                           const float border[4]) {
        glUseProgram(rrect_program_);
        set_common_uniforms(rrect_program_, x, y, w, h);
        float size[2] = {w, h};
        glUniform2fv(glGetUniformLocation(rrect_program_, "u_size"), 1, size);
        glUniform1f(glGetUniformLocation(rrect_program_, "u_radius"), radius);
        glUniform1f(glGetUniformLocation(rrect_program_, "u_border_width"),
                    border_width);
        float fc[4] = {fill[0], fill[1], fill[2], fill[3] * opacity_};
        float bc[4] = {border[0], border[1], border[2], border[3] * opacity_};
        glUniform4fv(glGetUniformLocation(rrect_program_, "u_fill_color"), 1,
                     fc);
        glUniform4fv(glGetUniformLocation(rrect_program_, "u_border_color"), 1,
                     bc);
        draw_quad(rrect_program_);
    }

    void draw_texture(float x, float y, const Texture &tex,
                      const float tint[4]) {
        float inv_scale =
            1.0f / static_cast<float>(tex.scale > 0 ? tex.scale : 1);
        draw_texture_rect(x, y, static_cast<float>(tex.width) * inv_scale,
                          static_cast<float>(tex.height) * inv_scale, tex,
                          tint);
    }

    void draw_texture_rect(float x, float y, float w, float h,
                           const Texture &tex, const float tint[4]) {
        x = std::round(x);
        y = std::round(y);
        glUseProgram(tex_program_);
        set_common_uniforms(tex_program_, x, y, w, h);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex.id);
        glUniform1i(glGetUniformLocation(tex_program_, "u_tex"), 0);
        float tc[4] = {tint[0], tint[1], tint[2], tint[3] * opacity_};
        glUniform4fv(glGetUniformLocation(tex_program_, "u_color"), 1, tc);
        draw_quad(tex_program_);
    }

  private:
    struct ClipRect {
        float x0, y0, x1, y1;
    };

    void apply_clip(const ClipRect &r) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(static_cast<GLint>(r.x0 * scale_),
                  static_cast<GLint>((viewport_[1] - r.y1) * scale_),
                  static_cast<GLsizei>(std::max(0.0f, r.x1 - r.x0) * scale_),
                  static_cast<GLsizei>(std::max(0.0f, r.y1 - r.y0) * scale_));
    }

    void set_common_uniforms(GLuint program, float x, float y, float w,
                             float h) {
        glUniform2fv(glGetUniformLocation(program, "u_viewport"), 1, viewport_);
        float rect[4] = {x, y, w, h};
        glUniform4fv(glGetUniformLocation(program, "u_rect"), 1, rect);
    }

    void draw_quad(GLuint program) {
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
        GLint loc = glGetAttribLocation(program, "a_pos");
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(loc);
    }

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

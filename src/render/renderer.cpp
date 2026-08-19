#include <algorithm>
#include <cmath>

#include "render/gl.h"
#include "render/renderer.h"

#include "shaders/renderer_shaders.h"

bool Renderer::init() {
    rect_program_ = gl_compile_program(kRendererQuadVs, kRendererRectFs);
    tex_program_ = gl_compile_program(kRendererQuadVs, kRendererTexFs);
    rrect_program_ = gl_compile_program(kRendererQuadVs, kRendererRrectFs);
    if (!rect_program_ || !tex_program_ || !rrect_program_)
        return false;

    static const float quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glGenBuffers(1, &quad_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    return true;
}

void Renderer::destroy() {
    if (rect_program_)
        glDeleteProgram(rect_program_);
    if (tex_program_)
        glDeleteProgram(tex_program_);
    if (rrect_program_)
        glDeleteProgram(rrect_program_);
    if (quad_vbo_)
        glDeleteBuffers(1, &quad_vbo_);
    *this = Renderer{};
}

void Renderer::begin_frame(int logical_width, int logical_height,
                           int32_t scale) {
    viewport_[0] = static_cast<float>(logical_width);
    viewport_[1] = static_cast<float>(logical_height);
    scale_ = scale > 0 ? scale : 1;
    glViewport(0, 0, logical_width * scale_, logical_height * scale_);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);
    opacity_ = 1.0f;
}

void Renderer::set_clip(float x, float y, float w, float h) {
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

void Renderer::clear_clip() {
    clip_stack_.pop_back();
    if (clip_stack_.empty())
        glDisable(GL_SCISSOR_TEST);
    else
        apply_clip(clip_stack_.back());
}

void Renderer::draw_rect(float x, float y, float w, float h,
                         const float color[4]) {
    glUseProgram(rect_program_);
    set_common_uniforms(rect_program_, x, y, w, h);
    float c[4] = {color[0], color[1], color[2], color[3] * opacity_};
    glUniform4fv(glGetUniformLocation(rect_program_, "u_color"), 1, c);
    draw_quad(rect_program_);
}

void Renderer::draw_rounded_rect(float x, float y, float w, float h,
                                 float radius, float border_width,
                                 const float fill[4], const float border[4]) {
    glUseProgram(rrect_program_);
    set_common_uniforms(rrect_program_, x, y, w, h);
    float size[2] = {w, h};
    glUniform2fv(glGetUniformLocation(rrect_program_, "u_size"), 1, size);
    glUniform1f(glGetUniformLocation(rrect_program_, "u_radius"), radius);
    glUniform1f(glGetUniformLocation(rrect_program_, "u_border_width"),
                border_width);
    float fc[4] = {fill[0], fill[1], fill[2], fill[3] * opacity_};
    float bc[4] = {border[0], border[1], border[2], border[3] * opacity_};
    glUniform4fv(glGetUniformLocation(rrect_program_, "u_fill_color"), 1, fc);
    glUniform4fv(glGetUniformLocation(rrect_program_, "u_border_color"), 1, bc);
    draw_quad(rrect_program_);
}

void Renderer::draw_texture(float x, float y, const Texture &tex,
                            const float tint[4]) {
    float inv_scale = 1.0f / static_cast<float>(tex.scale > 0 ? tex.scale : 1);
    draw_texture_rect(x, y, static_cast<float>(tex.width) * inv_scale,
                      static_cast<float>(tex.height) * inv_scale, tex, tint);
}

void Renderer::draw_texture_rect(float x, float y, float w, float h,
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

void Renderer::apply_clip(const ClipRect &r) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<GLint>(r.x0 * scale_),
              static_cast<GLint>((viewport_[1] - r.y1) * scale_),
              static_cast<GLsizei>(std::max(0.0f, r.x1 - r.x0) * scale_),
              static_cast<GLsizei>(std::max(0.0f, r.y1 - r.y0) * scale_));
}

void Renderer::set_common_uniforms(GLuint program, float x, float y, float w,
                                   float h) {
    glUniform2fv(glGetUniformLocation(program, "u_viewport"), 1, viewport_);
    float rect[4] = {x, y, w, h};
    glUniform4fv(glGetUniformLocation(program, "u_rect"), 1, rect);
}

void Renderer::draw_quad(GLuint program) {
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    GLint loc = glGetAttribLocation(program, "a_pos");
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(loc);
}

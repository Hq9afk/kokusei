#pragma once

#include "../core/log.hpp"
#include <GLES2/gl2.h>

inline GLuint gl_compile_program(const char *vs_src, const char *fs_src) {
    auto compile = [](GLenum type, const char *src) -> GLuint {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char info[512];
            glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
            klog("shader compile failed: %s", info);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };

    GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs)
        return 0;

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char info[512];
        glGetProgramInfoLog(program, sizeof(info), nullptr, info);
        klog("program link failed: %s", info);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}


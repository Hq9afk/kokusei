#pragma once

// The shared Renderer's four GLES2 programs (src/render/renderer.cpp),
// extracted verbatim from their previous inline R"(...)" literals.

constexpr const char *kRendererQuadVs = R"(
    attribute vec2 a_pos;
    uniform vec2 u_viewport;
    uniform vec4 u_rect;
    varying vec2 v_uv;
    void main() {
        v_uv = a_pos;
        vec2 px = u_rect.xy + a_pos * u_rect.zw;
        vec2 ndc = vec2(px.x / u_viewport.x * 2.0 - 1.0,
                         1.0 - px.y / u_viewport.y * 2.0);
        gl_Position = vec4(ndc, 0.0, 1.0);
    }
)";

constexpr const char *kRendererRectFs = R"(
    precision mediump float;
    uniform vec4 u_color;
    void main() { gl_FragColor = u_color; }
)";

constexpr const char *kRendererTexFs = R"(
    precision mediump float;
    varying vec2 v_uv;
    uniform sampler2D u_tex;
    uniform vec4 u_color;
    void main() { gl_FragColor = texture2D(u_tex, v_uv) * u_color; }
)";

constexpr const char *kRendererRrectFs = R"(
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

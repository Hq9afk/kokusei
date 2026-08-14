#pragma once

#include "render/overlay_panel.h"
#include "render/rect.h"
#include "render/renderer.h"
#include "render/scene.h"
#include "render/text.h"
#include "render/texture.h"
#include "service/keyboard.h"
#include "starward/starward_config.h"

#include <array>

struct StarwardState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    int selected_index = 0;
    bool opened_by_widget = false;
    wl_output *bound_output = nullptr;

    bool input_ready = false;

    float logo_scale = 0.0f;
    std::array<float, kStarwardButtonCount> button_expand{};

    std::array<float, kStarwardButtonCount> button_highlight_scale{};
    std::array<float, kStarwardButtonCount> button_highlight_border{};

    std::array<Texture, kStarwardButtonCount> glyph_tex{};

    Texture logo_tex;
};

RasterizedText rasterize_yujimai_glyph(const std::string &codepoint_utf8);

Rect starward_detail_button_rect(int index, float center_x, float center_y,
                                 float radius_fraction = 1.0f);

bool starward_create_surface(StarwardState &state, wl_compositor *compositor,
                             zwlr_layer_shell_v1 *layer_shell,
                             wl_output *output = nullptr);

bool starward_init_egl(StarwardState &state, Renderer &renderer,
                       EGLDisplay display, EGLConfig config,
                       EGLContext context);

void starward_retarget(StarwardState &state, wl_compositor *compositor,
                       zwlr_layer_shell_v1 *layer_shell, wl_display *display,
                       Renderer &renderer, EGLDisplay egl_display,
                       EGLConfig egl_config, EGLContext egl_context,
                       wl_output *target_output, const char *target_name);

void starward_request_frame(StarwardState &state);

void starward_toggle(StarwardState &state, bool by_widget = false);

void starward_execute(StarwardState &state, int index);

void starward_handle_key_event(StarwardState &state, const KeyEvent &event);

void starward_handle_click(StarwardState &state, double px, double py);

void starward_paint(StarwardState &state);

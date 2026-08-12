#pragma once

#include "../render/overlay_panel.h"
#include "../render/rect.h"
#include "../render/renderer.h"
#include "../render/scene.h"
#include "../render/text.h"
#include "../render/texture.h"
#include "../wayland/keyboard.h"
#include "logout_config.h"

#include <array>

struct LogoutState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    int selected_index = 0;
    bool opened_by_widget = false;

    bool input_ready = false;

    float logo_scale = 0.0f;
    std::array<float, kLogoutButtonCount> button_expand{};

    std::array<float, kLogoutButtonCount> button_highlight_scale{};
    std::array<float, kLogoutButtonCount> button_highlight_border{};

    std::array<Texture, kLogoutButtonCount> glyph_tex{};

    Texture logo_tex;
};

RasterizedText rasterize_yujimai_glyph(const std::string &codepoint_utf8);

Rect logout_detail_button_rect(int index, float center_x, float center_y,
                               float radius_fraction = 1.0f);

bool logout_create_surface(LogoutState &state, wl_compositor *compositor,
                           zwlr_layer_shell_v1 *layer_shell,
                           wl_output *output = nullptr);

bool logout_init_egl(LogoutState &state, Renderer &renderer,
                     EGLDisplay display, EGLConfig config,
                     EGLContext context);

void logout_request_frame(LogoutState &state);

void logout_toggle(LogoutState &state, bool by_widget = false);

void logout_execute(LogoutState &state, int index);

void logout_handle_key_event(LogoutState &state, const KeyEvent &event);

void logout_handle_click(LogoutState &state, double px, double py);

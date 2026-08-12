#pragma once

#include "../../../dbus/tray/tray_service.h"
#include "../../../render/overlay_panel.h"
#include "../../../render/panel_chrome.h"
#include "../../../render/rect.h"
#include "../../../render/renderer.h"
#include "../../../render/scene.h"
#include "../../../render/texture_cache.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <EGL/egl.h>
#include <cstdint>
#include <string>
#include <vector>
#include <wayland-client.h>

constexpr float kTrayMenuRowHeight = 32.0f;

struct TrayMenuState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    TextureCache tcache;

    Rect panel_rect;
    std::vector<PanelClickRegion> click_regions;

    std::string item_key;
    std::vector<int32_t> menu_path;
};

bool tray_menu_create_surface(TrayMenuState &state, wl_compositor *compositor,
                              zwlr_layer_shell_v1 *layer_shell,
                              wl_output *output = nullptr);

bool tray_menu_init_egl(TrayMenuState &state, Renderer &renderer,
                        TrayState &tray, EGLDisplay display, EGLConfig config,
                        EGLContext context);

void tray_menu_request_frame(TrayMenuState &state);

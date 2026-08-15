#pragma once

#include "app/ipc.h"
#include "config/controlcenter_config.h"
#include "render/overlay_panel.h"
#include "render/panel_chrome.h"
#include "render/renderer.h"
#include "render/scene.h"
#include "render/texture_cache.h"
#include "service/keyboard.h"
#include "service/volume_slider.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <EGL/egl.h>
#include <optional>
#include <vector>
#include <wayland-client.h>

struct WaylandState;

struct ControlCenterState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    TextureCache tcache;

    Rect panel_rect;
    std::vector<PanelClickRegion> click_regions;
    bool opened_by_widget = false;
    wl_output *bound_output = nullptr;
    std::optional<DraggedSlider> dragging;

    float pending_bar_height = 0.0f;
    float pending_bar_top_margin = 0.0f;
};

bool controlcenter_create_surface(ControlCenterState &state,
                                  wl_compositor *compositor,
                                  zwlr_layer_shell_v1 *layer_shell,
                                  wl_output *output = nullptr);

bool controlcenter_init_egl(ControlCenterState &state, Renderer &renderer,
                            WaylandState &app, EGLDisplay display,
                            EGLConfig config, EGLContext context);

void controlcenter_retarget(ControlCenterState &state,
                            wl_compositor *compositor,
                            zwlr_layer_shell_v1 *layer_shell,
                            wl_display *display, Renderer &renderer,
                            WaylandState &app, EGLDisplay egl_display,
                            EGLConfig egl_config, EGLContext egl_context,
                            wl_output *target_output, const char *target_name);

void controlcenter_request_frame(ControlCenterState &state, float bar_height,
                                 float bar_top_margin);

void controlcenter_toggle(ControlCenterState &state, bool by_widget = false);

std::vector<IpcHandler> controlcenter_ipc_handlers(WaylandState &state);

void controlcenter_handle_click(ControlCenterState &state, WaylandState &app,
                                double px, double py);

void controlcenter_handle_pointer_move(ControlCenterState &state,
                                       PipewireState &pw, double px);

void controlcenter_handle_key_event(ControlCenterState &state,
                                    const KeyEvent &event);

void controlcenter_paint(ControlCenterState &state, WaylandState &app,
                         float bar_height, float bar_top_margin);

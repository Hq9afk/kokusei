#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <wayland-client.h>

#include "app/ipc.h"

#include "config/overview_config.h"

#include "render/overlay_panel.h"
#include "render/rect.h"
#include "render/renderer.h"
#include "render/scene.h"

#include "service/keyboard.h"
#include "service/toplevel_export.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct WaylandState;

struct OverviewWindowTile {
    std::string address;
    Rect rect;
    int workspace_id = -1;
};

struct OverviewState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    ToplevelExportState capture;
    std::unordered_map<int, Texture> workspace_number_tex;
    bool opened_by_widget = false;
    wl_output *bound_output = nullptr;
    // Module::handle_pointer_release() takes no WaylandState&, unlike every
    // other pointer/key callback here; stashed at init_egl time so the drag-
    // release handler (which needs app.hypr) still has something to call.
    WaylandState *app_ptr = nullptr;

    int workspace_group = 0;
    int selected_workspace = -1;
    std::vector<OverviewWindowTile> tiles;

    bool dragging = false;
    std::string drag_address;
    int drag_from_workspace = -1;
    int drag_target_workspace = -1;
    double drag_pointer_x = 0.0;
    double drag_pointer_y = 0.0;
    double drag_offset_x = 0.0;
    double drag_offset_y = 0.0;

    int poll_tick = 0;
};

bool overview_create_surface(OverviewState &state, wl_compositor *compositor,
                             zwlr_layer_shell_v1 *layer_shell,
                             wl_output *output = nullptr);

bool overview_init_egl(OverviewState &state, Renderer &renderer,
                       EGLDisplay display, EGLConfig config,
                       EGLContext context);

void overview_retarget(OverviewState &state, wl_compositor *compositor,
                       zwlr_layer_shell_v1 *layer_shell, wl_display *display,
                       Renderer &renderer, EGLDisplay egl_display,
                       EGLConfig egl_config, EGLContext egl_context,
                       wl_output *target_output, const char *target_name);

void overview_request_frame(OverviewState &state);

void overview_toggle(OverviewState &state, WaylandState &app,
                     bool by_widget = false);

std::vector<IpcHandler> overview_ipc_handlers(OverviewState &overview,
                                              WaylandState &state);

void overview_handle_click(OverviewState &state, WaylandState &app, double px,
                           double py);

void overview_handle_pointer_move(OverviewState &state, WaylandState &app,
                                  double px, double py);

void overview_handle_pointer_release(OverviewState &state, WaylandState &app);

void overview_handle_key_event(OverviewState &state, WaylandState &app,
                               const KeyEvent &event);

void overview_paint(OverviewState &state, WaylandState &app);

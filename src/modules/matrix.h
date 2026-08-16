#pragma once

#include "app/ipc.h"
#include "config/matrix_config.h"
#include "render/matrix_grid.h"
#include "render/renderer.h"
#include "render/scene.h"
#include "render/toplevel_window.h"
#include "service/keyboard.h"

#include <chrono>
#include <vector>

struct WaylandState;

struct MatrixState {
    ToplevelWindowBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    MatrixGrid grid;
    int grid_width = 0;
    int grid_height = 0;
    std::chrono::steady_clock::time_point last_tick;
};

void matrix_request_frame(MatrixState &state);

void matrix_toggle(MatrixState &state, WaylandState &app);

void matrix_handle_key_event(MatrixState &state, WaylandState &app,
                             const KeyEvent &event);

std::vector<IpcHandler> matrix_ipc_handlers(MatrixState &matrix,
                                            WaylandState &state);

void matrix_paint(MatrixState &state);

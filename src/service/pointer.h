#pragma once

#include <linux/input-event-codes.h>
#include <vector>
#include <wayland-client.h>

#include "cursor-shape-v1-client-protocol.h"

struct PointerClick {
    wl_surface *surface;
    bool pressed;
    uint32_t button = BTN_LEFT;
    double x = 0, y = 0;
};

struct PointerScroll {
    wl_surface *surface;
    double dy;
};

struct PointerState {
    wl_pointer *pointer = nullptr;
    wl_surface *focused_surface = nullptr;
    double x = -1, y = -1;
    bool dirty = false;
    std::vector<PointerClick> pending_clicks;
    std::vector<PointerScroll> pending_scrolls;

    // Bound once from the registry global; outlives individual pointer
    // binds, so it is not cleared by pointer_release.
    wp_cursor_shape_manager_v1 *cursor_shape_manager = nullptr;
    wp_cursor_shape_device_v1 *cursor_shape_device = nullptr;
    uint32_t last_enter_serial = 0;
};

void pointer_bind(PointerState &state, wl_seat *seat);

void pointer_release(PointerState &state);

std::vector<PointerClick> pointer_drain_clicks(PointerState &state);

std::vector<PointerScroll> pointer_drain_scrolls(PointerState &state);

// No-op if cursor-shape-v1 isn't available from the compositor.
void pointer_set_cursor_shape(PointerState &state,
                              wp_cursor_shape_device_v1_shape shape);

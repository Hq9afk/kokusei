#pragma once

#include <linux/input-event-codes.h>
#include <wayland-client.h>

#include <vector>

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
};

void pointer_bind(PointerState &state, wl_seat *seat);

void pointer_release(PointerState &state);

std::vector<PointerClick> pointer_drain_clicks(PointerState &state);

std::vector<PointerScroll> pointer_drain_scrolls(PointerState &state);

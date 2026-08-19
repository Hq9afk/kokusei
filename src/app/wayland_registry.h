#pragma once

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct WaylandState;

extern const wl_registry_listener registry_listener;
extern const zwlr_layer_surface_v1_listener bar_layer_surface_listener;

bool bootstrap_egl(WaylandState &state);
bool renderer_bootstrap_init(WaylandState &state);

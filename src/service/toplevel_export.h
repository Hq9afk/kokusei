#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <wayland-client.h>

#include "hyprland-toplevel-export-v1-client-protocol.h"

#include "render/texture.h"

// Per-window hyprland-toplevel-export-v1 capture. One instance per captured
// window address; the wl_shm pool/buffer is kept and reused across captures
// as long as the compositor keeps offering the same format/size, matching
// the real window size once it stops changing.
struct ToplevelExportCapture {
    wl_shm *shm = nullptr;
    hyprland_toplevel_export_frame_v1 *frame = nullptr;
    wl_buffer *buffer = nullptr;
    void *shm_data = nullptr;
    size_t shm_size = 0;
    uint32_t buf_width = 0;
    uint32_t buf_height = 0;
    uint32_t buf_stride = 0;
    uint32_t buf_format = 0;
    uint32_t pending_width = 0;
    uint32_t pending_height = 0;
    uint32_t pending_stride = 0;
    uint32_t pending_format = 0;
    bool have_pending_shm_format = false;
    bool y_invert = false;
    bool in_flight = false;
    Texture tex;
    std::chrono::steady_clock::time_point last_capture{};
};

struct ToplevelExportState {
    std::unordered_map<std::string, ToplevelExportCapture> captures;
};

// Starts (or continues) a live capture for `address` (kokusei's HyprClient
// address string, e.g. "0x5591234abcde"), throttled to at most one capture
// every `min_interval_ms`. No-ops if `manager` or `shm` is null (protocol or
// wl_shm global unavailable).
void toplevel_export_request(ToplevelExportState &state,
                             hyprland_toplevel_export_manager_v1 *manager,
                             wl_shm *shm, const std::string &address,
                             int min_interval_ms);

// Returns the latest captured texture for `address`, or nullptr if no frame
// has completed yet.
const Texture *toplevel_export_texture(const ToplevelExportState &state,
                                       const std::string &address);

// Drops capture state (and GL/shm resources) for every address not present
// in `live_addresses`, called once per overview refresh with the current
// window list so closed windows don't leak.
void toplevel_export_prune(ToplevelExportState &state,
                           const std::vector<std::string> &live_addresses);

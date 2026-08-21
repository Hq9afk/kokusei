#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <wayland-client.h>

#include "hyprland-toplevel-export-v1-client-protocol.h"

#include "render/texture.h"

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

void toplevel_export_request(ToplevelExportState &state,
                             hyprland_toplevel_export_manager_v1 *manager,
                             wl_shm *shm, const std::string &address,
                             int min_interval_ms);

const Texture *toplevel_export_texture(const ToplevelExportState &state,
                                       const std::string &address);

void toplevel_export_prune(ToplevelExportState &state,
                           const std::vector<std::string> &live_addresses);

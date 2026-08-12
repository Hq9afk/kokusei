#pragma once

#include <pipewire/pipewire.h>
#include <spa/utils/hook.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct PipewireState;

struct PwNodeEntry {
    PipewireState *state = nullptr;
    uint32_t id = 0;
    pw_proxy *proxy = nullptr;
    spa_hook listener{};
    std::string name;
    std::string description;
    std::string app_name;
    bool is_sink = false;
    bool is_stream = false;
    bool is_playback = false;
    uint32_t channels = 2;
    float level = 0.0f;
    bool muted = false;
    uint32_t device_id = 0;
    int32_t card_profile_device = -1;
};

struct PwDeviceEntry {
    pw_proxy *proxy = nullptr;
    spa_hook listener{};
    std::unordered_map<int32_t, int32_t> route_index;
};

struct PipewireState {
    pw_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_registry *registry = nullptr;
    spa_hook registry_listener{};
    pw_proxy *default_metadata = nullptr;
    spa_hook metadata_listener{};

    std::unordered_map<uint32_t, PwNodeEntry> nodes;
    std::unordered_map<uint32_t, PwDeviceEntry> devices;
    std::string default_sink_name;
    std::string default_source_name;
    uint32_t default_sink_id = 0;
    uint32_t default_source_id = 0;

    bool sink_changed = false;
    bool source_changed = false;
};

bool pipewire_init(PipewireState &state);

int pipewire_fd(const PipewireState &state);

struct PipewireChange {
    bool sink = false;
    bool source = false;
};

PipewireChange pipewire_poll(PipewireState &state);

float pipewire_sink_level(const PipewireState &state, bool &muted);

float pipewire_source_level(const PipewireState &state, bool &muted);

void pipewire_set_node_volume(PipewireState &state, uint32_t id, float level);

void pipewire_set_node_muted(PipewireState &state, uint32_t id, bool muted);

void pipewire_set_default(PipewireState &state, uint32_t node_id);

std::vector<const PwNodeEntry *> pipewire_sinks(const PipewireState &state);

std::vector<const PwNodeEntry *> pipewire_sources(const PipewireState &state);

std::vector<const PwNodeEntry *> pipewire_streams(const PipewireState &state,
                                                  bool playback);

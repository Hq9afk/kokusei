#include "service/pipewire.h"

#include "core/log.h"

#include <pipewire/extensions/metadata.h>
#include <pipewire/keys.h>
#include <spa/node/keys.h>
#include <spa/param/param.h>
#include <spa/param/props.h>
#include <spa/param/route.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/pod/parser.h>
#include <spa/pod/vararg.h>
#include <spa/utils/keys.h>

#include <cmath>
#include <cstring>
#include <string_view>

namespace {

std::string extract_json_name(const char *value) {
    if (!value)
        return {};
    std::string_view sv(value);
    size_t key = sv.find("\"name\"");
    if (key == std::string_view::npos)
        return {};
    size_t start = sv.find('"', sv.find(':', key) + 1);
    if (start == std::string_view::npos)
        return {};
    size_t end = sv.find('"', start + 1);
    if (end == std::string_view::npos)
        return {};
    return std::string(sv.substr(start + 1, end - start - 1));
}

void node_param_cb(void *data, int, uint32_t id, uint32_t index, uint32_t,
                   const spa_pod *param) {
    if (id != SPA_PARAM_Props || index != 0)
        return;
    auto *entry = static_cast<PwNodeEntry *>(data);

    const spa_pod_prop *volumes_prop =
        spa_pod_find_prop(param, nullptr, SPA_PROP_channelVolumes);
    const spa_pod_prop *mute_prop =
        spa_pod_find_prop(param, nullptr, SPA_PROP_mute);

    float level = entry->level;
    bool muted = entry->muted;

    if (volumes_prop) {
        const auto *arr =
            reinterpret_cast<const spa_pod_array *>(&volumes_prop->value);
        float total = 0.0f;
        int count = 0;
        spa_pod *iter = nullptr;
        SPA_POD_ARRAY_FOREACH(arr, iter) {
            total += std::cbrt(*reinterpret_cast<float *>(iter));
            ++count;
        }
        if (count > 0) {
            level = total / count;
            entry->channels = static_cast<uint32_t>(count);
        }
    }
    if (mute_prop)
        spa_pod_get_bool(&mute_prop->value, &muted);

    bool changed = (level != entry->level) || (muted != entry->muted);
    entry->level = level;
    entry->muted = muted;
    if (!changed)
        return;

    if (entry->id == entry->state->default_sink_id)
        entry->state->sink_changed = true;
    else if (entry->id == entry->state->default_source_id)
        entry->state->source_changed = true;
}

void node_info_cb(void *data, const pw_node_info *info) {
    auto *entry = static_cast<PwNodeEntry *>(data);
    if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS) {
        const char *card_profile_device_str =
            info->props ? spa_dict_lookup(info->props, "card.profile.device")
                        : nullptr;
        if (card_profile_device_str)
            entry->card_profile_device = std::stoi(card_profile_device_str);
    }
    if (!(info->change_mask & PW_NODE_CHANGE_MASK_PARAMS))
        return;
    for (uint32_t i = 0; i < info->n_params; ++i) {
        const auto &param = info->params[i];
        if (param.id == SPA_PARAM_Props &&
            (param.flags & SPA_PARAM_INFO_READWRITE) ==
                SPA_PARAM_INFO_READWRITE) {
            pw_node_enum_params(reinterpret_cast<pw_node *>(entry->proxy), 0,
                                SPA_PARAM_Props, 0, UINT32_MAX, nullptr);
        }
    }
}

constexpr pw_node_events kNodeEvents = {
    .version = PW_VERSION_NODE_EVENTS,
    .info = node_info_cb,
    .param = node_param_cb,
};

void device_param_cb(void *data, int, uint32_t id, uint32_t, uint32_t,
                     const spa_pod *param) {
    if (id != SPA_PARAM_Route)
        return;
    auto *entry = static_cast<PwDeviceEntry *>(data);

    spa_pod_parser parser;
    spa_pod_parser_pod(&parser, param);
    int32_t device = 0;
    int32_t index = 0;
    uint32_t route_id = SPA_PARAM_Route;
    if (spa_pod_parser_get_object(&parser, SPA_TYPE_OBJECT_ParamRoute,
                                  &route_id, SPA_PARAM_ROUTE_device,
                                  SPA_POD_Int(&device), SPA_PARAM_ROUTE_index,
                                  SPA_POD_Int(&index)) < 0)
        return;
    entry->route_index[device] = index;
}

void device_info_cb(void *data, const pw_device_info *info) {
    auto *entry = static_cast<PwDeviceEntry *>(data);
    if (!(info->change_mask & PW_DEVICE_CHANGE_MASK_PARAMS))
        return;
    for (uint32_t i = 0; i < info->n_params; ++i) {
        const auto &param = info->params[i];
        if (param.id == SPA_PARAM_Route &&
            (param.flags & SPA_PARAM_INFO_READWRITE) ==
                SPA_PARAM_INFO_READWRITE) {
            pw_device_enum_params(reinterpret_cast<pw_device *>(entry->proxy),
                                  0, SPA_PARAM_Route, 0, UINT32_MAX, nullptr);
        }
    }
}

constexpr pw_device_events kDeviceEvents = {
    .version = PW_VERSION_DEVICE_EVENTS,
    .info = device_info_cb,
    .param = device_param_cb,
};

int metadata_property_cb(void *data, uint32_t, const char *key, const char *,
                         const char *value) {
    auto *state = static_cast<PipewireState *>(data);
    if (!key)
        return 0;
    bool is_sink_key = strcmp(key, "default.audio.sink") == 0;
    bool is_source_key = strcmp(key, "default.audio.source") == 0;
    if (!is_sink_key && !is_source_key)
        return 0;

    std::string name = extract_json_name(value);
    uint32_t resolved_id = 0;
    for (const auto &[id, entry] : state->nodes) {
        if (entry.name == name && entry.is_sink == is_sink_key) {
            resolved_id = id;
            break;
        }
    }
    if (is_sink_key) {
        state->default_sink_name = name;
        state->default_sink_id = resolved_id;
        state->sink_changed = true;
    } else {
        state->default_source_name = name;
        state->default_source_id = resolved_id;
        state->source_changed = true;
    }
    return 0;
}

constexpr pw_metadata_events kMetadataEvents = {
    .version = PW_VERSION_METADATA_EVENTS,
    .property = metadata_property_cb,
};

void registry_global_cb(void *data, uint32_t id, uint32_t, const char *type,
                        uint32_t, const spa_dict *props) {
    auto *state = static_cast<PipewireState *>(data);

    if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        const char *media_class =
            props ? spa_dict_lookup(props, SPA_KEY_MEDIA_CLASS) : nullptr;
        bool is_sink = media_class && strcmp(media_class, "Audio/Sink") == 0;
        bool is_source =
            media_class && strcmp(media_class, "Audio/Source") == 0;
        bool is_stream_out =
            media_class && strcmp(media_class, "Stream/Output/Audio") == 0;
        bool is_stream_in =
            media_class && strcmp(media_class, "Stream/Input/Audio") == 0;
        if (!is_sink && !is_source && !is_stream_out && !is_stream_in)
            return;
        const char *name =
            props ? spa_dict_lookup(props, SPA_KEY_NODE_NAME) : nullptr;
        const char *description =
            props ? spa_dict_lookup(props, SPA_KEY_NODE_DESCRIPTION) : nullptr;
        const char *device_id_str =
            props ? spa_dict_lookup(props, PW_KEY_DEVICE_ID) : nullptr;
        const char *card_profile_device_str =
            props ? spa_dict_lookup(props, "card.profile.device") : nullptr;

        PwNodeEntry &entry = state->nodes[id];
        entry.state = state;
        entry.id = id;
        entry.is_sink = is_sink;
        entry.is_stream = is_stream_out || is_stream_in;
        entry.is_playback = is_stream_out;
        entry.name = name ? name : "";
        entry.description = description ? description : "";
        entry.device_id = device_id_str ? std::stoul(device_id_str) : 0;
        entry.card_profile_device =
            card_profile_device_str ? std::stoi(card_profile_device_str) : -1;
        if (entry.is_stream) {
            const char *app_name =
                props ? spa_dict_lookup(props, PW_KEY_APP_NAME) : nullptr;
            const char *node_desc =
                props ? spa_dict_lookup(props, SPA_KEY_NODE_DESCRIPTION)
                      : nullptr;
            const char *node_nick =
                props ? spa_dict_lookup(props, PW_KEY_NODE_NICK) : nullptr;
            entry.app_name = app_name ? app_name : "";
            entry.description = node_desc   ? node_desc
                                : node_nick ? node_nick
                                            : entry.app_name;
        }
        entry.proxy = static_cast<pw_proxy *>(pw_registry_bind(
            state->registry, id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
        pw_node_add_listener(reinterpret_cast<pw_node *>(entry.proxy),
                             &entry.listener, &kNodeEvents, &entry);
        uint32_t subscribe_params[] = {SPA_PARAM_Props};
        pw_node_subscribe_params(reinterpret_cast<pw_node *>(entry.proxy),
                                 subscribe_params, 1);

        if (is_sink && entry.name == state->default_sink_name)
            state->default_sink_id = id;
        if (is_source && entry.name == state->default_source_name)
            state->default_source_id = id;
    } else if (strcmp(type, PW_TYPE_INTERFACE_Device) == 0) {
        PwDeviceEntry &entry = state->devices[id];
        entry.proxy = static_cast<pw_proxy *>(
            pw_registry_bind(state->registry, id, PW_TYPE_INTERFACE_Device,
                             PW_VERSION_DEVICE, 0));
        pw_device_add_listener(reinterpret_cast<pw_device *>(entry.proxy),
                               &entry.listener, &kDeviceEvents, &entry);
    } else if (strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
        const char *meta_name =
            props ? spa_dict_lookup(props, PW_KEY_METADATA_NAME) : nullptr;
        if (!meta_name || strcmp(meta_name, "default") != 0)
            return;
        state->default_metadata = static_cast<pw_proxy *>(
            pw_registry_bind(state->registry, id, PW_TYPE_INTERFACE_Metadata,
                             PW_VERSION_METADATA, 0));
        pw_metadata_add_listener(
            reinterpret_cast<pw_metadata *>(state->default_metadata),
            &state->metadata_listener, &kMetadataEvents, state);
    }
}

void registry_global_remove_cb(void *data, uint32_t id) {
    auto *state = static_cast<PipewireState *>(data);
    if (auto it = state->devices.find(id); it != state->devices.end()) {
        spa_hook_remove(&it->second.listener);
        if (it->second.proxy)
            pw_proxy_destroy(it->second.proxy);
        state->devices.erase(it);
    }
    auto it = state->nodes.find(id);
    if (it == state->nodes.end())
        return;
    if (state->default_sink_id == id)
        state->default_sink_id = 0;
    if (state->default_source_id == id)
        state->default_source_id = 0;
    spa_hook_remove(&it->second.listener);
    if (it->second.proxy)
        pw_proxy_destroy(it->second.proxy);
    state->nodes.erase(it);
}

constexpr pw_registry_events kRegistryEvents = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = registry_global_cb,
    .global_remove = registry_global_remove_cb,
};

void set_node_volume(PwNodeEntry &entry, float level) {
    std::vector<float> volumes(entry.channels, level * level * level);
    uint8_t buffer[512];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_pod_frame f;
    spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    spa_pod_builder_prop(&b, SPA_PROP_channelVolumes, 0);
    spa_pod_builder_array(&b, sizeof(float), SPA_TYPE_Float, volumes.size(),
                          volumes.data());
    spa_pod *pod = static_cast<spa_pod *>(spa_pod_builder_pop(&b, &f));
    pw_node_set_param(reinterpret_cast<pw_node *>(entry.proxy), SPA_PARAM_Props,
                      0, pod);
}

void set_node_muted(PwNodeEntry &entry, bool muted) {
    uint8_t buffer[512];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_pod_frame f;
    spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    spa_pod_builder_prop(&b, SPA_PROP_mute, 0);
    spa_pod_builder_bool(&b, muted);
    spa_pod *pod = static_cast<spa_pod *>(spa_pod_builder_pop(&b, &f));
    pw_node_set_param(reinterpret_cast<pw_node *>(entry.proxy), SPA_PARAM_Props,
                      0, pod);
}

bool resolve_device_route(PipewireState &state, const PwNodeEntry &entry,
                          pw_device *&device_proxy, int32_t &route_index) {
    if (entry.card_profile_device < 0)
        return false;
    auto dit = state.devices.find(entry.device_id);
    if (dit == state.devices.end())
        return false;
    auto rit = dit->second.route_index.find(entry.card_profile_device);
    if (rit == dit->second.route_index.end())
        return false;
    device_proxy = reinterpret_cast<pw_device *>(dit->second.proxy);
    route_index = rit->second;
    return true;
}

void set_device_route_volume(pw_device *device_proxy, int32_t route_device,
                             int32_t route_index, uint32_t channels,
                             float level) {
    std::vector<float> volumes(channels, level * level * level);
    uint8_t buffer[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    auto *props = static_cast<spa_pod *>(spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props, SPA_PROP_channelVolumes,
        SPA_POD_Array(sizeof(float), SPA_TYPE_Float, volumes.size(),
                      volumes.data())));
    auto *route = static_cast<spa_pod *>(spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route, SPA_PARAM_ROUTE_device,
        SPA_POD_Int(route_device), SPA_PARAM_ROUTE_index,
        SPA_POD_Int(route_index), SPA_PARAM_ROUTE_props,
        SPA_POD_PodObject(props), SPA_PARAM_ROUTE_save, SPA_POD_Bool(true)));
    pw_device_set_param(device_proxy, SPA_PARAM_Route, 0, route);
}

void set_device_route_muted(pw_device *device_proxy, int32_t route_device,
                            int32_t route_index, bool muted) {
    uint8_t buffer[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    auto *props = static_cast<spa_pod *>(
        spa_pod_builder_add_object(&b, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
                                   SPA_PROP_mute, SPA_POD_Bool(muted)));
    auto *route = static_cast<spa_pod *>(spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route, SPA_PARAM_ROUTE_device,
        SPA_POD_Int(route_device), SPA_PARAM_ROUTE_index,
        SPA_POD_Int(route_index), SPA_PARAM_ROUTE_props,
        SPA_POD_PodObject(props), SPA_PARAM_ROUTE_save, SPA_POD_Bool(true)));
    pw_device_set_param(device_proxy, SPA_PARAM_Route, 0, route);
}

} // namespace

bool pipewire_init(PipewireState &state) {
    pw_init(nullptr, nullptr);
    state.loop = pw_loop_new(nullptr);
    if (!state.loop) {
        klog("pipewire: failed to create loop");
        return false;
    }
    state.context = pw_context_new(state.loop, nullptr, 0);
    if (!state.context) {
        klog("pipewire: failed to create context");
        return false;
    }
    state.core = pw_context_connect(state.context, nullptr, 0);
    if (!state.core) {
        klog("pipewire: failed to connect");
        return false;
    }
    state.registry = pw_core_get_registry(state.core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(state.registry, &state.registry_listener,
                             &kRegistryEvents, &state);

    return true;
}

int pipewire_fd(const PipewireState &state) {
    return state.loop ? pw_loop_get_fd(state.loop) : -1;
}

PipewireChange pipewire_poll(PipewireState &state) {
    if (state.loop)
        pw_loop_iterate(state.loop, 0);
    PipewireChange result{state.sink_changed, state.source_changed};
    state.sink_changed = false;
    state.source_changed = false;
    return result;
}

float pipewire_sink_level(const PipewireState &state, bool &muted) {
    auto it = state.nodes.find(state.default_sink_id);
    if (it == state.nodes.end()) {
        muted = false;
        return 0.0f;
    }
    muted = it->second.muted;
    return it->second.level;
}

float pipewire_source_level(const PipewireState &state, bool &muted) {
    auto it = state.nodes.find(state.default_source_id);
    if (it == state.nodes.end()) {
        muted = false;
        return 0.0f;
    }
    muted = it->second.muted;
    return it->second.level;
}

void pipewire_set_node_volume(PipewireState &state, uint32_t id, float level) {
    auto it = state.nodes.find(id);
    if (it == state.nodes.end())
        return;
    PwNodeEntry &entry = it->second;
    entry.level = level;
    pw_device *device_proxy = nullptr;
    int32_t route_index = 0;
    if (resolve_device_route(state, entry, device_proxy, route_index))
        set_device_route_volume(device_proxy, entry.card_profile_device,
                                route_index, entry.channels, level);
    else
        set_node_volume(entry, level);
    if (id == state.default_sink_id)
        state.sink_changed = true;
    else if (id == state.default_source_id)
        state.source_changed = true;
}

void pipewire_set_node_muted(PipewireState &state, uint32_t id, bool muted) {
    auto it = state.nodes.find(id);
    if (it == state.nodes.end())
        return;
    PwNodeEntry &entry = it->second;
    entry.muted = muted;
    pw_device *device_proxy = nullptr;
    int32_t route_index = 0;
    if (resolve_device_route(state, entry, device_proxy, route_index))
        set_device_route_muted(device_proxy, entry.card_profile_device,
                               route_index, muted);
    else
        set_node_muted(entry, muted);
    if (id == state.default_sink_id)
        state.sink_changed = true;
    else if (id == state.default_source_id)
        state.source_changed = true;
}

void pipewire_set_default(PipewireState &state, uint32_t node_id) {
    auto it = state.nodes.find(node_id);
    if (it == state.nodes.end() || !state.default_metadata)
        return;
    const char *key =
        it->second.is_sink ? "default.audio.sink" : "default.audio.source";
    std::string json = "{ \"name\": \"" + it->second.name + "\" }";
    pw_metadata_set_property(
        reinterpret_cast<pw_metadata *>(state.default_metadata), PW_ID_CORE,
        key, "Spa:String:JSON", json.c_str());
}

std::vector<const PwNodeEntry *> pipewire_sinks(const PipewireState &state) {
    std::vector<const PwNodeEntry *> result;
    for (const auto &[id, entry] : state.nodes)
        if (entry.is_sink)
            result.push_back(&entry);
    return result;
}

std::vector<const PwNodeEntry *> pipewire_sources(const PipewireState &state) {
    std::vector<const PwNodeEntry *> result;
    for (const auto &[id, entry] : state.nodes)
        if (!entry.is_sink && !entry.is_stream)
            result.push_back(&entry);
    return result;
}

std::vector<const PwNodeEntry *> pipewire_streams(const PipewireState &state,
                                                  bool playback) {
    std::vector<const PwNodeEntry *> result;
    for (const auto &[id, entry] : state.nodes)
        if (entry.is_stream && entry.is_playback == playback)
            result.push_back(&entry);
    return result;
}

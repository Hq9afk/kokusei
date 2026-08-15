#include "service/mpris_service.h"

#include "core/log.h"

#include <optional>

MprisPlaybackStatus mpris_detail_parse_playback_status(const std::string &s) {
    if (s == "Playing")
        return MprisPlaybackStatus::Playing;
    if (s == "Paused")
        return MprisPlaybackStatus::Paused;
    return MprisPlaybackStatus::Stopped;
}

std::string mpris_detail_format_position(int64_t position_us) {
    if (position_us < 0)
        position_us = 0;
    int64_t total_seconds = position_us / 1000000;
    int64_t minutes = total_seconds / 60;
    int64_t seconds = total_seconds % 60;
    std::string result = std::to_string(minutes) + ":";
    if (seconds < 10)
        result += "0";
    result += std::to_string(seconds);
    return result;
}

bool mpris_detail_is_local_art_url(const std::string &url) {
    return url.starts_with("file://");
}

int mpris_detail_select_player(
    const std::vector<MprisPlayerCandidate> &players) {
    if (players.empty())
        return -1;
    for (size_t i = 0; i < players.size(); ++i)
        if (players[i].status == MprisPlaybackStatus::Playing)
            return static_cast<int>(i);
    return 0;
}

namespace mpris_detail {

constexpr const char *kBusDaemonService = "org.freedesktop.DBus";
constexpr const char *kBusDaemonPath = "/org/freedesktop/DBus";
constexpr const char *kPlayerObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char *kPlayerIface = "org.mpris.MediaPlayer2.Player";
constexpr const char *kPropertiesIface = "org.freedesktop.DBus.Properties";
constexpr const char *kNamePrefix = "org.mpris.MediaPlayer2.";

template <typename T> std::optional<T> variant_get(const sdbus::Variant &v) {
    try {
        return v.get<T>();
    } catch (const sdbus::Error &) {
        return std::nullopt;
    }
}

MprisTrackInfo
parse_metadata(const std::map<std::string, sdbus::Variant> &metadata) {
    MprisTrackInfo info;
    if (auto it = metadata.find("xesam:title"); it != metadata.end())
        if (auto v = variant_get<std::string>(it->second))
            info.title = *v;
    if (auto it = metadata.find("xesam:artist"); it != metadata.end())
        if (auto v = variant_get<std::vector<std::string>>(it->second))
            if (!v->empty())
                info.artist = (*v)[0];
    if (auto it = metadata.find("mpris:artUrl"); it != metadata.end())
        if (auto v = variant_get<std::string>(it->second))
            info.art_url = *v;
    if (auto it = metadata.find("mpris:length"); it != metadata.end())
        if (auto v = variant_get<int64_t>(it->second))
            info.length_us = *v;
    return info;
}

std::vector<std::string> list_player_names(sdbus::IProxy &dbus_daemon) {
    std::vector<std::string> names;
    try {
        std::vector<std::string> all_names;
        dbus_daemon.callMethod("ListNames")
            .onInterface(kBusDaemonService)
            .storeResultsTo(all_names);
        for (const std::string &name : all_names)
            if (name.starts_with(kNamePrefix))
                names.push_back(name);
    } catch (const sdbus::Error &e) {
        klog("mpris: ListNames failed: %s", e.getMessage().c_str());
    }
    return names;
}

std::optional<MprisPlaybackStatus>
query_playback_status(sdbus::IConnection &bus, const std::string &name) {
    try {
        auto proxy = sdbus::createProxy(bus, sdbus::ServiceName{name},
                                        sdbus::ObjectPath{kPlayerObjectPath});
        sdbus::Variant v;
        proxy->callMethod("Get")
            .onInterface(kPropertiesIface)
            .withArguments(std::string(kPlayerIface),
                           std::string("PlaybackStatus"))
            .storeResultsTo(v);
        if (auto s = variant_get<std::string>(v))
            return mpris_detail_parse_playback_status(*s);
    } catch (const sdbus::Error &) {
    }
    return std::nullopt;
}

void subscribe_player(MprisState &state, sdbus::IConnection &bus,
                      const std::string &name) {
    state.player = sdbus::createProxy(bus, sdbus::ServiceName{name},
                                      sdbus::ObjectPath{kPlayerObjectPath});
    state.selected_bus_name = name;
    state.has_player = true;

    state.player->uponSignal("PropertiesChanged")
        .onInterface(kPropertiesIface)
        .call([&state](const std::string &iface,
                       const std::map<std::string, sdbus::Variant> &changed,
                       const std::vector<std::string> &) {
            if (iface != kPlayerIface)
                return;
            if (auto it = changed.find("PlaybackStatus"); it != changed.end())
                if (auto s = variant_get<std::string>(it->second))
                    state.status = mpris_detail_parse_playback_status(*s);
            if (auto it = changed.find("Metadata"); it != changed.end())
                if (auto m = variant_get<std::map<std::string, sdbus::Variant>>(
                        it->second))
                    state.track = parse_metadata(*m);
        });

    if (auto status = query_playback_status(bus, name))
        state.status = *status;
    try {
        sdbus::Variant v;
        state.player->callMethod("Get")
            .onInterface(kPropertiesIface)
            .withArguments(std::string(kPlayerIface), std::string("Metadata"))
            .storeResultsTo(v);
        if (auto m = variant_get<std::map<std::string, sdbus::Variant>>(v))
            state.track = parse_metadata(*m);
    } catch (const sdbus::Error &) {
    }
}

void refresh_selected_player(MprisState &state, sdbus::IConnection &bus) {
    std::vector<std::string> names = list_player_names(*state.dbus_daemon);
    std::vector<MprisPlayerCandidate> candidates;
    candidates.reserve(names.size());
    for (const std::string &name : names) {
        auto status = query_playback_status(bus, name);
        candidates.push_back(
            {name, status.value_or(MprisPlaybackStatus::Stopped)});
    }

    int selected = mpris_detail_select_player(candidates);
    if (selected < 0) {
        state.player.reset();
        state.selected_bus_name.clear();
        state.has_player = false;
        state.status = MprisPlaybackStatus::Stopped;
        state.track = MprisTrackInfo{};
        return;
    }
    if (candidates[static_cast<size_t>(selected)].bus_name ==
        state.selected_bus_name)
        return;
    subscribe_player(state, bus,
                     candidates[static_cast<size_t>(selected)].bus_name);
}

} // namespace mpris_detail

bool mpris_init(MprisState &state) {
    using namespace mpris_detail;
    try {
        state.bus = sdbus::createSessionBusConnection();
        sdbus::IConnection &bus = *state.bus;

        state.dbus_daemon =
            sdbus::createProxy(bus, sdbus::ServiceName{kBusDaemonService},
                               sdbus::ObjectPath{kBusDaemonPath});

        state.dbus_daemon->uponSignal("NameOwnerChanged")
            .onInterface(kBusDaemonService)
            .call([&state, &bus](const std::string &name, const std::string &,
                                 const std::string &) {
                if (name.starts_with(kNamePrefix))
                    refresh_selected_player(state, bus);
            });

        refresh_selected_player(state, bus);
        klog("mpris: connected, has_player=%d", state.has_player);
        return true;
    } catch (const sdbus::Error &e) {
        klog("mpris: connection failed (%s): %s", e.getName().c_str(),
             e.getMessage().c_str());
        state.dbus_daemon.reset();
        state.bus.reset();
        return false;
    }
}

namespace {

void call_player_method(MprisState &state, const char *method) {
    if (!state.player)
        return;
    try {
        state.player->callMethodAsync(method)
            .onInterface(mpris_detail::kPlayerIface)
            .uponReplyInvoke([method](std::optional<sdbus::Error> err) {
                if (err)
                    klog("mpris: %s failed: %s", method,
                         err->getMessage().c_str());
            });
    } catch (const sdbus::Error &e) {
        klog("mpris: %s dispatch failed: %s", method, e.getMessage().c_str());
    }
}

} // namespace

void mpris_play_pause(MprisState &state) {
    call_player_method(state, "PlayPause");
}

void mpris_next(MprisState &state) { call_player_method(state, "Next"); }

void mpris_previous(MprisState &state) {
    call_player_method(state, "Previous");
}

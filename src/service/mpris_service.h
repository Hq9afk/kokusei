#pragma once

#include <sdbus-c++/sdbus-c++.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class MprisPlaybackStatus { Stopped, Paused, Playing };

MprisPlaybackStatus mpris_detail_parse_playback_status(const std::string &s);

std::string mpris_detail_format_position(int64_t position_us);

bool mpris_detail_is_local_art_url(const std::string &url);

struct MprisPlayerCandidate {
    std::string bus_name;
    MprisPlaybackStatus status;
};

// "First Playing player, else first present player." Returns -1 if players
// is empty.
int mpris_detail_select_player(
    const std::vector<MprisPlayerCandidate> &players);

struct MprisTrackInfo {
    std::string title;
    std::string artist;
    std::string art_url;
    int64_t length_us = 0;
    int64_t position_us = 0;
};

struct MprisState {
    std::unique_ptr<sdbus::IConnection> bus;
    std::unique_ptr<sdbus::IProxy> dbus_daemon;
    std::unique_ptr<sdbus::IProxy> player;
    std::string selected_bus_name;
    MprisPlaybackStatus status = MprisPlaybackStatus::Stopped;
    MprisTrackInfo track;
    bool has_player = false;
};

bool mpris_init(MprisState &state);

void mpris_play_pause(MprisState &state);

void mpris_next(MprisState &state);

void mpris_previous(MprisState &state);

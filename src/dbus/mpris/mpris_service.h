#pragma once

#include "mpris_types.h"

#include <sdbus-c++/sdbus-c++.h>

#include <memory>
#include <string>

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

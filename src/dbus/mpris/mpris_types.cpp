#include "mpris_types.h"

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

int mpris_detail_select_player(const std::vector<MprisPlayerCandidate> &players) {
    if (players.empty())
        return -1;
    for (size_t i = 0; i < players.size(); ++i)
        if (players[i].status == MprisPlaybackStatus::Playing)
            return static_cast<int>(i);
    return 0;
}

#include "../../src/dbus/mpris/mpris_types.h"

#include <cassert>

void test_mpris() {
    assert(mpris_detail_parse_playback_status("Playing") ==
           MprisPlaybackStatus::Playing);
    assert(mpris_detail_parse_playback_status("Paused") ==
           MprisPlaybackStatus::Paused);
    assert(mpris_detail_parse_playback_status("Stopped") ==
           MprisPlaybackStatus::Stopped);
    assert(mpris_detail_parse_playback_status("") ==
           MprisPlaybackStatus::Stopped);

    assert(mpris_detail_format_position(0) == "0:00");
    assert(mpris_detail_format_position(65 * 1000000LL) == "1:05");
    assert(mpris_detail_format_position(600 * 1000000LL) == "10:00");
    assert(mpris_detail_format_position(-5) == "0:00");

    assert(mpris_detail_is_local_art_url("file:///home/user/art.jpg"));
    assert(!mpris_detail_is_local_art_url("https://example.com/art.jpg"));
    assert(!mpris_detail_is_local_art_url(""));

    assert(mpris_detail_select_player({}) == -1);

    std::vector<MprisPlayerCandidate> only_paused = {
        {"org.mpris.MediaPlayer2.a", MprisPlaybackStatus::Paused},
        {"org.mpris.MediaPlayer2.b", MprisPlaybackStatus::Stopped},
    };
    assert(mpris_detail_select_player(only_paused) == 0);

    std::vector<MprisPlayerCandidate> one_playing = {
        {"org.mpris.MediaPlayer2.a", MprisPlaybackStatus::Paused},
        {"org.mpris.MediaPlayer2.b", MprisPlaybackStatus::Playing},
    };
    assert(mpris_detail_select_player(one_playing) == 1);
}

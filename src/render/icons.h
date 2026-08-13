#pragma once

namespace icon {

inline constexpr const char *adjustments = "\uea03";
inline constexpr const char *bell = "\uea35";
inline constexpr const char *device_desktop = "\uea89";
inline constexpr const char *edit = "\uea98";
inline constexpr const char *home = "\ueac1";
inline constexpr const char *layout_bottombar = "\uead3";
inline constexpr const char *layout_navbar = "\uead7";
inline constexpr const char *moon_stars = "\uece7";
inline constexpr const char *palette_icon = "\ueb01";
inline constexpr const char *wallpaper = "\uef56";

inline constexpr const char *alert_triangle = "\uea06";

inline constexpr const char *apps = "\uebb6";
inline constexpr const char *tray = "\uebb6";
inline constexpr const char *terminal = "\uebdc";

inline constexpr const char *arrow_left = "\uea19";
inline constexpr const char *arrow_narrow_down = "\uea1a";
inline constexpr const char *arrow_narrow_up = "\uea1d";
inline constexpr const char *arrow_right = "\uea1f";

inline constexpr const char *battery1 = "\uea2f";
inline constexpr const char *battery2 = "\uea30";
inline constexpr const char *battery3 = "\uea31";
inline constexpr const char *battery4 = "\uea32";
inline constexpr const char *battery_charging = "\uea33";
inline constexpr const char *battery_disabled = "\ued1c";
inline constexpr const char *plugged_in = "\uef3b";

inline constexpr const char *bluetooth_connected = "\uecea";
inline constexpr const char *bluetooth_device = "\uea37";
inline constexpr const char *bluetooth_off = "\ueceb";
inline constexpr const char *bluetooth_on = "\uea37";

inline constexpr const char *brand_google = "\uec1f";
inline constexpr const char *brand_youtube = "\uec90";

inline constexpr const char *check = "\uea5e";
inline constexpr const char *chevron_down = "\uea5f";
inline constexpr const char *chevron_left = "\uea60";
inline constexpr const char *chevron_right = "\uea61";
inline constexpr const char *chevron_up = "\uea62";
inline constexpr const char *close = "\ueb55";
inline constexpr const char *code = "\uea77";
inline constexpr const char *link = "\ueade";
inline constexpr const char *refresh = "\ueb13";

inline constexpr const char *control_center = "\uec42";
inline constexpr const char *cpu = "\uef8e";
inline constexpr const char *folder = "\ueaad";
inline constexpr const char *folder_open = "\ufaf7";
inline constexpr const char *gpu = "\uef8d";
inline constexpr const char *settings = "\ueb20";
inline constexpr const char *user = "\ueb4d";

inline constexpr const char *lock = "\ueae2";
inline constexpr const char *lock_open = "\ueae1";

inline constexpr const char *mic_off = "\ued16";
inline constexpr const char *mic_on = "\ueaf0";

inline constexpr const char *music_note = "\ueafc";
inline constexpr const char *player_next = "\ued4b";
inline constexpr const char *player_pause = "\ued45";
inline constexpr const char *player_play = "\ued46";
inline constexpr const char *player_prev = "\ued4c";

inline constexpr const char *overview = "\ueef6";

inline constexpr const char *power = "\ueb0d";

inline constexpr const char *volume_empty = "\u0001\u1d9d";
inline constexpr const char *volume_high = "\ueb51";
inline constexpr const char *volume_low = "\ueb4f";
inline constexpr const char *volume_mute = "\uf1c3";

inline constexpr const char *router = "\ueb18";
inline constexpr const char *wifi = "\ueb52";
inline constexpr const char *wifi0 = "\ueba3";
inline constexpr const char *wifi1 = "\ueba4";
inline constexpr const char *wifi2 = "\ueba5";
inline constexpr const char *wifi_off = "\uecfa";

}

inline const char *volume_threshold_icon(bool muted, float level) {
    if (muted)
        return icon::volume_mute;
    if (level < 0.01f)
        return icon::volume_empty;
    if (level < 0.5f)
        return icon::volume_low;
    return icon::volume_high;
}

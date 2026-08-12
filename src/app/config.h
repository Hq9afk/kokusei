#pragma once

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

inline std::string default_wallpaper_dir() {
    const char *home = getenv("HOME");
    return std::string(home ? home : "") + "/Pictures";
}

struct MonitorOverride {
    bool enabled = false;
    bool osd = true;
    bool notifications = true;
    bool autohide = false;
};

struct Config {
    std::string wallpaper_path = KOKUSEI_DEFAULT_WALLPAPER;
    std::string wallpaper_dir = default_wallpaper_dir();

    std::map<std::string, std::vector<std::string>> wallpaper_columns;
    std::map<std::string, int> wallpaper_column_counts;
    std::map<std::string, std::string> wallpaper_fill_modes;
    bool autohide = false;
    bool default_osd_enabled = true;
    bool default_notifications_enabled = true;
    std::map<std::string, MonitorOverride> monitor_overrides;
    uint32_t idle_timeout_seconds = 300;
    std::string idle_command;
    std::string idle_resume_command;
};

int wallpaper_effective_column_count(const Config &cfg,
                                     const std::string &monitor_name);

std::string wallpaper_effective_column_path(const Config &cfg,
                                            const std::string &monitor_name,
                                            int column_index);

std::string wallpaper_effective_fill_mode(const Config &cfg,
                                          const std::string &monitor_name);

bool osd_effective_enabled(const Config &cfg, const std::string &monitor_name);

bool notifications_effective_enabled(const Config &cfg,
                                     const std::string &monitor_name);

bool autohide_effective_enabled(const Config &cfg,
                                const std::string &monitor_name);

std::string config_path();

Config load_config();

void save_config(const Config &cfg);

int config_watch_init(const std::string &path);

struct ConfigWatchEvent {
    bool changed = false;
    bool removed = false;
};

ConfigWatchEvent config_watch_poll(int fd);

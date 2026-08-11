#pragma once

#include <toml++/toml.hpp>

#include "../core/log.hpp"
#include "../render/palette.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <sys/inotify.h>
#include <unistd.h>

inline std::string default_wallpaper_dir() {
    const char *home = getenv("HOME");
    return std::string(home ? home : "") + "/Pictures";
}

struct Config {
    int32_t height = 35;

    float bg[4] = {palette::overlay.r, palette::overlay.g, palette::overlay.b,
                   palette::overlay.a};
    std::string wallpaper_path = KOKUSEI_DEFAULT_WALLPAPER;
    std::string wallpaper_dir = default_wallpaper_dir();
    std::map<std::string, std::string> wallpaper_paths;
    std::map<std::string, std::string> wallpaper_fill_modes;
    bool autohide = false;
    uint32_t idle_timeout_seconds = 300;
    std::string idle_command;
    std::string idle_resume_command;
};

// Per-monitor wallpaper lookups: fall back to the global default when a
// monitor has no explicit entry.
inline std::string wallpaper_effective_path(const Config &cfg,
                                            const std::string &monitor_name) {
    auto it = cfg.wallpaper_paths.find(monitor_name);
    if (it != cfg.wallpaper_paths.end() && !it->second.empty())
        return it->second;
    return cfg.wallpaper_path;
}

inline std::string
wallpaper_effective_fill_mode(const Config &cfg,
                              const std::string &monitor_name) {
    auto it = cfg.wallpaper_fill_modes.find(monitor_name);
    if (it != cfg.wallpaper_fill_modes.end() && !it->second.empty())
        return it->second;
    return "crop";
}

inline std::string
device_name(const std::string &hostname_path = "/etc/hostname") {
    std::ifstream f(hostname_path);
    std::string device;
    if (f && std::getline(f, device) && !device.empty())
        return device;
    return "hq9afk";
}

inline std::string config_path() {
    const char *home = getenv("HOME");
    if (!home)
        return "";
    return std::string(home) + "/.config/kokusei/" + device_name() + ".toml";
}

inline Config load_config() {
    Config cfg;
    std::string path = config_path();
    if (path.empty())
        return cfg;
    try {
        toml::table tbl = toml::parse_file(path);
        cfg.height = tbl["bar"]["height"].value_or(cfg.height);
        cfg.autohide = tbl["bar"]["autohide"].value_or(cfg.autohide);
        cfg.bg[0] = tbl["bar"]["bg_r"].value_or(cfg.bg[0]);
        cfg.bg[1] = tbl["bar"]["bg_g"].value_or(cfg.bg[1]);
        cfg.bg[2] = tbl["bar"]["bg_b"].value_or(cfg.bg[2]);
        cfg.bg[3] = tbl["bar"]["bg_a"].value_or(cfg.bg[3]);
        cfg.wallpaper_path =
            tbl["wallpaper"]["path"].value_or(cfg.wallpaper_path);
        cfg.wallpaper_dir =
            tbl["wallpaper"]["dir"].value_or(cfg.wallpaper_dir);
        if (const auto *paths = tbl["wallpaper"]["paths"].as_table()) {
            for (const auto &[name, val] : *paths)
                if (auto s = val.value<std::string>())
                    cfg.wallpaper_paths[std::string(name.str())] = *s;
        }
        if (const auto *modes = tbl["wallpaper"]["fill_modes"].as_table()) {
            for (const auto &[name, val] : *modes)
                if (auto s = val.value<std::string>())
                    cfg.wallpaper_fill_modes[std::string(name.str())] = *s;
        }
        cfg.idle_timeout_seconds =
            tbl["idle"]["timeout_seconds"].value_or(cfg.idle_timeout_seconds);
        cfg.idle_command = tbl["idle"]["command"].value_or(cfg.idle_command);
        cfg.idle_resume_command =
            tbl["idle"]["resume_command"].value_or(cfg.idle_resume_command);
    } catch (const toml::parse_error &) {
    }
    return cfg;
}

inline bool write_file_atomic(const std::string &path,
                              const std::string &content) {
    std::string tmp_path = path + ".tmp";
    {
        std::ofstream f(tmp_path, std::ios::trunc);
        if (!f || !(f << content))
            return false;
    }
    if (rename(tmp_path.c_str(), path.c_str()) != 0) {
        unlink(tmp_path.c_str());
        return false;
    }
    return true;
}

inline void save_config(const Config &cfg) {
    std::string path = config_path();
    if (path.empty())
        return;
    toml::table tbl;
    tbl.insert_or_assign("bar", toml::table{
                                    {"height", cfg.height},
                                    {"autohide", cfg.autohide},
                                    {"bg_r", cfg.bg[0]},
                                    {"bg_g", cfg.bg[1]},
                                    {"bg_b", cfg.bg[2]},
                                    {"bg_a", cfg.bg[3]},
                                });
    toml::table wallpaper_paths_tbl;
    for (const auto &[name, path] : cfg.wallpaper_paths)
        wallpaper_paths_tbl.insert_or_assign(name, path);
    toml::table wallpaper_fill_modes_tbl;
    for (const auto &[name, mode] : cfg.wallpaper_fill_modes)
        wallpaper_fill_modes_tbl.insert_or_assign(name, mode);
    tbl.insert_or_assign(
        "wallpaper",
        toml::table{{"path", cfg.wallpaper_path},
                    {"dir", cfg.wallpaper_dir},
                    {"paths", wallpaper_paths_tbl},
                    {"fill_modes", wallpaper_fill_modes_tbl}});
    tbl.insert_or_assign(
        "idle",
        toml::table{
            {"timeout_seconds", static_cast<int64_t>(cfg.idle_timeout_seconds)},
            {"command", cfg.idle_command},
            {"resume_command", cfg.idle_resume_command},
        });
    std::ostringstream ss;
    ss << tbl;
    if (!write_file_atomic(path, ss.str()))
        klog("config: failed to save %s", path.c_str());
}

inline int config_watch_init(const std::string &path) {
    if (path.empty())
        return -1;
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0)
        return -1;
    if (inotify_add_watch(fd, path.c_str(), IN_MODIFY | IN_CLOSE_WRITE) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

struct ConfigWatchEvent {
    bool changed = false;
    bool removed = false;
};

inline ConfigWatchEvent config_watch_poll(int fd) {
    char buf[4096] __attribute__((aligned(alignof(struct inotify_event))));
    ConfigWatchEvent result;
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (char *p = buf; p < buf + n;) {
            auto *ev = reinterpret_cast<struct inotify_event *>(p);
            if (ev->mask & IN_IGNORED)
                result.removed = true;
            else
                result.changed = true;
            p += sizeof(struct inotify_event) + ev->len;
        }
    }
    return result;
}

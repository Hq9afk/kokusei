#pragma once

#include <toml++/toml.hpp>

#include "../core/log.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <sys/inotify.h>
#include <unistd.h>
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

inline int wallpaper_effective_column_count(const Config &cfg,
                                            const std::string &monitor_name) {
    auto it = cfg.wallpaper_column_counts.find(monitor_name);
    return it != cfg.wallpaper_column_counts.end() && it->second > 0
              ? it->second
              : 1;
}

inline std::string wallpaper_effective_column_path(const Config &cfg,
                                                    const std::string &monitor_name,
                                                    int column_index) {
    auto it = cfg.wallpaper_columns.find(monitor_name);
    if (it != cfg.wallpaper_columns.end() &&
        column_index >= 0 &&
        static_cast<size_t>(column_index) < it->second.size() &&
        !it->second[static_cast<size_t>(column_index)].empty())
        return it->second[static_cast<size_t>(column_index)];
    return column_index == 0 ? cfg.wallpaper_path : "";
}

inline std::string
wallpaper_effective_fill_mode(const Config &cfg,
                              const std::string &monitor_name) {
    auto it = cfg.wallpaper_fill_modes.find(monitor_name);
    if (it != cfg.wallpaper_fill_modes.end() && !it->second.empty())
        return it->second;
    return "crop";
}

inline bool osd_effective_enabled(const Config &cfg,
                                  const std::string &monitor_name) {
    auto it = cfg.monitor_overrides.find(monitor_name);
    if (it != cfg.monitor_overrides.end() && it->second.enabled)
        return it->second.osd;
    return cfg.default_osd_enabled;
}

inline bool notifications_effective_enabled(const Config &cfg,
                                            const std::string &monitor_name) {
    auto it = cfg.monitor_overrides.find(monitor_name);
    if (it != cfg.monitor_overrides.end() && it->second.enabled)
        return it->second.notifications;
    return cfg.default_notifications_enabled;
}

inline bool autohide_effective_enabled(const Config &cfg,
                                       const std::string &monitor_name) {
    auto it = cfg.monitor_overrides.find(monitor_name);
    if (it != cfg.monitor_overrides.end() && it->second.enabled)
        return it->second.autohide;
    return cfg.autohide;
}

inline std::string config_path() {
    const char *home = getenv("HOME");
    if (!home)
        return "";
    return std::string(home) + "/.config/kokusei/config.toml";
}

inline Config load_config() {
    Config cfg;
    std::string path = config_path();
    if (path.empty())
        return cfg;
    try {
        toml::table tbl = toml::parse_file(path);
        cfg.autohide = tbl["autohide"].value_or(cfg.autohide);
        cfg.wallpaper_path =
            tbl["wallpaper"]["path"].value_or(cfg.wallpaper_path);
        cfg.wallpaper_dir =
            tbl["wallpaper"]["dir"].value_or(cfg.wallpaper_dir);
        if (const auto *columns = tbl["wallpaper"]["columns"].as_table()) {
            for (const auto &[name, val] : *columns) {
                if (const auto *arr = val.as_array()) {
                    std::vector<std::string> paths;
                    for (const auto &el : *arr)
                        paths.push_back(el.value_or(std::string()));
                    cfg.wallpaper_columns[std::string(name.str())] = paths;
                }
            }
        }
        if (const auto *counts =
                tbl["wallpaper"]["column_counts"].as_table()) {
            for (const auto &[name, val] : *counts)
                if (auto n = val.value<int64_t>())
                    cfg.wallpaper_column_counts[std::string(name.str())] =
                        static_cast<int>(*n);
        }
        if (const auto *modes = tbl["wallpaper"]["fill_modes"].as_table()) {
            for (const auto &[name, val] : *modes)
                if (auto s = val.value<std::string>())
                    cfg.wallpaper_fill_modes[std::string(name.str())] = *s;
        }
        cfg.default_osd_enabled =
            tbl["displays"]["default_osd"].value_or(cfg.default_osd_enabled);
        cfg.default_notifications_enabled =
            tbl["displays"]["default_notifications"].value_or(
                cfg.default_notifications_enabled);
        if (const auto *overrides =
                tbl["displays"]["monitor_overrides"].as_table()) {
            for (const auto &[name, val] : *overrides) {
                if (const auto *ov = val.as_table()) {
                    MonitorOverride mo;
                    mo.enabled = (*ov)["enabled"].value_or(mo.enabled);
                    mo.osd = (*ov)["osd"].value_or(mo.osd);
                    mo.notifications =
                        (*ov)["notifications"].value_or(mo.notifications);
                    mo.autohide = (*ov)["autohide"].value_or(mo.autohide);
                    cfg.monitor_overrides[std::string(name.str())] = mo;
                }
            }
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
    tbl.insert_or_assign("autohide", cfg.autohide);
    toml::table wallpaper_columns_tbl;
    for (const auto &[name, paths] : cfg.wallpaper_columns) {
        toml::array arr;
        for (const std::string &p : paths)
            arr.push_back(p);
        wallpaper_columns_tbl.insert_or_assign(name, arr);
    }
    toml::table wallpaper_column_counts_tbl;
    for (const auto &[name, count] : cfg.wallpaper_column_counts)
        wallpaper_column_counts_tbl.insert_or_assign(
            name, static_cast<int64_t>(count));
    toml::table wallpaper_fill_modes_tbl;
    for (const auto &[name, mode] : cfg.wallpaper_fill_modes)
        wallpaper_fill_modes_tbl.insert_or_assign(name, mode);
    tbl.insert_or_assign(
        "wallpaper",
        toml::table{{"path", cfg.wallpaper_path},
                    {"dir", cfg.wallpaper_dir},
                    {"columns", wallpaper_columns_tbl},
                    {"column_counts", wallpaper_column_counts_tbl},
                    {"fill_modes", wallpaper_fill_modes_tbl}});
    toml::table monitor_overrides_tbl;
    for (const auto &[name, ov] : cfg.monitor_overrides)
        monitor_overrides_tbl.insert_or_assign(
            name, toml::table{{"enabled", ov.enabled},
                              {"osd", ov.osd},
                              {"notifications", ov.notifications},
                              {"autohide", ov.autohide}});
    tbl.insert_or_assign(
        "displays",
        toml::table{{"default_osd", cfg.default_osd_enabled},
                    {"default_notifications", cfg.default_notifications_enabled},
                    {"monitor_overrides", monitor_overrides_tbl}});
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

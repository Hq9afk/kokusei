#include "app/config.h"

#include "core/log.h"

#include <toml++/toml.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/inotify.h>
#include <unistd.h>

bool osd_effective_enabled(const Config &cfg, const std::string &monitor_name) {
    auto it = cfg.monitor_overrides.find(monitor_name);
    if (it != cfg.monitor_overrides.end() && it->second.enabled)
        return it->second.osd;
    return cfg.default_osd_enabled;
}

bool notifications_effective_enabled(const Config &cfg,
                                     const std::string &monitor_name) {
    auto it = cfg.monitor_overrides.find(monitor_name);
    if (it != cfg.monitor_overrides.end() && it->second.enabled)
        return it->second.notifications;
    return cfg.default_notifications_enabled;
}

bool autohide_effective_enabled(const Config &cfg,
                                const std::string &monitor_name) {
    auto it = cfg.monitor_overrides.find(monitor_name);
    if (it != cfg.monitor_overrides.end() && it->second.enabled)
        return it->second.autohide;
    return cfg.autohide;
}

std::string config_path() {
    const char *home = getenv("HOME");
    if (!home)
        return "";
    return std::string(home) + "/.config/kokusei/config.toml";
}

Config load_config() {
    Config cfg;
    std::string path = config_path();
    if (path.empty())
        return cfg;
    try {
        toml::table tbl = toml::parse_file(path);
        cfg.autohide = tbl["autohide"].value_or(cfg.autohide);
        cfg.wallpaper_dir = tbl["wallpaper"]["dir"].value_or(cfg.wallpaper_dir);
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
        if (const auto *counts = tbl["wallpaper"]["column_counts"].as_table()) {
            for (const auto &[name, val] : *counts)
                if (auto n = val.value<int64_t>())
                    cfg.wallpaper_column_counts[std::string(name.str())] =
                        static_cast<int>(*n);
        }
        if (const auto *modes = tbl["wallpaper"]["fill_modes"].as_table()) {
            for (const auto &[name, val] : *modes) {
                if (const auto *arr = val.as_array()) {
                    std::vector<std::string> column_modes;
                    for (const auto &el : *arr)
                        column_modes.push_back(el.value_or(std::string()));
                    cfg.wallpaper_fill_modes[std::string(name.str())] =
                        column_modes;
                }
            }
        }
        cfg.wallpaper_animated_enabled =
            tbl["wallpaper"]["animated_enabled"].value_or(
                cfg.wallpaper_animated_enabled);
        cfg.wallpaper_animated_dir = tbl["wallpaper"]["animated_dir"].value_or(
            cfg.wallpaper_animated_dir);
        if (const auto *columns =
                tbl["wallpaper"]["animated_columns"].as_table()) {
            for (const auto &[name, val] : *columns) {
                if (const auto *arr = val.as_array()) {
                    std::vector<std::string> paths;
                    for (const auto &el : *arr)
                        paths.push_back(el.value_or(std::string()));
                    cfg.wallpaper_animated_columns[std::string(name.str())] =
                        paths;
                }
            }
        }
        if (const auto *counts =
                tbl["wallpaper"]["animated_column_counts"].as_table()) {
            for (const auto &[name, val] : *counts)
                if (auto n = val.value<int64_t>())
                    cfg.wallpaper_animated_column_counts[std::string(
                        name.str())] = static_cast<int>(*n);
        }
        if (const auto *modes =
                tbl["wallpaper"]["animated_fill_modes"].as_table()) {
            for (const auto &[name, val] : *modes) {
                if (const auto *arr = val.as_array()) {
                    std::vector<std::string> column_modes;
                    for (const auto &el : *arr)
                        column_modes.push_back(el.value_or(std::string()));
                    cfg.wallpaper_animated_fill_modes[std::string(name.str())] =
                        column_modes;
                }
            }
        }
        cfg.default_osd_enabled =
            tbl["displays"]["default_osd"].value_or(cfg.default_osd_enabled);
        cfg.default_notifications_enabled =
            tbl["displays"]["default_notifications"].value_or(
                cfg.default_notifications_enabled);
        cfg.default_wallpaper_enabled =
            tbl["displays"]["default_wallpaper"].value_or(
                cfg.default_wallpaper_enabled);
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

namespace {

bool write_file_atomic(const std::string &path, const std::string &content) {
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

} // namespace

void save_config(const Config &cfg) {
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
    for (const auto &[name, modes] : cfg.wallpaper_fill_modes) {
        toml::array arr;
        for (const std::string &m : modes)
            arr.push_back(m);
        wallpaper_fill_modes_tbl.insert_or_assign(name, arr);
    }
    toml::table wallpaper_animated_columns_tbl;
    for (const auto &[name, paths] : cfg.wallpaper_animated_columns) {
        toml::array arr;
        for (const std::string &p : paths)
            arr.push_back(p);
        wallpaper_animated_columns_tbl.insert_or_assign(name, arr);
    }
    toml::table wallpaper_animated_column_counts_tbl;
    for (const auto &[name, count] : cfg.wallpaper_animated_column_counts)
        wallpaper_animated_column_counts_tbl.insert_or_assign(
            name, static_cast<int64_t>(count));
    toml::table wallpaper_animated_fill_modes_tbl;
    for (const auto &[name, modes] : cfg.wallpaper_animated_fill_modes) {
        toml::array arr;
        for (const std::string &m : modes)
            arr.push_back(m);
        wallpaper_animated_fill_modes_tbl.insert_or_assign(name, arr);
    }
    tbl.insert_or_assign(
        "wallpaper",
        toml::table{
            {"dir", cfg.wallpaper_dir},
            {"columns", wallpaper_columns_tbl},
            {"column_counts", wallpaper_column_counts_tbl},
            {"fill_modes", wallpaper_fill_modes_tbl},
            {"animated_enabled", cfg.wallpaper_animated_enabled},
            {"animated_dir", cfg.wallpaper_animated_dir},
            {"animated_columns", wallpaper_animated_columns_tbl},
            {"animated_column_counts", wallpaper_animated_column_counts_tbl},
            {"animated_fill_modes", wallpaper_animated_fill_modes_tbl}});
    toml::table monitor_overrides_tbl;
    for (const auto &[name, ov] : cfg.monitor_overrides)
        monitor_overrides_tbl.insert_or_assign(
            name, toml::table{{"enabled", ov.enabled},
                              {"osd", ov.osd},
                              {"notifications", ov.notifications},
                              {"autohide", ov.autohide}});
    tbl.insert_or_assign(
        "displays",
        toml::table{
            {"default_osd", cfg.default_osd_enabled},
            {"default_notifications", cfg.default_notifications_enabled},
            {"default_wallpaper", cfg.default_wallpaper_enabled},
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

int config_watch_init(const std::string &path) {
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

ConfigWatchEvent config_watch_poll(int fd) {
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

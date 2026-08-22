#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app/config.h"

#include "core/log.h"

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

bool ambient_effective_enabled(const Config &cfg,
                               const std::string &monitor_name) {
    if (!cfg.idle_management_enabled)
        return false;
    auto it = cfg.monitor_overrides.find(monitor_name);
    if (it != cfg.monitor_overrides.end() && it->second.enabled)
        return it->second.ambient_enabled;
    return cfg.ambient_enabled;
}

uint32_t ambient_effective_timeout_seconds(const Config &cfg,
                                           const std::string &monitor_name) {
    auto it = cfg.monitor_overrides.find(monitor_name);
    if (it != cfg.monitor_overrides.end() && it->second.enabled)
        return it->second.ambient_timeout_seconds;
    return cfg.ambient_timeout_seconds;
}

bool screensaver_effective_enabled(const Config &cfg,
                                   const std::string &monitor_name) {
    if (!cfg.idle_management_enabled)
        return false;
    auto it = cfg.monitor_overrides.find(monitor_name);
    if (it != cfg.monitor_overrides.end() && it->second.enabled)
        return it->second.screensaver_enabled;
    return cfg.screensaver_enabled;
}

uint32_t
screensaver_effective_timeout_seconds(const Config &cfg,
                                      const std::string &monitor_name) {
    auto it = cfg.monitor_overrides.find(monitor_name);
    if (it != cfg.monitor_overrides.end() && it->second.enabled)
        return it->second.screensaver_timeout_seconds;
    return cfg.screensaver_timeout_seconds;
}

std::string config_path() {
    const char *home = getenv("HOME");
    if (!home)
        return "";
    return std::string(home) + "/.config/kokusei/config.json";
}

namespace {

bool is_reserved_displays_key(const std::string &key) {
    return key == "defaultOsd" || key == "defaultNotifications" ||
           key == "defaultWallpaper";
}

} // namespace

Config load_config() {
    Config cfg;
    std::string path = config_path();
    if (path.empty())
        return cfg;
    try {
        std::ifstream f(path);
        if (!f)
            return cfg;
        nlohmann::json j = nlohmann::json::parse(f);

        nlohmann::json bar = j.value("bar", nlohmann::json::object());
        cfg.autohide = bar.value("autohideEnabled", cfg.autohide);

        nlohmann::json wallpaper =
            j.value("wallpaper", nlohmann::json::object());
        cfg.wallpaper_dir = wallpaper.value("dir", cfg.wallpaper_dir);
        if (auto it = wallpaper.find("columns");
            it != wallpaper.end() && it->is_object())
            for (const auto &[name, val] : it->items())
                if (val.is_array())
                    cfg.wallpaper_columns[name] =
                        val.get<std::vector<std::string>>();
        if (auto it = wallpaper.find("columnCounts");
            it != wallpaper.end() && it->is_object())
            for (const auto &[name, val] : it->items())
                if (val.is_number_integer())
                    cfg.wallpaper_column_counts[name] = val.get<int>();
        if (auto it = wallpaper.find("fillModes");
            it != wallpaper.end() && it->is_object())
            for (const auto &[name, val] : it->items())
                if (val.is_array())
                    cfg.wallpaper_fill_modes[name] =
                        val.get<std::vector<std::string>>();
        cfg.wallpaper_animated_enabled =
            wallpaper.value("animatedEnabled", cfg.wallpaper_animated_enabled);
        cfg.wallpaper_animated_dir =
            wallpaper.value("animatedDir", cfg.wallpaper_animated_dir);
        if (auto it = wallpaper.find("animatedColumns");
            it != wallpaper.end() && it->is_object())
            for (const auto &[name, val] : it->items())
                if (val.is_array())
                    cfg.wallpaper_animated_columns[name] =
                        val.get<std::vector<std::string>>();
        if (auto it = wallpaper.find("animatedColumnCounts");
            it != wallpaper.end() && it->is_object())
            for (const auto &[name, val] : it->items())
                if (val.is_number_integer())
                    cfg.wallpaper_animated_column_counts[name] = val.get<int>();
        if (auto it = wallpaper.find("animatedFillModes");
            it != wallpaper.end() && it->is_object())
            for (const auto &[name, val] : it->items())
                if (val.is_array())
                    cfg.wallpaper_animated_fill_modes[name] =
                        val.get<std::vector<std::string>>();

        nlohmann::json displays = j.value("displays", nlohmann::json::object());
        cfg.default_osd_enabled =
            displays.value("defaultOsd", cfg.default_osd_enabled);
        cfg.default_notifications_enabled = displays.value(
            "defaultNotifications", cfg.default_notifications_enabled);
        cfg.default_wallpaper_enabled =
            displays.value("defaultWallpaper", cfg.default_wallpaper_enabled);
        for (const auto &[name, val] : displays.items()) {
            if (is_reserved_displays_key(name) || !val.is_object())
                continue;
            MonitorOverride mo;
            mo.enabled = val.value("_enabled", mo.enabled);
            mo.osd = val.value("osd", mo.osd);
            mo.notifications = val.value("notifications", mo.notifications);
            mo.autohide = val.value("autohide", mo.autohide);
            mo.ambient_enabled =
                val.value("ambientEnabled", mo.ambient_enabled);
            mo.ambient_timeout_seconds =
                val.value("ambientTimeoutSeconds", mo.ambient_timeout_seconds);
            mo.screensaver_enabled =
                val.value("screensaverEnabled", mo.screensaver_enabled);
            mo.screensaver_timeout_seconds = val.value(
                "screensaverTimeoutSeconds", mo.screensaver_timeout_seconds);
            cfg.monitor_overrides[name] = mo;
        }

        nlohmann::json idle = j.value("idle", nlohmann::json::object());
        cfg.idle_timeout_seconds =
            idle.value("timeoutSeconds", cfg.idle_timeout_seconds);
        cfg.idle_command = idle.value("command", cfg.idle_command);
        cfg.idle_resume_command =
            idle.value("resumeCommand", cfg.idle_resume_command);
        cfg.idle_management_enabled =
            idle.value("enabled", cfg.idle_management_enabled);
        cfg.ambient_enabled = idle.value("ambientEnabled", cfg.ambient_enabled);
        cfg.ambient_timeout_seconds =
            idle.value("ambientTimeoutSeconds", cfg.ambient_timeout_seconds);
        cfg.screensaver_enabled =
            idle.value("screensaverEnabled", cfg.screensaver_enabled);
        cfg.screensaver_timeout_seconds = idle.value(
            "screensaverTimeoutSeconds", cfg.screensaver_timeout_seconds);

        nlohmann::json visualizer =
            j.value("visualizer", nlohmann::json::object());
        cfg.visualizer_shape = visualizer.value("shape", cfg.visualizer_shape);
        if (cfg.visualizer_shape == "bars")
            cfg.visualizer_shape = "bar";
        else if (cfg.visualizer_shape == "ncs")
            cfg.visualizer_shape = "sphere";
    } catch (const nlohmann::json::exception &) {
    }
    return cfg;
}

namespace {

bool write_file_atomic(const std::string &path, const std::string &content) {
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos)
        mkdir(path.substr(0, slash).c_str(), 0755);
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

    nlohmann::json wallpaper;
    wallpaper["dir"] = cfg.wallpaper_dir;
    wallpaper["columns"] = cfg.wallpaper_columns;
    wallpaper["columnCounts"] = cfg.wallpaper_column_counts;
    wallpaper["fillModes"] = cfg.wallpaper_fill_modes;
    wallpaper["animatedEnabled"] = cfg.wallpaper_animated_enabled;
    wallpaper["animatedDir"] = cfg.wallpaper_animated_dir;
    wallpaper["animatedColumns"] = cfg.wallpaper_animated_columns;
    wallpaper["animatedColumnCounts"] = cfg.wallpaper_animated_column_counts;
    wallpaper["animatedFillModes"] = cfg.wallpaper_animated_fill_modes;

    nlohmann::json displays;
    displays["defaultOsd"] = cfg.default_osd_enabled;
    displays["defaultNotifications"] = cfg.default_notifications_enabled;
    displays["defaultWallpaper"] = cfg.default_wallpaper_enabled;
    for (const auto &[name, ov] : cfg.monitor_overrides) {
        nlohmann::json mo;
        mo["_enabled"] = ov.enabled;
        mo["osd"] = ov.osd;
        mo["notifications"] = ov.notifications;
        mo["autohide"] = ov.autohide;
        mo["ambientEnabled"] = ov.ambient_enabled;
        mo["ambientTimeoutSeconds"] = ov.ambient_timeout_seconds;
        mo["screensaverEnabled"] = ov.screensaver_enabled;
        mo["screensaverTimeoutSeconds"] = ov.screensaver_timeout_seconds;
        displays[name] = mo;
    }

    nlohmann::json idle;
    idle["timeoutSeconds"] = cfg.idle_timeout_seconds;
    idle["command"] = cfg.idle_command;
    idle["resumeCommand"] = cfg.idle_resume_command;
    idle["enabled"] = cfg.idle_management_enabled;
    idle["ambientEnabled"] = cfg.ambient_enabled;
    idle["ambientTimeoutSeconds"] = cfg.ambient_timeout_seconds;
    idle["screensaverEnabled"] = cfg.screensaver_enabled;
    idle["screensaverTimeoutSeconds"] = cfg.screensaver_timeout_seconds;

    nlohmann::json j;
    j["bar"] = {{"autohideEnabled", cfg.autohide}};
    j["wallpaper"] = wallpaper;
    j["displays"] = displays;
    j["idle"] = idle;
    j["visualizer"] = {{"shape", cfg.visualizer_shape}};

    if (!write_file_atomic(path, j.dump(2)))
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

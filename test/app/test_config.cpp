
#include "../../src/app/config.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

void test_config() {
    char tmp_template[] = "/tmp/kokusei_test_config_XXXXXX";
    char *tmp_dir = mkdtemp(tmp_template);
    assert(tmp_dir != nullptr);

    const char *old_home = getenv("HOME");
    std::string old_home_str = old_home ? old_home : "";
    setenv("HOME", tmp_dir, 1);

    std::string config_dir = std::string(tmp_dir) + "/.config/kokusei";
    mkdir((std::string(tmp_dir) + "/.config").c_str(), 0755);
    mkdir(config_dir.c_str(), 0755);

    std::string path = config_path();
    assert(path == config_dir + "/config.toml");

    Config cfg;
    save_config(cfg);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    assert(content.find("[bar]") == std::string::npos);
    assert(content.find("autohide") != std::string::npos);

    unlink(path.c_str());
    rmdir(config_dir.c_str());
    rmdir((std::string(tmp_dir) + "/.config").c_str());
    rmdir(tmp_dir);

    if (old_home)
        setenv("HOME", old_home_str.c_str(), 1);
}

void test_config_watch() {
    std::string path =
        "/tmp/kokusei_test_config_watch_" + std::to_string(getpid()) + ".toml";
    {
        std::ofstream f(path);
        f << "[idle]\ntimeout_seconds = 300\n";
    }

    int fd = config_watch_init(path);
    assert(fd >= 0);

    {
        std::ofstream f(path, std::ios::trunc);
        f << "[idle]\ntimeout_seconds = 400\n";
    }

    bool changed = false;
    for (int i = 0; i < 100 && !changed; ++i) {
        usleep(10000);
        changed = config_watch_poll(fd).changed;
    }
    assert(changed);

    close(fd);
    unlink(path.c_str());
}

void test_monitor_overrides() {
    Config cfg;

    assert(osd_effective_enabled(cfg, "DP-1") == cfg.default_osd_enabled);
    assert(notifications_effective_enabled(cfg, "DP-1") ==
          cfg.default_notifications_enabled);
    assert(autohide_effective_enabled(cfg, "DP-1") == cfg.autohide);

    cfg.monitor_overrides["DP-1"] = MonitorOverride{
       .enabled = false, .osd = false, .notifications = false, .autohide = true};
    assert(osd_effective_enabled(cfg, "DP-1") == cfg.default_osd_enabled);

    cfg.monitor_overrides["DP-1"].enabled = true;
    assert(osd_effective_enabled(cfg, "DP-1") == false);
    assert(notifications_effective_enabled(cfg, "DP-1") == false);
    assert(autohide_effective_enabled(cfg, "DP-1") == true);

    assert(osd_effective_enabled(cfg, "HDMI-1") == cfg.default_osd_enabled);
}

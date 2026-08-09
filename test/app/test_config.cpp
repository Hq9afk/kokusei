
#include "../../src/app/config.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>

void test_config() {
    std::string path = "/tmp/kokusei_test_hostname_" + std::to_string(getpid());

    {
        std::ofstream f(path);
        f << "hq9afk-letsnote\n";
    }
    assert(device_name(path) == "hq9afk-letsnote");
    unlink(path.c_str());

    assert(device_name(path) == "hq9afk");
}

void test_config_watch() {
    std::string path =
        "/tmp/kokusei_test_config_watch_" + std::to_string(getpid()) + ".toml";
    {
        std::ofstream f(path);
        f << "[bar]\nheight = 35\n";
    }

    int fd = config_watch_init(path);
    assert(fd >= 0);

    {
        std::ofstream f(path, std::ios::trunc);
        f << "[bar]\nheight = 40\n";
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


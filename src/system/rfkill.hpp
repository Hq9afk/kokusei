#pragma once

#include "../core/log.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/rfkill.h>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

namespace rfkill_detail {

struct Entry {
    unsigned index = 0;
    std::string type;
    bool soft = false;
    bool hard = false;
};

inline std::optional<unsigned> read_sysfs_uint(const std::string &path) {
    FILE *f = fopen(path.c_str(), "r");
    if (!f)
        return std::nullopt;
    unsigned value = 0;
    bool ok = fscanf(f, "%u", &value) == 1;
    fclose(f);
    if (!ok)
        return std::nullopt;
    return value;
}

inline std::optional<std::string> read_sysfs_string(const std::string &path) {
    FILE *f = fopen(path.c_str(), "r");
    if (!f)
        return std::nullopt;
    char buf[32]{};
    bool ok = fscanf(f, "%31s", buf) == 1;
    fclose(f);
    if (!ok)
        return std::nullopt;
    return std::string(buf);
}

inline std::vector<Entry> bluetooth_entries() {
    std::vector<Entry> entries;
    DIR *dir = opendir("/sys/class/rfkill");
    if (!dir)
        return entries;
    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (!name.starts_with("rfkill"))
            continue;
        std::string base = "/sys/class/rfkill/" + name + "/";
        auto index = read_sysfs_uint(base + "index");
        auto type = read_sysfs_string(base + "type");
        if (!index || !type || *type != "bluetooth")
            continue;
        Entry e;
        e.index = *index;
        e.type = *type;
        e.soft = read_sysfs_uint(base + "soft").value_or(0) != 0;
        e.hard = read_sysfs_uint(base + "hard").value_or(0) != 0;
        entries.push_back(e);
    }
    closedir(dir);
    return entries;
}

}

inline bool rfkill_bluetooth_hard_blocked() {
    for (const rfkill_detail::Entry &e : rfkill_detail::bluetooth_entries())
        if (e.hard)
            return true;
    return false;
}

inline bool rfkill_bluetooth_soft_blocked() {
    for (const rfkill_detail::Entry &e : rfkill_detail::bluetooth_entries())
        if (e.soft)
            return true;
    return false;
}

inline bool rfkill_set_bluetooth_soft_blocked(bool blocked) {
    std::vector<rfkill_detail::Entry> entries =
        rfkill_detail::bluetooth_entries();
    if (entries.empty())
        return false;
    for (const rfkill_detail::Entry &e : entries)
        if (e.hard)
            return false;
    bool already = true;
    for (const rfkill_detail::Entry &e : entries)
        if (e.soft != blocked)
            already = false;
    if (already)
        return true;

    int fd = open("/dev/rfkill", O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        klog("rfkill: open /dev/rfkill: %s", strerror(errno));
        return false;
    }
    rfkill_event ev{};
    ev.type = RFKILL_TYPE_BLUETOOTH;
    ev.op = RFKILL_OP_CHANGE_ALL;
    ev.soft = blocked ? 1 : 0;
    ssize_t written;
    do {
        written = write(fd, &ev, sizeof(ev));
    } while (written < 0 && errno == EINTR);
    int write_errno = errno;
    close(fd);
    if (written != static_cast<ssize_t>(sizeof(ev))) {
        klog("rfkill: write /dev/rfkill: %s",
             written < 0 ? strerror(write_errno) : "short write");
        return false;
    }
    return true;
}

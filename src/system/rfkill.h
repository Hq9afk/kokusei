#pragma once

#include <optional>
#include <string>

namespace rfkill_detail {

struct Entry {
    unsigned index = 0;
    std::string type;
    bool soft = false;
    bool hard = false;
};

std::optional<unsigned> read_sysfs_uint(const std::string &path);

std::optional<std::string> read_sysfs_string(const std::string &path);

} // namespace rfkill_detail

bool rfkill_bluetooth_hard_blocked();

bool rfkill_bluetooth_soft_blocked();

bool rfkill_set_bluetooth_soft_blocked(bool blocked);

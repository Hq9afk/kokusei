#include "system_stats.h"

#include <fstream>
#include <sstream>

std::optional<CpuJiffies> system_stats_detail_parse_proc_stat(const std::string &text) {
    std::istringstream ss(text);
    std::string line;
    if (!std::getline(ss, line))
        return std::nullopt;
    std::istringstream ls(line);
    std::string cpu_label;
    ls >> cpu_label;
    if (cpu_label != "cpu")
        return std::nullopt;

    uint64_t value, total = 0, idle = 0;
    int field = 0;
    while (ls >> value) {
        total += value;
        // /proc/stat cpu fields: user nice system idle iowait irq softirq ...
        // idle is field index 3, iowait is field index 4.
        if (field == 3 || field == 4)
            idle += value;
        ++field;
    }
    if (field == 0)
        return std::nullopt;

    CpuJiffies result;
    result.idle = idle;
    result.total = total;
    return result;
}

float system_stats_detail_cpu_usage(const CpuJiffies &prev, const CpuJiffies &cur) {
    if (cur.total <= prev.total)
        return -1.0f;
    uint64_t total_delta = cur.total - prev.total;
    uint64_t idle_delta = cur.idle - prev.idle;
    if (idle_delta > total_delta)
        return -1.0f;
    return static_cast<float>(total_delta - idle_delta) /
          static_cast<float>(total_delta);
}

std::optional<MemInfo> system_stats_detail_parse_proc_meminfo(const std::string &text) {
    std::istringstream ss(text);
    std::string line;
    MemInfo info;
    bool have_total = false, have_available = false;
    while (std::getline(ss, line)) {
        std::istringstream ls(line);
        std::string key;
        uint64_t value;
        ls >> key >> value;
        if (key == "MemTotal:") {
            info.total_kb = value;
            have_total = true;
        } else if (key == "MemAvailable:") {
            info.available_kb = value;
            have_available = true;
        }
    }
    if (!have_total || !have_available)
        return std::nullopt;
    return info;
}

float system_stats_detail_mem_usage(const MemInfo &info) {
    if (info.total_kb == 0 || info.available_kb > info.total_kb)
        return -1.0f;
    return static_cast<float>(info.total_kb - info.available_kb) /
          static_cast<float>(info.total_kb);
}

namespace {

std::string read_file(const char *path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

void system_stats_poll(SystemStatsState &state) {
    auto stat = system_stats_detail_parse_proc_stat(read_file("/proc/stat"));
    if (stat) {
        if (state.have_prev)
            state.cpu_usage =
                system_stats_detail_cpu_usage(state.prev_jiffies, *stat);
        state.prev_jiffies = *stat;
        state.have_prev = true;
    } else {
        state.cpu_usage = -1.0f;
    }

    auto mem = system_stats_detail_parse_proc_meminfo(read_file("/proc/meminfo"));
    state.mem_usage = mem ? system_stats_detail_mem_usage(*mem) : -1.0f;
}

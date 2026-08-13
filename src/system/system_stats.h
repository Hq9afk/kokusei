#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct CpuJiffies {
    uint64_t idle = 0;
    uint64_t total = 0;
};

std::optional<CpuJiffies> system_stats_detail_parse_proc_stat(const std::string &text);

// Returns 0..1, or -1 if either sample is invalid.
float system_stats_detail_cpu_usage(const CpuJiffies &prev, const CpuJiffies &cur);

struct MemInfo {
    uint64_t total_kb = 0;
    uint64_t available_kb = 0;
};

std::optional<MemInfo> system_stats_detail_parse_proc_meminfo(const std::string &text);

// Returns 0..1, or -1 if unavailable.
float system_stats_detail_mem_usage(const MemInfo &info);

struct SystemStatsState {
    CpuJiffies prev_jiffies;
    bool have_prev = false;
    float cpu_usage = -1.0f;
    float mem_usage = -1.0f;
};

void system_stats_poll(SystemStatsState &state);

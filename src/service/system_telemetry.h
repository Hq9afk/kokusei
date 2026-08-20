#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "core/async_process.h"

bool cpu_temp_detail_is_cpu_hwmon_name(const std::string &name);

bool cpu_temp_detail_is_cpu_thermal_zone_type(const std::string &type);

struct CpuTempState {
    std::string sensor_path;
    float celsius = -1.0f;
};

void cpu_temp_init(CpuTempState &state);

void cpu_temp_poll(CpuTempState &state);

bool cpu_temp_available(const CpuTempState &state);

bool gpu_temp_detail_is_gpu_hwmon_name(const std::string &name);

std::optional<float>
gpu_temp_detail_parse_nvidia_smi_output(const std::string &text);

struct GpuTempState {
    std::string sensor_path;
    std::string usage_sensor_path;
    bool nvidia_smi_present = false;
    AsyncProcess nvidia_smi_proc;
    float celsius = -1.0f;
    float usage_percent = -1.0f;
};

void gpu_temp_init(GpuTempState &state);

void gpu_temp_poll(GpuTempState &state);

bool gpu_temp_available(const GpuTempState &state);

bool gpu_stats_available(const GpuTempState &state);

struct CpuJiffies {
    uint64_t idle = 0;
    uint64_t total = 0;
};

std::optional<CpuJiffies>
system_stats_detail_parse_proc_stat(const std::string &text);

float system_stats_detail_cpu_usage(const CpuJiffies &prev,
                                    const CpuJiffies &cur);

struct MemInfo {
    uint64_t total_kb = 0;
    uint64_t available_kb = 0;
};

std::optional<MemInfo>
system_stats_detail_parse_proc_meminfo(const std::string &text);

float system_stats_detail_mem_usage(const MemInfo &info);

std::optional<float>
system_stats_detail_parse_cpu_freq_avg_mhz(const std::string &cpuinfo_text);

struct DiskUsage {
    uint64_t used_bytes = 0;
    uint64_t total_bytes = 0;
};

std::optional<DiskUsage> system_stats_detail_disk_usage(const char *path);

struct NetBytes {
    uint64_t rx_bytes = 0;
    uint64_t tx_bytes = 0;
};

std::optional<NetBytes>
system_stats_detail_parse_proc_net_dev(const std::string &text);

std::string system_stats_detail_format_speed(double bytes_per_sec);

struct SystemStatsState {
    CpuJiffies prev_jiffies;
    bool have_prev = false;
    float cpu_usage = -1.0f;
    float mem_usage = -1.0f;
    float mem_used_gb = -1.0f;
    float mem_total_gb = -1.0f;
    float cpu_freq_ghz = -1.0f;
    float disk_used_gb = -1.0f;
    float disk_total_gb = -1.0f;
    float disk_pct = -1.0f;
    NetBytes prev_net;
    std::chrono::steady_clock::time_point prev_net_time;
    bool have_prev_net = false;
    double net_rx_bps = -1.0;
    double net_tx_bps = -1.0;
};

void system_stats_poll(SystemStatsState &state);

#pragma once

#include "core/async_process.h"

#include <cstdint>
#include <optional>
#include <string>

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
    bool nvidia_smi_present = false;
    AsyncProcess nvidia_smi_proc;
    float celsius = -1.0f;
};

void gpu_temp_init(GpuTempState &state);

void gpu_temp_poll(GpuTempState &state);

bool gpu_temp_available(const GpuTempState &state);

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

struct SystemStatsState {
    CpuJiffies prev_jiffies;
    bool have_prev = false;
    float cpu_usage = -1.0f;
    float mem_usage = -1.0f;
};

void system_stats_poll(SystemStatsState &state);

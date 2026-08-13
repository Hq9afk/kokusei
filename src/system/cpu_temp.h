#pragma once

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

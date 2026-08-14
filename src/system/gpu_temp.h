#pragma once

#include "core/async_process.h"

#include <optional>
#include <string>

bool gpu_temp_detail_is_gpu_hwmon_name(const std::string &name);

std::optional<float> gpu_temp_detail_parse_nvidia_smi_output(const std::string &text);

struct GpuTempState {
    std::string sensor_path;
    bool nvidia_smi_present = false;
    AsyncProcess nvidia_smi_proc;
    float celsius = -1.0f;
};

void gpu_temp_init(GpuTempState &state);

// Called on a timer (5s). Reads sysfs synchronously, or kicks off/collects
// the async nvidia-smi fallback.
void gpu_temp_poll(GpuTempState &state);

bool gpu_temp_available(const GpuTempState &state);

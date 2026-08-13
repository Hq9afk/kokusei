#include "gpu_temp.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

bool gpu_temp_detail_is_gpu_hwmon_name(const std::string &name) {
    return name == "amdgpu" || name == "i915" || name == "xe";
}

std::optional<float> gpu_temp_detail_parse_nvidia_smi_output(const std::string &text) {
    std::istringstream ss(text);
    std::string first_line;
    if (!std::getline(ss, first_line))
        return std::nullopt;
    while (!first_line.empty() &&
          (first_line.back() == '\r' || first_line.back() == ' '))
        first_line.pop_back();
    if (first_line.empty())
        return std::nullopt;
    try {
        return std::stof(first_line);
    } catch (...) {
        return std::nullopt;
    }
}

namespace {

std::string read_trimmed(const std::filesystem::path &path) {
    std::ifstream f(path);
    std::string line;
    std::getline(f, line);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
    return line;
}

std::string find_hwmon_sensor() {
    std::error_code ec;
    if (!std::filesystem::exists("/sys/class/hwmon", ec))
        return {};
    for (const auto &entry :
        std::filesystem::directory_iterator("/sys/class/hwmon", ec)) {
        std::string name = read_trimmed(entry.path() / "name");
        if (gpu_temp_detail_is_gpu_hwmon_name(name))
            return entry.path() / "temp1_input";
    }
    return {};
}

bool nvidia_smi_on_path() {
    const char *path_env = getenv("PATH");
    if (!path_env)
        return false;
    std::string paths = path_env;
    size_t start = 0;
    while (start <= paths.size()) {
        size_t colon = paths.find(':', start);
        std::string dir =
            paths.substr(start, colon == std::string::npos ? std::string::npos
                                                           : colon - start);
        if (!dir.empty() && access((dir + "/nvidia-smi").c_str(), X_OK) == 0)
            return true;
        if (colon == std::string::npos)
            break;
        start = colon + 1;
    }
    return false;
}

} // namespace

void gpu_temp_init(GpuTempState &state) {
    state.sensor_path = find_hwmon_sensor();
    if (state.sensor_path.empty())
        state.nvidia_smi_present = nvidia_smi_on_path();
}

void gpu_temp_poll(GpuTempState &state) {
    if (!state.sensor_path.empty()) {
        std::ifstream f(state.sensor_path);
        long millidegrees = 0;
        state.celsius = (f >> millidegrees)
                            ? static_cast<float>(millidegrees) / 1000.0f
                            : -1.0f;
        return;
    }
    if (!state.nvidia_smi_present)
        return;
    if (async_process_pid(state.nvidia_smi_proc) > 0) {
        if (async_process_poll(state.nvidia_smi_proc)) {
            auto parsed = gpu_temp_detail_parse_nvidia_smi_output(
                state.nvidia_smi_proc.buffer);
            state.celsius = parsed.value_or(-1.0f);
        }
        return;
    }
    async_process_start(state.nvidia_smi_proc,
                        {"nvidia-smi", "--query-gpu=temperature.gpu",
                         "--format=csv,noheader,nounits"});
}

bool gpu_temp_available(const GpuTempState &state) {
    return state.celsius >= 0.0f;
}

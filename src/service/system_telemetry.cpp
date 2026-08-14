#include "service/system_telemetry.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

bool cpu_temp_detail_is_cpu_hwmon_name(const std::string &name) {
    return name == "coretemp" || name == "k10temp" || name == "zenpower";
}

bool cpu_temp_detail_is_cpu_thermal_zone_type(const std::string &type) {
    return type.starts_with("cpu");
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

std::string find_cpu_hwmon_sensor() {
    std::error_code ec;
    if (!std::filesystem::exists("/sys/class/hwmon", ec))
        return {};
    for (const auto &entry :
        std::filesystem::directory_iterator("/sys/class/hwmon", ec)) {
        std::string name = read_trimmed(entry.path() / "name");
        if (cpu_temp_detail_is_cpu_hwmon_name(name))
            return entry.path() / "temp1_input";
    }
    return {};
}

std::string find_thermal_zone_sensor() {
    std::error_code ec;
    if (!std::filesystem::exists("/sys/class/thermal", ec))
        return {};
    for (const auto &entry :
        std::filesystem::directory_iterator("/sys/class/thermal", ec)) {
        if (!entry.path().filename().string().starts_with("thermal_zone"))
            continue;
        std::string type = read_trimmed(entry.path() / "type");
        if (cpu_temp_detail_is_cpu_thermal_zone_type(type))
            return entry.path() / "temp";
    }
    return {};
}

} // namespace

void cpu_temp_init(CpuTempState &state) {
    state.sensor_path = find_cpu_hwmon_sensor();
    if (state.sensor_path.empty())
        state.sensor_path = find_thermal_zone_sensor();
}

void cpu_temp_poll(CpuTempState &state) {
    if (state.sensor_path.empty()) {
        state.celsius = -1.0f;
        return;
    }
    std::ifstream f(state.sensor_path);
    long millidegrees = 0;
    if (f >> millidegrees)
        state.celsius = static_cast<float>(millidegrees) / 1000.0f;
    else
        state.celsius = -1.0f;
}

bool cpu_temp_available(const CpuTempState &state) {
    return !state.sensor_path.empty() && state.celsius >= 0.0f;
}

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

std::string find_gpu_hwmon_sensor() {
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
    state.sensor_path = find_gpu_hwmon_sensor();
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

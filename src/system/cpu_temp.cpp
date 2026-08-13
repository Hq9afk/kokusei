#include "cpu_temp.h"

#include <filesystem>
#include <fstream>

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

std::string find_hwmon_sensor() {
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
    state.sensor_path = find_hwmon_sensor();
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

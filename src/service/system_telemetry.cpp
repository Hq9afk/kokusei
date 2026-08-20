#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/statvfs.h>
#include <unistd.h>

#include "service/system_telemetry.h"

namespace {

constexpr std::array<const char *, 11> kVirtualIfacePrefixes = {
    "lo",  "docker", "veth", "br-",       "virbr",   "vnet",
    "tun", "tap",    "wg",   "tailscale", "nordlynx"};

} // namespace

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

std::optional<float>
gpu_temp_detail_parse_nvidia_smi_output(const std::string &text) {
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

namespace {

std::string find_gpu_usage_sensor(const std::string &temp_hwmon_path) {
    if (temp_hwmon_path.empty())
        return {};
    std::filesystem::path device_dir =
        std::filesystem::path(temp_hwmon_path).parent_path() / "device";
    std::error_code ec;
    std::filesystem::path busy = device_dir / "gpu_busy_percent";
    return std::filesystem::exists(busy, ec) ? busy.string() : std::string();
}

} // namespace

void gpu_temp_init(GpuTempState &state) {
    state.sensor_path = find_gpu_hwmon_sensor();
    if (!state.sensor_path.empty())
        state.usage_sensor_path = find_gpu_usage_sensor(state.sensor_path);
    else
        state.nvidia_smi_present = nvidia_smi_on_path();
}

void gpu_temp_poll(GpuTempState &state) {
    if (!state.sensor_path.empty()) {
        std::ifstream f(state.sensor_path);
        long millidegrees = 0;
        state.celsius = (f >> millidegrees)
                            ? static_cast<float>(millidegrees) / 1000.0f
                            : -1.0f;
        if (!state.usage_sensor_path.empty()) {
            std::ifstream uf(state.usage_sensor_path);
            long pct = 0;
            state.usage_percent = (uf >> pct) ? static_cast<float>(pct) : -1.0f;
        }
        return;
    }
    if (!state.nvidia_smi_present)
        return;
    if (async_process_pid(state.nvidia_smi_proc) > 0) {
        if (async_process_poll(state.nvidia_smi_proc)) {
            const std::string &out = state.nvidia_smi_proc.buffer;
            size_t comma = out.find(',');
            auto temp_parsed = gpu_temp_detail_parse_nvidia_smi_output(
                comma == std::string::npos ? out : out.substr(0, comma));
            state.celsius = temp_parsed.value_or(-1.0f);
            if (comma != std::string::npos) {
                auto usage_parsed = gpu_temp_detail_parse_nvidia_smi_output(
                    out.substr(comma + 1));
                state.usage_percent = usage_parsed.value_or(-1.0f);
            }
        }
        return;
    }
    async_process_start(state.nvidia_smi_proc,
                        {"nvidia-smi",
                         "--query-gpu=temperature.gpu,utilization.gpu",
                         "--format=csv,noheader,nounits"});
}

bool gpu_temp_available(const GpuTempState &state) {
    return state.celsius >= 0.0f;
}

bool gpu_stats_available(const GpuTempState &state) {
    return state.celsius >= 0.0f && state.usage_percent >= 0.0f;
}

std::optional<CpuJiffies>
system_stats_detail_parse_proc_stat(const std::string &text) {
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

float system_stats_detail_cpu_usage(const CpuJiffies &prev,
                                    const CpuJiffies &cur) {
    if (cur.total <= prev.total)
        return -1.0f;
    uint64_t total_delta = cur.total - prev.total;
    uint64_t idle_delta = cur.idle - prev.idle;
    if (idle_delta > total_delta)
        return -1.0f;
    return static_cast<float>(total_delta - idle_delta) /
           static_cast<float>(total_delta);
}

std::optional<MemInfo>
system_stats_detail_parse_proc_meminfo(const std::string &text) {
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

std::optional<float>
system_stats_detail_parse_cpu_freq_avg_mhz(const std::string &cpuinfo_text) {
    std::istringstream ss(cpuinfo_text);
    std::string line;
    double sum = 0.0;
    int count = 0;
    while (std::getline(ss, line)) {
        if (!line.starts_with("cpu MHz"))
            continue;
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        try {
            sum += std::stod(line.substr(colon + 1));
            ++count;
        } catch (...) {
            continue;
        }
    }
    if (count == 0)
        return std::nullopt;
    return static_cast<float>(sum / count);
}

std::optional<DiskUsage> system_stats_detail_disk_usage(const char *path) {
    struct statvfs vfs;
    if (statvfs(path, &vfs) != 0)
        return std::nullopt;
    uint64_t total = static_cast<uint64_t>(vfs.f_blocks) * vfs.f_frsize;
    uint64_t free = static_cast<uint64_t>(vfs.f_bfree) * vfs.f_frsize;
    if (total == 0 || free > total)
        return std::nullopt;
    return DiskUsage{total - free, total};
}

std::optional<NetBytes>
system_stats_detail_parse_proc_net_dev(const std::string &text) {
    std::istringstream ss(text);
    std::string line;
    std::getline(ss, line);
    std::getline(ss, line);
    NetBytes total;
    bool any = false;
    while (std::getline(ss, line)) {
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string iface = line.substr(0, colon);
        size_t start = iface.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        iface = iface.substr(start);
        bool is_virtual = false;
        for (const char *prefix : kVirtualIfacePrefixes) {
            if (iface.starts_with(prefix)) {
                is_virtual = true;
                break;
            }
        }
        if (is_virtual)
            continue;
        std::istringstream ls(line.substr(colon + 1));
        uint64_t values[16] = {};
        int n = 0;
        uint64_t v;
        while (n < 16 && ls >> v)
            values[n++] = v;
        if (n < 9)
            continue;
        total.rx_bytes += values[0];
        total.tx_bytes += values[8];
        any = true;
    }
    if (!any)
        return std::nullopt;
    return total;
}

std::string system_stats_detail_format_speed(double bytes_per_sec) {
    static constexpr const char *kUnits[] = {"KiB", "MiB", "GiB"};
    double v = bytes_per_sec / 1024.0;
    size_t i = 0;
    while (v >= 1024.0 && i < 2) {
        v /= 1024.0;
        ++i;
    }
    if (v < 0.0)
        v = 0.0;
    char buf[32];
    if (v < 10.0)
        std::snprintf(buf, sizeof(buf), "%.1f%s", v, kUnits[i]);
    else
        std::snprintf(buf, sizeof(buf), "%.0f%s", v, kUnits[i]);
    return buf;
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

    auto mem =
        system_stats_detail_parse_proc_meminfo(read_file("/proc/meminfo"));
    if (mem) {
        state.mem_usage = system_stats_detail_mem_usage(*mem);
        state.mem_used_gb =
            static_cast<float>(mem->total_kb - mem->available_kb) / 1048576.0f;
        state.mem_total_gb = static_cast<float>(mem->total_kb) / 1048576.0f;
    } else {
        state.mem_usage = -1.0f;
        state.mem_used_gb = -1.0f;
        state.mem_total_gb = -1.0f;
    }

    auto freq_mhz =
        system_stats_detail_parse_cpu_freq_avg_mhz(read_file("/proc/cpuinfo"));
    state.cpu_freq_ghz = freq_mhz ? *freq_mhz / 1000.0f : -1.0f;

    auto disk = system_stats_detail_disk_usage("/");
    if (disk) {
        state.disk_used_gb =
            static_cast<float>(disk->used_bytes) / 1073741824.0f;
        state.disk_total_gb =
            static_cast<float>(disk->total_bytes) / 1073741824.0f;
        state.disk_pct = static_cast<float>(disk->used_bytes) * 100.0f /
                         static_cast<float>(disk->total_bytes);
    } else {
        state.disk_used_gb = -1.0f;
        state.disk_total_gb = -1.0f;
        state.disk_pct = -1.0f;
    }

    auto net =
        system_stats_detail_parse_proc_net_dev(read_file("/proc/net/dev"));
    auto now = std::chrono::steady_clock::now();
    if (net) {
        if (state.have_prev_net) {
            double dt = std::chrono::duration<double>(now - state.prev_net_time)
                            .count();
            if (dt > 0.0) {
                state.net_rx_bps =
                    std::max<int64_t>(
                        0, static_cast<int64_t>(net->rx_bytes -
                                                state.prev_net.rx_bytes)) /
                    dt;
                state.net_tx_bps =
                    std::max<int64_t>(
                        0, static_cast<int64_t>(net->tx_bytes -
                                                state.prev_net.tx_bytes)) /
                    dt;
            }
        }
        state.prev_net = *net;
        state.prev_net_time = now;
        state.have_prev_net = true;
    } else {
        state.net_rx_bps = -1.0;
        state.net_tx_bps = -1.0;
    }
}

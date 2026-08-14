#include "service/system_telemetry.h"

#include <cassert>

void test_cpu_temp() {
    assert(cpu_temp_detail_is_cpu_hwmon_name("coretemp"));
    assert(cpu_temp_detail_is_cpu_hwmon_name("k10temp"));
    assert(cpu_temp_detail_is_cpu_hwmon_name("zenpower"));
    assert(!cpu_temp_detail_is_cpu_hwmon_name("amdgpu"));
    assert(!cpu_temp_detail_is_cpu_hwmon_name(""));

    assert(cpu_temp_detail_is_cpu_thermal_zone_type("cpu-thermal"));
    assert(cpu_temp_detail_is_cpu_thermal_zone_type("cpu"));
    assert(!cpu_temp_detail_is_cpu_thermal_zone_type("gpu-thermal"));
    assert(!cpu_temp_detail_is_cpu_thermal_zone_type(""));

    CpuTempState state;
    assert(!cpu_temp_available(state));
}

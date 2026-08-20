#include <cassert>

#include "service/system_telemetry.h"

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

    assert(cpu_temp_detail_core_label_index("Core 0") == 0);
    assert(cpu_temp_detail_core_label_index("Core 9") == 9);
    assert(cpu_temp_detail_core_label_index("Core 10") == 10);
    assert(cpu_temp_detail_core_label_index("Core 9") <
           cpu_temp_detail_core_label_index("Core 10"));
    assert(cpu_temp_detail_core_label_index("") == -1);
    assert(cpu_temp_detail_core_label_index("Package id 0") == 0);

    CpuTempState state;
    assert(!cpu_temp_available(state));
}

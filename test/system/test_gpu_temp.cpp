#include <cassert>

#include "service/system_telemetry.h"

void test_gpu_temp() {
    assert(gpu_temp_detail_is_gpu_hwmon_name("amdgpu"));
    assert(gpu_temp_detail_is_gpu_hwmon_name("i915"));
    assert(gpu_temp_detail_is_gpu_hwmon_name("xe"));
    assert(!gpu_temp_detail_is_gpu_hwmon_name("coretemp"));
    assert(!gpu_temp_detail_is_gpu_hwmon_name(""));

    auto ok = gpu_temp_detail_parse_nvidia_smi_output("62\n");
    assert(ok.has_value());
    assert(*ok == 62.0f);

    auto empty = gpu_temp_detail_parse_nvidia_smi_output("");
    assert(!empty.has_value());

    auto garbage = gpu_temp_detail_parse_nvidia_smi_output("not-a-number\n");
    assert(!garbage.has_value());

    GpuTempState state;
    assert(!gpu_temp_available(state));
}

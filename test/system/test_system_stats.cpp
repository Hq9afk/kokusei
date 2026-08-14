#include "system/system_stats.h"

#include <cassert>

void test_system_stats() {
    {
        std::string text = "cpu  100 0 100 800 0 0 0 0 0 0\n"
                           "cpu0 100 0 100 800 0 0 0 0 0 0\n";
        auto j = system_stats_detail_parse_proc_stat(text);
        assert(j.has_value());
        assert(j->total == 1000);
        assert(j->idle == 800);
    }
    assert(!system_stats_detail_parse_proc_stat("").has_value());
    assert(!system_stats_detail_parse_proc_stat("notcpu 1 2 3\n").has_value());

    {
        CpuJiffies prev{800, 1000};
        CpuJiffies cur{850, 1100};
        float usage = system_stats_detail_cpu_usage(prev, cur);
        assert(usage > 0.49f && usage < 0.51f);
    }
    assert(system_stats_detail_cpu_usage({800, 1000}, {800, 1000}) < 0.0f);

    {
        std::string text = "MemTotal:       16384000 kB\n"
                           "MemFree:         2000000 kB\n"
                           "MemAvailable:    8192000 kB\n";
        auto m = system_stats_detail_parse_proc_meminfo(text);
        assert(m.has_value());
        assert(m->total_kb == 16384000);
        assert(m->available_kb == 8192000);
        float usage = system_stats_detail_mem_usage(*m);
        assert(usage > 0.49f && usage < 0.51f);
    }
    assert(!system_stats_detail_parse_proc_meminfo("garbage\n").has_value());

    SystemStatsState state;
    assert(!state.have_prev);
}

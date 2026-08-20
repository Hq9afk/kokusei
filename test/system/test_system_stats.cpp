#include <cassert>

#include "service/system_telemetry.h"

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

    {
        std::string text = "processor : 0\ncpu MHz : 2400.000\n"
                           "processor : 1\ncpu MHz : 2600.000\n";
        auto freq = system_stats_detail_parse_cpu_freq_avg_mhz(text);
        assert(freq.has_value());
        assert(*freq > 2499.0f && *freq < 2501.0f);
    }
    assert(!system_stats_detail_parse_cpu_freq_avg_mhz("no such line\n")
                .has_value());

    {
        std::string text =
            "Inter-|   Receive                                                "
            "|  Transmit\n face |bytes packets errs drop fifo frame "
            "compressed multicast|bytes packets errs drop fifo colls "
            "carrier compressed\n"
            "    lo: 100 1 0 0 0 0 0 0 100 1 0 0 0 0 0 0\n"
            "  eth0: 1000 2 0 0 0 0 0 0 2000 3 0 0 0 0 0 0\n";
        auto net = system_stats_detail_parse_proc_net_dev(text);
        assert(net.has_value());
        assert(net->rx_bytes == 1000);
        assert(net->tx_bytes == 2000);
    }
    assert(!system_stats_detail_parse_proc_net_dev("").has_value());

    assert(system_stats_detail_format_speed(512) == "0.5KiB");
    assert(system_stats_detail_format_speed(15.0 * 1024) == "15KiB");
    assert(system_stats_detail_format_speed(2.5 * 1024 * 1024) == "2.5MiB");

    auto disk = system_stats_detail_disk_usage("/");
    assert(disk.has_value());
    assert(disk->total_bytes > 0);
    assert(disk->used_bytes <= disk->total_bytes);
    assert(!system_stats_detail_disk_usage("/does/not/exist").has_value());

    SystemStatsState state;
    assert(!state.have_prev);
    assert(!state.have_prev_net);
}

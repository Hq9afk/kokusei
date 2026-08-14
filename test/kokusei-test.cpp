#include "kokusei-test.hpp"

#include <cstdio>
#include <iterator>

int main() {
    struct Case {
        const char *name;
        void (*fn)();
    };
    Case cases[] = {
        {"config", test_config},
        {"config_watch", test_config_watch},
        {"monitor_overrides", test_monitor_overrides},
        {"wallpaper_resolve", test_wallpaper_resolve},

        {"async_process", test_async_process},
        {"deferred_call", test_deferred_call},
        {"poll_source", test_poll_source},

        {"network_parse", test_network_parse},
        {"bluetooth", test_bluetooth},

        {"spawn_helpers", test_spawn_helpers},
        {"desktop_entry", test_desktop_entry},
        {"visit_store", test_visit_store},
        {"apps_provider", test_apps_provider},
        {"files_provider", test_files_provider},
        {"search", test_search},
        {"submenu", test_submenu},
        {"launch_action", test_launch_action},
        {"icon_theme", test_icon_theme},

        {"keyboard", test_keyboard},
        {"active_output", test_active_output},

        {"rfkill", test_rfkill},
        {"cpu_temp", test_cpu_temp},
        {"gpu_temp", test_gpu_temp},
        {"system_stats", test_system_stats},
        {"mpris", test_mpris},

        {"animation", test_animation},
        {"palette", test_palette},
        {"image_decode", test_image_decode},
    };
    for (auto &c : cases) {
        std::printf("[ RUN ] %s\n", c.name);
        c.fn();
        std::printf("[ OK  ] %s\n", c.name);
    }
    std::printf("All %zu tests passed.\n", std::size(cases));
}

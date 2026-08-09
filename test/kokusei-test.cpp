#include "kokusei-test.hpp"

#include <cstdio>
#include <iterator>

int main() {
    struct Case { const char *name; void (*fn)(); };
    Case cases[] = {
        {"config", test_config},
        {"config_watch", test_config_watch},

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

        {"rfkill", test_rfkill},

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


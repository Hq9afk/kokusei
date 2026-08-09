
#include "../../src/launcher/apps_provider.hpp"

#include <cassert>
#include <cmath>

void test_apps_provider() {
    assert(score_app("Firefox", "fire") > score_app("Bonfire", "fire"));
    assert(std::abs(score_app("nomatch", "xyz") - (-1.0f)) < 0.001f);
    assert(score_app("Firefox", "") < 0.0f);
    assert(score_app("", "firefox") < 0.0f);

    DesktopEntry firefox;
    firefox.id = "firefox.desktop";
    firefox.name = "Firefox";
    firefox.exec = "firefox";

    DesktopEntry bonfire;
    bonfire.id = "bonfire.desktop";
    bonfire.name = "Bonfire";
    bonfire.exec = "bonfire";

    DesktopEntry unrelated;
    unrelated.id = "calc.desktop";
    unrelated.name = "Calculator";
    unrelated.exec = "calc";

    std::vector<DesktopEntry> entries = {firefox, bonfire, unrelated};
    auto results = search_apps(entries, "fire");
    assert(results.size() == 2);

    assert(results[0].entry->name == "Firefox" ||
           results[1].entry->name == "Firefox");
    for (const auto &r : results)
        assert(r.entry->name != "Calculator");
}

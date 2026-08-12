
#include "../../src/app/config.h"

#include <cassert>

void test_wallpaper_resolve() {
    Config cfg;
    cfg.wallpaper_path = "/global.png";
    cfg.wallpaper_columns["DP-1"] = {"/dp1.png"};
    cfg.wallpaper_fill_modes["DP-1"] = "fit";

    assert(wallpaper_effective_column_path(cfg, "DP-1", 0) == "/dp1.png");
    assert(wallpaper_effective_column_path(cfg, "HDMI-1", 0) == "/global.png");
    assert(wallpaper_effective_fill_mode(cfg, "DP-1") == "fit");
    assert(wallpaper_effective_fill_mode(cfg, "HDMI-1") == "crop");

    assert(wallpaper_effective_column_path(cfg, "DP-1", 1).empty());
    assert(wallpaper_effective_column_count(cfg, "DP-1") == 1);
    cfg.wallpaper_column_counts["DP-1"] = 2;
    assert(wallpaper_effective_column_count(cfg, "DP-1") == 2);
}

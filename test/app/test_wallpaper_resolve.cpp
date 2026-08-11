
#include "../../src/app/config.hpp"

#include <cassert>

void test_wallpaper_resolve() {
    Config cfg;
    cfg.wallpaper_path = "/global.png";
    cfg.wallpaper_paths["DP-1"] = "/dp1.png";
    cfg.wallpaper_fill_modes["DP-1"] = "fit";

    assert(wallpaper_effective_path(cfg, "DP-1") == "/dp1.png");
    assert(wallpaper_effective_path(cfg, "HDMI-1") == "/global.png");
    assert(wallpaper_effective_fill_mode(cfg, "DP-1") == "fit");
    assert(wallpaper_effective_fill_mode(cfg, "HDMI-1") == "crop");
}

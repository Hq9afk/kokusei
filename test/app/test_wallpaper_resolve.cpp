
#include <cassert>

#include "service/wallpaper_service.h"

void test_wallpaper_resolve() {
    Config cfg;
    cfg.wallpaper_path = "/global.png";
    cfg.wallpaper_columns["DP-1"] = {"/dp1.png"};
    cfg.wallpaper_fill_modes["DP-1"] = {"fit", "tile"};

    assert(wallpaper_service_column_path(cfg, "DP-1", 0) == "/dp1.png");
    assert(wallpaper_service_column_path(cfg, "HDMI-1", 0) == "/global.png");
    assert(wallpaper_service_fill_mode(cfg, "DP-1", 0) == "fit");
    assert(wallpaper_service_fill_mode(cfg, "DP-1", 1) == "tile");
    assert(wallpaper_service_fill_mode(cfg, "DP-1", 2) == "crop");
    assert(wallpaper_service_fill_mode(cfg, "HDMI-1", 0) == "crop");

    assert(wallpaper_service_column_path(cfg, "DP-1", 1) == "/global.png");
    assert(wallpaper_service_column_path(cfg, "HDMI-1", 3) == "/global.png");
    assert(wallpaper_service_column_count(cfg, "DP-1") == 1);
    cfg.wallpaper_column_counts["DP-1"] = 2;
    assert(wallpaper_service_column_count(cfg, "DP-1") == 2);

    cfg.default_wallpaper_enabled = false;
    assert(wallpaper_service_column_path(cfg, "HDMI-1", 0).empty());
    assert(wallpaper_service_column_path(cfg, "DP-1", 1).empty());
    assert(wallpaper_service_column_path(cfg, "DP-1", 0) == "/dp1.png");

    assert(wallpaper_service_column_override(cfg, "HDMI-1", 0).empty());
    assert(wallpaper_service_column_override(cfg, "DP-1", 0) == "/dp1.png");
    cfg.default_wallpaper_enabled = true;
    assert(wallpaper_service_column_override(cfg, "HDMI-1", 0).empty());

    cfg.wallpaper_animated_columns["DP-1"] = {"/dp1.mp4"};
    cfg.wallpaper_animated_fill_modes["DP-1"] = {"fit"};
    assert(wallpaper_service_animated_column_path(cfg, "DP-1", 0) ==
           "/dp1.mp4");
    assert(wallpaper_service_animated_column_path(cfg, "HDMI-1", 0) ==
           "/global.png");
    assert(
        wallpaper_service_animated_column_override(cfg, "HDMI-1", 0).empty());
    assert(wallpaper_service_animated_column_override(cfg, "DP-1", 0) ==
           "/dp1.mp4");
    assert(wallpaper_service_animated_fill_mode(cfg, "DP-1", 0) == "fit");
    assert(wallpaper_service_animated_fill_mode(cfg, "HDMI-1", 0) == "crop");
    assert(wallpaper_service_animated_column_count(cfg, "DP-1") == 1);
    cfg.wallpaper_animated_column_counts["DP-1"] = 3;
    assert(wallpaper_service_animated_column_count(cfg, "DP-1") == 3);

    cfg.default_wallpaper_enabled = false;
    assert(wallpaper_service_animated_column_path(cfg, "HDMI-1", 0).empty());
    assert(wallpaper_service_animated_column_path(cfg, "DP-1", 0) ==
           "/dp1.mp4");
}

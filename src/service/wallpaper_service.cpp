#include "service/wallpaper_service.h"

int wallpaper_service_column_count(const Config &cfg,
                                   const std::string &monitor_name) {
    auto it = cfg.wallpaper_column_counts.find(monitor_name);
    return it != cfg.wallpaper_column_counts.end() && it->second > 0
              ? it->second
              : 1;
}

std::string wallpaper_service_column_path(const Config &cfg,
                                          const std::string &monitor_name,
                                          int column_index) {
    auto it = cfg.wallpaper_columns.find(monitor_name);
    if (it != cfg.wallpaper_columns.end() && column_index >= 0 &&
        static_cast<size_t>(column_index) < it->second.size() &&
        !it->second[static_cast<size_t>(column_index)].empty())
        return it->second[static_cast<size_t>(column_index)];
    return column_index == 0 ? cfg.wallpaper_path : "";
}

std::string wallpaper_service_fill_mode(const Config &cfg,
                                        const std::string &monitor_name) {
    auto it = cfg.wallpaper_fill_modes.find(monitor_name);
    if (it != cfg.wallpaper_fill_modes.end() && !it->second.empty())
        return it->second;
    return "crop";
}

#pragma once

#include "app/config.h"

#include <string>

int wallpaper_service_column_count(const Config &cfg,
                                   const std::string &monitor_name);

std::string wallpaper_service_column_path(const Config &cfg,
                                          const std::string &monitor_name,
                                          int column_index);

std::string wallpaper_service_fill_mode(const Config &cfg,
                                        const std::string &monitor_name);

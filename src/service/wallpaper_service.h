#pragma once

#include "app/config.h"

#include <string>

unsigned char *wallpaper_decode_scaled(const std::string &path, int target_w,
                                       int target_h, int &out_width,
                                       int &out_height);

int wallpaper_service_column_count(const Config &cfg,
                                   const std::string &monitor_name);

std::string wallpaper_service_column_path(const Config &cfg,
                                          const std::string &monitor_name,
                                          int column_index);

std::string wallpaper_service_fill_mode(const Config &cfg,
                                        const std::string &monitor_name);

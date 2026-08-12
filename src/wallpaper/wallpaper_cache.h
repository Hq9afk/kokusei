#pragma once

#include <ctime>
#include <string>

std::string wallpaper_cache_path(const std::string &path, time_t mtime,
                                 int target_w, int target_h);

unsigned char *wallpaper_cache_read(const std::string &cache_path,
                                    int &out_width, int &out_height);

void wallpaper_cache_write(const std::string &cache_path, int width,
                           int height, const unsigned char *data);

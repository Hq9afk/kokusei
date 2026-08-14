#pragma once

#include "render/texture.h"
#include <EGL/egl.h>

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

struct WallpaperPickerState {
    std::string dir;
    bool scanning = false;
    std::vector<std::string> files;
    std::map<std::string, Texture> thumbnails;
    std::set<std::string> pending;
    uint64_t scan_generation = 0;

    std::function<void()> request_frame;
};

bool wallpaper_picker_less(const std::string &a, const std::string &b);

void wallpaper_picker_scan(WallpaperPickerState &state, std::string dir);

void wallpaper_picker_request_thumbnail(WallpaperPickerState &state,
                                        const std::string &path,
                                        int target_size, EGLDisplay display,
                                        EGLSurface surface, EGLContext context);

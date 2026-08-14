#pragma once

#include "render/texture.h"

#include <EGL/egl.h>

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

struct Config;
struct Node;
struct PanelClickRegion;
struct SettingsState;

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

float wallpaper_tab_paint(SettingsState &state, Node *root, int32_t scale,
                          float x, float y, const Config &cfg);

bool wallpaper_tab_handle_click(SettingsState &state, const Config &cfg,
                                const std::function<void(Config)> &on_commit,
                                const PanelClickRegion &region);

void wallpaper_tab_handle_scroll(SettingsState &state, double dy);

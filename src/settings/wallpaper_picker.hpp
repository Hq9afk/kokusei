#pragma once

#include "../core/deferred_call.hpp"
#include "../core/log.hpp"
#include "../render/texture.hpp"
#include "../wallpaper/wallpaper.hpp"

#include <EGL/egl.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct WallpaperPickerState {
    std::string dir;
    bool scanning = false;
    std::vector<std::string> files; // PNG/JPEG only, full paths
    std::map<std::string, Texture> thumbnails;
    std::set<std::string> pending;
    uint64_t scan_generation = 0;
};

inline bool wallpaper_picker_is_image(const std::string &path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
}

inline std::string wallpaper_picker_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Extension first (so .jpg/.png each cluster together), then filename,
// both case-insensitive.
inline bool wallpaper_picker_less(const std::string &a, const std::string &b) {
    std::filesystem::path pa(a), pb(b);
    std::string ea = wallpaper_picker_lower(pa.extension().string());
    std::string eb = wallpaper_picker_lower(pb.extension().string());
    if (ea != eb)
        return ea < eb;
    return wallpaper_picker_lower(pa.filename().string()) <
           wallpaper_picker_lower(pb.filename().string());
}

// find -L $dir -maxdepth 2: direct children of dir plus one level of
// subdirectory contents.
inline void wallpaper_picker_scan(WallpaperPickerState &state,
                                  std::string dir) {
    state.dir = dir;
    state.scanning = true;
    state.thumbnails.clear();
    state.pending.clear();
    uint64_t generation = ++state.scan_generation;
    std::thread([&state, dir, generation] {
        std::vector<std::string> found;
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(
            dir, std::filesystem::directory_options::skip_permission_denied,
            ec);
        std::filesystem::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            if (it.depth() >= 1)
                it.disable_recursion_pending();
            if (it->is_regular_file(ec) &&
                wallpaper_picker_is_image(it->path().string()))
                found.push_back(it->path().string());
        }
        std::sort(found.begin(), found.end(), wallpaper_picker_less);
        DeferredCall::call_later([&state, found = std::move(found),
                                  generation] {
            if (generation != state.scan_generation)
                return;
            state.files = std::move(found);
            state.scanning = false;
        });
    }).detach();
}

inline void wallpaper_picker_request_thumbnail(WallpaperPickerState &state,
                                               const std::string &path,
                                               int target_size,
                                               EGLDisplay display,
                                               EGLSurface surface,
                                               EGLContext context) {
    if (state.thumbnails.count(path) || state.pending.count(path))
        return;
    state.pending.insert(path);
    uint64_t generation = state.scan_generation;
    std::thread([&state, path, target_size, generation, display, surface,
                context] {
        int w = 0, h = 0;
        unsigned char *data = wallpaper_decode_scaled(
            path, target_size, target_size, w, h);
        DeferredCall::call_later([&state, path, data, w, h, generation,
                                  display, surface, context] {
            state.pending.erase(path);
            if (generation != state.scan_generation) {
                delete[] data;
                return;
            }
            if (!data)
                return;
            eglMakeCurrent(display, surface, surface, context);
            state.thumbnails[path] = make_texture_rgba(w, h, data, true);
            delete[] data;
        });
    }).detach();
}

#include "wallpaper_picker.h"

#include "../core/deferred_call.h"

#include <algorithm>
#include <filesystem>
#include <thread>

namespace {

bool wallpaper_picker_is_image(const std::string &path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
}

std::string wallpaper_picker_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

} // namespace

bool wallpaper_picker_less(const std::string &a, const std::string &b) {
    std::filesystem::path pa(a), pb(b);
    std::string ea = wallpaper_picker_lower(pa.extension().string());
    std::string eb = wallpaper_picker_lower(pb.extension().string());
    if (ea != eb)
        return ea < eb;
    return wallpaper_picker_lower(pa.filename().string()) <
           wallpaper_picker_lower(pb.filename().string());
}

void wallpaper_picker_scan(WallpaperPickerState &state, std::string dir) {
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
            if (state.request_frame)
                state.request_frame();
        });
    }).detach();
}

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <sys/stat.h>
#include <thread>

#include "core/async_process.h"
#include "core/log.h"

#include "service/wallpaper_animate_service.h"

namespace {

std::string wallpaper_animate_cache_dir(const char *subdir) {
    const char *cache_home = getenv("XDG_CACHE_HOME");
    std::string base =
        cache_home && *cache_home
            ? std::string(cache_home)
            : std::string(getenv("HOME") ? getenv("HOME") : "") + "/.cache";
    std::error_code ec;
    std::filesystem::create_directories(
        base + "/kokusei/wallpaper-animated/" + subdir, ec);
    return base + "/kokusei/wallpaper-animated/" + subdir;
}

std::string wallpaper_animate_cache_key(const std::string &path, time_t mtime) {
    std::string key = path + ":" + std::to_string(mtime);
    return std::to_string(std::hash<std::string>{}(key));
}

bool run_and_wait(const std::vector<std::string> &argv) {
    AsyncProcess proc;
    if (async_process_start(proc, argv, true) <= 0)
        return false;
    while (!async_process_poll(proc))
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return true;
}

bool file_exists(const std::string &path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

} // namespace

std::string wallpaper_animate_prepare(const std::string &source_path) {
    struct stat st{};
    if (stat(source_path.c_str(), &st) != 0)
        return "";
    std::string cache_path =
        wallpaper_animate_cache_dir("optimized") + "/" +
        wallpaper_animate_cache_key(source_path, st.st_mtime) + ".mp4";
    if (file_exists(cache_path))
        return cache_path;

    std::error_code ec;
    std::filesystem::copy_file(
        source_path, cache_path,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec || !file_exists(cache_path)) {
        klog("wallpaper_animate: prepare failed for '%s'", source_path.c_str());
        return "";
    }
    return cache_path;
}

std::string
wallpaper_animate_thumbnail_prepare(const std::string &source_path) {
    struct stat st{};
    if (stat(source_path.c_str(), &st) != 0)
        return "";
    std::string cache_path =
        wallpaper_animate_cache_dir("thumbs") + "/" +
        wallpaper_animate_cache_key(source_path, st.st_mtime) + ".jpg";
    if (file_exists(cache_path))
        return cache_path;

    bool ok =
        run_and_wait({"ffmpeg", "-y", "-ss", "0", "-i", source_path,
                      "-frames:v", "1", "-vf", "scale=320:-1", cache_path});
    if (!ok || !file_exists(cache_path)) {
        klog("wallpaper_animate: thumbnail prepare failed for '%s'",
             source_path.c_str());
        return "";
    }
    return cache_path;
}

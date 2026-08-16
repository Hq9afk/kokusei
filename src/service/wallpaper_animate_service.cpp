#include "service/wallpaper_animate_service.h"

#include "config/wallpaper_config.h"
#include "core/async_process.h"
#include "core/log.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sys/stat.h>
#include <thread>

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

std::string wallpaper_animate_cache_key(const std::string &path, time_t mtime,
                                        int target_h) {
    std::string key =
        path + ":" + std::to_string(mtime) + ":" + std::to_string(target_h);
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

bool parse_ffprobe_height_fps(const std::string &csv_line, int &out_height,
                              double &out_fps) {
    size_t comma = csv_line.find(',');
    if (comma == std::string::npos)
        return false;
    out_height = atoi(csv_line.substr(0, comma).c_str());
    std::string fps_field = csv_line.substr(comma + 1);
    size_t slash = fps_field.find('/');
    double num = atof(fps_field.substr(0, slash).c_str());
    double den = slash == std::string::npos
                     ? 1.0
                     : atof(fps_field.substr(slash + 1).c_str());
    out_fps = den > 0.0 ? num / den : 0.0;
    return out_height > 0 && out_fps > 0.0;
}

} // namespace

std::string wallpaper_animate_prepare(const std::string &source_path,
                                      int target_h) {
    struct stat st{};
    if (stat(source_path.c_str(), &st) != 0)
        return "";
    std::string cache_path =
        wallpaper_animate_cache_dir("optimized") + "/" +
        wallpaper_animate_cache_key(source_path, st.st_mtime, target_h) +
        ".mp4";
    if (file_exists(cache_path))
        return cache_path;

    AsyncProcess probe;
    if (async_process_start(probe,
                            {"ffprobe", "-v", "error", "-select_streams", "v:0",
                             "-show_entries", "stream=height,avg_frame_rate",
                             "-of", "csv=p=0", source_path}) <= 0) {
        klog("wallpaper_animate: ffprobe not runnable for '%s'",
             source_path.c_str());
        return "";
    }
    while (!async_process_poll(probe))
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::string csv_line;
    {
        std::lock_guard<std::mutex> lock(probe.mutex);
        csv_line = probe.buffer;
    }

    int src_h = 0;
    double fps = 0.0;
    bool within_caps = parse_ffprobe_height_fps(csv_line, src_h, fps) &&
                       src_h <= kAnimatedWallpaperMaxHeight &&
                       fps <= static_cast<double>(kAnimatedWallpaperMaxFps);

    bool ok;
    if (within_caps) {
        std::error_code ec;
        std::filesystem::copy_file(
            source_path, cache_path,
            std::filesystem::copy_options::overwrite_existing, ec);
        ok = !ec;
    } else {
        std::string scale_fps =
            "scale=-2:" + std::to_string(kAnimatedWallpaperMaxHeight) +
            ",fps=" + std::to_string(kAnimatedWallpaperMaxFps);
        ok = run_and_wait({"ffmpeg", "-y", "-i", source_path, "-vf", scale_fps,
                           "-c:v", "libx264", "-preset", "veryfast",
                           "-movflags", "+faststart", cache_path});
    }
    if (!ok || !file_exists(cache_path)) {
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
        wallpaper_animate_cache_key(source_path, st.st_mtime, 0) + ".jpg";
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

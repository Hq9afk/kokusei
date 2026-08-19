#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

using WallpaperHwDecodeFrameCallback =
    std::function<void(unsigned char *rgba, int width, int height)>;

struct WallpaperHwDecodePlayback {
    std::thread worker;
    std::shared_ptr<std::atomic<bool>> stop_flag;
};

WallpaperHwDecodePlayback
wallpaper_hw_decode_start(const std::string &path, const std::string &filter_desc,
                          int fps, WallpaperHwDecodeFrameCallback on_frame);

void wallpaper_hw_decode_stop(WallpaperHwDecodePlayback &playback);

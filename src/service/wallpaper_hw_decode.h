#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

using WallpaperHwDecodeFrameCallback = std::function<void(
    unsigned char *rgba, int width, int height, int stride_px)>;

struct WallpaperHwDrmPlane {
    int fd = -1;
    uint64_t modifier = 0;
    int offset = 0;
    int pitch = 0;
};

struct WallpaperHwDrmFrame {
    WallpaperHwDrmPlane planes[2];
    int plane_count = 0;
    int width = 0;
    int height = 0;
    void *avframe_handle = nullptr;
};

using WallpaperHwDecodeDrmFrameCallback =
    std::function<void(WallpaperHwDrmFrame frame)>;

enum class WallpaperHwDecodeStatus { Idle, ZeroCopy, CpuFallback };

struct WallpaperHwDecodePlayback {
    std::thread worker;
    std::shared_ptr<std::atomic<bool>> stop_flag;
    std::shared_ptr<std::atomic<bool>> pause_flag;
    std::shared_ptr<std::atomic<WallpaperHwDecodeStatus>> status;
    std::shared_ptr<std::atomic<bool>> egl_import_failed;

    WallpaperHwDecodePlayback() = default;
    WallpaperHwDecodePlayback(const WallpaperHwDecodePlayback &) = delete;
    WallpaperHwDecodePlayback &
    operator=(const WallpaperHwDecodePlayback &) = delete;
    WallpaperHwDecodePlayback(WallpaperHwDecodePlayback &&) = default;
    WallpaperHwDecodePlayback &
    operator=(WallpaperHwDecodePlayback &&other) noexcept {
        if (this != &other) {
            join();
            worker = std::move(other.worker);
            stop_flag = std::move(other.stop_flag);
            pause_flag = std::move(other.pause_flag);
            status = std::move(other.status);
            egl_import_failed = std::move(other.egl_import_failed);
        }
        return *this;
    }
    ~WallpaperHwDecodePlayback() { join(); }

  private:
    void join() {
        if (stop_flag)
            stop_flag->store(true);
        if (worker.joinable())
            worker.join();
    }
};

WallpaperHwDecodePlayback wallpaper_hw_decode_start(
    const std::string &path, const std::string &filter_desc, int fps,
    bool supports_row_length, WallpaperHwDecodeFrameCallback on_frame,
    WallpaperHwDecodeDrmFrameCallback on_drm_frame = nullptr);

void wallpaper_hw_decode_stop(WallpaperHwDecodePlayback &playback);
void wallpaper_hw_decode_pause(WallpaperHwDecodePlayback &playback);
void wallpaper_hw_decode_resume(WallpaperHwDecodePlayback &playback);

WallpaperHwDecodeStatus
wallpaper_hw_decode_status(const WallpaperHwDecodePlayback &playback);

void wallpaper_hw_decode_release_drm_frame(void *avframe_handle);

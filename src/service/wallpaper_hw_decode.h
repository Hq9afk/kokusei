#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

// stride_px is the row length in pixels the buffer was allocated with; 0
// means the buffer is tightly packed (stride == width).
using WallpaperHwDecodeFrameCallback =
    std::function<void(unsigned char *rgba, int width, int height, int stride_px)>;

// One dma-buf plane of a zero-copy VAAPI decode surface, ready to import as
// an EGLImage. No pixel data: fd is a handle into GPU memory.
struct WallpaperHwDrmPlane {
    int fd = -1;
    uint32_t fourcc = 0;
    uint64_t modifier = 0;
    int offset = 0;
    int pitch = 0;
    int width = 0;
    int height = 0;
};

// An NV12 frame still living in VAAPI decode memory. avframe_handle is an
// opaque owning reference (AVFrame*) that pins the underlying VASurface;
// the caller must pass it to wallpaper_hw_decode_release_drm_frame once it
// is done using the planes (i.e. once the GL import completes and, ideally,
// only after the previous frame's import has been released too, to give
// the driver a frame of slack before it recycles the decode surface).
struct WallpaperHwDrmFrame {
    WallpaperHwDrmPlane planes[2];
    int plane_count = 0;
    int width = 0;
    int height = 0;
    void *avframe_handle = nullptr;
};

using WallpaperHwDecodeDrmFrameCallback =
    std::function<void(WallpaperHwDrmFrame frame)>;

// Idle: no frame delivered yet. ZeroCopy: at least the most recent frame
// went through the dma-buf/EGLImage path. CpuFallback: this playback is (or
// has fallen back to) decoding through the CPU path - either zero-copy was
// never attempted (no GPU support, non-VAAPI hw, or no HW decode at all) or
// it was attempted and failed. Surfaced in the wallpaper settings tab so a
// silently-degraded animated wallpaper (high CPU) is visible, not just logged.
enum class WallpaperHwDecodeStatus { Idle, ZeroCopy, CpuFallback };

struct WallpaperHwDecodePlayback {
    std::thread worker;
    std::shared_ptr<std::atomic<bool>> stop_flag;
    std::shared_ptr<std::atomic<bool>> pause_flag;
    std::shared_ptr<std::atomic<WallpaperHwDecodeStatus>> status;

    WallpaperHwDecodePlayback() = default;
    WallpaperHwDecodePlayback(const WallpaperHwDecodePlayback &) = delete;
    WallpaperHwDecodePlayback &operator=(const WallpaperHwDecodePlayback &) = delete;
    WallpaperHwDecodePlayback(WallpaperHwDecodePlayback &&) = default;
    WallpaperHwDecodePlayback &
    operator=(WallpaperHwDecodePlayback &&other) noexcept {
        if (this != &other) {
            join();
            worker = std::move(other.worker);
            stop_flag = std::move(other.stop_flag);
            pause_flag = std::move(other.pause_flag);
            status = std::move(other.status);
        }
        return *this;
    }
    // std::thread aborts via std::terminate() if a joinable thread is
    // destroyed without join()/detach() first - callers should normally
    // call wallpaper_hw_decode_stop() explicitly, but this is the backstop
    // for the object going out of scope (vector reallocation, teardown
    // paths that don't call stop()) so that never happens.
    ~WallpaperHwDecodePlayback() { join(); }

  private:
    void join() {
        if (stop_flag)
            stop_flag->store(true);
        if (worker.joinable())
            worker.join();
    }
};

// supports_row_length: whether the caller's GL context can upload a padded
// buffer directly (GL_EXT_unpack_subimage); if false, frames are stripped to
// tightly-packed rows before on_frame is called.
//
// on_drm_frame: if set, VAAPI-decoded frames are exported zero-copy (no
// av_hwframe_transfer_data, no avfilter software scale/convert) and handed
// here instead of on_frame. If the zero-copy export fails for a frame (or
// on_drm_frame is empty, or the frame isn't VAAPI), that frame falls back
// to the existing CPU path and on_frame is called instead.
WallpaperHwDecodePlayback wallpaper_hw_decode_start(
    const std::string &path, const std::string &filter_desc, int fps,
    bool supports_row_length, WallpaperHwDecodeFrameCallback on_frame,
    WallpaperHwDecodeDrmFrameCallback on_drm_frame = nullptr);

void wallpaper_hw_decode_stop(WallpaperHwDecodePlayback &playback);
void wallpaper_hw_decode_pause(WallpaperHwDecodePlayback &playback);
void wallpaper_hw_decode_resume(WallpaperHwDecodePlayback &playback);

// Idle (not WallpaperHwDecodeStatus::Idle default) if playback was never
// started, e.g. a default-constructed WallpaperHwDecodePlayback.
WallpaperHwDecodeStatus
wallpaper_hw_decode_status(const WallpaperHwDecodePlayback &playback);

// Releases a WallpaperHwDrmFrame::avframe_handle once its GL import is done
// with the underlying VASurface.
void wallpaper_hw_decode_release_drm_frame(void *avframe_handle);

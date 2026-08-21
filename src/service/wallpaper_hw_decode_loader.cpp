#include <dlfcn.h>

#include "core/log.h"

#include "service/wallpaper_hw_decode.h"

namespace {

using StartFn = WallpaperHwDecodePlayback (*)(
    const std::string &, const std::string &, int, bool,
    WallpaperHwDecodeFrameCallback, WallpaperHwDecodeDrmFrameCallback);
using ReleaseDrmFrameFn = void (*)(void *);

struct Plugin {
    StartFn start = nullptr;
    ReleaseDrmFrameFn release_drm_frame = nullptr;
};

const Plugin &plugin() {
    static const Plugin loaded = [] {
        Plugin p;
        const char *candidates[] = {
            KOKUSEI_WALLPAPER_HW_DECODE_PLUGIN,
            "build/libkokusei-wallpaper-hw-decode.so",
        };
        void *lib = nullptr;
        for (const char *path : candidates) {
            lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
            if (lib)
                break;
        }
        if (!lib) {
            klog("wallpaper_hw_decode: plugin not available (%s); animated "
                 "wallpapers disabled",
                 dlerror());
            return p;
        }
        p.start =
            reinterpret_cast<StartFn>(dlsym(lib, "kokusei_whd_plugin_start"));
        p.release_drm_frame = reinterpret_cast<ReleaseDrmFrameFn>(
            dlsym(lib, "kokusei_whd_plugin_release_drm_frame"));
        if (!p.start || !p.release_drm_frame) {
            klog("wallpaper_hw_decode: plugin missing expected symbols (%s); "
                 "animated wallpapers disabled",
                 dlerror());
            p = {};
        }
        return p;
    }();
    return loaded;
}

} // namespace

WallpaperHwDecodePlayback wallpaper_hw_decode_start(
    const std::string &path, const std::string &filter_desc, int fps,
    bool supports_row_length, WallpaperHwDecodeFrameCallback on_frame,
    WallpaperHwDecodeDrmFrameCallback on_drm_frame) {
    if (!plugin().start)
        return {};
    return plugin().start(path, filter_desc, fps, supports_row_length,
                          std::move(on_frame), std::move(on_drm_frame));
}

void wallpaper_hw_decode_stop(WallpaperHwDecodePlayback &playback) {
    if (!playback.stop_flag)
        return;
    playback.stop_flag->store(true);
    if (playback.worker.joinable())
        playback.worker.join();
    playback.stop_flag.reset();
    playback.pause_flag.reset();
    playback.status.reset();
    playback.egl_import_failed.reset();
}

void wallpaper_hw_decode_pause(WallpaperHwDecodePlayback &playback) {
    if (playback.pause_flag)
        playback.pause_flag->store(true);
}

void wallpaper_hw_decode_resume(WallpaperHwDecodePlayback &playback) {
    if (playback.pause_flag)
        playback.pause_flag->store(false);
}

void wallpaper_hw_decode_release_drm_frame(void *avframe_handle) {
    if (plugin().release_drm_frame)
        plugin().release_drm_frame(avframe_handle);
}

WallpaperHwDecodeStatus
wallpaper_hw_decode_status(const WallpaperHwDecodePlayback &playback) {
    return playback.status ? playback.status->load()
                           : WallpaperHwDecodeStatus::Idle;
}

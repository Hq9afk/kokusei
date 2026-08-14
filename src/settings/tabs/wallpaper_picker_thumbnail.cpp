#include "settings/tabs/wallpaper_picker.h"

#include "core/deferred_call.h"
#include "wallpaper/wallpaper.h"

#include <thread>

void wallpaper_picker_request_thumbnail(WallpaperPickerState &state,
                                        const std::string &path,
                                        int target_size, EGLDisplay display,
                                        EGLSurface surface,
                                        EGLContext context) {
    if (state.thumbnails.count(path) || state.pending.count(path))
        return;
    state.pending.insert(path);
    uint64_t generation = state.scan_generation;
    std::thread([&state, path, target_size, generation, display, surface,
                context] {
        int w = 0, h = 0;
        unsigned char *data =
            wallpaper_decode_scaled(path, target_size, target_size, w, h);
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
            if (state.request_frame)
                state.request_frame();
        });
    }).detach();
}

#pragma once

#include "app/config.h"
#include "render/scene.h"
#include "render/texture.h"
#include "service/frame_clock.h"
#include "service/output_scale.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <cstdint>
#include <string>
#include <vector>

class Renderer;

enum class FillMode { Crop, Fit };

struct WallpaperState {
    wl_surface *surface = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLDisplay egl_display = nullptr;
    EGLContext egl_context = nullptr;
    Renderer *renderer = nullptr;
    int32_t width = 0;
    int32_t height = 0;
    bool configured = false;
    OutputScale output_scale;
    FrameClock frame_clock;
    Scene scene;

    std::vector<Texture> column_textures;

    uint64_t load_generation = 0;
    unsigned char *pending_pixels = nullptr;
    int pending_width = 0;
    int pending_height = 0;
    int pending_column = 0;
    FillMode fill_mode = FillMode::Crop;
};

bool wallpaper_create_surface(WallpaperState &wp, wl_compositor *compositor,
                              zwlr_layer_shell_v1 *layer_shell,
                              wl_output *output = nullptr);

bool wallpaper_init_egl(WallpaperState &wp, Renderer &renderer,
                        EGLDisplay display, EGLConfig config,
                        EGLContext context);

void wallpaper_request_frame(WallpaperState &wp);

void wallpaper_upload_pending(WallpaperState &wp);

unsigned char *wallpaper_decode_scaled(const std::string &path, int target_w,
                                       int target_h, int &out_width,
                                       int &out_height);

void wallpaper_decode_column_async(WallpaperState &wp, std::string path,
                                   int column_index, int column_count);

void wallpaper_sync_from_config(WallpaperState &wp, const Config &cfg,
                                const std::string &monitor_name);

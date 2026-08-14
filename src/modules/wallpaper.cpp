#include "modules/wallpaper.h"

#include "core/deferred_call.h"
#include "core/log.h"
#include "render/image.h"
#include "render/node.h"
#include "render/palette.h"
#include "render/renderer.h"
#include "service/layer_surface.h"
#include "service/wallpaper_service.h"
#include "config/wallpaper_config.h"

#include <GLES2/gl2.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <thread>

namespace {

std::string wallpaper_cache_dir() {
    const char *cache_home = getenv("XDG_CACHE_HOME");
    std::string base =
        cache_home && *cache_home
            ? std::string(cache_home)
            : std::string(getenv("HOME") ? getenv("HOME") : "") + "/.cache";
    for (size_t pos = 1; pos <= base.size(); ++pos) {
        if (pos == base.size() || base[pos] == '/')
            mkdir(base.substr(0, pos).c_str(), 0755);
    }
    std::string dir = base + "/kokusei";
    mkdir(dir.c_str(), 0755);
    dir += "/wallpaper";
    mkdir(dir.c_str(), 0755);
    return dir;
}

std::string wallpaper_cache_path(const std::string &path, time_t mtime,
                                 int target_w, int target_h) {
    std::string key = path + ":" + std::to_string(mtime) + ":" +
                      std::to_string(target_w) + "x" + std::to_string(target_h);
    size_t hash = std::hash<std::string>{}(key);
    return wallpaper_cache_dir() + "/" + std::to_string(hash) + ".rgba";
}

unsigned char *wallpaper_cache_read(const std::string &cache_path,
                                    int &out_width, int &out_height) {
    FILE *fp = fopen(cache_path.c_str(), "rb");
    if (!fp)
        return nullptr;
    uint32_t header[2];
    if (fread(header, sizeof(uint32_t), 2, fp) != 2) {
        fclose(fp);
        return nullptr;
    }
    int width = static_cast<int>(header[0]);
    int height = static_cast<int>(header[1]);
    size_t pixel_bytes = static_cast<size_t>(width) * height * 4;
    auto *data = new unsigned char[pixel_bytes];
    size_t read = fread(data, 1, pixel_bytes, fp);
    fclose(fp);
    if (read != pixel_bytes) {
        delete[] data;
        return nullptr;
    }
    out_width = width;
    out_height = height;
    return data;
}

void wallpaper_cache_write(const std::string &cache_path, int width, int height,
                           const unsigned char *data) {
    FILE *fp = fopen(cache_path.c_str(), "wb");
    if (!fp)
        return;
    uint32_t header[2] = {static_cast<uint32_t>(width),
                          static_cast<uint32_t>(height)};
    fwrite(header, sizeof(uint32_t), 2, fp);
    fwrite(data, 1, static_cast<size_t>(width) * height * 4, fp);
    fclose(fp);
}

void wallpaper_paint(WallpaperState &wp);

void wallpaper_layer_surface_configure(void *data,
                                       zwlr_layer_surface_v1 *layer_surface,
                                       uint32_t serial, uint32_t width,
                                       uint32_t height) {
    auto *wp = static_cast<WallpaperState *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    wp->width = static_cast<int32_t>(width);
    wp->height = static_cast<int32_t>(height);
    if (wp->egl_window) {
        int32_t scale = wp->output_scale.scale;
        wl_egl_window_resize(wp->egl_window, wp->width * scale,
                             wp->height * scale, 0, 0);
        if (wp->frame_clock.surface)
            request_frame(wp->frame_clock);
    }
    wp->configured = true;
}

void wallpaper_layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

constexpr zwlr_layer_surface_v1_listener wallpaper_layer_surface_listener = {
    .configure = wallpaper_layer_surface_configure,
    .closed = wallpaper_layer_surface_closed,
};

void wallpaper_paint(WallpaperState &wp) {
    if (wp.egl_surface == EGL_NO_SURFACE)
        return;

    eglMakeCurrent(wp.egl_display, wp.egl_surface, wp.egl_surface,
                   wp.egl_context);
    wp.renderer->begin_frame(wp.width, wp.height, wp.output_scale.scale);
    glClearColor(palette::base.r, palette::base.g, palette::base.b,
                 palette::base.a);
    glClear(GL_COLOR_BUFFER_BIT);

    wp.scene.rebuild();
    size_t columns = std::max<size_t>(wp.column_textures.size(), 1);
    float column_w = static_cast<float>(wp.width) / static_cast<float>(columns);
    for (size_t i = 0; i < wp.column_textures.size(); ++i) {
        Texture &tex = wp.column_textures[i];
        if (!tex.id)
            continue;
        float scale =
            wp.fill_mode == FillMode::Fit
                ? std::min(column_w / tex.width,
                           static_cast<float>(wp.height) / tex.height)
                : std::max(column_w / tex.width,
                           static_cast<float>(wp.height) / tex.height);
        float draw_w = tex.width * scale;
        float draw_h = tex.height * scale;

        Node *img = wp.scene.root.claim_child();
        img->kind = NodeKind::Texture;
        img->x = static_cast<float>(i) * column_w + (column_w - draw_w) / 2.0f;
        img->y = (wp.height - draw_h) / 2.0f;
        img->w = draw_w;
        img->h = draw_h;
        img->tex = &tex;
    }
    wp.scene.draw(*wp.renderer);

    eglSwapBuffers(wp.egl_display, wp.egl_surface);
}

void box_downsample_rgba(const unsigned char *src, int sw, int sh,
                         unsigned char *dst, int dw, int dh) {
    for (int y = 0; y < dh; ++y) {
        int y0 = static_cast<int>(static_cast<int64_t>(y) * sh / dh);
        int y1 = static_cast<int>(static_cast<int64_t>(y + 1) * sh / dh);
        y1 = std::max(y1, y0 + 1);
        for (int x = 0; x < dw; ++x) {
            int x0 = static_cast<int>(static_cast<int64_t>(x) * sw / dw);
            int x1 = static_cast<int>(static_cast<int64_t>(x + 1) * sw / dw);
            x1 = std::max(x1, x0 + 1);
            long sum[4] = {0, 0, 0, 0};
            int count = 0;
            for (int sy = y0; sy < y1 && sy < sh; ++sy) {
                const unsigned char *row =
                    src + static_cast<size_t>(sy) * sw * 4;
                for (int sx = x0; sx < x1 && sx < sw; ++sx) {
                    const unsigned char *px = row + static_cast<size_t>(sx) * 4;
                    sum[0] += px[0];
                    sum[1] += px[1];
                    sum[2] += px[2];
                    sum[3] += px[3];
                    ++count;
                }
            }
            count = std::max(count, 1);
            unsigned char *out = dst + (static_cast<size_t>(y) * dw + x) * 4;
            out[0] = static_cast<unsigned char>(sum[0] / count);
            out[1] = static_cast<unsigned char>(sum[1] / count);
            out[2] = static_cast<unsigned char>(sum[2] / count);
            out[3] = static_cast<unsigned char>(sum[3] / count);
        }
    }
}

} // namespace

bool wallpaper_create_surface(WallpaperState &wp, wl_compositor *compositor,
                              zwlr_layer_shell_v1 *layer_shell,
                              wl_output *output) {
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
        .name_space = kWallpaperLayerNamespace,
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
    };
    wp.layer_surface =
        layer_surface_create(wp.surface, compositor, layer_shell, cfg,
                             &wallpaper_layer_surface_listener, &wp, output);
    if (!wp.layer_surface)
        return false;
    wp.output_scale.on_change = [&wp](int32_t scale) {
        if (wp.egl_window)
            wl_egl_window_resize(wp.egl_window, wp.width * scale,
                                 wp.height * scale, 0, 0);
        if (wp.frame_clock.surface)
            request_frame(wp.frame_clock);
    };
    output_scale_watch(wp.output_scale, wp.surface);
    wl_surface_commit(wp.surface);
    return true;
}

bool wallpaper_init_egl(WallpaperState &wp, Renderer &renderer,
                        EGLDisplay display, EGLConfig config,
                        EGLContext context) {
    wp.egl_display = display;
    wp.egl_context = context;
    wp.renderer = &renderer;
    int32_t scale = wp.output_scale.scale;
    wp.egl_window =
        wl_egl_window_create(wp.surface, wp.width * scale, wp.height * scale);
    wp.egl_surface = eglCreateWindowSurface(
        display, config, reinterpret_cast<EGLNativeWindowType>(wp.egl_window),
        nullptr);
    if (wp.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!eglMakeCurrent(display, wp.egl_surface, wp.egl_surface, context))
        return false;
    wp.frame_clock.surface = wp.surface;
    wp.frame_clock.draw = [&wp] { wallpaper_paint(wp); };
    return true;
}

void wallpaper_request_frame(WallpaperState &wp) {
    if (wp.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(wp.frame_clock);
}

void wallpaper_upload_pending(WallpaperState &wp) {
    if (!wp.pending_pixels || wp.egl_surface == EGL_NO_SURFACE)
        return;
    eglMakeCurrent(wp.egl_display, wp.egl_surface, wp.egl_surface,
                   wp.egl_context);
    if (static_cast<size_t>(wp.pending_column) >= wp.column_textures.size())
        wp.column_textures.resize(static_cast<size_t>(wp.pending_column) + 1);
    wp.column_textures[static_cast<size_t>(wp.pending_column)] =
        make_texture_rgba(wp.pending_width, wp.pending_height,
                          wp.pending_pixels, true);
    delete[] wp.pending_pixels;
    wp.pending_pixels = nullptr;
    klog("wallpaper: uploaded column %d %dx%d texture", wp.pending_column,
         wp.pending_width, wp.pending_height);
    wallpaper_request_frame(wp);
}

unsigned char *wallpaper_decode_scaled(const std::string &path, int target_w,
                                       int target_h, int &out_width,
                                       int &out_height) {
    struct stat st{};
    bool cacheable =
        target_w > 0 && target_h > 0 && stat(path.c_str(), &st) == 0;
    std::string cache_path;
    if (cacheable) {
        cache_path =
            wallpaper_cache_path(path, st.st_mtime, target_w, target_h);
        if (unsigned char *cached =
                wallpaper_cache_read(cache_path, out_width, out_height)) {
            klog("wallpaper: cache hit '%s' (%dx%d)", path.c_str(), out_width,
                 out_height);
            return cached;
        }
    }

    unsigned char *data = load_image_decode(path, out_width, out_height);
    if (!data)
        return nullptr;
    if (target_w <= 0 || target_h <= 0)
        return data;
    float scale = std::max(static_cast<float>(target_w) / out_width,
                           static_cast<float>(target_h) / out_height);
    unsigned char *result = data;
    if (scale < 1.0f) {
        int dw = std::max(1, static_cast<int>(std::lround(out_width * scale)));
        int dh = std::max(1, static_cast<int>(std::lround(out_height * scale)));
        auto *scaled = new unsigned char[static_cast<size_t>(dw) * dh * 4];
        box_downsample_rgba(data, out_width, out_height, scaled, dw, dh);
        delete[] data;
        out_width = dw;
        out_height = dh;
        result = scaled;
    }
    if (cacheable)
        wallpaper_cache_write(cache_path, out_width, out_height, result);
    return result;
}

void wallpaper_decode_column_async(WallpaperState &wp, std::string path,
                                   int column_index, int column_count) {
    uint64_t generation = ++wp.load_generation;
    int target_w =
        (wp.width / std::max(1, column_count)) * wp.output_scale.scale;
    int target_h = wp.height * wp.output_scale.scale;
    std::thread([&wp, path = std::move(path), generation, target_w, target_h,
                 column_index] {
        klog("wallpaper: decode start '%s' (column %d)", path.c_str(),
             column_index);
        int width = 0, height = 0;
        unsigned char *data =
            wallpaper_decode_scaled(path, target_w, target_h, width, height);
        if (data)
            klog("wallpaper: decode done '%s' (%dx%d)", path.c_str(), width,
                 height);
        DeferredCall::call_later(
            [&wp, data, width, height, generation, column_index] {
                if (generation != wp.load_generation) {
                    if (data)
                        delete[] data;
                    return;
                }
                if (!data)
                    return;
                wp.pending_pixels = data;
                wp.pending_width = width;
                wp.pending_height = height;
                wp.pending_column = column_index;
                wallpaper_upload_pending(wp);
            });
    }).detach();
}

void wallpaper_sync_from_config(WallpaperState &wp, const Config &cfg,
                                const std::string &monitor_name) {
    int count = wallpaper_service_column_count(cfg, monitor_name);
    wp.column_textures.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        std::string path = wallpaper_service_column_path(cfg, monitor_name, i);
        if (!path.empty())
            wallpaper_decode_column_async(wp, path, i, count);
    }
}

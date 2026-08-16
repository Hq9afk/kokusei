#include "modules/wallpaper.h"

#include "config/wallpaper_config.h"
#include "core/async_process.h"
#include "core/deferred_call.h"
#include "core/log.h"
#include "render/node.h"
#include "render/palette.h"
#include "render/renderer.h"
#include "service/layer_surface.h"
#include "service/wallpaper_animate_service.h"
#include "service/wallpaper_service.h"

#include <GLES2/gl2.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

void wallpaper_paint(WallpaperState &wp);

FillMode wallpaper_column_fill_mode(const WallpaperState &wp, size_t column) {
    return column < wp.column_fill_modes.size() ? wp.column_fill_modes[column]
                                                : FillMode::Crop;
}

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
    if (wp->on_resize)
        wp->on_resize();
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
        float column_x = static_cast<float>(i) * column_w;
        FillMode mode = wallpaper_column_fill_mode(wp, i);

        float scale =
            mode == FillMode::Fit
                ? std::min(column_w / tex.width,
                           static_cast<float>(wp.height) / tex.height)
                : std::max(column_w / tex.width,
                           static_cast<float>(wp.height) / tex.height);
        float draw_w = tex.width * scale;
        float draw_h = tex.height * scale;

        Node *clip = node_add_group(&wp.scene.root, column_x, 0.0f, column_w,
                                    static_cast<float>(wp.height), true);
        Node *img = clip->claim_child();
        img->kind = NodeKind::Texture;
        img->x = (column_w - draw_w) / 2.0f;
        img->y = (wp.height - draw_h) / 2.0f;
        img->w = draw_w;
        img->h = draw_h;
        img->tex = &tex;
    }
    wp.scene.draw(*wp.renderer);

    eglSwapBuffers(wp.egl_display, wp.egl_surface);
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
        if (wp.on_resize)
            wp.on_resize();
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

void wallpaper_decode_column_async(WallpaperState &wp, std::string path,
                                   int column_index, int column_count) {
    if (static_cast<size_t>(column_index) >= wp.column_generations.size())
        wp.column_generations.resize(static_cast<size_t>(column_index) + 1);
    uint64_t generation =
        ++wp.column_generations[static_cast<size_t>(column_index)];
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
                if (generation !=
                    wp.column_generations[static_cast<size_t>(column_index)]) {
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
    wp.column_generations.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        std::string path = wallpaper_service_column_path(cfg, monitor_name, i);
        if (!path.empty())
            wallpaper_decode_column_async(wp, path, i, count);
    }
}

void wallpaper_animate_stop(WallpaperState &wp, int column_index) {
    if (static_cast<size_t>(column_index) >= wp.column_animations.size())
        return;
    AnimatedColumnPlayback &pb =
        wp.column_animations[static_cast<size_t>(column_index)];
    if (pb.pid <= 0)
        return;
    pid_t pid = pb.pid;
    kill(pid, SIGKILL);
    while (async_process_is_alive(pid))
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    pb.pid = -1;
    pb.path.clear();
}

void wallpaper_animate_stop_all(WallpaperState &wp) {
    for (size_t i = 0; i < wp.column_animations.size(); ++i)
        wallpaper_animate_stop(wp, static_cast<int>(i));
}

void wallpaper_animate_start(WallpaperState &wp, const std::string &cached_path,
                             int column_index, int width, int height) {
    wallpaper_animate_stop(wp, column_index);
    if (static_cast<size_t>(column_index) >= wp.column_animations.size())
        wp.column_animations.resize(static_cast<size_t>(column_index) + 1);
    if (static_cast<size_t>(column_index) >= wp.column_generations.size())
        wp.column_generations.resize(static_cast<size_t>(column_index) + 1);
    if (width <= 0 || height <= 0)
        return;
    uint64_t generation =
        ++wp.column_generations[static_cast<size_t>(column_index)];

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        klog("wallpaper_animate: pipe failed");
        return;
    }
    std::string resolved = async_process_detail_resolve_path("ffmpeg");
    std::vector<std::string> argv = {resolved,
                                     "-re",
                                     "-stream_loop",
                                     "-1",
                                     "-i",
                                     cached_path,
                                     "-f",
                                     "rawvideo",
                                     "-pix_fmt",
                                     "rgba",
                                     "-vf",
                                     "scale=" + std::to_string(width) + ":" +
                                         std::to_string(height),
                                     "-"};
    std::vector<char *> cargv;
    cargv.reserve(argv.size() + 1);
    for (std::string &a : argv)
        cargv.push_back(a.data());
    cargv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        klog("wallpaper_animate: fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0)
            dup2(devnull, STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execv(cargv[0], cargv.data());
        _exit(127);
    }
    close(pipefd[1]);
    int read_fd = pipefd[0];

    AnimatedColumnPlayback &pb =
        wp.column_animations[static_cast<size_t>(column_index)];
    pb.pid = pid;
    pb.reader = std::thread([&wp, read_fd, pid, generation, column_index, width,
                             height] {
        size_t frame_bytes = static_cast<size_t>(width) * height * 4;
        std::vector<unsigned char> frame(frame_bytes);
        for (;;) {
            size_t got = 0;
            bool eof = false;
            while (got < frame_bytes) {
                ssize_t n =
                    read(read_fd, frame.data() + got, frame_bytes - got);
                if (n <= 0) {
                    eof = true;
                    break;
                }
                got += static_cast<size_t>(n);
            }
            if (eof)
                break;
            auto *copy = new unsigned char[frame_bytes];
            memcpy(copy, frame.data(), frame_bytes);
            DeferredCall::call_later([&wp, copy, width, height, generation,
                                      column_index] {
                if (generation !=
                    wp.column_generations[static_cast<size_t>(column_index)]) {
                    delete[] copy;
                    return;
                }
                wp.pending_pixels = copy;
                wp.pending_width = width;
                wp.pending_height = height;
                wp.pending_column = column_index;
                wallpaper_upload_pending(wp);
            });
        }
        close(read_fd);
        waitpid(pid, nullptr, 0);
    });
    pb.reader.detach();
}

void wallpaper_animate_sync_from_config(WallpaperState &wp, const Config &cfg,
                                        const std::string &monitor_name) {
    int count = wallpaper_service_animated_column_count(cfg, monitor_name);
    wp.column_textures.resize(static_cast<size_t>(count));
    wp.column_generations.resize(static_cast<size_t>(count));
    wp.column_animations.resize(static_cast<size_t>(count));
    int target_w = (wp.width / std::max(1, count)) * wp.output_scale.scale;
    int target_h = wp.height * wp.output_scale.scale;
    for (int i = 0; i < count; ++i) {
        std::string path =
            wallpaper_service_animated_column_path(cfg, monitor_name, i);
        if (path.empty()) {
            wallpaper_animate_stop(wp, i);
            continue;
        }
        if (wp.column_animations[static_cast<size_t>(i)].pid > 0 &&
            wp.column_animations[static_cast<size_t>(i)].path == path)
            continue;
        wp.column_animations[static_cast<size_t>(i)].path = path;
        std::thread([&wp, path, i, target_w, target_h] {
            std::string cached = wallpaper_animate_prepare(path, target_h);
            if (cached.empty()) {
                klog("wallpaper_animate: prepare failed, skipping column %d",
                     i);
                return;
            }
            DeferredCall::call_later([&wp, cached, path, i, target_w,
                                      target_h] {
                if (static_cast<size_t>(i) >= wp.column_animations.size() ||
                    wp.column_animations[static_cast<size_t>(i)].path != path)
                    return;
                wallpaper_animate_start(wp, cached, i, target_w, target_h);
            });
        }).detach();
    }
}

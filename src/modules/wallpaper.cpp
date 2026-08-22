#include <GLES2/gl2.h>
#include <algorithm>
#include <thread>
#include <vector>

#include "config/wallpaper_config.h"

#include "core/deferred_call.h"
#include "core/log.h"

#include "modules/wallpaper.h"

#include "render/node.h"
#include "render/palette.h"
#include "render/renderer.h"
#include "render/video_texture.h"

#include "service/layer_surface.h"
#include "service/wallpaper_animate_service.h"
#include "service/wallpaper_hw_decode.h"
#include "service/wallpaper_service.h"

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
    bool changed = wp->width != static_cast<int32_t>(width) ||
                   wp->height != static_cast<int32_t>(height);
    wp->width = static_cast<int32_t>(width);
    wp->height = static_cast<int32_t>(height);
    if (changed && wp->egl_window) {
        int32_t scale = wp->output_scale.scale;
        wl_egl_window_resize(wp->egl_window, wp->width * scale,
                             wp->height * scale, 0, 0);
        if (wp->frame_clock.surface)
            request_frame(wp->frame_clock);
    }
    wp->configured = true;
    if (changed && wp->on_resize)
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
    wallpaper_draw_columns(wp, &wp.scene.root, wp.width, wp.height);
    wp.scene.draw(*wp.renderer);

    eglSwapBuffers(wp.egl_display, wp.egl_surface);
}

} // namespace

void wallpaper_draw_columns(const WallpaperState &wp, Node *parent,
                            int32_t width, int32_t height) {
    size_t columns = std::max<size_t>(
        std::max(wp.column_textures.size(), wp.column_animations.size()), 1);
    float column_w = static_cast<float>(width) / static_cast<float>(columns);
    for (size_t i = 0; i < columns; ++i) {
        bool zero_copy = i < wp.column_animations.size() &&
                         wp.column_animations[i].zero_copy &&
                         wp.column_animations[i].video_tex.tex;
        const Texture *tex =
            i < wp.column_textures.size() && wp.column_textures[i].id
                ? &wp.column_textures[i]
                : nullptr;
        if (!zero_copy && !tex)
            continue;
        int tex_w =
            zero_copy ? wp.column_animations[i].video_tex.width : tex->width;
        int tex_h =
            zero_copy ? wp.column_animations[i].video_tex.height : tex->height;

        float column_x = static_cast<float>(i) * column_w;
        FillMode mode = wallpaper_column_fill_mode(wp, i);

        float scale = mode == FillMode::Fit
                          ? std::min(column_w / tex_w,
                                     static_cast<float>(height) / tex_h)
                          : std::max(column_w / tex_w,
                                     static_cast<float>(height) / tex_h);
        float draw_w = tex_w * scale;
        float draw_h = tex_h * scale;

        Node *clip = node_add_group(parent, column_x, 0.0f, column_w,
                                    static_cast<float>(height), true);
        Node *img = clip->claim_child();
        img->x = (column_w - draw_w) / 2.0f;
        img->y = (height - draw_h) / 2.0f;
        img->w = draw_w;
        img->h = draw_h;
        if (zero_copy) {
            img->kind = NodeKind::VideoTexture;
            img->video_tex = &wp.column_animations[i].video_tex;
        } else {
            img->kind = NodeKind::Texture;
            img->tex = tex;
        }
    }
}

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
    size_t column = static_cast<size_t>(wp.pending_column);
    bool animated = column < wp.column_animations.size() &&
                    wp.column_animations[column].decode.stop_flag != nullptr;
    update_texture_rgba(wp.column_textures[column], wp.pending_width,
                        wp.pending_height, wp.pending_pixels, !animated,
                        wp.pending_stride);
    delete[] wp.pending_pixels;
    wp.pending_pixels = nullptr;
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
        DeferredCall::call_later([&wp, data, width, height, generation,
                                  column_index] {
            if (static_cast<size_t>(column_index) >=
                    wp.column_generations.size() ||
                generation !=
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
            wp.pending_stride = 0;
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
        if (!path.empty()) {
            wallpaper_decode_column_async(wp, path, i, count);
            continue;
        }
        wp.column_textures[static_cast<size_t>(i)].reset();
        ++wp.column_generations[static_cast<size_t>(i)];
        wallpaper_request_frame(wp);
    }
}

void wallpaper_animate_stop(WallpaperState &wp, int column_index) {
    if (static_cast<size_t>(column_index) >= wp.column_animations.size())
        return;
    AnimatedColumnPlayback &pb =
        wp.column_animations[static_cast<size_t>(column_index)];
    wallpaper_hw_decode_stop(pb.decode);
    pb.path.clear();
    if (pb.pinned_frame) {
        wallpaper_hw_decode_release_drm_frame(pb.pinned_frame);
        pb.pinned_frame = nullptr;
    }
    if (pb.pinned_frame_prev) {
        wallpaper_hw_decode_release_drm_frame(pb.pinned_frame_prev);
        pb.pinned_frame_prev = nullptr;
    }
    if (pb.video_tex.tex && wp.egl_surface != EGL_NO_SURFACE) {
        eglMakeCurrent(wp.egl_display, wp.egl_surface, wp.egl_surface,
                       wp.egl_context);
        pb.video_tex.reset();
    }
    pb.zero_copy = false;
}

void wallpaper_animate_stop_all(WallpaperState &wp) {
    for (auto &pb : wp.column_animations)
        if (pb.decode.stop_flag)
            pb.decode.stop_flag->store(true);
    for (size_t i = 0; i < wp.column_animations.size(); ++i)
        wallpaper_animate_stop(wp, static_cast<int>(i));
}

void wallpaper_animate_pause_all(WallpaperState &wp) {
    for (auto &pb : wp.column_animations)
        wallpaper_hw_decode_pause(pb.decode);
}

void wallpaper_animate_resume_all(WallpaperState &wp) {
    for (auto &pb : wp.column_animations)
        wallpaper_hw_decode_resume(pb.decode);
}

WallpaperHwDecodeStatus
wallpaper_animate_decode_status(const WallpaperState &wp, int column_index) {
    if (column_index < 0 ||
        static_cast<size_t>(column_index) >= wp.column_animations.size())
        return WallpaperHwDecodeStatus::Idle;
    return wallpaper_hw_decode_status(
        wp.column_animations[static_cast<size_t>(column_index)].decode);
}

void wallpaper_animate_start(WallpaperState &wp, const std::string &cached_path,
                             int column_index, int width, int height,
                             FillMode mode) {
    wallpaper_animate_stop(wp, column_index);
    if (static_cast<size_t>(column_index) >= wp.column_animations.size())
        wp.column_animations.resize(static_cast<size_t>(column_index) + 1);
    if (static_cast<size_t>(column_index) >= wp.column_generations.size())
        wp.column_generations.resize(static_cast<size_t>(column_index) + 1);
    if (width <= 0 || height <= 0)
        return;
    uint64_t generation =
        ++wp.column_generations[static_cast<size_t>(column_index)];

    std::string vf =
        mode == FillMode::Fit
            ? "scale=" + std::to_string(width) + ":" + std::to_string(height) +
                  ":force_original_aspect_ratio=decrease,pad=" +
                  std::to_string(width) + ":" + std::to_string(height) +
                  ":(ow-iw)/2:(oh-ih)/2:color=black,format=rgba"
            : "scale=" + std::to_string(width) + ":" + std::to_string(height) +
                  ":force_original_aspect_ratio=increase,crop=" +
                  std::to_string(width) + ":" + std::to_string(height) +
                  ",format=rgba";

    AnimatedColumnPlayback &pb =
        wp.column_animations[static_cast<size_t>(column_index)];
    pb.mode = mode;
    pb.decode = wallpaper_hw_decode_start(
        cached_path, vf, kAnimatedWallpaperMaxFps,
        texture_row_length_supported(),
        [&wp, generation, column_index](unsigned char *rgba, int w, int h,
                                        int stride_px) {
            DeferredCall::call_later(
                [&wp, rgba, w, h, stride_px, generation, column_index] {
                    if (static_cast<size_t>(column_index) >=
                            wp.column_generations.size() ||
                        generation != wp.column_generations[static_cast<size_t>(
                                          column_index)]) {
                        delete[] rgba;
                        return;
                    }
                    wp.pending_pixels = rgba;
                    wp.pending_width = w;
                    wp.pending_height = h;
                    wp.pending_stride = stride_px;
                    wp.pending_column = column_index;
                    wallpaper_upload_pending(wp);
                });
        },
        video_texture_import_supported()
            ? WallpaperHwDecodeDrmFrameCallback(
                  [&wp, generation, column_index](WallpaperHwDrmFrame frame) {
                      DeferredCall::call_later([&wp, frame, generation,
                                                column_index] {
                          if (static_cast<size_t>(column_index) >=
                                  wp.column_generations.size() ||
                              generation !=
                                  wp.column_generations[static_cast<size_t>(
                                      column_index)] ||
                              wp.egl_surface == EGL_NO_SURFACE) {
                              wallpaper_hw_decode_release_drm_frame(
                                  frame.avframe_handle);
                              return;
                          }
                          AnimatedColumnPlayback &pb =
                              wp.column_animations[static_cast<size_t>(
                                  column_index)];
                          DrmFrameImport import;
                          import.plane_count = frame.plane_count;
                          import.width = frame.width;
                          import.height = frame.height;
                          for (int i = 0; i < frame.plane_count; ++i)
                              import.planes[i] = {frame.planes[i].fd,
                                                  frame.planes[i].modifier,
                                                  frame.planes[i].offset,
                                                  frame.planes[i].pitch};
                          eglMakeCurrent(wp.egl_display, wp.egl_surface,
                                         wp.egl_surface, wp.egl_context);
                          if (video_texture_import(pb.video_tex, wp.egl_display,
                                                   import)) {
                              pb.zero_copy = true;
                              if (pb.pinned_frame_prev)
                                  wallpaper_hw_decode_release_drm_frame(
                                      pb.pinned_frame_prev);
                              pb.pinned_frame_prev = pb.pinned_frame;
                              pb.pinned_frame = frame.avframe_handle;
                              wallpaper_request_frame(wp);
                          } else {
                              if (pb.decode.egl_import_failed)
                                  pb.decode.egl_import_failed->store(true);
                              wallpaper_hw_decode_release_drm_frame(
                                  frame.avframe_handle);
                          }
                      });
                  })
            : WallpaperHwDecodeDrmFrameCallback());
}

void wallpaper_animate_sync_from_config(WallpaperState &wp, const Config &cfg,
                                        const std::string &monitor_name) {
    int count = wallpaper_service_animated_column_count(cfg, monitor_name);
    for (size_t i = static_cast<size_t>(count); i < wp.column_animations.size();
         ++i)
        wallpaper_animate_stop(wp, static_cast<int>(i));
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
            wp.column_textures[static_cast<size_t>(i)].reset();
            ++wp.column_generations[static_cast<size_t>(i)];
            wallpaper_request_frame(wp);
            continue;
        }
        if (wp.column_animations[static_cast<size_t>(i)].decode.stop_flag &&
            wp.column_animations[static_cast<size_t>(i)].path == path &&
            wp.column_animations[static_cast<size_t>(i)].mode ==
                wallpaper_column_fill_mode(wp, static_cast<size_t>(i)))
            continue;
        wp.column_animations[static_cast<size_t>(i)].path = path;
        std::thread([&wp, path, i, target_w, target_h] {
            std::string cached = wallpaper_animate_prepare(path);
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
                wallpaper_animate_start(
                    wp, cached, i, target_w, target_h,
                    wallpaper_column_fill_mode(wp, static_cast<size_t>(i)));
            });
        }).detach();
    }
}

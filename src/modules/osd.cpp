#include "modules/osd.h"

#include "core/log.h"
#include "render/color_ops.h"
#include "render/icon.h"
#include "render/icons.h"
#include "render/node.h"
#include "render/palette.h"
#include "render/text.h"
#include "service/layer_surface.h"

#include <GLES2/gl2.h>

#include <algorithm>
#include <cmath>
#include <dirent.h>
#include <fstream>
#include <sys/inotify.h>
#include <unistd.h>

namespace {

void osd_layer_surface_configure(void *data,
                                 zwlr_layer_surface_v1 *layer_surface,
                                 uint32_t serial, uint32_t, uint32_t) {
    auto *state = static_cast<OsdState *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    state->configured = true;
}

void osd_layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

constexpr zwlr_layer_surface_v1_listener osd_layer_surface_listener = {
    .configure = osd_layer_surface_configure,
    .closed = osd_layer_surface_closed,
};

void osd_paint(OsdState &state) {
    eglMakeCurrent(state.egl_display, state.egl_surface, state.egl_surface,
                   state.egl_context);
    int32_t scale = state.output_scale.scale;
    state.renderer->begin_frame(kOsdSurfaceWidth, kOsdSurfaceHeight, scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.animations.tick(std::chrono::steady_clock::now());

    Color icon_color;

    state.scene.rebuild();
    if (state.opacity > 0.0f) {
        Node *bg = state.scene.root.claim_child();
        bg->kind = NodeKind::RoundedRect;
        bg->w = kOsdSurfaceWidth;
        bg->h = kOsdSurfaceHeight;
        bg->radius = kOsdSurfaceHeight / 2.0f;
        bg->border_width = metrics::border_thin;
        bg->fill = rgba(palette::overlay);
        bg->border = rgba(palette::electro);

        float icon_y = (kOsdSurfaceHeight - KOKUSEI_ICON_PX) / 2.0f;
        if (state.icon_texture.id) {
            icon_color = lerp_color(palette::text, palette::text_muted,
                                    state.icon_color_t);
            Node *icon = state.scene.root.claim_child();
            icon->kind = NodeKind::Texture;
            icon->x = kOsdContentMargin;
            icon->y = icon_y;
            icon->w = static_cast<float>(state.icon_texture.width) /
                      static_cast<float>(state.icon_texture.scale);
            icon->h = static_cast<float>(state.icon_texture.height) /
                      static_cast<float>(state.icon_texture.scale);
            icon->tex = &state.icon_texture;
            icon->tint = rgba(icon_color);
        }

        float bar_x =
            kOsdContentMargin +
            (state.icon_texture.id ? KOKUSEI_ICON_PX + kOsdBarMargin : 0.0f);
        float bar_w = kOsdSurfaceWidth - bar_x - kOsdBarMargin -
                      kOsdLabelWidth - kOsdContentMargin;
        float bar_y = kOsdSurfaceHeight / 2.0f - 3;

        Node *track = state.scene.root.claim_child();
        track->kind = NodeKind::RoundedRect;
        track->x = bar_x;
        track->y = bar_y;
        track->w = bar_w;
        track->h = 6;
        track->radius = 3;
        track->fill = rgba(palette::text_alpha11);

        const Color &fill_color =
            state.muted ? palette::text_muted : palette::accent;
        float fill_w = bar_w * std::clamp(state.bar_fill, 0.0f, 1.0f);

        Node *fill = state.scene.root.claim_child();
        fill->kind = NodeKind::RoundedRect;
        fill->x = bar_x;
        fill->y = bar_y;
        fill->w = fill_w;
        fill->h = 6;
        fill->radius = 3;
        fill->fill = rgba(fill_color);

        if (state.label_texture.id) {
            float label_h = static_cast<float>(state.label_texture.height) /
                            static_cast<float>(state.label_texture.scale);
            float label_w = static_cast<float>(state.label_texture.width) /
                            static_cast<float>(state.label_texture.scale);
            Node *label = state.scene.root.claim_child();
            label->kind = NodeKind::Texture;
            label->x = kOsdSurfaceWidth - kOsdContentMargin - label_w;
            label->y = (kOsdSurfaceHeight - label_h) / 2.0f;
            label->w = label_w;
            label->h = label_h;
            label->tex = &state.label_texture;
            label->tint = rgba(palette::text);
        }
    }

    state.renderer->set_opacity(state.opacity);
    state.scene.draw(*state.renderer);
    state.renderer->set_opacity(1.0f);
    eglSwapBuffers(state.egl_display, state.egl_surface);

    if (state.animations.hasActive())
        request_frame(state.frame_clock);
}

PangoFontDescription *osd_label_font() {
    static PangoFontDescription *d =
        pango_font_description_from_string("ComicShannsMono Nerd Font 15");
    return d;
}

std::string find_backlight_device() {
    DIR *dir = opendir("/sys/class/backlight");
    if (!dir)
        return {};
    std::string name;
    while (dirent *entry = readdir(dir)) {
        if (entry->d_name[0] == '.')
            continue;
        name = entry->d_name;
        break;
    }
    closedir(dir);
    return name;
}

int read_int_file(const std::string &path) {
    std::ifstream f(path);
    int value = 0;
    f >> value;
    return value;
}

} // namespace

bool osd_create_surface(OsdState &state, wl_compositor *compositor,
                        zwlr_layer_shell_v1 *layer_shell, wl_output *output) {
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        .name_space = "kokusei-osd",

        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
        .width = kOsdSurfaceWidth,
        .height = kOsdSurfaceHeight,
        .margin_bottom = 30,
        .empty_input_region = true,
    };
    state.layer_surface =
        layer_surface_create(state.surface, compositor, layer_shell, cfg,
                             &osd_layer_surface_listener, &state, output);
    if (!state.layer_surface)
        return false;
    state.output_scale.on_change = [&state](int32_t scale) {
        if (state.egl_window)
            wl_egl_window_resize(state.egl_window, kOsdSurfaceWidth * scale,
                                 kOsdSurfaceHeight * scale, 0, 0);
        if (state.frame_clock.surface)
            request_frame(state.frame_clock);
    };
    output_scale_watch(state.output_scale, state.surface);
    wl_surface_commit(state.surface);
    return true;
}

bool osd_init_egl(OsdState &state, Renderer &renderer, EGLDisplay display,
                  EGLConfig config, EGLContext context) {
    state.egl_display = display;
    state.egl_context = context;
    state.renderer = &renderer;
    int32_t scale = state.output_scale.scale;
    state.egl_window = wl_egl_window_create(
        state.surface, kOsdSurfaceWidth * scale, kOsdSurfaceHeight * scale);
    state.egl_surface = eglCreateWindowSurface(
        display, config,
        reinterpret_cast<EGLNativeWindowType>(state.egl_window), nullptr);
    if (state.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!eglMakeCurrent(display, state.egl_surface, state.egl_surface, context))
        return false;
    state.frame_clock.surface = state.surface;
    state.frame_clock.draw = [&state] { osd_paint(state); };
    return true;
}

void osd_request_frame(OsdState &state) {
    if (state.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(state.frame_clock);
}

void osd_show(OsdState &state, OsdKind kind, float level, bool muted) {
    auto now = std::chrono::steady_clock::now();
    if (now - state.created_at < kOsdReadyDelay)
        return;

    state.kind = kind;
    state.level = level;
    state.muted = muted;
    state.visible = true;
    state.hide_at = now + kOsdVisibleFor;

    const char *codepoint;
    if (kind == OsdKind::Brightness) {
        codepoint = icon::adjustments;
    } else if (kind == OsdKind::Mic) {
        codepoint = muted ? icon::mic_off : icon::mic_on;
    } else if (muted) {
        codepoint = icon::volume_mute;
    } else if (level < 0.01f) {
        codepoint = icon::volume_empty;
    } else if (level < 0.5f) {
        codepoint = icon::volume_low;
    } else {
        codepoint = icon::volume_high;
    }
    RasterizedText icon_text =
        rasterize_icon(codepoint, state.output_scale.scale);
    state.icon_texture = make_texture_from_raster(icon_text);

    std::string label_str =
        muted
            ? "muted"
            : std::to_string(static_cast<int>(std::lround(level * 100))) + "%";
    RasterizedText label_text = rasterize_text_with(
        label_str, osd_label_font(), state.output_scale.scale, kOsdLabelWidth);
    state.label_texture = make_texture_from_raster(label_text);

    state.animations.animate(
        state.opacity, 1.0f, kOsdAnimNormal, Easing::EaseOutCubic,
        [&state](float v) { state.opacity = v; }, {}, kOsdOwnerOpacity);
    state.animations.animate(
        state.bar_fill, std::clamp(level, 0.0f, 1.0f), kOsdAnimFast,
        Easing::EaseOutCubic, [&state](float v) { state.bar_fill = v; }, {},
        kOsdOwnerBarFill);
    state.animations.animate(
        state.icon_color_t, muted ? 1.0f : 0.0f, kOsdAnimFast, Easing::Linear,
        [&state](float v) { state.icon_color_t = v; }, {}, kOsdOwnerIconColor);
    osd_request_frame(state);
}

void osd_hide(OsdState &state) {
    state.animations.animate(
        state.opacity, 0.0f, kOsdAnimNormal, Easing::EaseOutCubic,
        [&state](float v) { state.opacity = v; },
        [&state] { state.visible = false; }, kOsdOwnerOpacity);
    osd_request_frame(state);
}

void brightness_init(BrightnessBackend &backend) {
    backend.device = find_backlight_device();
    if (backend.device.empty()) {
        klog("osd: no backlight device found, brightness OSD disabled");
        return;
    }
    backend.max = read_int_file("/sys/class/backlight/" + backend.device +
                                "/max_brightness");
    klog("osd: brightness device %s (max %d)", backend.device.c_str(),
         backend.max);
}

float brightness_get(const BrightnessBackend &backend) {
    if (backend.device.empty() || backend.max <= 0)
        return 0.0f;
    int current =
        read_int_file("/sys/class/backlight/" + backend.device + "/brightness");
    return static_cast<float>(current) / backend.max;
}

int brightness_watch_init(const BrightnessBackend &backend) {
    if (backend.device.empty())
        return -1;
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0)
        return -1;
    std::string path = "/sys/class/backlight/" + backend.device + "/brightness";
    if (inotify_add_watch(fd, path.c_str(), IN_MODIFY) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool brightness_watch_poll(int fd) {
    char buf[256];
    bool changed = false;
    while (read(fd, buf, sizeof(buf)) > 0)
        changed = true;
    return changed;
}

#include "modules/notification.h"

#include "core/log.h"
#include "render/color_ops.h"
#include "render/node.h"
#include "render/text.h"
#include "service/layer_surface.h"

#include <GLES2/gl2.h>

#include <algorithm>
#include <map>
#include <tuple>
#include <vector>

float notification_detail_texture_height(const Texture &tex) {
    return tex.id ? static_cast<float>(tex.height) /
                        static_cast<float>(tex.scale > 0 ? tex.scale : 1)
                  : 0.0f;
}

const Color &notification_detail_urgency_color(uint8_t urgency) {
    if (urgency == 2)
        return palette::critical;
    if (urgency == 0)
        return palette::text_muted;
    return palette::accent;
}

namespace {

void notification_layer_surface_configure(void *data,
                                           zwlr_layer_surface_v1 *layer_surface,
                                           uint32_t serial, uint32_t, uint32_t) {
    auto *view = static_cast<NotificationView *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    view->configured = true;
}

void notification_layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

constexpr zwlr_layer_surface_v1_listener notification_layer_surface_listener = {
    .configure = notification_layer_surface_configure,
    .closed = notification_layer_surface_closed,
};

PangoFontDescription *font_app_name() {
    static PangoFontDescription *d =
        pango_font_description_from_string("ComicShannsMono Nerd Font Bold 13");
    return d;
}

PangoFontDescription *font_summary() {
    static PangoFontDescription *d = pango_font_description_from_string(
        "ComicShannsMono Nerd Font Semi-Bold 17");
    return d;
}

PangoFontDescription *font_body() {
    static PangoFontDescription *d =
        pango_font_description_from_string("ComicShannsMono Nerd Font 15");
    return d;
}

int32_t resolve_timeout_ms(int32_t expire_timeout_ms) {
    return expire_timeout_ms > 0 ? expire_timeout_ms : 5000;
}

constexpr int32_t kContentScale = 1;

float notification_entry_height(const NotificationEntry &entry) {
    float content_h = std::max(
        kNotificationUrgencyDotSize,
        notification_detail_texture_height(entry.app_name_texture));
    if (entry.summary_texture.id)
        content_h += kNotificationContentSpacing +
                     notification_detail_texture_height(entry.summary_texture);
    if (entry.body_texture.id)
        content_h += kNotificationContentSpacing +
                     notification_detail_texture_height(entry.body_texture);
    return content_h + kNotificationCardPadding * 2.0f;
}

void notification_apply_content(NotificationEntry &entry,
                                const std::string &app_name,
                                const std::string &summary,
                                const std::string &body, uint8_t urgency,
                                int32_t expire_timeout_ms) {
    entry.urgency = urgency;
    entry.timeout_ms = resolve_timeout_ms(expire_timeout_ms);
    entry.expires_at = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(entry.timeout_ms);

    int content_width = kNotificationSurfaceWidth -
                        static_cast<int>(kNotificationCardPadding * 2.0f);
    int32_t scale = kContentScale;
    RasterizedText app_name_text =
        rasterize_text_with(app_name.empty() ? "Notification" : app_name,
                            font_app_name(), scale, content_width);
    entry.app_name_texture = make_texture_from_raster(app_name_text);

    entry.summary_texture = Texture{};
    if (!summary.empty()) {
        RasterizedText summary_text =
            rasterize_text_with(summary, font_summary(), scale, content_width);
        entry.summary_texture = make_texture_from_raster(summary_text);
    }

    entry.body_texture = Texture{};
    if (!body.empty()) {
        RasterizedText body_text =
            rasterize_text_with(body, font_body(), scale, content_width);
        entry.body_texture = make_texture_from_raster(body_text);
    }

    entry.height = notification_entry_height(entry);
}

uint64_t opacity_owner(uint32_t id) { return (static_cast<uint64_t>(id) << 2) | 0; }

uint64_t slide_owner(uint32_t id) { return (static_cast<uint64_t>(id) << 2) | 1; }

uint64_t exit_owner(uint32_t id) { return (static_cast<uint64_t>(id) << 2) | 2; }

void notification_start_exit(NotificationService &service, uint32_t id) {
    auto it =
        std::find_if(service.entries.begin(), service.entries.end(),
                     [id](const NotificationEntry &e) { return e.id == id; });
    if (it == service.entries.end() || it->exiting)
        return;
    it->exiting = true;

    service.animations.animate(
        it->opacity, 0.0f, kNotificationAnimNormal, Easing::EaseOutCubic,
        [&service, id](float v) {
            auto e = std::find_if(
                service.entries.begin(), service.entries.end(),
                [id](const NotificationEntry &en) { return en.id == id; });
            if (e != service.entries.end())
                e->opacity = v;
        },
        {}, opacity_owner(id));

    service.animations.animate(
        0.0f, 1.0f, kNotificationAnimNormal + kNotificationAnimExitBuffer,
        Easing::Linear, [](float) {},
        [&service, id] {
            std::erase_if(service.entries, [id](const NotificationEntry &e) {
                return e.id == id;
            });
        },
        exit_owner(id));
}

} // namespace

bool notification_view_create_surface(NotificationView &view,
                                      wl_compositor *compositor,
                                      zwlr_layer_shell_v1 *layer_shell,
                                      wl_output *output) {
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        .name_space = "kokusei-notification",
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
        .width = kNotificationSurfaceWidth,
        .height = kNotificationSurfaceHeight,
        .margin_right = 10,
        .margin_bottom = 10,
        .empty_input_region = true,
    };
    view.layer_surface = layer_surface_create(
        view.surface, compositor, layer_shell, cfg,
        &notification_layer_surface_listener, &view, output);
    if (!view.layer_surface)
        return false;
    view.output_scale.on_change = [&view](int32_t scale) {
        if (view.egl_window)
            wl_egl_window_resize(view.egl_window, kNotificationSurfaceWidth * scale,
                                 kNotificationSurfaceHeight * scale, 0, 0);
        if (view.frame_clock.surface)
            request_frame(view.frame_clock);
    };
    output_scale_watch(view.output_scale, view.surface);
    wl_surface_commit(view.surface);
    return true;
}

bool notification_view_init_egl(NotificationView &view,
                                NotificationService &service,
                                Renderer &renderer, EGLDisplay display,
                                EGLConfig config, EGLContext context) {
    view.egl_display = display;
    view.egl_context = context;
    view.renderer = &renderer;
    int32_t scale = view.output_scale.scale;
    view.egl_window =
        wl_egl_window_create(view.surface, kNotificationSurfaceWidth * scale,
                             kNotificationSurfaceHeight * scale);
    view.egl_surface = eglCreateWindowSurface(
        display, config, reinterpret_cast<EGLNativeWindowType>(view.egl_window),
        nullptr);
    if (view.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!eglMakeCurrent(display, view.egl_surface, view.egl_surface, context))
        return false;
    view.frame_clock.surface = view.surface;
    view.frame_clock.draw = [&view, &service] {
        notification_paint(view, service);
    };
    return true;
}

void notification_view_request_frame(NotificationView &view) {
    if (view.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(view.frame_clock);
}

bool notification_sweep_expired(NotificationService &service) {
    auto now = std::chrono::steady_clock::now();
    std::vector<uint32_t> to_exit;
    for (const NotificationEntry &e : service.entries)
        if (!e.exiting && now >= e.expires_at)
            to_exit.push_back(e.id);
    for (uint32_t id : to_exit)
        notification_start_exit(service, id);
    return !to_exit.empty();
}

void notification_push(NotificationService &service, const std::string &app_name,
                       const std::string &summary, const std::string &body,
                       int32_t expire_timeout_ms, uint32_t id, uint8_t urgency) {
    if (id == 0)
        id = service.next_id++;
    else if (id >= service.next_id)
        service.next_id = id + 1;
    NotificationEntry entry;
    entry.id = id;
    notification_apply_content(entry, app_name, summary, body, urgency,
                               expire_timeout_ms);
    klog("notification: id=%u app='%s' summary='%s'", id, app_name.c_str(),
         summary.c_str());
    service.entries.push_front(std::move(entry));

    service.animations.animate(
        0.0f, 1.0f, kNotificationAnimNormal, Easing::EaseOutCubic,
        [&service, id](float v) {
            auto e = std::find_if(
                service.entries.begin(), service.entries.end(),
                [id](const NotificationEntry &en) { return en.id == id; });
            if (e != service.entries.end())
                e->opacity = v;
        },
        {}, opacity_owner(id));
    service.animations.animate(
        kNotificationSlideOffset, 0.0f, kNotificationAnimNormal,
        Easing::EaseOutCubic,
        [&service, id](float v) {
            auto e = std::find_if(
                service.entries.begin(), service.entries.end(),
                [id](const NotificationEntry &en) { return en.id == id; });
            if (e != service.entries.end())
                e->slide_offset = v;
        },
        {}, slide_owner(id));
}

bool notification_init(NotificationService &service,
                       const std::function<void()> &on_repaint) {
    try {
        service.bus = sdbus::createSessionBusConnection();
        service.object = sdbus::createObject(
            *service.bus, sdbus::ObjectPath{"/org/freedesktop/Notifications"});

        service.object
            ->addVTable(
                sdbus::registerMethod("Notify").implementedAs(
                    [&service](const std::string &app_name, uint32_t replaces_id,
                             const std::string &, const std::string &summary,
                             const std::string &body,
                             const std::vector<std::string> &,
                             const std::map<std::string, sdbus::Variant> &hints,
                             int32_t expire_timeout) -> uint32_t {
                        uint8_t urgency = 1;
                        auto hint_it = hints.find("urgency");
                        if (hint_it != hints.end()) {
                            try {
                                urgency = hint_it->second.get<uint8_t>();
                            } catch (const sdbus::Error &) {
                            }
                        }

                        uint32_t id =
                            replaces_id != 0 ? replaces_id : service.next_id++;
                        auto existing = std::find_if(
                            service.entries.begin(), service.entries.end(),
                            [id](const NotificationEntry &e) {
                                return e.id == id;
                            });
                        if (existing != service.entries.end()) {
                            notification_apply_content(
                                *existing, app_name, summary, body, urgency,
                                expire_timeout);
                            klog("notification: replaced id=%u", id);
                        } else {
                            notification_push(service, app_name, summary, body,
                                              expire_timeout, id, urgency);
                        }
                        return id;
                    }),
                sdbus::registerMethod("CloseNotification")
                    .implementedAs([&service, on_repaint](uint32_t id) {
                        klog("notification: closed id=%u", id);
                        notification_start_exit(service, id);
                        on_repaint();
                    }),
                sdbus::registerMethod("GetCapabilities")
                    .implementedAs(
                        []() -> std::vector<std::string> { return {"body"}; }),
                sdbus::registerMethod("GetServerInformation")
                    .implementedAs(
                        []() -> std::tuple<std::string, std::string,
                                           std::string, std::string> {
                            return {"kokusei", "kokusei", "0.1.0", "1.2"};
                        }))
            .forInterface("org.freedesktop.Notifications");

        service.bus->requestName(
            sdbus::ServiceName{"org.freedesktop.Notifications"});
        klog("notification: registered org.freedesktop.Notifications");
        return true;
    } catch (const sdbus::Error &e) {
        klog("notification: D-Bus registration failed (%s): %s - is another "
             "notification daemon running?",
             e.getName().c_str(), e.getMessage().c_str());
        service.object.reset();
        service.bus.reset();
        return false;
    }
}

void notification_paint(NotificationView &view, NotificationService &service) {
    eglMakeCurrent(view.egl_display, view.egl_surface, view.egl_surface,
                   view.egl_context);
    view.renderer->begin_frame(kNotificationSurfaceWidth,
                               kNotificationSurfaceHeight,
                               view.output_scale.scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    auto now = std::chrono::steady_clock::now();
    service.animations.tick(now);

    view.scene.rebuild();
    float y_cursor = static_cast<float>(kNotificationSurfaceHeight);

    std::deque<Color> blend;
    for (const NotificationEntry &entry : service.entries) {
        y_cursor -= entry.height;
        if (y_cursor < 0.0f)
            break;
        float card_y = y_cursor + entry.slide_offset;

        const Color &urgency_color =
            notification_detail_urgency_color(entry.urgency);

        blend.push_back(with_alpha(palette::overlay, entry.opacity));
        const Color &card_fill = blend.back();
        blend.push_back(with_alpha(urgency_color, entry.opacity));
        const Color &card_border = blend.back();

        Node *card = node_add_rrect(
            &view.scene.root, 0, card_y, kNotificationSurfaceWidth,
            entry.height, kNotificationCardRadius, kNotificationCardBorderWidth,
            rgba(card_fill), rgba(card_border));
        card->clip_children = true;

        float remaining_ms =
            std::chrono::duration<float, std::milli>(entry.expires_at - now)
                .count();
        float progress = entry.timeout_ms > 0
                             ? std::clamp(remaining_ms / static_cast<float>(
                                                             entry.timeout_ms),
                                          0.0f, 1.0f)
                             : 0.0f;
        float bar_rrect_h = 2.0f * kNotificationCardRadius;
        blend.push_back(with_alpha(
            urgency_color, kNotificationProgressTrackOpacity * entry.opacity));
        Node *track_clip = node_add_group(card, 0, 0, kNotificationSurfaceWidth,
                                          kNotificationProgressHeight, true);
        node_add_rrect(track_clip, 0, 0, kNotificationSurfaceWidth, bar_rrect_h,
                       kNotificationCardRadius, 0, rgba(blend.back()),
                       kNodeTransparent);

        float fill_x = kNotificationSurfaceWidth * (1.0f - progress);
        float fill_w = kNotificationSurfaceWidth * progress;
        blend.push_back(with_alpha(urgency_color, entry.opacity));
        Node *fill_clip = node_add_group(card, fill_x, 0, fill_w,
                                         kNotificationProgressHeight, true);
        node_add_rrect(fill_clip, -fill_x, 0, kNotificationSurfaceWidth,
                       bar_rrect_h, kNotificationCardRadius, 0,
                       rgba(blend.back()), kNodeTransparent);

        float content_x = kNotificationCardPadding;
        float content_y = kNotificationCardPadding;
        float header_h =
            std::max(kNotificationUrgencyDotSize,
                     notification_detail_texture_height(entry.app_name_texture));

        blend.push_back(with_alpha(urgency_color, entry.opacity));
        node_add_rrect(card, content_x,
                       content_y +
                           (header_h - kNotificationUrgencyDotSize) / 2.0f,
                       kNotificationUrgencyDotSize, kNotificationUrgencyDotSize,
                       kNotificationUrgencyDotRadius, 0, rgba(blend.back()),
                       kNodeTransparent);

        if (entry.app_name_texture.id) {
            blend.push_back(with_alpha(palette::text, 0.65f * entry.opacity));
            node_add_texture(
                card,
                content_x + kNotificationUrgencyDotSize +
                    kNotificationHeaderSpacing,
                content_y +
                    (header_h - notification_detail_texture_height(
                                    entry.app_name_texture)) /
                        2.0f,
                entry.app_name_texture, rgba(blend.back()));
        }

        float row_y = content_y + header_h;
        if (entry.summary_texture.id) {
            row_y += kNotificationContentSpacing;
            blend.push_back(with_alpha(palette::text, entry.opacity));
            node_add_texture(card, content_x, row_y, entry.summary_texture,
                             rgba(blend.back()));
            row_y += notification_detail_texture_height(entry.summary_texture);
        }
        if (entry.body_texture.id) {
            row_y += kNotificationContentSpacing;
            blend.push_back(with_alpha(palette::text, 0.72f * entry.opacity));
            node_add_texture(card, content_x, row_y, entry.body_texture,
                             rgba(blend.back()));
        }

        y_cursor -= kNotificationCardGap;
    }
    view.scene.draw(*view.renderer);
    eglSwapBuffers(view.egl_display, view.egl_surface);

    if (!service.entries.empty() || service.animations.hasActive())
        request_frame(view.frame_clock);
}

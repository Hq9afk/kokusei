#pragma once

#include "../render/color_ops.hpp"
#include "notification_state.hpp"

inline void notification_paint(NotificationView &view,
                               NotificationService &service) {
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
            notification_detail::urgency_color(entry.urgency);

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
        float header_h = std::max(
            kNotificationUrgencyDotSize,
            notification_detail::texture_height(entry.app_name_texture));

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
                content_y + (header_h - notification_detail::texture_height(
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
            row_y += notification_detail::texture_height(entry.summary_texture);
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

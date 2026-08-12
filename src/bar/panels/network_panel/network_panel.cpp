#include "network_panel.h"

#include "../../../render/icon.h"
#include "../../../render/icons.h"
#include "../../../render/node.h"
#include "../../../render/palette.h"
#include "../../../render/panel_scroll.h"
#include "../../../render/text.h"

#include <GLES2/gl2.h>

#include <algorithm>

using namespace network_panel_detail;

void network_panel_paint(NetworkPanelState &state, NetworkState &net,
                         float pill_center_x, float bar_height,
                         float bar_top_margin) {
    using namespace panel_chrome_detail;
    if (state.base.egl_surface == EGL_NO_SURFACE)
        return;
    state.base.animations.tick(std::chrono::steady_clock::now());
    eglMakeCurrent(state.base.egl_display, state.base.egl_surface,
                   state.base.egl_surface, state.base.egl_context);
    int32_t scale = state.base.output_scale.scale;
    state.renderer->begin_frame(state.base.width, state.base.height, scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.click_regions.clear();
    state.panel_rect = {};
    state.sub_rect = {};
    state.scene.rebuild();

    if (!state.base.open) {
        state.scene.draw(*state.renderer);
        eglSwapBuffers(state.base.egl_display, state.base.egl_surface);
        return;
    }

    constexpr float kNoBorder[4] = {0, 0, 0, 0};
    Node *root = &state.scene.root;

    std::vector<PanelRow> rows = build_rows(net);
    float content_h = content_height(rows);
    float panel_w = kPanelWidth;
    float panel_h = panel_height(content_h);
    if (state.locked_center_x < 0.0f)
        state.locked_center_x = pill_center_x;
    if (state.visible_height < 0.0f) {
        state.visible_height = 0.0f;
        state.base.animations.animate(
            state.visible_height, panel_h, kOverlayFadeMs, Easing::EaseOutCubic,
            [&state](float v) { state.visible_height = v; }, {},
            kPanelHeightAnimOwner);
    }
    float panel_x = std::clamp(
        state.locked_center_x - panel_w / 2.0f, kPanelSideMargin,
        static_cast<float>(state.base.width) - panel_w - kPanelSideMargin);
    float panel_y = bar_height + bar_top_margin + kPanelGap;
    state.panel_rect = {panel_x, panel_y, panel_w, panel_h};

    const float *white = rgba(palette::text);
    const float *dim = rgba(palette::text_dim);

    panel_draw_box(root, panel_x, panel_y, panel_w, panel_h);
    float header_y = panel_y + kPanelPadding;
    float hx = panel_draw_header(root, state.tcache, scale, "Network", panel_x,
                                 panel_y, panel_w, state.click_regions);

    if (net.wifi_available) {
        float toggle_w = 36.0f, toggle_h = 20.0f;
        Rect toggle_rect = {hx - toggle_w,
                            header_y + (kPanelHeaderHeight - toggle_h) / 2.0f,
                            toggle_w, toggle_h};
        node_add_rrect(root, toggle_rect.x, toggle_rect.y, toggle_rect.w,
                       toggle_rect.h, toggle_rect.h / 2.0f, 0.0f,
                       net.wifi_enabled ? rgba(palette::accent)
                                        : rgba(palette::text_alpha11),
                       kNoBorder);
        float knob = 16.0f;
        float knob_x = net.wifi_enabled
                           ? toggle_rect.x + toggle_rect.w - knob - 2.0f
                           : toggle_rect.x + 2.0f;
        node_add_rrect(root, knob_x, toggle_rect.y + (toggle_h - knob) / 2.0f,
                       knob, knob, knob / 2.0f, 0.0f, white, kNoBorder);
        state.click_regions.push_back(
            {PanelClickKind::HeaderToggle, toggle_rect, ""});
        hx = toggle_rect.x - kPanelRowGap;

        Rect scan_rect = {
            hx - kPanelActionButtonSize,
            header_y + (kPanelHeaderHeight - kPanelActionButtonSize) / 2.0f,
            kPanelActionButtonSize, kPanelActionButtonSize};
        node_add_rrect(root, scan_rect.x, scan_rect.y, scan_rect.w, scan_rect.h,
                       kPanelActionButtonSize / 2.0f, 0.0f,
                       rgba(palette::overlay), rgba(palette::overlay));
        const Texture *scan_tex =
            cached_icon(state.tcache, icon::refresh, scale);
        if (scan_tex)
            node_add_texture(
                root, scan_rect.x + (scan_rect.w - scan_tex->width) / 2.0f,
                scan_rect.y + (scan_rect.h - scan_tex->height) / 2.0f,
                *scan_tex, net.scanning ? rgba(palette::accent) : white);
        state.click_regions.push_back(
            {PanelClickKind::HeaderAction, scan_rect, ""});
    }

    float divider_y = header_y + kPanelHeaderHeight + kPanelHeaderDividerGap;
    node_add_rect(root, panel_x + kPanelPadding, divider_y,
                  panel_w - 2 * kPanelPadding, 1.0f,
                  rgba(palette::text_alpha06));

    PanelScrollRegion region =
        panel_scroll_region(panel_x, panel_y, panel_w, panel_h);
    float content_x = region.content_x;
    float content_w = region.content_w;
    float content_top = region.content_top;

    float scroll_top = content_top;

    float scroll_visible_h =
        std::clamp(region.content_bottom - content_top, 0.0f, content_h);
    state.visible_content_height = scroll_visible_h;

    auto draw_row = [&](Node *clip, const PanelRow &row, float y,
                        float rx_origin, float ry_origin) {
        auto rx = [&](float v) { return v - rx_origin; };
        auto ry = [&](float v) { return v - ry_origin; };
        float row_h = row.height;
        switch (row.kind) {
        case RowKind::Error: {
            node_add_rrect(clip, rx(content_x), ry(y), content_w, row_h, 8.0f,
                           0.0f, rgba(palette::critical_alpha15), kNoBorder);
            const Texture *warn =
                cached_icon(state.tcache, icon::alert_triangle, scale);
            float tx = content_x + kPanelRowIconGap;
            if (warn) {
                node_add_texture(clip, rx(tx),
                                 ry(y + (row_h - warn->height) / 2.0f), *warn,
                                 rgba(palette::critical));
                tx += warn->width + kPanelRowGap;
            }
            float close_w = 18.0f;
            const Texture *err_tex =
                cached_text(state.tcache, elide(net.last_error, 48), scale);
            if (err_tex)
                node_add_texture(clip, rx(tx),
                                 ry(y + (row_h - err_tex->height) / 2.0f),
                                 *err_tex, rgba(palette::critical));
            Rect err_close = {content_x + content_w - kPanelRowIconGap -
                                  close_w,
                              y + (row_h - close_w) / 2.0f, close_w, close_w};
            const Texture *err_close_tex =
                cached_icon(state.tcache, icon::close, scale);
            if (err_close_tex)
                node_add_texture(
                    clip,
                    rx(err_close.x + (close_w - err_close_tex->width) / 2.0f),
                    ry(err_close.y + (close_w - err_close_tex->height) / 2.0f),
                    *err_close_tex, rgba(palette::critical));
            state.click_regions.push_back(
                {PanelClickKind::ErrorClose, err_close, ""});
            break;
        }
        case RowKind::Ethernet: {
            node_add_rrect(clip, rx(content_x), ry(y), content_w, row_h, 8.0f,
                           0.0f, rgba(palette::accent_alpha19), kNoBorder);
            const Texture *t = cached_text(state.tcache, "Ethernet", scale);
            if (t)
                node_add_texture(clip,
                                 rx(content_x + (content_w - t->width) / 2.0f),
                                 ry(y + (row_h - t->height) / 2.0f), *t, white);
            break;
        }
        case RowKind::NoAdapter:
        case RowKind::Disabled: {
            const Texture *t = cached_text(state.tcache,
                                           row.kind == RowKind::NoAdapter
                                               ? "No Wi-Fi adapter found"
                                               : "WiFi is disabled",
                                           scale);
            if (t)
                node_add_texture(clip,
                                 rx(content_x + (content_w - t->width) / 2.0f),
                                 ry(y + (row_h - t->height) / 2.0f), *t, dim);
            break;
        }
        case RowKind::Scanning: {
            std::string dots(static_cast<size_t>(net.scan_dot_step), '.');
            const Texture *t =
                cached_text(state.tcache, "Scanning" + dots, scale);
            if (t)
                node_add_texture(clip,
                                 rx(content_x + (content_w - t->width) / 2.0f),
                                 ry(y + (row_h - t->height) / 2.0f), *t, dim);
            break;
        }
        case RowKind::SectionConnected:
        case RowKind::SectionKnown:
        case RowKind::SectionAvailable: {
            const char *label =
                row.kind == RowKind::SectionConnected ? "Connected"
                : row.kind == RowKind::SectionKnown   ? "Known"
                                                      : "Available";
            const Texture *t = cached_text(state.tcache, label, scale);
            if (t)
                node_add_texture(clip, rx(content_x), ry(y + row_h - t->height),
                                 *t, dim);
            break;
        }
        case RowKind::Device: {
            const NetworkInfo &info = *row.info;
            bool is_captive = info.connected && net.connectivity == "portal";
            bool is_connected = info.connected;
            bool is_busy = net.connecting_to == info.ssid;
            bool is_secured = !info.security.empty() && info.security != "--";

            const float *row_bg = is_captive     ? kNetworkCaptiveBg
                                  : is_connected ? rgba(palette::accent_alpha25)
                                  : is_busy      ? rgba(palette::accent_alpha12)
                                                 : rgba(palette::overlay);
            float row_rect_h = kPanelDeviceRowHeight;
            node_add_rrect(clip, rx(content_x), ry(y), content_w, row_rect_h,
                           8.0f, 0.0f, row_bg, kNoBorder);

            const float *fg = is_captive     ? kNetworkCaptiveFg
                              : is_connected ? rgba(palette::accent)
                                             : rgba(palette::text_dim);
            const Texture *sig =
                cached_icon(state.tcache, signal_icon(info.signal), scale);
            if (sig)
                node_add_texture(clip, rx(content_x + kPanelRowIconGap),
                                 ry(y + (row_rect_h - sig->height) / 2.0f),
                                 *sig, fg);

            float actions_w = 0.0f;
            std::string action_label = is_connected ? "Disconnect" : "Connect";
            const Texture *action_tex =
                cached_text(state.tcache, action_label, scale);
            float connect_btn_w = (action_tex ? action_tex->width : 0) +
                                  kPanelDialogButtonPaddingH;
            bool show_forget = info.existing && !is_connected && !is_busy;
            const Texture *busy_tex = nullptr;
            if (is_busy) {
                busy_tex =
                    cached_text(state.tcache, "Connecting\xE2\x80\xA6", scale);
                actions_w = static_cast<float>(busy_tex ? busy_tex->width : 0);
            } else {
                actions_w = connect_btn_w;
                if (show_forget)
                    actions_w += kPanelTightGap + kPanelActionButtonSize;
            }

            float text_left = content_x + 38.0f;
            float text_right = content_x + content_w - kPanelRowIconGap -
                               actions_w -
                               (actions_w > 0 ? kPanelRowActionGap : 0.0f);
            int text_w_px =
                static_cast<int>(std::max(0.0f, text_right - text_left));
            Node *tclip =
                node_add_group(clip, rx(text_left), ry(y),
                               text_right - text_left, row_rect_h, true);
            const float *ssid_fg = is_captive     ? kNetworkCaptiveFg
                                   : is_connected ? rgba(palette::accent)
                                                  : white;
            const Texture *ssid_tex =
                cached_text_clipped(state.tcache, info.ssid, scale, text_w_px);
            std::string subtitle = is_captive   ? "Sign in required"
                                   : is_secured ? info.security
                                                : "Open";
            const Texture *sub_tex =
                cached_text_clipped(state.tcache, subtitle, scale, text_w_px);
            panel_draw_row_text(tclip, ssid_tex, sub_tex, row_rect_h, ssid_fg,
                                is_captive ? kNetworkCaptiveFg : dim);

            float ax = content_x + content_w - kPanelRowIconGap;
            if (is_busy) {
                if (busy_tex) {
                    ax -= busy_tex->width;
                    node_add_texture(
                        clip, rx(ax),
                        ry(y + (row_rect_h - busy_tex->height) / 2.0f),
                        *busy_tex, rgba(palette::accent));
                }
            } else {
                if (show_forget) {
                    Rect forget_rect = {
                        ax - kPanelActionButtonSize,
                        y + (row_rect_h - kPanelActionButtonSize) / 2.0f,
                        kPanelActionButtonSize, kPanelActionButtonSize};
                    node_add_rrect(clip, rx(forget_rect.x), ry(forget_rect.y),
                                   forget_rect.w, forget_rect.h,
                                   kPanelActionButtonSize / 2.0f, 0.0f,
                                   rgba(palette::critical_alpha15), kNoBorder);
                    const Texture *x_tex =
                        cached_icon(state.tcache, icon::close, scale);
                    if (x_tex)
                        node_add_texture(
                            clip,
                            rx(forget_rect.x +
                               (forget_rect.w - x_tex->width) / 2.0f),
                            ry(forget_rect.y +
                               (forget_rect.h - x_tex->height) / 2.0f),
                            *x_tex, rgba(palette::critical));
                    state.click_regions.push_back(
                        {PanelClickKind::RowForget, forget_rect, info.ssid});
                    ax -= kPanelActionButtonSize + kPanelTightGap;
                }
                Rect connect_rect = {ax - connect_btn_w,
                                     y + (row_rect_h - kPanelActionButtonSize) /
                                             2.0f,
                                     connect_btn_w, kPanelActionButtonSize};
                node_add_rrect(clip, rx(connect_rect.x), ry(connect_rect.y),
                               connect_rect.w, connect_rect.h,
                               kPanelActionButtonSize / 2.0f, 0.0f,
                               is_connected ? rgba(palette::text_alpha11)
                                            : rgba(palette::accent),
                               kNoBorder);
                if (action_tex)
                    node_add_texture(
                        clip,
                        rx(connect_rect.x +
                           (connect_rect.w - action_tex->width) / 2.0f),
                        ry(connect_rect.y +
                           (connect_rect.h - action_tex->height) / 2.0f),
                        *action_tex, white);
                state.click_regions.push_back(
                    {PanelClickKind::RowConnect, connect_rect, info.ssid});
            }
            break;
        }
        case RowKind::Spacer:
            break;
        }
    };

    Node *scroll_clip = node_add_group(root, panel_x, scroll_top, panel_w,
                                       scroll_visible_h, true);
    {
        float scroll_bottom = scroll_top + scroll_visible_h;
        float y = scroll_top - state.scroll_offset;
        for (size_t i = 0; i < rows.size(); ++i) {
            const PanelRow &row = rows[i];
            if (i > 0)
                y += kPanelListSpacing;
            bool row_visible = y + row.height > scroll_top && y < scroll_bottom;
            if (row_visible)
                draw_row(scroll_clip, row, y, panel_x, scroll_top);
            y += row.height;
        }
    }

    if (!state.sub_mode.empty()) {
        float sub_h = sub_panel_height(state.sub_mode);
        float sub_y = panel_y + panel_h + kPanelGap;
        state.sub_rect = {panel_x, sub_y, panel_w, sub_h};
        float inner_x = panel_x + kPanelPadding;
        float inner_w = panel_w - 2 * kPanelPadding;

        if (state.sub_mode == "password") {
            float iy = panel_draw_subpanel_top(
                root, state.tcache, scale, state.sub_ssid, panel_x, sub_y,
                panel_w, sub_h, state.click_regions);
            Rect field_rect = {inner_x, iy, inner_w, kPanelSubPanelRowHeight};
            node_add_rrect(root, field_rect.x, field_rect.y, field_rect.w,
                           field_rect.h, 6.0f, 1.0f,
                           rgba(palette::text_alpha08),
                           rgba(palette::text_alpha15));
            if (state.password_input.empty()) {
                const Texture *ph =
                    cached_text(state.tcache, "Password\xE2\x80\xA6", scale);
                if (ph)
                    node_add_texture(
                        root, field_rect.x + (field_rect.w - ph->width) / 2.0f,
                        field_rect.y + (field_rect.h - ph->height) / 2.0f, *ph,
                        dim);
            } else {

                float dot = 8.0f, gap = 4.0f;
                size_t n = state.password_input.size();
                float dots_w = n * dot + (n > 0 ? (n - 1) * gap : 0);
                float dx = field_rect.x + (field_rect.w - dots_w) / 2.0f;
                float dy = field_rect.y + (field_rect.h - dot) / 2.0f;
                for (size_t i = 0; i < n; ++i) {
                    node_add_rrect(root, dx, dy, dot, dot, dot / 2.0f, 0.0f,
                                   white, kNoBorder);
                    dx += dot + gap;
                }
            }
            iy += kPanelSubPanelRowHeight + kPanelRowGap;

            bool ok = state.password_input.size() >= 8;
            const Texture *connect_tex =
                cached_text(state.tcache, "Connect", scale);
            float btn_w = (connect_tex ? connect_tex->width : 0) +
                          kPanelDialogButtonPaddingH;
            Rect confirm_rect = {inner_x + (inner_w - btn_w) / 2.0f, iy, btn_w,
                                 kPanelConfirmButtonSize};
            float accent_alpha = ok ? 1.0f : kNetworkConnectDisabledAlpha;
            float fill[4] = {palette::accent.r, palette::accent.g,
                             palette::accent.b, accent_alpha};
            node_add_rrect(root, confirm_rect.x, confirm_rect.y, confirm_rect.w,
                           confirm_rect.h, 6.0f, 0.0f, fill, kNoBorder);
            if (connect_tex)
                node_add_texture(
                    root,
                    confirm_rect.x +
                        (confirm_rect.w - connect_tex->width) / 2.0f,
                    confirm_rect.y +
                        (confirm_rect.h - connect_tex->height) / 2.0f,
                    *connect_tex, white);
            if (ok)
                state.click_regions.push_back(
                    {PanelClickKind::SubConfirm, confirm_rect, ""});
        } else {
            const char *prompt =
                state.sub_mode == "disconnect" ? "Disconnect?" : "Forget?";
            const char *confirm_label =
                state.sub_mode == "disconnect" ? "Disconnect" : "Forget";
            panel_draw_confirm_subpanel(
                root, state.tcache, scale, state.sub_ssid, prompt,
                confirm_label, panel_x, sub_y, panel_w, state.click_regions);
        }
    }

    if (state.base.animations.hasActive()) {
        ScopedClip clip(*state.renderer, panel_x, panel_y, panel_w,
                        std::max(0.0f, state.visible_height));
        state.scene.draw(*state.renderer);
    } else {
        state.scene.draw(*state.renderer);
    }
    eglSwapBuffers(state.base.egl_display, state.base.egl_surface);

    if (state.base.animations.hasActive())
        overlay_panel_request_frame(state.base);
}

#include "bluetooth_panel.h"

#include "../../../render/icon.h"
#include "../../../render/icons.h"
#include "../../../render/node.h"
#include "../../../render/palette.h"
#include "../../../render/panel_scroll.h"
#include "../../../render/text.h"

#include <GLES2/gl2.h>

#include <algorithm>

using namespace bluetooth_panel_detail;

void bluetooth_panel_paint(BluetoothPanelState &state, BluetoothState &bt,
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

    Node *root = &state.scene.root;

    std::vector<PanelRow> rows = build_rows(bt);
    float panel_w = kPanelWidth;
    float panel_h = panel_height(rows);
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
    float hx =
        panel_draw_header(root, state.tcache, scale, "Bluetooth", panel_x,
                          panel_y, panel_w, state.click_regions);

    if (bt.adapter_present) {
        float toggle_w = 36.0f, toggle_h = 20.0f;
        Rect toggle_rect = {hx - toggle_w,
                            header_y + (kPanelHeaderHeight - toggle_h) / 2.0f,
                            toggle_w, toggle_h};
        node_add_rrect(root, toggle_rect.x, toggle_rect.y, toggle_rect.w,
                       toggle_rect.h, toggle_rect.h / 2.0f, 0.0f,
                       bt.powered ? rgba(palette::accent)
                                  : rgba(palette::text_alpha11),
                       kPanelNoBorder);
        float knob = 16.0f;
        float knob_x = bt.powered ? toggle_rect.x + toggle_rect.w - knob - 2.0f
                                  : toggle_rect.x + 2.0f;
        node_add_rrect(root, knob_x, toggle_rect.y + (toggle_h - knob) / 2.0f,
                       knob, knob, knob / 2.0f, 0.0f, white, kPanelNoBorder);
        state.click_regions.push_back(
            {PanelClickKind::HeaderToggle, toggle_rect, ""});
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
    float content_bottom = region.content_bottom;

    state.visible_content_height = std::max(0.0f, content_bottom - content_top);

    Node *scroll_clip =
        node_add_group(root, panel_x, content_top, panel_w,
                       std::max(0.0f, content_bottom - content_top), true);

    auto rx = [&](float v) { return v - panel_x; };
    auto ry = [&](float v) { return v - content_top; };

    float y = content_top - state.scroll_offset;
    for (size_t i = 0; i < rows.size(); ++i) {
        const PanelRow &row = rows[i];
        if (i > 0)
            y += kPanelListSpacing;
        Node *clip = scroll_clip;
        float row_h = row.height;
        bool row_visible = y + row_h > content_top && y < content_bottom;
        if (!row_visible) {
            y += row_h;
            continue;
        }

        switch (row.kind) {
        case RowKind::NoAdapter: {
            const Texture *t =
                cached_text(state.tcache, "No adapter found", scale);
            if (t)
                node_add_texture(clip,
                                 rx(content_x + (content_w - t->width) / 2.0f),
                                 ry(y + (row_h - t->height) / 2.0f), *t, dim);
            break;
        }
        case RowKind::Off: {
            const Texture *t =
                cached_text(state.tcache, "Bluetooth is off", scale);
            if (t)
                node_add_texture(clip,
                                 rx(content_x + (content_w - t->width) / 2.0f),
                                 ry(y + (row_h - t->height) / 2.0f), *t, dim);
            break;
        }
        case RowKind::Empty: {
            const Texture *t =
                cached_text(state.tcache, "No paired devices", scale);
            if (t)
                node_add_texture(clip,
                                 rx(content_x + (content_w - t->width) / 2.0f),
                                 ry(y + (row_h - t->height) / 2.0f), *t, dim);
            break;
        }
        case RowKind::SectionConnected:
        case RowKind::SectionPaired:
        case RowKind::SectionNearby: {
            const char *label = row.kind == RowKind::SectionConnected
                                    ? "Connected"
                                : row.kind == RowKind::SectionPaired ? "Paired"
                                                                     : "Nearby";
            const Texture *t = cached_text(state.tcache, label, scale);
            if (t)
                node_add_texture(clip, rx(content_x), ry(y + row_h - t->height),
                                 *t, dim);
            break;
        }
        case RowKind::Device: {
            const BluetoothDeviceInfo &info = *row.info;
            bool is_connected = info.connected;
            bool is_busy = info.connecting;
            bool is_paired_or_connected =
                info.paired || info.trusted || info.connected;

            const float *row_bg = is_connected ? rgba(palette::accent_alpha25)
                                  : is_busy    ? rgba(palette::accent_alpha12)
                                               : rgba(palette::overlay);
            node_add_rrect(clip, rx(content_x), ry(y), content_w, row_h, 8.0f,
                           0.0f, row_bg, kPanelNoBorder);

            const float *fg = is_connected ? rgba(palette::accent) : dim;
            const Texture *dev_icon =
                cached_icon(state.tcache, icon::bluetooth_device, scale);
            if (dev_icon)
                node_add_texture(clip, rx(content_x + kPanelRowIconGap),
                                 ry(y + (row_h - dev_icon->height) / 2.0f),
                                 *dev_icon, is_connected ? white : fg);

            float actions_w = 0.0f;
            std::string action_label = is_connected ? "Disconnect" : "Connect";
            const Texture *action_tex =
                cached_text(state.tcache, action_label, scale);
            float connect_btn_w = (action_tex ? action_tex->width : 0) +
                                  kPanelDialogButtonPaddingH;
            bool show_forget =
                is_paired_or_connected && !is_connected && !is_busy;
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
            Node *tclip = node_add_group(clip, rx(text_left), ry(y),
                                         text_right - text_left, row_h, true);
            const Texture *name_tex =
                cached_text_clipped(state.tcache, info.name, scale, text_w_px);

            std::string subtitle;
            if (is_busy)
                subtitle = "Connecting\xE2\x80\xA6";
            else if (info.has_battery)
                subtitle = std::to_string(info.battery_percent) + "%";
            const Texture *sub_tex =
                subtitle.empty() ? nullptr
                                 : cached_text_clipped(state.tcache, subtitle,
                                                       scale, text_w_px);

            panel_draw_row_text(tclip, name_tex, sub_tex, row_h,
                                is_connected ? rgba(palette::accent) : white,
                                dim);

            float ax = content_x + content_w - kPanelRowIconGap;
            if (is_busy) {
                if (busy_tex) {
                    ax -= busy_tex->width;
                    node_add_texture(clip, rx(ax),
                                     ry(y + (row_h - busy_tex->height) / 2.0f),
                                     *busy_tex, rgba(palette::accent));
                }
            } else {
                if (show_forget) {
                    Rect forget_rect = {
                        ax - kPanelActionButtonSize,
                        y + (row_h - kPanelActionButtonSize) / 2.0f,
                        kPanelActionButtonSize, kPanelActionButtonSize};
                    node_add_rrect(clip, rx(forget_rect.x), ry(forget_rect.y),
                                   forget_rect.w, forget_rect.h,
                                   kPanelActionButtonSize / 2.0f, 0.0f,
                                   rgba(palette::critical_alpha15),
                                   kPanelNoBorder);
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
                        {PanelClickKind::RowForget, forget_rect, info.path});
                    ax -= kPanelActionButtonSize + kPanelTightGap;
                }
                Rect connect_rect = {ax - connect_btn_w,
                                     y + (row_h - kPanelActionButtonSize) /
                                             2.0f,
                                     connect_btn_w, kPanelActionButtonSize};
                node_add_rrect(clip, rx(connect_rect.x), ry(connect_rect.y),
                               connect_rect.w, connect_rect.h,
                               kPanelActionButtonSize / 2.0f, 0.0f,
                               is_connected ? rgba(palette::text_alpha11)
                                            : rgba(palette::accent),
                               kPanelNoBorder);
                if (action_tex)
                    node_add_texture(
                        clip,
                        rx(connect_rect.x +
                           (connect_rect.w - action_tex->width) / 2.0f),
                        ry(connect_rect.y +
                           (connect_rect.h - action_tex->height) / 2.0f),
                        *action_tex, white);
                state.click_regions.push_back(
                    {PanelClickKind::RowConnect, connect_rect, info.path});
            }
            break;
        }
        case RowKind::Spacer:
            break;
        }
        y += row_h;
    }

    if (!state.sub_mode.empty()) {
        float sub_h = panel_confirm_subpanel_height();
        float sub_y = panel_y + panel_h + kPanelGap;
        state.sub_rect = {panel_x, sub_y, panel_w, sub_h};

        const BluetoothDeviceInfo *dev = nullptr;
        for (const BluetoothDeviceInfo &d : bt.devices)
            if (d.path == state.sub_device_path)
                dev = &d;
        std::string top_label = dev ? dev->name : state.sub_device_path;

        const char *prompt =
            state.sub_mode == "disconnect" ? "Disconnect?" : "Forget?";
        const char *confirm_label =
            state.sub_mode == "disconnect" ? "Disconnect" : "Forget";
        panel_draw_confirm_subpanel(root, state.tcache, scale, top_label,
                                    prompt, confirm_label, panel_x, sub_y,
                                    panel_w, state.click_regions);
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

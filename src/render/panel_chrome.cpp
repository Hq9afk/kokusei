#include "render/panel_chrome.h"

#include "config/settings_config.h"

namespace panel_chrome_detail {

const Texture *cached_text(TextureCache &cache, const std::string &s,
                           int32_t scale) {
    if (s.empty())
        return nullptr;
    return cache.get("t" + std::to_string(scale) + ":" + s,
                     [&] { return rasterize_text(s, scale); });
}

const Texture *cached_icon(TextureCache &cache, const char *codepoint,
                           int32_t scale) {
    return cache.get("i" + std::to_string(scale) + ":" + codepoint,
                     [&] { return rasterize_icon(codepoint, scale); });
}

const Texture *cached_text_clipped(TextureCache &cache, const std::string &s,
                                   int32_t scale, int max_width_px) {
    if (s.empty())
        return nullptr;
    return cache.get("t" + std::to_string(scale) + ":w" +
                         std::to_string(max_width_px) + ":" + s,
                     [&] { return rasterize_text(s, scale, max_width_px); });
}

} // namespace panel_chrome_detail

Node *panel_draw_box(Node *parent, float x, float y, float w, float h,
                     float border_width) {
    return node_add_rrect(parent, x, y, w, h, metrics::radius_md, border_width,
                          rgba(palette::overlay), rgba(palette::accent));
}

float panel_draw_header(Node *parent, TextureCache &cache, int32_t scale,
                        const std::string &title, float panel_x, float panel_y,
                        float panel_w,
                        std::vector<PanelClickRegion> &click_regions) {
    using namespace panel_chrome_detail;
    const float *white = rgba(palette::text);

    float header_y = panel_y + kPanelPadding;
    const Texture *title_tex = cached_text(cache, title, scale);
    if (title_tex)
        node_add_texture(parent, panel_x + kPanelPadding,
                         header_y +
                             (kPanelHeaderHeight - title_tex->height) / 2.0f,
                         *title_tex, white);

    float header_right = panel_x + panel_w - kPanelPadding;
    Rect close_rect = {header_right - kPanelActionButtonSize,
                       header_y +
                           (kPanelHeaderHeight - kPanelActionButtonSize) / 2.0f,
                       kPanelActionButtonSize, kPanelActionButtonSize};
    node_add_rrect(parent, close_rect.x, close_rect.y, close_rect.w,
                   close_rect.h, kPanelActionButtonSize / 2.0f, 0.0f,
                   rgba(palette::overlay), rgba(palette::overlay));
    const Texture *close_tex = cached_icon(cache, icon::close, scale);
    if (close_tex)
        node_add_texture(
            parent, close_rect.x + (close_rect.w - close_tex->width) / 2.0f,
            close_rect.y + (close_rect.h - close_tex->height) / 2.0f,
            *close_tex, white);
    click_regions.push_back({PanelClickKind::Close, close_rect, ""});

    return close_rect.x - kPanelRowGap;
}

void panel_draw_row_text(Node *tclip, const Texture *name_tex,
                         const Texture *sub_tex, float row_h,
                         const float *name_color, const float *sub_color) {
    if (!name_tex)
        return;
    float name_y = sub_tex ? row_h / 2.0f - name_tex->height - 1
                           : row_h / 2.0f - name_tex->height / 2.0f;
    node_add_texture(tclip, 0, name_y, *name_tex, name_color);
    if (sub_tex)
        node_add_texture(tclip, 0, row_h / 2.0f + 1, *sub_tex, sub_color);
}

float panel_confirm_subpanel_height() {
    float inner = kPanelDialogSpacerHeight + kPanelSubLabelHeight +
                  kPanelRowGap + kPanelSubPanelRowHeight + kPanelRowGap;
    return inner + 2.0f * kPanelPadding;
}

float panel_draw_subpanel_top(Node *parent, TextureCache &cache, int32_t scale,
                              const std::string &top_label, float panel_x,
                              float sub_y, float panel_w, float sub_h,
                              std::vector<PanelClickRegion> &click_regions) {
    using namespace panel_chrome_detail;
    const float *white = rgba(palette::text);
    const float *dim = rgba(palette::text_dim);

    panel_draw_box(parent, panel_x, sub_y, panel_w, sub_h);

    float sub_top_x = panel_x + kPanelPadding;
    float sub_top_y = sub_y + kPanelSubPanelTopMargin;
    const Texture *label_tex = cached_text(cache, elide(top_label, 32), scale);
    if (label_tex)
        node_add_texture(parent, sub_top_x, sub_top_y, *label_tex, dim);

    Rect sub_close = {
        panel_x + panel_w - kPanelSubPanelTopMargin - kPanelConfirmButtonSize,
        sub_top_y, kPanelConfirmButtonSize, kPanelConfirmButtonSize};
    node_add_rrect(parent, sub_close.x, sub_close.y, sub_close.w, sub_close.h,
                   6.0f, 0.0f, rgba(palette::overlay), kPanelNoBorder);
    const Texture *sub_close_tex = cached_icon(cache, icon::close, scale);
    if (sub_close_tex)
        node_add_texture(
            parent, sub_close.x + (sub_close.w - sub_close_tex->width) / 2.0f,
            sub_close.y + (sub_close.h - sub_close_tex->height) / 2.0f,
            *sub_close_tex, white);
    click_regions.push_back({PanelClickKind::SubClose, sub_close, ""});

    return sub_y + kPanelPadding + kPanelDialogSpacerHeight;
}

void panel_draw_confirm_subpanel(Node *parent, TextureCache &cache,
                                 int32_t scale, const std::string &top_label,
                                 const std::string &prompt,
                                 const std::string &confirm_label,
                                 float panel_x, float sub_y, float panel_w,
                                 std::vector<PanelClickRegion> &click_regions) {
    using namespace panel_chrome_detail;
    const float *white = rgba(palette::text);
    float sub_h = panel_confirm_subpanel_height();

    float iy = panel_draw_subpanel_top(parent, cache, scale, top_label, panel_x,
                                       sub_y, panel_w, sub_h, click_regions);
    float inner_x = panel_x + kPanelPadding;
    float inner_w = panel_w - 2 * kPanelPadding;

    const Texture *prompt_tex = cached_text(cache, prompt, scale);
    if (prompt_tex)
        node_add_texture(parent, inner_x + (inner_w - prompt_tex->width) / 2.0f,
                         iy, *prompt_tex, white);
    iy += kPanelSubLabelHeight + kPanelRowGap;

    const Texture *ok_tex = cached_text(cache, confirm_label, scale);
    const Texture *cancel_tex = cached_text(cache, "Cancel", scale);
    float ok_w = (ok_tex ? ok_tex->width : 0) + kPanelDialogButtonPaddingH;
    float cancel_w =
        (cancel_tex ? cancel_tex->width : 0) + kPanelDialogButtonPaddingH;
    float row_w = ok_w + kPanelRowGap + cancel_w;
    float row_x = inner_x + (inner_w - row_w) / 2.0f;

    Rect ok_rect = {row_x, iy, ok_w, kPanelConfirmButtonSize};
    node_add_rrect(parent, ok_rect.x, ok_rect.y, ok_rect.w, ok_rect.h, 6.0f,
                   0.0f, rgba(palette::critical_alpha15), kPanelNoBorder);
    if (ok_tex)
        node_add_texture(parent, ok_rect.x + (ok_rect.w - ok_tex->width) / 2.0f,
                         ok_rect.y + (ok_rect.h - ok_tex->height) / 2.0f,
                         *ok_tex, rgba(palette::critical));
    click_regions.push_back({PanelClickKind::SubConfirm, ok_rect, ""});

    Rect cancel_rect = {row_x + ok_w + kPanelRowGap, iy, cancel_w,
                        kPanelConfirmButtonSize};
    node_add_rrect(parent, cancel_rect.x, cancel_rect.y, cancel_rect.w,
                   cancel_rect.h, 6.0f, 0.0f, rgba(palette::overlay),
                   kPanelNoBorder);
    if (cancel_tex)
        node_add_texture(
            parent, cancel_rect.x + (cancel_rect.w - cancel_tex->width) / 2.0f,
            cancel_rect.y + (cancel_rect.h - cancel_tex->height) / 2.0f,
            *cancel_tex, white);
    click_regions.push_back({PanelClickKind::SubCancel, cancel_rect, ""});
}

float panel_draw_dropdown(Node *parent, TextureCache &cache, int32_t scale,
                          float x, float y, float row_w,
                          const std::string &label,
                          const std::string &active_value,
                          const std::vector<PanelDropdownOption> &options,
                          const std::string &dropdown_id,
                          const std::string &open_dropdown_id,
                          std::vector<PanelClickRegion> &click_regions) {
    using namespace panel_chrome_detail;
    bool open = open_dropdown_id == dropdown_id;

    const Texture *lbl_tex = cached_text(cache, label, scale);
    if (lbl_tex)
        node_add_texture(parent, x,
                         y + (kSettingsFieldHeight - lbl_tex->height) / 2.0f,
                         *lbl_tex, rgba(palette::text));

    std::string active_label = "\xE2\x80\x94";
    for (const PanelDropdownOption &opt : options)
        if (opt.value == active_value) {
            active_label = opt.label;
            break;
        }

    const Texture *active_tex = cached_text(cache, active_label, scale);
    const Texture *chevron_tex = cached_icon(
        cache, open ? icon::chevron_up : icon::chevron_down, scale);
    float content_w = (active_tex ? active_tex->width : 0) + 6.0f +
                      (chevron_tex ? chevron_tex->width : 0);
    float trigger_w = content_w + 20.0f;
    float trigger_x = x + row_w - trigger_w;

    node_add_rrect(parent, trigger_x, y, trigger_w, kSettingsFieldHeight,
                   metrics::radius_sm, metrics::border_thin,
                   open ? rgba(palette::accent_alpha12)
                        : rgba(palette::text_alpha06),
                   open ? rgba(palette::accent) : rgba(palette::text_alpha11));
    if (active_tex)
        node_add_texture(
            parent, trigger_x + 10.0f,
            y + (kSettingsFieldHeight - active_tex->height) / 2.0f,
            *active_tex, rgba(palette::text));
    if (chevron_tex)
        node_add_texture(parent,
                         trigger_x + trigger_w - 10.0f - chevron_tex->width,
                         y + (kSettingsFieldHeight - chevron_tex->height) /
                                 2.0f,
                         *chevron_tex, rgba(palette::text_dim));
    click_regions.push_back({PanelClickKind::DropdownToggle,
                             {trigger_x, y, trigger_w, kSettingsFieldHeight},
                             dropdown_id});

    if (open) {
        float popup_y = y + kSettingsFieldHeight + kPanelTightGap;
        float popup_h =
            static_cast<float>(options.size()) * kSettingsFieldHeight;
        node_add_rrect(parent, trigger_x, popup_y, trigger_w, popup_h,
                       metrics::radius_sm, metrics::border_thin,
                       rgba(palette::overlay), rgba(palette::accent));
        float oy = popup_y;
        for (const PanelDropdownOption &opt : options) {
            bool active = opt.value == active_value;
            if (active)
                node_add_rrect(parent, trigger_x, oy, trigger_w,
                               kSettingsFieldHeight, metrics::radius_sm, 0.0f,
                               rgba(palette::accent_alpha19), kPanelNoBorder);
            const Texture *opt_tex = cached_text(cache, opt.label, scale);
            if (opt_tex)
                node_add_texture(
                    parent, trigger_x + 10.0f,
                    oy + (kSettingsFieldHeight - opt_tex->height) / 2.0f,
                    *opt_tex,
                    active ? rgba(palette::accent) : rgba(palette::text));
            click_regions.push_back(
                {PanelClickKind::DropdownSelect,
                 {trigger_x, oy, trigger_w, kSettingsFieldHeight},
                 dropdown_id + "|" + opt.value});
            oy += kSettingsFieldHeight;
        }
    }

    return y + kSettingsRowHeight;
}

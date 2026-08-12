#include "panel_chrome.h"

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

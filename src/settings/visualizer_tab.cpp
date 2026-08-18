#include "settings/visualizer_tab.h"

#include "modules/settings.h"

using panel_chrome_detail::cached_text;

namespace {

struct ShapeOption {
    const char *label;
    const char *shape;
};

constexpr ShapeOption kShapeOptions[2] = {
    {"Bars", "bars"},
    {"NCS Circle", "ncs"},
};

} // namespace

float visualizer_tab_paint(SettingsState &state, Node *root, int32_t scale,
                           float x, float y, const Config &cfg) {
    const Texture *label_tex = cached_text(state.tcache, "Shape", scale);
    if (label_tex)
        node_add_texture(root, x,
                         y + (kSettingsToggleTileHeight - label_tex->height) /
                                 2.0f,
                         *label_tex, rgba(palette::text));

    float row_w = state.panel_rect.x + state.panel_rect.w - kPanelPadding - x;
    float tile_gap = kSettingsGroupSpacingSm;
    float tile_w = (row_w - tile_gap) / 2.0f;
    float tile_x = x;

    for (const ShapeOption &opt : kShapeOptions) {
        bool active = cfg.visualizer_shape == opt.shape;
        node_add_rrect(root, tile_x, y, tile_w, kSettingsToggleTileHeight,
                       kSettingsTileRadius, kSettingsToggleTileBorderWidth,
                       active ? rgba(palette::accent_alpha19)
                              : rgba(palette::text_alpha04),
                       active ? rgba(palette::accent) : rgba(palette::text_alpha07));
        const Texture *tex = cached_text(state.tcache, opt.label, scale);
        if (tex)
            node_add_texture(
                root, tile_x + (tile_w - tex->width) / 2.0f,
                y + (kSettingsToggleTileHeight - tex->height) / 2.0f, *tex,
                active ? rgba(palette::accent) : rgba(palette::text_alpha85));
        state.click_regions.push_back(
            {PanelClickKind::ToggleFlip,
             {tile_x, y, tile_w, kSettingsToggleTileHeight},
             std::string("visualizershape|") + opt.shape});
        tile_x += tile_w + tile_gap;
    }

    y += kSettingsToggleTileHeight + kPanelRowGap;
    return y;
}

bool visualizer_tab_handle_click(SettingsState &state, const Config &cfg,
                                 const std::function<void(Config)> &on_commit,
                                 const PanelClickRegion &region) {
    if (region.kind != PanelClickKind::ToggleFlip)
        return false;
    if (region.tag.rfind("visualizershape|", 0) != 0)
        return false;

    settings_commit_focused_field(state, cfg, on_commit);
    Config updated = cfg;
    updated.visualizer_shape = region.tag.substr(16);
    on_commit(updated);
    settings_request_frame(state);
    return true;
}

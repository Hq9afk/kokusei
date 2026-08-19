#include "settings/visualizer_tab.h"

#include "modules/settings.h"

namespace {

const std::vector<PanelDropdownOption> kShapeOptions = {
    {"Bar", "bar"},
    {"Sphere", "sphere"},
};

constexpr const char *kShapeDropdownId = "visualizershape";

} // namespace

float visualizer_tab_paint(SettingsState &state, Node *root, int32_t scale,
                           float x, float y, const Config &cfg) {
    float row_w = state.panel_rect.x + state.panel_rect.w - kPanelPadding - x;
    return panel_draw_dropdown(root, state.tcache, scale, x, y, row_w, "Shape",
                               cfg.visualizer_shape, kShapeOptions,
                               kShapeDropdownId, state.open_dropdown_id,
                               state.click_regions);
}

bool visualizer_tab_handle_click(SettingsState &state, const Config &cfg,
                                 const std::function<void(Config)> &on_commit,
                                 const PanelClickRegion &region) {
    if (region.kind != PanelClickKind::DropdownSelect)
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

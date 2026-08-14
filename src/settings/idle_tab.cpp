#include "settings/idle_tab.h"

using panel_chrome_detail::cached_text;

namespace {

void draw_field_row(SettingsState &state, Node *parent, int32_t scale,
                    float label_x, float y, float field_x, float field_w,
                    const std::string &label, SettingsFieldId id,
                    const Config &cfg) {
    const Texture *label_tex = cached_text(state.tcache, label, scale);
    if (label_tex)
        node_add_texture(parent, label_x,
                         y + (kSettingsFieldHeight - label_tex->height) / 2.0f,
                         *label_tex, rgba(palette::text));

    bool focused = state.focused_field == id;
    node_add_rrect(parent, field_x, y, field_w, kSettingsFieldHeight,
                   metrics::radius_sm, metrics::border_thin,
                   rgba(palette::field_bg),
                   focused ? rgba(palette::accent) : kPanelNoBorder);

    std::string display = focused ? state.field_buffer.text
                                  : settings_detail_format_field(cfg, id);
    if (id == SettingsFieldId::WallpaperPath && !focused)
        display = elide(display, 30);
    const Texture *value_tex = cached_text(state.tcache, display, scale);
    if (value_tex)
        node_add_texture(parent, field_x + 8,
                         y + (kSettingsFieldHeight - value_tex->height) / 2.0f,
                         *value_tex, rgba(palette::text));

    if (focused && state.field_buffer.cursor_blink_visible) {
        float cursor_x = field_x + 8 + (value_tex ? value_tex->width : 0) + 2;
        node_add_rect(parent, cursor_x, y + 5, 1.5f, kSettingsFieldHeight - 10,
                      rgba(palette::text));
    }

    state.click_regions.push_back({PanelClickKind::FieldFocus,
                                   {field_x, y, field_w, kSettingsFieldHeight},
                                   std::to_string(static_cast<int>(id))});
}

} // namespace

void idle_tab_paint(SettingsState &state, Node *root, int32_t scale,
                    float label_x, float field_x, float y, const Config &cfg) {
    draw_field_row(state, root, scale, label_x, y, field_x,
                   kSettingsNumberFieldWidth, "Timeout (s)",
                   SettingsFieldId::IdleTimeout, cfg);
    y += kSettingsRowHeight;
    draw_field_row(state, root, scale, label_x, y, field_x, kSettingsFieldWidth,
                   "On idle", SettingsFieldId::IdleCommand, cfg);
    y += kSettingsRowHeight;
    draw_field_row(state, root, scale, label_x, y, field_x, kSettingsFieldWidth,
                   "On resume", SettingsFieldId::IdleResumeCommand, cfg);
    y += kSettingsRowHeight;
}

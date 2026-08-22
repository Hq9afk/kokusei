#include "settings/idle_tab.h"
#include "settings/displays_tab.h"

#include "render/icons.h"

using panel_chrome_detail::cached_icon;
using panel_chrome_detail::cached_text;

namespace {

std::string idle_monitor_from_tag(const std::string &tag) {
    return tag == kSettingsDisplaysDefaultTag ? "" : tag;
}

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

// One tile in the "Ambient Mode" / "Screensaver" group: label, numeric
// timeout field (clamped kSettingsIdleTimeoutMin..Max), a reset-to-default
// icon (override rows only, hidden once already at the default), a divider,
// then an enable toggle - mirrors keqing-shell's IdleTab.qml tile Repeater.
void draw_idle_tier_tile(SettingsState &state, Node *parent, int32_t scale,
                         float x, float y, float w, const std::string &label,
                         SettingsFieldId field_id, uint32_t value,
                         uint32_t default_value, bool enabled_value,
                         bool show_reset, const char *reset_tag,
                         const char *toggle_tag) {
    float h = kSettingsToggleTileHeight;
    node_add_rrect(parent, x, y, w, h, kSettingsTileRadius,
                   kSettingsToggleTileBorderWidth, rgba(palette::text_alpha04),
                   rgba(palette::text_alpha07));
    float inset = kSettingsToggleTileContentMargin;

    const Texture *label_tex = cached_text(state.tcache, label, scale);
    if (label_tex)
        node_add_texture(parent, x + inset, y + (h - label_tex->height) / 2.0f,
                         *label_tex, rgba(palette::text_alpha85));

    float switch_x = x + w - inset - kSettingsToggleTrackWidth;
    float divider_x = switch_x - kSettingsToggleTileContentSpacing;
    float reset_x =
        divider_x - kSettingsToggleTileContentSpacing - kSettingsIdleResetIconSize;
    float field_w = kSettingsNumberFieldWidth;
    float field_x = reset_x - kSettingsToggleTileContentSpacing - field_w;
    float field_y = y + (h - kSettingsFieldHeight) / 2.0f;

    bool focused = state.focused_field == field_id;
    node_add_rrect(parent, field_x, field_y, field_w, kSettingsFieldHeight,
                   metrics::radius_sm, metrics::border_thin,
                   rgba(palette::field_bg),
                   focused ? rgba(palette::accent) : kPanelNoBorder);
    std::string display =
        focused ? state.field_buffer.text : std::to_string(value);
    const Texture *value_tex = cached_text(state.tcache, display, scale);
    if (value_tex)
        node_add_texture(
            parent, field_x + 8,
            field_y + (kSettingsFieldHeight - value_tex->height) / 2.0f,
            *value_tex, rgba(palette::text));
    if (focused && state.field_buffer.cursor_blink_visible) {
        float cursor_x = field_x + 8 + (value_tex ? value_tex->width : 0) + 2;
        node_add_rect(parent, cursor_x, field_y + 5, 1.5f,
                      kSettingsFieldHeight - 10, rgba(palette::text));
    }
    state.click_regions.push_back(
        {PanelClickKind::FieldFocus,
         {field_x, field_y, field_w, kSettingsFieldHeight},
         std::to_string(static_cast<int>(field_id))});

    if (show_reset && value != default_value) {
        float reset_y = y + (h - kSettingsIdleResetIconSize) / 2.0f;
        const Texture *reset_icon = cached_icon(state.tcache, icon::refresh, scale);
        if (reset_icon)
            node_add_texture(
                parent,
                reset_x + (kSettingsIdleResetIconSize - reset_icon->width) / 2.0f,
                reset_y +
                    (kSettingsIdleResetIconSize - reset_icon->height) / 2.0f,
                *reset_icon, rgba(palette::text_dim));
        state.click_regions.push_back(
            {PanelClickKind::ToggleFlip,
             {reset_x, reset_y, kSettingsIdleResetIconSize,
              kSettingsIdleResetIconSize},
             reset_tag});
    }

    node_add_rect(parent, divider_x, y + 10.0f, 1.0f, h - 20.0f,
                  rgba(palette::text_alpha11));

    float switch_y = y + (h - kSettingsToggleTrackHeight) / 2.0f;
    draw_toggle_switch(state, parent, switch_x, switch_y, enabled_value,
                       toggle_tag);
}

} // namespace

void idle_tab_paint(SettingsState &state, Node *root, int32_t scale, float x,
                    float y, float w, const Config &cfg) {
    draw_toggle_row(state, root, scale, x, y, w, "Enable Idle Management",
                    cfg.idle_management_enabled, "idlemanagementenabled",
                    true);
    y += kSettingsToggleTileHeight + kPanelRowGap;

    settings_draw_monitor_row(state, root, scale, x, y, w,
                              state.idle_selected_monitor);
    y += kSettingsScreenSelectorHeight + kPanelRowGap;

    bool is_default = state.idle_selected_monitor.empty();
    const MonitorOverride *ov = nullptr;
    if (!is_default) {
        auto it = cfg.monitor_overrides.find(state.idle_selected_monitor);
        if (it != cfg.monitor_overrides.end())
            ov = &it->second;
    }
    bool override_enabled = ov && ov->enabled;

    if (!is_default) {
        draw_toggle_row(state, root, scale, x, y, w,
                        "Override default settings", override_enabled,
                        "idleoverride", false);
        y += kSettingsToggleTrackHeight + kPanelRowGap;
    }

    if (is_default || override_enabled) {
        bool ambient_enabled_val =
            is_default ? cfg.ambient_enabled : ov->ambient_enabled;
        uint32_t ambient_timeout_val =
            is_default ? cfg.ambient_timeout_seconds
                       : ov->ambient_timeout_seconds;
        bool screensaver_enabled_val =
            is_default ? cfg.screensaver_enabled : ov->screensaver_enabled;
        uint32_t screensaver_timeout_val =
            is_default ? cfg.screensaver_timeout_seconds
                       : ov->screensaver_timeout_seconds;

        draw_idle_tier_tile(state, root, scale, x, y, w, "Ambient Mode",
                            SettingsFieldId::AmbientTimeout,
                            ambient_timeout_val, cfg.ambient_timeout_seconds,
                            ambient_enabled_val, !is_default,
                            "idleambientreset", "idleambientenabled");
        y += kSettingsToggleTileHeight + kSettingsGroupSpacingSm;

        draw_idle_tier_tile(state, root, scale, x, y, w, "Screensaver",
                            SettingsFieldId::ScreensaverTimeout,
                            screensaver_timeout_val,
                            cfg.screensaver_timeout_seconds,
                            screensaver_enabled_val, !is_default,
                            "idlescreensaverreset", "idlescreensaverenabled");
        y += kSettingsToggleTileHeight + kPanelRowGap;
    }

    float field_x = x + kSettingsLabelWidth;
    draw_field_row(state, root, scale, x, y, field_x, kSettingsNumberFieldWidth,
                   "Timeout (s)", SettingsFieldId::IdleTimeout, cfg);
    y += kSettingsRowHeight;
    draw_field_row(state, root, scale, x, y, field_x, kSettingsFieldWidth,
                   "On idle", SettingsFieldId::IdleCommand, cfg);
    y += kSettingsRowHeight;
    draw_field_row(state, root, scale, x, y, field_x, kSettingsFieldWidth,
                   "On resume", SettingsFieldId::IdleResumeCommand, cfg);
    y += kSettingsRowHeight;
}

bool idle_tab_handle_click(SettingsState &state, const Config &cfg,
                           const SettingsCommitFn &on_commit,
                           const PanelClickRegion &region) {
    if (region.kind == PanelClickKind::MonitorSelect) {
        state.idle_selected_monitor = idle_monitor_from_tag(region.tag);
        settings_request_frame(state);
        return true;
    }
    if (region.kind != PanelClickKind::ToggleFlip)
        return false;

    settings_commit_focused_field(state, cfg, on_commit);
    const std::string &mon = state.idle_selected_monitor;
    bool is_default = mon.empty();

    if (region.tag == "idlemanagementenabled") {
        Config updated = cfg;
        updated.idle_management_enabled = !cfg.idle_management_enabled;
        on_commit(updated);
    } else if (region.tag == "idleoverride") {
        Config updated = cfg;
        MonitorOverride &ov = updated.monitor_overrides[mon];
        if (!ov.enabled) {
            ov.osd = cfg.default_osd_enabled;
            ov.notifications = cfg.default_notifications_enabled;
            ov.autohide = cfg.autohide;
            ov.ambient_enabled = cfg.ambient_enabled;
            ov.ambient_timeout_seconds = cfg.ambient_timeout_seconds;
            ov.screensaver_enabled = cfg.screensaver_enabled;
            ov.screensaver_timeout_seconds = cfg.screensaver_timeout_seconds;
        }
        ov.enabled = !ov.enabled;
        on_commit(updated);
    } else if (region.tag == "idleambientenabled" ||
              region.tag == "idlescreensaverenabled") {
        Config updated = cfg;
        if (is_default) {
            if (region.tag == "idleambientenabled")
                updated.ambient_enabled = !cfg.ambient_enabled;
            else
                updated.screensaver_enabled = !cfg.screensaver_enabled;
        } else {
            MonitorOverride &ov = updated.monitor_overrides[mon];
            if (region.tag == "idleambientenabled")
                ov.ambient_enabled = !ov.ambient_enabled;
            else
                ov.screensaver_enabled = !ov.screensaver_enabled;
        }
        on_commit(updated);
    } else if (region.tag == "idleambientreset" && !is_default) {
        Config updated = cfg;
        updated.monitor_overrides[mon].ambient_timeout_seconds =
            cfg.ambient_timeout_seconds;
        on_commit(updated);
    } else if (region.tag == "idlescreensaverreset" && !is_default) {
        Config updated = cfg;
        updated.monitor_overrides[mon].screensaver_timeout_seconds =
            cfg.screensaver_timeout_seconds;
        on_commit(updated);
    } else {
        return false;
    }
    settings_request_frame(state);
    return true;
}

#include "settings/displays_tab.h"

#include <algorithm>

using panel_chrome_detail::cached_text;

namespace {

std::string displays_monitor_from_tag(const std::string &tag) {
    return tag == kSettingsDisplaysDefaultTag ? "" : tag;
}

void draw_displays_monitor_row(SettingsState &state, Node *parent,
                               int32_t scale, float x, float y, float row_w) {
    std::vector<std::string> sorted_names = state.monitor_names;
    std::sort(sorted_names.begin(), sorted_names.end());

    int n = static_cast<int>(sorted_names.size());
    float tile_w = (row_w - n * kSettingsScreenSelectorSpacing) / (n + 1);

    float cx = x;
    auto draw_tile = [&](const std::string &label, const std::string &tag,
                         bool active) {
        node_add_rrect(parent, cx, y, tile_w, kSettingsScreenSelectorHeight,
                       kSettingsTileRadius, kSettingsSelectorBorderWidth,
                       rgba(palette::lavender_alpha20),
                       active ? rgba(palette::accent_alt) : kPanelNoBorder);
        const Texture *tex = cached_text(state.tcache, label, scale);
        if (tex)
            node_add_texture(parent, cx + (tile_w - tex->width) / 2.0f,
                             y + (kSettingsScreenSelectorHeight - tex->height) /
                                     2.0f,
                             *tex, rgba(palette::text));
        state.click_regions.push_back(
            {PanelClickKind::MonitorSelect,
             {cx, y, tile_w, kSettingsScreenSelectorHeight},
             tag});
        cx += tile_w + kSettingsScreenSelectorSpacing;
    };

    draw_tile("Default", kSettingsDisplaysDefaultTag,
              state.displays_selected_monitor.empty());
    for (const std::string &name : sorted_names)
        draw_tile(name, name, name == state.displays_selected_monitor);
}

} // namespace

void displays_tab_paint(SettingsState &state, Node *root, int32_t scale,
                        float x, float y, float w, const Config &cfg) {
    draw_displays_monitor_row(state, root, scale, x, y, w);
    y += kSettingsScreenSelectorHeight + kPanelRowGap;

    bool is_default = state.displays_selected_monitor.empty();
    const MonitorOverride *ov = nullptr;
    if (!is_default) {
        auto it = cfg.monitor_overrides.find(state.displays_selected_monitor);
        if (it != cfg.monitor_overrides.end())
            ov = &it->second;
    }
    bool override_enabled = ov && ov->enabled;

    if (!is_default) {
        draw_toggle_row(state, root, scale, x, y, w,
                        "Override default settings", override_enabled,
                        "displaysoverride", false);
        y += kSettingsToggleTrackHeight + kPanelRowGap;
    }

    if (is_default || override_enabled) {
        bool osd_val = is_default ? cfg.default_osd_enabled : ov->osd;
        bool notif_val =
            is_default ? cfg.default_notifications_enabled : ov->notifications;
        bool autohide_val = is_default ? cfg.autohide : ov->autohide;

        draw_toggle_row(state, root, scale, x, y, w, "OSD", osd_val,
                        "osdenabled", true);
        y += kSettingsToggleTileHeight + kSettingsGroupSpacingSm;
        draw_toggle_row(state, root, scale, x, y, w, "Notifications", notif_val,
                        "notificationsenabled", true);
        y += kSettingsToggleTileHeight + kSettingsGroupSpacingSm;
        draw_toggle_row(state, root, scale, x, y, w, "Bar Autohide",
                        autohide_val, "autohideenabled", true);
        y += kSettingsToggleTileHeight;
    }
}

bool displays_tab_handle_click(SettingsState &state, const Config &cfg,
                               const SettingsCommitFn &on_commit,
                               const PanelClickRegion &region) {
    if (region.kind == PanelClickKind::MonitorSelect) {
        state.displays_selected_monitor = displays_monitor_from_tag(region.tag);
        settings_request_frame(state);
        return true;
    }
    if (region.kind != PanelClickKind::ToggleFlip)
        return false;

    settings_commit_focused_field(state, cfg, on_commit);
    if (region.tag == "displaysoverride") {
        Config updated = cfg;
        MonitorOverride &ov =
            updated.monitor_overrides[state.displays_selected_monitor];
        if (!ov.enabled) {
            ov.osd = cfg.default_osd_enabled;
            ov.notifications = cfg.default_notifications_enabled;
            ov.autohide = cfg.autohide;
        }
        ov.enabled = !ov.enabled;
        on_commit(updated);
    } else if (region.tag == "osdenabled" ||
               region.tag == "notificationsenabled" ||
               region.tag == "autohideenabled") {
        Config updated = cfg;
        bool is_default = state.displays_selected_monitor.empty();
        if (is_default) {
            if (region.tag == "osdenabled")
                updated.default_osd_enabled = !cfg.default_osd_enabled;
            else if (region.tag == "notificationsenabled")
                updated.default_notifications_enabled =
                    !cfg.default_notifications_enabled;
            else
                updated.autohide = !cfg.autohide;
        } else {
            MonitorOverride &ov =
                updated.monitor_overrides[state.displays_selected_monitor];
            if (region.tag == "osdenabled")
                ov.osd = !ov.osd;
            else if (region.tag == "notificationsenabled")
                ov.notifications = !ov.notifications;
            else
                ov.autohide = !ov.autohide;
        }
        on_commit(updated);
    } else {
        return false;
    }
    settings_request_frame(state);
    return true;
}

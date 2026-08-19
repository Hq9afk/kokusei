#include "settings/wallpaper_tab.h"

#include "core/deferred_call.h"
#include "modules/settings.h"
#include "render/panel_scroll.h"
#include "service/wallpaper_animate_service.h"
#include "service/wallpaper_service.h"

#include <algorithm>
#include <filesystem>
#include <thread>

using panel_chrome_detail::cached_icon;
using panel_chrome_detail::cached_text;
using panel_chrome_detail::cached_text_clipped;

bool wallpaper_picker_is_image(const std::string &path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
}

bool wallpaper_picker_is_video(const std::string &path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".mp4" || ext == ".webm" || ext == ".mkv";
}

namespace {

std::string wallpaper_picker_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

} // namespace

bool wallpaper_picker_less(const std::string &a, const std::string &b) {
    std::filesystem::path pa(a), pb(b);
    std::string ea = wallpaper_picker_lower(pa.extension().string());
    std::string eb = wallpaper_picker_lower(pb.extension().string());
    if (ea != eb)
        return ea < eb;
    return wallpaper_picker_lower(pa.filename().string()) <
           wallpaper_picker_lower(pb.filename().string());
}

void wallpaper_picker_scan(WallpaperPickerState &state, std::string dir,
                           bool (*is_match)(const std::string &)) {
    state.dir = dir;
    state.scanning = true;
    uint64_t generation = ++state.scan_generation;
    std::thread([&state, dir, generation, is_match] {
        std::vector<std::string> found;
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(
            dir, std::filesystem::directory_options::skip_permission_denied,
            ec);
        std::filesystem::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            if (it.depth() >= 1)
                it.disable_recursion_pending();
            if (it->is_regular_file(ec) && is_match(it->path().string()))
                found.push_back(it->path().string());
        }
        std::sort(found.begin(), found.end(), wallpaper_picker_less);
        DeferredCall::call_later(
            [&state, found = std::move(found), generation] {
                if (generation != state.scan_generation)
                    return;
                state.files = std::move(found);
                state.scanning = false;
                if (state.request_frame)
                    state.request_frame();
            });
    }).detach();
}

void wallpaper_picker_request_thumbnail(WallpaperPickerState &state,
                                        const std::string &path,
                                        int target_size, EGLDisplay display,
                                        EGLSurface surface, EGLContext context,
                                        bool video) {
    if (state.thumbnails.count(path) || state.pending.count(path))
        return;
    state.pending.insert(path);
    uint64_t generation = state.scan_generation;
    std::thread([&state, path, target_size, generation, display, surface,
                 context, video] {
        std::string decode_path = path;
        if (video) {
            decode_path = wallpaper_animate_thumbnail_prepare(path);
            if (decode_path.empty()) {
                DeferredCall::call_later(
                    [&state, path] { state.pending.erase(path); });
                return;
            }
        }
        int w = 0, h = 0;
        unsigned char *data = wallpaper_decode_scaled(decode_path, target_size,
                                                      target_size, w, h);
        DeferredCall::call_later(
            [&state, path, data, w, h, generation, display, surface, context] {
                state.pending.erase(path);
                if (generation != state.scan_generation) {
                    delete[] data;
                    return;
                }
                if (!data)
                    return;
                eglMakeCurrent(display, surface, surface, context);
                state.thumbnails[path] = make_texture_rgba(w, h, data, true);
                delete[] data;
                if (state.request_frame)
                    state.request_frame();
            });
    }).detach();
}

namespace {

int wallpaper_grid_columns(float) { return kSettingsWallpaperGridColumns; }

float wallpaper_grid_content_height(const WallpaperPickerState &picker,
                                    float content_w) {
    int cols = wallpaper_grid_columns(content_w);
    size_t rows = (picker.files.size() + cols - 1) / static_cast<size_t>(cols);
    if (rows == 0)
        return 0.0f;
    float cell = kSettingsWallpaperThumbSize + kSettingsWallpaperThumbGap;
    return static_cast<float>(rows) * cell - kSettingsWallpaperThumbGap;
}

int wallpaper_column_count(const Config &cfg, const std::string &region,
                           bool animated) {
    return animated ? wallpaper_service_animated_column_count(cfg, region)
                    : wallpaper_service_column_count(cfg, region);
}

std::string wallpaper_column_path(const Config &cfg, const std::string &region,
                                  int column, bool animated) {
    return animated
               ? wallpaper_service_animated_column_path(cfg, region, column)
               : wallpaper_service_column_path(cfg, region, column);
}

std::string wallpaper_column_override(const Config &cfg,
                                      const std::string &region, int column,
                                      bool animated) {
    return animated
               ? wallpaper_service_animated_column_override(cfg, region, column)
               : wallpaper_service_column_override(cfg, region, column);
}

std::string wallpaper_fill_mode(const Config &cfg, const std::string &region,
                                int column, bool animated) {
    return animated ? wallpaper_service_animated_fill_mode(cfg, region, column)
                    : wallpaper_service_fill_mode(cfg, region, column);
}

void set_wallpaper_column(Config &cfg, const std::string &monitor, int column,
                          const std::string &path, bool animated) {
    auto &all_cols =
        animated ? cfg.wallpaper_animated_columns : cfg.wallpaper_columns;
    std::vector<std::string> &cols = all_cols[monitor];
    if (static_cast<size_t>(column) >= cols.size())
        cols.resize(static_cast<size_t>(column) + 1);
    cols[static_cast<size_t>(column)] = path;
}

void set_wallpaper_fill_mode(Config &cfg, const std::string &monitor,
                             int column, const std::string &mode,
                             bool animated) {
    auto &all_modes =
        animated ? cfg.wallpaper_animated_fill_modes : cfg.wallpaper_fill_modes;
    std::vector<std::string> &modes = all_modes[monitor];
    if (static_cast<size_t>(column) >= modes.size())
        modes.resize(static_cast<size_t>(column) + 1);
    modes[static_cast<size_t>(column)] = mode;
}

void draw_wallpaper_dirbar(SettingsState &state, Node *parent, int32_t scale,
                           float x, float y, float w, WallpaperSubtabState &sub,
                           const std::string &dir, bool animated) {
    SettingsFieldId field_id = animated ? SettingsFieldId::WallpaperAnimatedDir
                                        : SettingsFieldId::WallpaperDir;
    bool focused = state.focused_field == field_id;
    node_add_rrect(parent, x, y, w, kSettingsDirBarHeight, metrics::radius_sm,
                   metrics::border_thin, rgba(palette::field_bg),
                   focused ? rgba(palette::accent) : kPanelNoBorder);

    const Texture *label_tex = cached_text(state.tcache, "Dir", scale);
    float input_x = x + kSettingsDirBarLabelMargin;
    if (label_tex) {
        node_add_texture(parent, input_x,
                         y + (kSettingsDirBarHeight - label_tex->height) / 2.0f,
                         *label_tex, rgba(palette::accent));
        input_x += label_tex->width;
    }
    input_x += kSettingsDirBarFieldMargin;

    float btn_x =
        x + w - kSettingsDirBarEdgeMargin - kSettingsDirBarButtonWidth;
    float input_w = btn_x - kSettingsDirBarEdgeMargin - input_x;

    std::string display = focused ? state.field_buffer.text : dir;
    const Texture *value_tex =
        focused ? cached_text(state.tcache, display, scale)
                : cached_text_clipped(state.tcache, display, scale,
                                      static_cast<int>(input_w));
    if (value_tex)
        node_add_texture(parent, input_x,
                         y + (kSettingsDirBarHeight - value_tex->height) / 2.0f,
                         *value_tex, rgba(palette::text));
    if (focused && state.field_buffer.cursor_blink_visible) {
        float cursor_x = input_x + (value_tex ? value_tex->width : 0) + 2;
        node_add_rect(parent, cursor_x, y + 8, 1.5f, kSettingsDirBarHeight - 16,
                      rgba(palette::text));
    }
    state.click_regions.push_back({PanelClickKind::FieldFocus,
                                   {input_x, y, input_w, kSettingsDirBarHeight},
                                   std::to_string(static_cast<int>(field_id))});

    float btn_y =
        y + (kSettingsDirBarHeight - kSettingsDirBarButtonHeight) / 2.0f;
    node_add_rrect(parent, btn_x, btn_y, kSettingsDirBarButtonWidth,
                   kSettingsDirBarButtonHeight, metrics::radius_sm, 0.0f,
                   rgba(palette::text_alpha11), kPanelNoBorder);
    const Texture *btn_tex = cached_text(
        state.tcache, sub.picker.scanning ? "\xE2\x80\xA6" : "Rescan", scale);
    if (btn_tex)
        node_add_texture(
            parent,
            btn_x + (kSettingsDirBarButtonWidth - btn_tex->width) / 2.0f,
            btn_y + (kSettingsDirBarButtonHeight - btn_tex->height) / 2.0f,
            *btn_tex, rgba(palette::text));
    state.click_regions.push_back(
        {PanelClickKind::ToggleFlip,
         {btn_x, btn_y, kSettingsDirBarButtonWidth,
          kSettingsDirBarButtonHeight},
         animated ? "animatedwallpaperrescan" : "wallpaperrescan"});
}

void draw_region_row(SettingsState &state, Node *parent, int32_t scale, float x,
                     float y, float w, const Config &cfg,
                     WallpaperSubtabState &sub, bool animated) {
    struct Chip {
        std::string name;
        int col;
    };
    std::vector<Chip> chips;
    for (const std::string &name : state.monitor_names) {
        int count = wallpaper_column_count(cfg, name, animated);
        for (int col = 0; col < count; ++col)
            chips.push_back({name, col});
    }
    if (chips.empty())
        return;

    float gap = kSettingsMonitorChipGap;
    float chip_w =
        (w - (chips.size() - 1) * gap) / static_cast<float>(chips.size());
    float cx = x;
    for (const Chip &chip : chips) {
        int count = wallpaper_column_count(cfg, chip.name, animated);
        std::string label = count > 1
                                ? chip.name + "-" + std::to_string(chip.col + 1)
                                : chip.name;
        const Texture *tex = cached_text(state.tcache, label, scale);
        bool active =
            chip.name == sub.selected_region && chip.col == sub.selected_column;
        node_add_rrect(parent, cx, y, chip_w, kSettingsMonitorChipHeight,
                       metrics::radius_sm, metrics::border_thin,
                       active ? rgba(palette::accent_alpha19)
                              : rgba(palette::field_bg),
                       active ? rgba(palette::accent_alt) : kPanelNoBorder);
        if (tex)
            node_add_texture(parent, cx + (chip_w - tex->width) / 2.0f,
                             y + (kSettingsMonitorChipHeight - tex->height) /
                                     2.0f,
                             *tex, rgba(palette::text));
        state.click_regions.push_back(
            {animated ? PanelClickKind::AnimatedRegionSelect
                      : PanelClickKind::RegionSelect,
             {cx, y, chip_w, kSettingsMonitorChipHeight},
             chip.name + "|" + std::to_string(chip.col)});
        cx += chip_w + gap;
    }
}

void draw_control_row(SettingsState &state, Node *parent, int32_t scale,
                      float x, float y, float w, const Config &cfg,
                      WallpaperSubtabState &sub, bool animated) {
    int count =
        sub.selected_region.empty()
            ? 1
            : wallpaper_column_count(cfg, sub.selected_region, animated);
    float step_y = y;
    float sub_x = x;
    node_add_rrect(parent, sub_x, step_y, kSettingsColumnStepperButtonSize,
                   kSettingsFieldHeight, metrics::radius_sm,
                   metrics::border_thin, rgba(palette::field_bg),
                   kPanelNoBorder);
    const Texture *sub_tex = cached_text(state.tcache, "-", scale);
    if (sub_tex)
        node_add_texture(
            parent,
            sub_x + (kSettingsColumnStepperButtonSize - sub_tex->width) / 2.0f,
            step_y + (kSettingsFieldHeight - sub_tex->height) / 2.0f, *sub_tex,
            rgba(palette::text));
    state.click_regions.push_back(
        {PanelClickKind::ToggleFlip,
         {sub_x, step_y, kSettingsColumnStepperButtonSize,
          kSettingsFieldHeight},
         animated ? "animatedcolumnsub" : "columnsub"});

    const Texture *count_tex =
        cached_text(state.tcache, std::to_string(count), scale);
    float count_w = (count_tex ? count_tex->width : 0) + 12.0f;
    float count_x = sub_x + kSettingsColumnStepperButtonSize;
    if (count_tex)
        node_add_texture(parent, count_x + (count_w - count_tex->width) / 2.0f,
                         step_y +
                             (kSettingsFieldHeight - count_tex->height) / 2.0f,
                         *count_tex, rgba(palette::text));

    float add_x = count_x + count_w;
    node_add_rrect(parent, add_x, step_y, kSettingsColumnStepperButtonSize,
                   kSettingsFieldHeight, metrics::radius_sm,
                   metrics::border_thin, rgba(palette::field_bg),
                   kPanelNoBorder);
    const Texture *add_tex = cached_text(state.tcache, "+", scale);
    if (add_tex)
        node_add_texture(
            parent,
            add_x + (kSettingsColumnStepperButtonSize - add_tex->width) / 2.0f,
            step_y + (kSettingsFieldHeight - add_tex->height) / 2.0f, *add_tex,
            rgba(palette::text));
    state.click_regions.push_back(
        {PanelClickKind::ToggleFlip,
         {add_x, step_y, kSettingsColumnStepperButtonSize,
          kSettingsFieldHeight},
         animated ? "animatedcolumnadd" : "columnadd"});

    if (!wallpaper_column_override(cfg, sub.selected_region,
                                   sub.selected_column, animated)
             .empty()) {
        const Texture *tex = cached_text(state.tcache, "Remove", scale);
        float rw = (tex ? tex->width : 0) + 20.0f;
        float rx = x + (w - rw) / 2.0f;
        node_add_rrect(parent, rx, y, rw, kSettingsFieldHeight,
                       metrics::radius_sm, metrics::border_thin,
                       rgba(palette::field_bg), rgba(palette::critical));
        if (tex)
            node_add_texture(parent, rx + 10.0f,
                             y + (kSettingsFieldHeight - tex->height) / 2.0f,
                             *tex, rgba(palette::critical));
        state.click_regions.push_back(
            {PanelClickKind::ToggleFlip,
             {rx, y, rw, kSettingsFieldHeight},
             animated ? "animatedwallpaperremove" : "wallpaperremove"});
    }

    std::string mode = wallpaper_fill_mode(cfg, sub.selected_region,
                                           sub.selected_column, animated);
    static const char *kLabels[2] = {"Crop", "Fit"};
    static const char *kModes[2] = {"crop", "fit"};
    float widths[2];
    float pair_w = 0.0f;
    for (int i = 0; i < 2; ++i) {
        const Texture *tex = cached_text(state.tcache, kLabels[i], scale);
        widths[i] = (tex ? tex->width : 0) + 20.0f;
        pair_w += widths[i] + (i == 0 ? 0.0f : 6.0f);
    }

    float cx = x + w - pair_w;
    for (int i = 0; i < 2; ++i) {
        bool active = mode == kModes[i];
        const Texture *tex = cached_text(state.tcache, kLabels[i], scale);
        float bw = widths[i];
        node_add_rrect(parent, cx, y, bw, kSettingsFieldHeight,
                       metrics::radius_sm, metrics::border_thin,
                       active ? rgba(palette::accent_alpha19)
                              : rgba(palette::field_bg),
                       active ? rgba(palette::accent) : kPanelNoBorder);
        if (tex)
            node_add_texture(
                parent, cx + 10.0f,
                y + (kSettingsFieldHeight - tex->height) / 2.0f, *tex,
                active ? rgba(palette::accent) : rgba(palette::text));
        state.click_regions.push_back(
            {PanelClickKind::ToggleFlip,
             {cx, y, bw, kSettingsFieldHeight},
             std::string(animated ? "animatedfillmode|" : "fillmode|") +
                 kModes[i]});
        cx += bw + 6.0f;
    }
}

void draw_wallpaper_grid(SettingsState &state, Node *parent, int32_t scale,
                         float x, float y, float w, float h, const Config &cfg,
                         WallpaperSubtabState &sub, bool animated) {
    WallpaperPickerState &picker = sub.picker;
    if (picker.files.empty()) {
        const Texture *t =
            cached_text(state.tcache,
                        picker.scanning ? "Scanning\xE2\x80\xA6"
                                        : (animated ? "No videos found"
                                                    : "No images found"),
                        scale);
        if (t)
            node_add_texture(parent, x, y, *t, rgba(palette::text_dim));
        return;
    }

    node_add_rrect(parent, x, y, w, h, metrics::radius_sm, metrics::border_thin,
                   kPanelNoBorder, rgba(palette::accent));

    float inset_x = x + kSettingsWallpaperGridInset;
    float inset_y = y + kSettingsWallpaperGridInset;
    float inset_w = w - kSettingsWallpaperGridInset * 2.0f;
    float inset_h = h - kSettingsWallpaperGridInset * 2.0f;

    int cols = wallpaper_grid_columns(inset_w);
    float content_h = wallpaper_grid_content_height(picker, inset_w);
    float visible_h = std::min(inset_h, content_h);
    float cell = kSettingsWallpaperThumbSize + kSettingsWallpaperThumbGap;
    float row_w = cols * cell - kSettingsWallpaperThumbGap;
    inset_x += (inset_w - row_w) / 2.0f;
    std::string selected = wallpaper_column_path(cfg, sub.selected_region,
                                                 sub.selected_column, animated);

    Node *clip =
        node_add_group(parent, inset_x, inset_y, row_w, visible_h, true);
    int total_rows =
        static_cast<int>((picker.files.size() + static_cast<size_t>(cols) - 1) /
                         static_cast<size_t>(cols));
    int first_row = std::clamp(static_cast<int>(sub.scroll_offset / cell), 0,
                               std::max(0, total_rows - 1));
    int last_row =
        std::clamp(static_cast<int>((sub.scroll_offset + visible_h) / cell), 0,
                   std::max(0, total_rows - 1));

    for (int row = first_row; row <= last_row; ++row) {
        for (int col = 0; col < cols; ++col) {
            size_t idx =
                static_cast<size_t>(row) * static_cast<size_t>(cols) + col;
            if (idx >= picker.files.size())
                break;
            const std::string &path = picker.files[idx];
            float cx = col * cell;
            float cy = row * cell - sub.scroll_offset;
            bool active = path == selected;

            auto it = picker.thumbnails.find(path);
            if (it != picker.thumbnails.end()) {
                node_add_rect(clip, cx, cy, kSettingsWallpaperThumbSize,
                              kSettingsWallpaperThumbSize,
                              rgba(palette::field_bg));

                const Texture &tex = it->second;
                Node *cell_clip =
                    node_add_group(clip, cx, cy, kSettingsWallpaperThumbSize,
                                   kSettingsWallpaperThumbSize, true);
                float tscale = std::max(kSettingsWallpaperThumbSize /
                                            static_cast<float>(tex.width),
                                        kSettingsWallpaperThumbSize /
                                            static_cast<float>(tex.height));
                float draw_w = tex.width * tscale;
                float draw_h = tex.height * tscale;
                node_add_texture_rect(
                    cell_clip, (kSettingsWallpaperThumbSize - draw_w) / 2.0f,
                    (kSettingsWallpaperThumbSize - draw_h) / 2.0f, draw_w,
                    draw_h, tex, rgba(palette::text));
            } else {
                node_add_rrect(clip, cx, cy, kSettingsWallpaperThumbSize,
                               kSettingsWallpaperThumbSize,
                               kSettingsWallpaperThumbRadius, 0.0f,
                               rgba(palette::field_bg), kPanelNoBorder);
                wallpaper_picker_request_thumbnail(
                    picker, path,
                    static_cast<int>(kSettingsWallpaperThumbSize * scale),
                    state.base.egl_display, state.base.egl_surface,
                    state.base.egl_context, animated);
            }

            std::string filename =
                std::filesystem::path(path).filename().string();
            const Texture *name_tex = cached_text_clipped(
                state.tcache, filename, scale,
                static_cast<int>(kSettingsWallpaperThumbSize -
                                 kSettingsWallpaperLabelPad));
            float label_h =
                (name_tex ? name_tex->height : 0) + kSettingsWallpaperLabelPad;
            node_add_rect(clip, cx, cy + kSettingsWallpaperThumbSize - label_h,
                          kSettingsWallpaperThumbSize, label_h,
                          rgba(palette::overlay));
            if (name_tex)
                node_add_texture(clip, cx + kSettingsWallpaperLabelPad / 2.0f,
                                 cy + kSettingsWallpaperThumbSize -
                                     (label_h + name_tex->height) / 2.0f,
                                 *name_tex, rgba(palette::text));

            node_add_rrect(
                clip, cx, cy, kSettingsWallpaperThumbSize,
                kSettingsWallpaperThumbSize, kSettingsWallpaperThumbRadius,
                active ? metrics::border_thick : 0.0f, kPanelNoBorder,
                active ? rgba(palette::accent_alt) : kPanelNoBorder);
            state.click_regions.push_back(
                {animated ? PanelClickKind::AnimatedWallpaperSelect
                          : PanelClickKind::WallpaperSelect,
                 {inset_x + cx, inset_y + cy, kSettingsWallpaperThumbSize,
                  kSettingsWallpaperThumbSize},
                 path});
        }
    }
}

} // namespace

float wallpaper_tab_paint(SettingsState &state, Node *root, int32_t scale,
                          float x, float y, const Config &cfg) {
    bool animated = cfg.wallpaper_animated_enabled;
    state.wallpaper_animated_active = animated;
    WallpaperSubtabState &sub =
        animated ? state.wallpaper_animated : state.wallpaper_static;
    std::string dir = animated ? cfg.wallpaper_animated_dir : cfg.wallpaper_dir;
    if (sub.picker.dir != dir)
        wallpaper_picker_scan(sub.picker, dir,
                              animated ? wallpaper_picker_is_video
                                       : wallpaper_picker_is_image);

    sub.grid_width =
        state.panel_rect.x + state.panel_rect.w - kPanelPadding - x;
    draw_toggle_row(state, root, scale, x, y, sub.grid_width,
                    "Use Default Wallpaper", cfg.default_wallpaper_enabled,
                    "usedefaultwallpaper", true);
    y += kSettingsToggleTileHeight + kPanelRowGap;
    draw_toggle_row(state, root, scale, x, y, sub.grid_width,
                    "Enable animated wallpaper", cfg.wallpaper_animated_enabled,
                    "enableanimatedwallpaper", true);
    y += kSettingsToggleTileHeight + kPanelRowGap;
    draw_region_row(state, root, scale, x, y, sub.grid_width, cfg, sub,
                    animated);
    y += kSettingsMonitorChipHeight + kPanelRowGap;
    draw_wallpaper_dirbar(state, root, scale, x, y, sub.grid_width, sub, dir,
                          animated);
    y += kSettingsDirBarHeight + kPanelRowGap;
    draw_control_row(state, root, scale, x, y, sub.grid_width, cfg, sub,
                     animated);
    y += kSettingsRowHeight;
    float grid_available_h =
        state.panel_rect.y + state.panel_rect.h - kPanelPadding - y;
    float grid_inset_w = sub.grid_width - kSettingsWallpaperGridInset * 2.0f;
    float grid_content_h =
        wallpaper_grid_content_height(sub.picker, grid_inset_w);
    sub.grid_height = std::min(
        grid_available_h, grid_content_h + kSettingsWallpaperGridInset * 2.0f);
    draw_wallpaper_grid(state, root, scale, x, y, sub.grid_width,
                        sub.grid_height, cfg, sub, animated);
    y += sub.grid_height;
    return y;
}

bool wallpaper_tab_handle_click(SettingsState &state, const Config &cfg,
                                const SettingsCommitFn &on_commit,
                                const PanelClickRegion &region) {
    if (region.kind == PanelClickKind::WallpaperSelect ||
        region.kind == PanelClickKind::AnimatedWallpaperSelect) {
        bool animated = region.kind == PanelClickKind::AnimatedWallpaperSelect;
        WallpaperSubtabState &sub =
            animated ? state.wallpaper_animated : state.wallpaper_static;
        settings_commit_focused_field(state, cfg, on_commit);
        Config updated = cfg;
        set_wallpaper_column(updated, sub.selected_region, sub.selected_column,
                             region.tag, animated);
        on_commit(updated);
        settings_request_frame(state);
        return true;
    }
    if (region.kind == PanelClickKind::RegionSelect ||
        region.kind == PanelClickKind::AnimatedRegionSelect) {
        bool animated = region.kind == PanelClickKind::AnimatedRegionSelect;
        WallpaperSubtabState &sub =
            animated ? state.wallpaper_animated : state.wallpaper_static;
        size_t sep = region.tag.find('|');
        sub.selected_region = region.tag.substr(0, sep);
        sub.selected_column = sep == std::string::npos
                                  ? 0
                                  : std::stoi(region.tag.substr(sep + 1));
        settings_request_frame(state);
        return true;
    }
    if (region.kind != PanelClickKind::ToggleFlip)
        return false;

    settings_commit_focused_field(state, cfg, on_commit);
    if (region.tag == "enableanimatedwallpaper") {
        Config updated = cfg;
        updated.wallpaper_animated_enabled = !cfg.wallpaper_animated_enabled;
        on_commit(updated);
        settings_request_frame(state);
        return true;
    }
    if (region.tag == "usedefaultwallpaper") {
        Config updated = cfg;
        updated.default_wallpaper_enabled = !cfg.default_wallpaper_enabled;
        on_commit(updated);
        settings_request_frame(state);
        return true;
    }

    bool animated = region.tag.rfind("animated", 0) == 0;
    WallpaperSubtabState &sub =
        animated ? state.wallpaper_animated : state.wallpaper_static;
    std::string tag = animated ? region.tag.substr(8) : region.tag;

    if (tag.rfind("fillmode|", 0) == 0) {
        Config updated = cfg;
        set_wallpaper_fill_mode(updated, sub.selected_region,
                                sub.selected_column, tag.substr(9), animated);
        on_commit(updated);
    } else if (tag == "wallpaperremove") {
        Config updated = cfg;
        set_wallpaper_column(updated, sub.selected_region, sub.selected_column,
                             "", animated);
        on_commit(updated);
    } else if (tag == "wallpaperrescan") {
        wallpaper_picker_scan(
            sub.picker,
            animated ? cfg.wallpaper_animated_dir : cfg.wallpaper_dir,
            animated ? wallpaper_picker_is_video : wallpaper_picker_is_image);
    } else if (tag == "columnadd" || tag == "columnsub") {
        Config updated = cfg;
        int count = wallpaper_column_count(cfg, sub.selected_region, animated);
        count = std::clamp(count + (tag == "columnadd" ? 1 : -1), 1, 6);
        auto &counts_map = animated ? updated.wallpaper_animated_column_counts
                                    : updated.wallpaper_column_counts;
        counts_map[sub.selected_region] = count;
        on_commit(updated);
    } else {
        return false;
    }
    settings_request_frame(state);
    return true;
}

void wallpaper_tab_handle_scroll(SettingsState &state, double dy) {
    WallpaperSubtabState &sub = state.wallpaper_animated_active
                                    ? state.wallpaper_animated
                                    : state.wallpaper_static;
    float inset_w = sub.grid_width - kSettingsWallpaperGridInset * 2.0f;
    float inset_h = sub.grid_height - kSettingsWallpaperGridInset * 2.0f;
    float content_h = wallpaper_grid_content_height(sub.picker, inset_w);
    sub.scroll_offset = panel_clamp_scroll(sub.scroll_offset,
                                           static_cast<float>(dy) *
                                               kSettingsWallpaperScrollSpeed,
                                           content_h, inset_h);
    settings_request_frame(state);
}

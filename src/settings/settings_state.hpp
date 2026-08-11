#pragma once

#include "../app/config.hpp"
#include "../core/log.hpp"
#include "../render/node.hpp"
#include "../render/overlay_panel.hpp"
#include "../render/palette.hpp"
#include "../render/panel_chrome.hpp"
#include "../render/rect.hpp"
#include "../render/renderer.hpp"
#include "../render/scene.hpp"
#include "../render/text.hpp"
#include "../render/text_field.hpp"
#include "../render/texture.hpp"
#include "../render/texture_cache.hpp"
#include "../wayland/keyboard.hpp"
#include "../wayland/layer_surface.hpp"
#include "settings_config.hpp"
#include "wallpaper_picker.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

enum class SettingsTab { Wallpaper, Idle };
constexpr int kSettingsTabCount = 2;

enum class SettingsFieldId {
    None,
    WallpaperPath,
    WallpaperDir,
    IdleTimeout,
    IdleCommand,
    IdleResumeCommand,
};

// Applies a fully-formed new Config: persist it and react to whatever
// actually changed (bar geometry, wallpaper reload, idle re-init). Kept as a
// caller-supplied callback rather than a direct call so this module doesn't
// need to know about WaylandState/MonitorOutput - see
// local/plan/settings-panel.md.
using SettingsCommitFn = std::function<void(Config)>;

struct SettingsState {
    OverlayPanelBase base;
    Renderer *renderer = nullptr;
    Scene scene;
    TextureCache tcache;

    Rect panel_rect;
    std::vector<PanelClickRegion> click_regions;

    SettingsTab active_tab = SettingsTab::Wallpaper;
    SettingsFieldId focused_field = SettingsFieldId::None;
    TextFieldState field_buffer;

    WallpaperPickerState wallpaper_picker;
    std::string wallpaper_selected_monitor;
    float wallpaper_scroll_offset = 0.0f;
    // Content width/height of the grid as last drawn; settings_handle_scroll()
    // uses these to clamp without redoing settings_paint()'s panel layout math.
    float wallpaper_grid_width = 0.0f;
    float wallpaper_grid_height = 0.0f;
    // Filled by settings_paint() each frame; settings_handle_click() reads it
    // back to decide whether the monitor row is dead UI for a single output.
    std::vector<std::string> monitor_names;
};

namespace settings_detail {

inline std::string format_field(const Config &cfg, SettingsFieldId id) {
    switch (id) {
    case SettingsFieldId::WallpaperPath:
        return cfg.wallpaper_path;
    case SettingsFieldId::WallpaperDir:
        return cfg.wallpaper_dir;
    case SettingsFieldId::IdleTimeout:
        return std::to_string(cfg.idle_timeout_seconds);
    case SettingsFieldId::IdleCommand:
        return cfg.idle_command;
    case SettingsFieldId::IdleResumeCommand:
        return cfg.idle_resume_command;
    default:
        return "";
    }
}

// Parses the field's edit buffer into cfg. Keeps the previous value on a
// parse failure (an unparsable numeric field commits as a no-op, not a
// crash or a silent zero).
inline void apply_field_text(Config &cfg, SettingsFieldId id,
                             const std::string &text) {
    try {
        switch (id) {
        case SettingsFieldId::WallpaperPath:
            cfg.wallpaper_path = text;
            break;
        case SettingsFieldId::WallpaperDir:
            cfg.wallpaper_dir = text;
            break;
        case SettingsFieldId::IdleTimeout:
            cfg.idle_timeout_seconds =
                static_cast<uint32_t>(std::max(0, std::stoi(text)));
            break;
        case SettingsFieldId::IdleCommand:
            cfg.idle_command = text;
            break;
        case SettingsFieldId::IdleResumeCommand:
            cfg.idle_resume_command = text;
            break;
        default:
            break;
        }
    } catch (const std::exception &) {
        klog("settings: could not parse '%s' for field %d, keeping previous "
             "value",
             text.c_str(), static_cast<int>(id));
    }
}

} // namespace settings_detail

inline void settings_paint(SettingsState &state, const Config &cfg,
                           const std::vector<std::string> &monitor_names);

inline bool settings_create_surface(SettingsState &state,
                                    wl_compositor *compositor,
                                    zwlr_layer_shell_v1 *layer_shell,
                                    wl_output *output = nullptr) {
    return overlay_panel_create_surface(state.base, compositor, layer_shell,
                                        "kokusei-settings", output);
}

// monitor_names_fn is a callback rather than a stored vector so this module
// stays decoupled from WaylandState/MonitorOutput (see SettingsCommitFn's
// comment above) while still reflecting the live output list each frame.
inline bool
settings_init_egl(SettingsState &state, const Config &cfg, Renderer &renderer,
                  EGLDisplay display, EGLConfig config, EGLContext context,
                  std::function<std::vector<std::string>()> monitor_names_fn) {
    state.renderer = &renderer;
    if (!overlay_panel_init_egl(state.base, display, config, context))
        return false;
    state.base.frame_clock.draw = [&state, &cfg, monitor_names_fn] {
        settings_paint(state, cfg, monitor_names_fn());
    };
    return true;
}

inline void settings_request_frame(SettingsState &state) {
    overlay_panel_request_frame(state.base);
}

inline void settings_commit_focused_field(SettingsState &state,
                                          const Config &cfg,
                                          const SettingsCommitFn &on_commit) {
    if (state.focused_field == SettingsFieldId::None)
        return;
    Config updated = cfg;
    settings_detail::apply_field_text(updated, state.focused_field,
                                      state.field_buffer.text);
    on_commit(updated);
    if (state.focused_field == SettingsFieldId::WallpaperDir)
        wallpaper_picker_scan(state.wallpaper_picker, updated.wallpaper_dir);
    state.focused_field = SettingsFieldId::None;
}

inline void settings_toggle(SettingsState &state, const Config &cfg,
                            const SettingsCommitFn &on_commit) {
    if (!state.base.layer_surface || state.base.egl_surface == EGL_NO_SURFACE) {
        klog("settings: toggle ignored, surface not ready (layer_surface=%p "
             "egl_surface_ready=%d)",
             static_cast<void *>(state.base.layer_surface),
             state.base.egl_surface != EGL_NO_SURFACE);
        return;
    }
    klog("settings: toggle called (was_open=%d opacity=%.2f "
         "focused_field=%d)",
         state.base.open, static_cast<double>(state.base.opacity),
         static_cast<int>(state.focused_field));
    if (state.base.open) {
        settings_commit_focused_field(state, cfg, on_commit);
    }
    overlay_panel_toggle(state.base);
    settings_request_frame(state);
}

inline void settings_focus_field(SettingsState &state, const Config &cfg,
                                 const SettingsCommitFn &on_commit,
                                 SettingsFieldId id) {
    if (id == state.focused_field)
        return;
    settings_commit_focused_field(state, cfg, on_commit);
    state.focused_field = id;
    state.field_buffer.text = settings_detail::format_field(cfg, id);
    state.field_buffer.cursor_blink_visible = true;
}

inline void settings_handle_click(SettingsState &state, const Config &cfg,
                                  const SettingsCommitFn &on_commit, double px,
                                  double py) {
    auto hit = [](const Rect &r, double x, double y) {
        return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y &&
               y < r.y + r.h;
    };

    klog("settings: handle_click at (%.0f,%.0f), panel_rect=%.0f,%.0f "
         "%.0fx%.0f",
         px, py, static_cast<double>(state.panel_rect.x),
         static_cast<double>(state.panel_rect.y),
         static_cast<double>(state.panel_rect.w),
         static_cast<double>(state.panel_rect.h));
    for (const PanelClickRegion &region : state.click_regions) {
        if (!hit(region.rect, px, py))
            continue;
        klog("settings: click (%.0f,%.0f) hit region kind=%d",
             px, py, static_cast<int>(region.kind));
        switch (region.kind) {
        case PanelClickKind::Close:
            settings_toggle(state, cfg, on_commit);
            return;
        case PanelClickKind::TabSelect:
            settings_commit_focused_field(state, cfg, on_commit);
            state.active_tab = static_cast<SettingsTab>(std::stoi(region.tag));
            if (state.active_tab == SettingsTab::Wallpaper &&
                state.wallpaper_picker.dir != cfg.wallpaper_dir)
                wallpaper_picker_scan(state.wallpaper_picker,
                                      cfg.wallpaper_dir);
            settings_request_frame(state);
            return;
        case PanelClickKind::ToggleFlip: {
            settings_commit_focused_field(state, cfg, on_commit);
            if (region.tag == "fillmode") {
                Config updated = cfg;
                std::string cur = wallpaper_effective_fill_mode(
                    cfg, state.wallpaper_selected_monitor);
                updated.wallpaper_fill_modes[state.wallpaper_selected_monitor] =
                    cur == "crop" ? "fit" : "crop";
                on_commit(updated);
            } else if (region.tag == "wallpaperremove") {
                Config updated = cfg;
                updated.wallpaper_paths.erase(state.wallpaper_selected_monitor);
                on_commit(updated);
            } else if (region.tag == "wallpaperrescan") {
                wallpaper_picker_scan(state.wallpaper_picker, cfg.wallpaper_dir);
            }
            settings_request_frame(state);
            return;
        }
        case PanelClickKind::FieldFocus:
            settings_focus_field(
                state, cfg, on_commit,
                static_cast<SettingsFieldId>(std::stoi(region.tag)));
            settings_request_frame(state);
            return;
        case PanelClickKind::MonitorSelect:
            state.wallpaper_selected_monitor = region.tag;
            settings_request_frame(state);
            return;
        case PanelClickKind::WallpaperSelect: {
            settings_commit_focused_field(state, cfg, on_commit);
            Config updated = cfg;
            updated.wallpaper_paths[state.wallpaper_selected_monitor] =
                region.tag;
            on_commit(updated);
            settings_request_frame(state);
            return;
        }
        default:
            return;
        }
    }

    klog("settings: click (%.0f,%.0f) hit no region (%zu checked, "
         "panel_rect=%.0f,%.0f %.0fx%.0f)",
         px, py, state.click_regions.size(),
         static_cast<double>(state.panel_rect.x),
         static_cast<double>(state.panel_rect.y),
         static_cast<double>(state.panel_rect.w),
         static_cast<double>(state.panel_rect.h));
    if (!hit(state.panel_rect, px, py))
        settings_toggle(state, cfg, on_commit);
}

inline void settings_handle_key_event(SettingsState &state, const Config &cfg,
                                      const SettingsCommitFn &on_commit,
                                      const KeyEvent &event) {
    if (state.focused_field == SettingsFieldId::None) {
        if (event.kind == KeyKind::Escape) {
            klog("settings: escape with no field focused -> toggle close");
            settings_toggle(state, cfg, on_commit);
        }
        return;
    }
    switch (text_field_handle_key(state.field_buffer, event)) {
    case TextFieldResult::Changed:
        settings_request_frame(state);
        break;
    case TextFieldResult::Committed:
        klog("settings: field %d committed",
             static_cast<int>(state.focused_field));
        settings_commit_focused_field(state, cfg, on_commit);
        settings_request_frame(state);
        break;
    case TextFieldResult::Cancelled:
        klog("settings: field %d edit cancelled",
             static_cast<int>(state.focused_field));
        state.focused_field = SettingsFieldId::None;
        settings_request_frame(state);
        break;
    case TextFieldResult::None:
        break;
    }
}

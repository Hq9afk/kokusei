#pragma once

#include "app/config.h"
#include "render/overlay_panel.h"
#include "render/panel_chrome.h"
#include "render/rect.h"
#include "render/renderer.h"
#include "render/scene.h"
#include "render/text_field.h"
#include "render/texture_cache.h"
#include "wayland/keyboard.h"
#include "settings/settings_config.h"
#include "settings/wallpaper_picker.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <functional>
#include <string>
#include <vector>

enum class SettingsTab { Wallpaper, Displays, Idle };
constexpr int kSettingsTabCount = 3;

enum class SettingsFieldId {
    None,
    WallpaperPath,
    WallpaperDir,
    IdleTimeout,
    IdleCommand,
    IdleResumeCommand,
};

inline constexpr const char *kSettingsDisplaysDefaultTag = "__default__";

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

    int wallpaper_selected_column = 0;
    float wallpaper_scroll_offset = 0.0f;

    float wallpaper_grid_width = 0.0f;
    float wallpaper_grid_height = 0.0f;

    std::vector<std::string> monitor_names;

    std::string displays_selected_monitor;
};

// Shared with the tab paint functions (idle_tab.cpp's draw_field_row and
// wallpaper_tab.cpp's draw_wallpaper_dirbar also read the live field text).
std::string settings_detail_format_field(const Config &cfg, SettingsFieldId id);

bool settings_create_surface(SettingsState &state, wl_compositor *compositor,
                             zwlr_layer_shell_v1 *layer_shell,
                             wl_output *output = nullptr);

bool settings_init_egl(SettingsState &state, const Config &cfg,
                       Renderer &renderer, EGLDisplay display,
                       EGLConfig config, EGLContext context,
                       std::function<std::vector<std::string>()> monitor_names_fn);

void settings_request_frame(SettingsState &state);

void settings_commit_focused_field(SettingsState &state, const Config &cfg,
                                   const SettingsCommitFn &on_commit);

void settings_toggle(SettingsState &state, const Config &cfg,
                     const SettingsCommitFn &on_commit);

void settings_focus_field(SettingsState &state, const Config &cfg,
                          const SettingsCommitFn &on_commit,
                          SettingsFieldId id);

void settings_handle_click(SettingsState &state, const Config &cfg,
                           const SettingsCommitFn &on_commit, double px,
                           double py);

void settings_handle_key_event(SettingsState &state, const Config &cfg,
                               const SettingsCommitFn &on_commit,
                               const KeyEvent &event);

void settings_paint(SettingsState &state, const Config &cfg,
                    const std::vector<std::string> &monitor_names);

void settings_handle_scroll(SettingsState &state, double dy);

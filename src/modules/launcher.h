#pragma once

#include "config/launcher_config.h"
#include "core/async_process.h"
#include "render/animation.h"
#include "render/overlay_panel.h"
#include "render/rect.h"
#include "render/renderer.h"
#include "render/scene.h"
#include "render/text.h"
#include "render/texture.h"
#include "render/texture_cache.h"
#include "service/frame_clock.h"
#include "service/keyboard.h"
#include "service/output_scale.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <chrono>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

struct LauncherState {
    wl_surface *surface = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_egl_window *egl_window = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLDisplay egl_display = nullptr;
    EGLContext egl_context = nullptr;
    Renderer *renderer = nullptr;
    OutputScale output_scale;
    FrameClock frame_clock;
    Scene scene;
    TextureCache tcache;
    bool configured = false;
    bool open = false;
    AnimationManager animations;
    float opacity = 0.0f;
    float anim_height = 0.0f;
    float anim_height_target = -1.0f;

    float highlight_offset = 0.0f;
    float highlight_offset_target = -1.0f;

    float scroll_offset = 0.0f;
    float scroll_offset_target = -1.0f;
    std::string query;
    std::deque<QueryCharAnim> query_char_anim;
    bool cursor_blink_visible = true;
    LauncherMode mode = LauncherMode::Drun;
    std::string effective_query;
    std::string search_root;
    std::vector<DesktopEntry> apps;
    std::vector<DrunResult> results;
    int selected_index = -1;
    bool search_dirty = false;
    std::chrono::steady_clock::time_point search_dirty_at{};
    AsyncProcess search_dirs_proc, search_files_proc;
    bool search_running = false;
    std::chrono::steady_clock::time_point search_started_at{};
    std::string search_query;
    bool awaiting_restart = false;
    pid_t pending_kill_dirs = -1, pending_kill_files = -1;
    std::chrono::steady_clock::time_point pending_kill_since{};
    std::string pending_restart_query;
    SubmenuState submenu;
    VisitStore visits;
    Texture bullet_tex[kLauncherMaxVisible];
    std::unordered_map<std::string, Texture> app_icon_cache;

    wl_compositor *compositor = nullptr;
    int32_t width = 0, height = 0;
    Rect box_rect{};
    wl_output *bound_output = nullptr;
};

bool launcher_create_surface(LauncherState &state, wl_compositor *compositor,
                             zwlr_layer_shell_v1 *layer_shell,
                             wl_output *output = nullptr);

bool launcher_init_egl(LauncherState &state, Renderer &renderer,
                       EGLDisplay display, EGLConfig config,
                       EGLContext context);

void launcher_retarget(LauncherState &state, wl_compositor *compositor,
                       zwlr_layer_shell_v1 *layer_shell, wl_display *display,
                       Renderer &renderer, EGLDisplay egl_display,
                       EGLConfig egl_config, EGLContext egl_context,
                       wl_output *target_output, const char *target_name);

void launcher_request_frame(LauncherState &state);

void launcher_search_start_pending(LauncherState &state);

bool launcher_search_poll(LauncherState &state);

// Shared with the paint function below (visible_rows() resolves each app row's
// icon).
const Texture *launcher_icon_lookup(LauncherState &state, const std::string &id,
                                    const std::string &icon_field);

int launcher_poll_timeout_ms(const LauncherState &state);

bool launcher_tick(LauncherState &state);

void launcher_toggle(LauncherState &state, bool global);

void launcher_handle_key_event(LauncherState &state, const KeyEvent &event);

void launcher_handle_click(LauncherState &state, double px, double py);

void launcher_paint(LauncherState &state);

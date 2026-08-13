#pragma once

#include "../core/async_process.h"
#include "../render/animation.h"
#include "../render/overlay_panel.h"
#include "../render/rect.h"
#include "../render/renderer.h"
#include "../render/scene.h"
#include "../render/text.h"
#include "../render/texture.h"
#include "../render/texture_cache.h"
#include "../wayland/frame_clock.h"
#include "../wayland/keyboard.h"
#include "../wayland/output_scale.h"
#include "launcher_config.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <chrono>
#include <deque>
#include <istream>
#include <optional>
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

// Shared with the paint function below (visible_rows() resolves each app row's icon).
const Texture *launcher_icon_lookup(LauncherState &state, const std::string &id,
                                    const std::string &icon_field);

int launcher_poll_timeout_ms(const LauncherState &state);

bool launcher_tick(LauncherState &state);

void launcher_toggle(LauncherState &state, bool global);

void launcher_handle_key_event(LauncherState &state, const KeyEvent &event);

void launcher_handle_click(LauncherState &state, double px, double py);

void launcher_paint(LauncherState &state);

// -- apps_provider --

std::string to_lower(const std::string &s);

float score_app(const std::string &name, const std::string &query);

std::vector<ScoredApp> search_apps(const std::vector<DesktopEntry> &entries,
                                   const std::string &query);

// -- desktop_entry --

namespace desktop_entry_detail {

std::optional<DesktopEntry> parse_stream(std::istream &in,
                                         const std::string &id);

}

std::optional<DesktopEntry> parse_desktop_entry(const std::string &path,
                                                const std::string &id);

std::string strip_exec_field_codes(const std::string &exec);

std::vector<std::string> desktop_entry_search_dirs();

std::vector<DesktopEntry> scan_desktop_entries();

void desktop_entry_launch(const DesktopEntry &entry);

// -- files_provider --

std::string basename_of(const std::string &path);

std::string dirname_of(const std::string &path);

std::string to_glob_pattern(const std::string &query);

std::vector<std::string> split_query_parts(const std::string &query);

float score_path(const std::string &name, const std::string &query);

std::vector<std::string> fd_search_argv(const std::string &pattern,
                                        const std::string &search_root,
                                        bool is_dir, int max_results,
                                        int depth = -1, bool full_path = true);

std::vector<FileEntry> fd_search_parse_output(const std::string &raw,
                                              bool is_dir);

std::vector<FileEntry> run_fd_search(const std::string &pattern,
                                     const std::string &search_root,
                                     bool is_dir, int max_results,
                                     int depth = -1, bool full_path = true);

// -- icon_theme --

std::string icon_direct_path(const std::string &icon_field);

std::string resolve_app_icon_path(const std::string &icon_field);

// -- launch_action --

namespace launch_action_detail {

std::string shell_quote(const std::string &s);

}

std::string make_search_url(const std::string &text, const std::string &base);

std::string normalize_url(const std::string &text);

std::string resolve_web_target(const std::string &text, const std::string &base);

bool launch_non_drun(LauncherMode mode, const std::string &query);

void launch_submenu_action(const SubmenuEntry &entry, VisitStore &visits);

void launch_drun_app(const DesktopEntry &entry, VisitStore &visits);

// -- search --

ModeQuery detect_mode_and_query(const std::string &raw);

std::vector<DrunResult>
combined_drun_results(const std::vector<ScoredApp> &apps,
                      const std::vector<FileEntry> &files,
                      const VisitStore &visits, int max_results);

// -- spawn --

void spawn_detached(const std::string &shell_command);

// -- submenu --

void submenu_close(SubmenuState &s);

void submenu_open_directory(SubmenuState &s, const std::string &path,
                            const DirLister &list_dir);

void submenu_open_directory_actions(SubmenuState &s, const std::string &path);

void submenu_open_file_actions(SubmenuState &s, const std::string &path);

bool submenu_handle_entry(SubmenuState &s, const SubmenuEntry &entry,
                          const DirLister &list_dir);

bool submenu_go_back(SubmenuState &s, const DirLister &list_dir);

// -- visit_store --

std::string visit_store_app_key(const std::string &desktop_id);

std::string visit_store_file_key(const std::string &file_path);

std::string visit_store_default_path();

VisitStore visit_store_load(const std::string &path = {});

int visit_store_get(const VisitStore &vs, const std::string &key);

void visit_store_record(VisitStore &vs, const std::string &key);

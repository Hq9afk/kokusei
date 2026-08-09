#pragma once

#include "../core/async_process.hpp"
#include "../core/log.hpp"
#include "../render/animation/animation.hpp"
#include "../render/icon.hpp"
#include "../render/icons.hpp"
#include "../render/image.hpp"
#include "../render/node.hpp"
#include "../render/overlay_panel.hpp"
#include "../render/palette.hpp"
#include "../render/rect.hpp"
#include "../render/renderer.hpp"
#include "../render/scene.hpp"
#include "../render/text.hpp"
#include "../render/texture.hpp"
#include "../render/texture_cache.hpp"
#include "../wayland/frame_clock.hpp"
#include "../wayland/keyboard.hpp"
#include "../wayland/layer_surface.hpp"
#include "../wayland/output_scale.hpp"
#include "apps_provider.hpp"
#include "desktop_entry.hpp"
#include "files_provider.hpp"
#include "icon_theme.hpp"
#include "launch_action.hpp"
#include "launcher_config.hpp"
#include "search.hpp"
#include "spawn.hpp"
#include "submenu.hpp"
#include "visit_store.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

// One entrance tween's live progress per character currently in `query`,
// same index order as `query`'s codepoints. Pushed on KeyKind::Text, popped
// (after cancelling its owners) on KeyKind::Backspace, see
// launcher_handle_key_event. A std::deque, not a std::vector, so a
// push_back/pop_back never relocates an existing entry an in-flight
// AnimationManager setter still points at by reference.
struct QueryCharAnim {
    float scale = 1.0f;
    float slide_x = 0.0f;
};

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
};

inline void launcher_layer_surface_configure(
    void *data, zwlr_layer_surface_v1 *layer_surface, uint32_t serial,
    uint32_t width, uint32_t height) {
    auto *state = static_cast<LauncherState *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    state->width = static_cast<int32_t>(width);
    state->height = static_cast<int32_t>(height);
    int32_t scale = state->output_scale.scale;
    if (state->egl_window)
        wl_egl_window_resize(state->egl_window, state->width * scale,
                             state->height * scale, 0, 0);
    state->configured = true;
}

inline void launcher_layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

inline constexpr zwlr_layer_surface_v1_listener
    launcher_layer_surface_listener = {
        .configure = launcher_layer_surface_configure,
        .closed = launcher_layer_surface_closed,
};

inline void launcher_update_input_region(LauncherState &state) {
    if (state.open) {
        wl_surface_set_input_region(state.surface, nullptr);
        return;
    }
    wl_region *empty_region = wl_compositor_create_region(state.compositor);
    wl_surface_set_input_region(state.surface, empty_region);
    wl_region_destroy(empty_region);
}

inline bool launcher_create_surface(LauncherState &state,
                                    wl_compositor *compositor,
                                    zwlr_layer_shell_v1 *layer_shell) {
    state.compositor = compositor;
    LayerSurfaceConfig cfg{
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        .name_space = "kokusei-launcher",
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
    };
    state.layer_surface =
        layer_surface_create(state.surface, compositor, layer_shell, cfg,
                             &launcher_layer_surface_listener, &state);
    if (!state.layer_surface)
        return false;
    state.output_scale.on_change = [&state](int32_t scale) {
        if (state.egl_window)
            wl_egl_window_resize(state.egl_window, state.width * scale,
                                 state.height * scale, 0, 0);
        if (state.frame_clock.surface)
            request_frame(state.frame_clock);
    };
    output_scale_watch(state.output_scale, state.surface);
    launcher_update_input_region(state);
    wl_surface_commit(state.surface);

    state.visits = visit_store_load();
    return true;
}

inline void launcher_paint(LauncherState &state);

inline bool launcher_init_egl(LauncherState &state, Renderer &renderer,
                              EGLDisplay display, EGLConfig config,
                              EGLContext context) {
    state.renderer = &renderer;
    state.egl_display = display;
    state.egl_context = context;
    int32_t scale = state.output_scale.scale;
    state.egl_window = wl_egl_window_create(state.surface, state.width * scale,
                                            state.height * scale);
    state.egl_surface = eglCreateWindowSurface(
        display, config,
        reinterpret_cast<EGLNativeWindowType>(state.egl_window), nullptr);
    if (state.egl_surface == EGL_NO_SURFACE)
        return false;
    if (!eglMakeCurrent(display, state.egl_surface, state.egl_surface, context))
        return false;
    for (int i = 0; i < kLauncherMaxVisible; ++i)
        state.bullet_tex[i] = load_image_texture(
            KOKUSEI_BULLET_DIR "/" + std::to_string(i + 1) + ".png");
    state.frame_clock.surface = state.surface;
    state.frame_clock.draw = [&state] { launcher_paint(state); };
    return true;
}

inline void launcher_request_frame(LauncherState &state) {
    if (state.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(state.frame_clock);
}

inline std::vector<FileEntry> launcher_dir_lister(const std::string &path,
                                                  bool want_dirs) {
    return run_fd_search("", path, want_dirs, 50, 1, false);
}

inline void launcher_search_start_now(LauncherState &state) {
    state.search_query = state.effective_query;
    std::string pattern = to_glob_pattern(state.search_query);
    state.search_started_at = std::chrono::steady_clock::now();
    pid_t dirs_pid = async_process_start(
        state.search_dirs_proc,
        fd_search_argv(pattern, state.search_root, true, kLauncherMaxResults));
    pid_t files_pid = async_process_start(
        state.search_files_proc,
        fd_search_argv(pattern, state.search_root, false, kLauncherMaxResults));
    klog("launcher: search_start query='%s' dirs_pid=%d files_pid=%d",
         state.search_query.c_str(), dirs_pid, files_pid);
    state.search_running = true;
}

inline void launcher_search_start(LauncherState &state) {
    if (state.awaiting_restart) {
        state.pending_restart_query = state.effective_query;
        return;
    }
    if (state.search_running) {
        auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - state.search_started_at)
                .count();
        klog("launcher: CANCELLING still-running search after %lldms, "
             "deferring restart",
             static_cast<long long>(ms));
        state.pending_kill_dirs = async_process_cancel(state.search_dirs_proc);
        state.pending_kill_files =
            async_process_cancel(state.search_files_proc);
        state.pending_kill_since = std::chrono::steady_clock::now();
        state.pending_restart_query = state.effective_query;
        state.search_running = false;
        state.awaiting_restart = true;
        return;
    }
    launcher_search_start_now(state);
}

inline void launcher_search_start_pending(LauncherState &state) {
    if (!state.awaiting_restart)
        return;
    bool still_alive = async_process_is_alive(state.pending_kill_dirs) ||
                       async_process_is_alive(state.pending_kill_files);
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - state.pending_kill_since)
            .count();
    if (still_alive && elapsed < kLauncherKillGraceMs)
        return;
    klog("launcher: pending restart fired (confirmed_dead=%d) after %lldms "
         "wait",
         !still_alive, static_cast<long long>(elapsed));
    state.awaiting_restart = false;
    state.pending_kill_dirs = -1;
    state.pending_kill_files = -1;
    state.effective_query = state.pending_restart_query;
    launcher_search_start_now(state);
}

inline bool launcher_search_poll(LauncherState &state) {
    if (!state.search_running)
        return false;
    bool dirs_done = async_process_poll(state.search_dirs_proc);
    bool files_done = async_process_poll(state.search_files_proc);
    if (!dirs_done || !files_done)
        return false;

    state.search_running = false;
    std::vector<ScoredApp> apps = search_apps(state.apps, state.search_query);
    std::vector<FileEntry> files;
    for (bool want_dirs : {true, false}) {
        const std::string &raw = want_dirs ? state.search_dirs_proc.buffer
                                           : state.search_files_proc.buffer;
        for (FileEntry &fe : fd_search_parse_output(raw, want_dirs)) {
            fe.score = score_path(fe.name, state.search_query);
            if (fe.score >= 0.0f)
                files.push_back(std::move(fe));
        }
    }
    state.results =
        combined_drun_results(apps, files, state.visits, kLauncherMaxResults);
    state.selected_index = state.results.empty() ? -1 : 0;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - state.search_started_at)
                  .count();
    klog("launcher: search_poll DONE after %lldms query='%s' results=%zu",
         static_cast<long long>(ms), state.search_query.c_str(),
         state.results.size());
    return true;
}

inline const Texture *launcher_icon_lookup(LauncherState &state,
                                           const std::string &id,
                                           const std::string &icon_field) {
    auto it = state.app_icon_cache.find(id);
    if (it == state.app_icon_cache.end()) {
        std::string path = resolve_app_icon_path(icon_field);
        it = state.app_icon_cache
                 .emplace(id,
                          path.empty() ? Texture{} : load_image_texture(path))
                 .first;
    }
    return it->second.id ? &it->second : nullptr;
}

inline void launcher_query_changed(LauncherState &state) {
    ModeQuery mq = detect_mode_and_query(state.query);
    state.mode = mq.mode;
    state.effective_query = mq.query;
    state.search_dirty = true;
    state.search_dirty_at = std::chrono::steady_clock::now();
    state.cursor_blink_visible = true;
}

// Ported from keqing-shell's PasswordInput.qml: a new character starts
// shrunk/offset and eases in (spring scale-pop, slide), matching that
// component's per-dot NumberAnimations. Unlike PasswordInput.qml this skips
// an opacity tween: the launcher surface itself already fades in as a whole
// (kOverlayFadeMs, see launcher_toggle), and a character typed during that
// window would have its own 0->1 opacity multiplied on top of the
// surface's still-ramping opacity (Renderer::set_opacity multiplies every
// node's alpha), double-blending it against the backdrop and making it
// settle on a visibly different color than a character typed after the
// surface has finished fading in. Scale and slide are pure geometry, so
// they don't have this compounding problem and every character (including
// the first) goes through the identical path here.
inline void launcher_query_char_push(LauncherState &state) {
    size_t idx = state.query_char_anim.size();
    state.query_char_anim.push_back({0.0f, kLauncherQuerySlideOffsetPx});
    state.animations.animate(
        0.0f, 1.0f, kLauncherQueryScaleMs, Easing::EaseOutBack,
        [&state, idx](float v) {
            if (idx < state.query_char_anim.size())
                state.query_char_anim[idx].scale = v;
        },
        {}, launcher_query_char_owner(idx, QueryCharProp::Scale));
    state.animations.animate(
        kLauncherQuerySlideOffsetPx, 0.0f, kLauncherQuerySlideMs,
        Easing::Linear,
        [&state, idx](float v) {
            if (idx < state.query_char_anim.size())
                state.query_char_anim[idx].slide_x = v;
        },
        {}, launcher_query_char_owner(idx, QueryCharProp::Slide));
}

// Matches PasswordInput.qml's instant dotModel.remove(), no exit animation.
// Cancels the removed character's own tweens first, a still-running setter
// must never be left pointing past the deque's new end.
inline void launcher_query_char_pop(LauncherState &state) {
    if (state.query_char_anim.empty())
        return;
    size_t idx = state.query_char_anim.size() - 1;
    state.animations.cancelForOwner(
        launcher_query_char_owner(idx, QueryCharProp::Scale));
    state.animations.cancelForOwner(
        launcher_query_char_owner(idx, QueryCharProp::Slide));
    state.query_char_anim.pop_back();
}

inline void launcher_query_char_clear(LauncherState &state) {
    while (!state.query_char_anim.empty())
        launcher_query_char_pop(state);
}

inline int launcher_poll_timeout_ms(const LauncherState &state) {
    int timeout_ms = -1;
    if (state.search_dirty) {
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - state.search_dirty_at)
                .count();
        timeout_ms = static_cast<int>(
            std::max<long long>(0, kLauncherSearchDebounceMs - elapsed));
    }
    if (state.awaiting_restart)
        timeout_ms = timeout_ms < 0
                         ? kLauncherKillCheckMs
                         : std::min(timeout_ms, kLauncherKillCheckMs);
    return timeout_ms;
}

inline bool launcher_tick(LauncherState &state) {
    if (!state.search_dirty || launcher_poll_timeout_ms(state) > 0)
        return false;
    state.search_dirty = false;
    if (state.mode == LauncherMode::Drun && !state.effective_query.empty()) {
        launcher_search_start(state);
    } else {
        state.results.clear();
        state.selected_index = -1;
    }
    return true;
}

inline void launcher_toggle(LauncherState &state, bool global) {
    if (!state.layer_surface || state.egl_surface == EGL_NO_SURFACE)
        return;

    if (state.open) {
        klog("launcher: CLOSE (was_search_running=%d dirs_pid=%d "
             "files_pid=%d)",
             state.search_running, async_process_pid(state.search_dirs_proc),
             async_process_pid(state.search_files_proc));
        state.search_dirty = false;
        async_process_cancel(state.search_dirs_proc);
        async_process_cancel(state.search_files_proc);
        state.search_running = false;
        state.awaiting_restart = false;
        state.pending_kill_dirs = state.pending_kill_files = -1;

        state.animations.animate(
            state.opacity, 0.0f, kOverlayFadeMs, Easing::EaseOutCubic,
            [&state](float v) { state.opacity = v; },
            [&state] {
                state.open = false;
                state.query.clear();
                launcher_query_char_clear(state);
                state.effective_query.clear();
                state.mode = LauncherMode::Drun;
                state.results.clear();
                state.selected_index = -1;
                state.anim_height_target = -1.0f;
                state.highlight_offset_target = -1.0f;
                state.scroll_offset_target = -1.0f;
                submenu_close(state.submenu);
                zwlr_layer_surface_v1_set_keyboard_interactivity(
                    state.layer_surface,
                    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
                launcher_update_input_region(state);
                wl_surface_commit(state.surface);
            },
            kOverlayFadeOwner);
        launcher_request_frame(state);
        return;
    }

    klog("launcher: OPEN (global=%d)", global);
    const char *home = getenv("HOME");
    state.search_root = global ? "/" : (home ? home : "/");
    state.apps = scan_desktop_entries();
    state.open = true;
    state.cursor_blink_visible = true;
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        state.layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    launcher_update_input_region(state);
    wl_surface_commit(state.surface);
    state.animations.animate(
        state.opacity, 1.0f, kOverlayFadeMs, Easing::EaseOutCubic,
        [&state](float v) { state.opacity = v; }, {}, kOverlayFadeOwner);
    launcher_request_frame(state);
}

inline void launcher_launch_selected(LauncherState &state) {
    if (state.submenu.screen != SubmenuScreen::Search) {
        if (state.selected_index < 0 ||
            state.selected_index >=
                static_cast<int>(state.submenu.items.size()))
            return;
        SubmenuEntry entry = state.submenu.items[state.selected_index];
        if (submenu_handle_entry(state.submenu, entry, launcher_dir_lister)) {

            state.selected_index = 0;
            return;
        }
        launch_submenu_action(entry, state.visits);
        launcher_toggle(state, false);
        return;
    }

    if (state.mode != LauncherMode::Drun) {
        launch_non_drun(state.mode, state.effective_query);
        launcher_toggle(state, false);
        return;
    }

    if (state.selected_index < 0 ||
        state.selected_index >= static_cast<int>(state.results.size()))
        return;
    const DrunResult &r = state.results[state.selected_index];
    switch (r.kind) {
    case DrunResult::Kind::App:
        launch_drun_app(*r.app, state.visits);
        launcher_toggle(state, false);
        break;
    case DrunResult::Kind::Dir:
        submenu_open_directory(state.submenu, r.file.path, launcher_dir_lister);
        state.selected_index = 0;
        break;
    case DrunResult::Kind::File:
        submenu_open_file_actions(state.submenu, r.file.path);
        state.selected_index = 0;
        break;
    }
}

inline void launcher_handle_key_event(LauncherState &state,
                                      const KeyEvent &event) {
    switch (event.kind) {
    case KeyKind::Text:

        if (state.submenu.screen != SubmenuScreen::Search)
            submenu_close(state.submenu);
        state.query += event.text;
        launcher_query_char_push(state);
        launcher_query_changed(state);
        break;

    case KeyKind::Backspace: {
        if (state.submenu.screen != SubmenuScreen::Search)
            submenu_close(state.submenu);

        while (!state.query.empty() &&
               (static_cast<unsigned char>(state.query.back()) & 0xC0) == 0x80)
            state.query.pop_back();
        if (!state.query.empty())
            state.query.pop_back();
        launcher_query_char_pop(state);
        launcher_query_changed(state);
        break;
    }

    case KeyKind::Up:
    case KeyKind::Down: {
        int count = state.submenu.screen == SubmenuScreen::Search
                        ? static_cast<int>(state.results.size())
                        : static_cast<int>(state.submenu.items.size());
        if (count == 0) {
            state.selected_index = -1;
            break;
        }
        int delta = event.kind == KeyKind::Down ? 1 : -1;
        state.selected_index =
            std::clamp(state.selected_index + delta, 0, count - 1);
        break;
    }

    case KeyKind::Escape:
        if (state.submenu.screen != SubmenuScreen::Search) {
            submenu_go_back(state.submenu, launcher_dir_lister);
            if (state.submenu.screen == SubmenuScreen::Search)
                state.selected_index = state.results.empty() ? -1 : 0;
            else
                state.selected_index = state.submenu.items.empty() ? -1 : 0;
        } else {
            launcher_toggle(state, false);
        }
        break;

    case KeyKind::Enter:
        launcher_launch_selected(state);
        break;

    case KeyKind::Tab:
    case KeyKind::Left:
    case KeyKind::Right:
        break;
    }
}

inline void launcher_handle_click(LauncherState &state, double px, double py) {
    const Rect &r = state.box_rect;
    bool inside = px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    if (!inside)
        launcher_toggle(state, false);
}

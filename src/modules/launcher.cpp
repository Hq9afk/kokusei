#include "modules/launcher.h"

#include "core/log.h"
#include "launcher/apps_provider.h"
#include "launcher/desktop_entry.h"
#include "launcher/files_provider.h"
#include "launcher/launch_action.h"
#include "launcher/search.h"
#include "launcher/submenu.h"
#include "launcher/visit_store.h"
#include "render/icon.h"
#include "render/icons.h"
#include "render/image.h"
#include "render/node.h"
#include "render/palette.h"
#include "render/text_field.h"
#include "service/icon_theme.h"
#include "service/layer_surface.h"

#include <GLES2/gl2.h>

#include <algorithm>
#include <cstdlib>

// -- launcher --

namespace {

void launcher_layer_surface_configure(void *data,
                                      zwlr_layer_surface_v1 *layer_surface,
                                      uint32_t serial, uint32_t width,
                                      uint32_t height) {
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

void launcher_layer_surface_closed(void *, zwlr_layer_surface_v1 *) {}

constexpr zwlr_layer_surface_v1_listener launcher_layer_surface_listener = {
    .configure = launcher_layer_surface_configure,
    .closed = launcher_layer_surface_closed,
};

void launcher_update_input_region(LauncherState &state) {
    if (state.open) {
        wl_surface_set_input_region(state.surface, nullptr);
        return;
    }
    wl_region *empty_region = wl_compositor_create_region(state.compositor);
    wl_surface_set_input_region(state.surface, empty_region);
    wl_region_destroy(empty_region);
}

std::vector<FileEntry> launcher_dir_lister(const std::string &path,
                                           bool want_dirs) {
    return run_fd_search("", path, want_dirs, 50, 1, false);
}

void launcher_search_start_now(LauncherState &state) {
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

void launcher_search_start(LauncherState &state) {
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

void launcher_query_changed(LauncherState &state) {
    ModeQuery mq = detect_mode_and_query(state.query);
    state.mode = mq.mode;
    state.effective_query = mq.query;
    state.search_dirty = true;
    state.search_dirty_at = std::chrono::steady_clock::now();
    state.cursor_blink_visible = true;
}

void launcher_query_char_push(LauncherState &state) {
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

void launcher_query_char_pop(LauncherState &state) {
    if (state.query_char_anim.empty())
        return;
    size_t idx = state.query_char_anim.size() - 1;
    state.animations.cancelForOwner(
        launcher_query_char_owner(idx, QueryCharProp::Scale));
    state.animations.cancelForOwner(
        launcher_query_char_owner(idx, QueryCharProp::Slide));
    state.query_char_anim.pop_back();
}

void launcher_query_char_clear(LauncherState &state) {
    while (!state.query_char_anim.empty())
        launcher_query_char_pop(state);
}

void launcher_launch_selected(LauncherState &state) {
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

} // namespace

bool launcher_create_surface(LauncherState &state, wl_compositor *compositor,
                             zwlr_layer_shell_v1 *layer_shell,
                             wl_output *output) {
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
                             &launcher_layer_surface_listener, &state, output);
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

bool launcher_init_egl(LauncherState &state, Renderer &renderer,
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

void launcher_request_frame(LauncherState &state) {
    if (state.egl_surface == EGL_NO_SURFACE)
        return;
    request_frame(state.frame_clock);
}

// LauncherState predates OverlayPanelBase and keeps its own surface/EGL
// fields directly, so this mirrors overlay_panel_retarget's skeleton by hand
// instead of reusing the template.
void launcher_retarget(LauncherState &state, wl_compositor *compositor,
                       zwlr_layer_shell_v1 *layer_shell, wl_display *display,
                       Renderer &renderer, EGLDisplay egl_display,
                       EGLConfig egl_config, EGLContext egl_context,
                       wl_output *target_output, const char *target_name) {
    wl_output *previous_output = state.bound_output;
    klog("panel: launcher retargeting from output=%p to '%s'",
         static_cast<void *>(previous_output), target_name);

    if (state.egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(egl_display, state.egl_surface);
        state.egl_surface = EGL_NO_SURFACE;
    }
    if (state.egl_window) {
        wl_egl_window_destroy(state.egl_window);
        state.egl_window = nullptr;
    }
    if (state.layer_surface) {
        zwlr_layer_surface_v1_destroy(state.layer_surface);
        state.layer_surface = nullptr;
    }
    if (state.surface) {
        wl_surface_destroy(state.surface);
        state.surface = nullptr;
    }
    state.configured = false;
    state.open = false;
    state.opacity = 0.0f;

    auto bind_to = [&](wl_output *out) -> bool {
        if (!launcher_create_surface(state, compositor, layer_shell, out))
            return false;
        while (!state.configured)
            wl_display_dispatch(display);
        return launcher_init_egl(state, renderer, egl_display, egl_config,
                                 egl_context);
    };

    if (bind_to(target_output)) {
        state.bound_output = target_output;
        return;
    }
    if (previous_output && bind_to(previous_output)) {
        state.bound_output = previous_output;
        return;
    }
    klog("panel: launcher retarget fallback also failed");
}

void launcher_search_start_pending(LauncherState &state) {
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

bool launcher_search_poll(LauncherState &state) {
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

const Texture *launcher_icon_lookup(LauncherState &state, const std::string &id,
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

int launcher_poll_timeout_ms(const LauncherState &state) {
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

bool launcher_tick(LauncherState &state) {
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

void launcher_toggle(LauncherState &state, bool global) {
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

void launcher_handle_key_event(LauncherState &state, const KeyEvent &event) {
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

        text_field_backspace(state.query);
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

void launcher_handle_click(LauncherState &state, double px, double py) {
    const Rect &r = state.box_rect;
    bool inside = px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    if (!inside)
        launcher_toggle(state, false);
}

namespace {

size_t utf8_char_len(unsigned char lead) {
    if ((lead & 0x80) == 0x00)
        return 1;
    if ((lead & 0xE0) == 0xC0)
        return 2;
    if ((lead & 0xF0) == 0xE0)
        return 3;
    if ((lead & 0xF8) == 0xF0)
        return 4;
    return 1;
}

std::string elide(const std::string &s) {
    if (s.size() <= launcher_detail::kMaxRowChars)
        return s;
    size_t cut = launcher_detail::kMaxRowChars - 1;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
        --cut;
    return s.substr(0, cut) + "…";
}

std::string home_relative(const std::string &path) {
    const char *home = getenv("HOME");
    if (home && *home) {
        std::string prefix(home);
        if (path.compare(0, prefix.size(), prefix) == 0) {
            if (path.size() == prefix.size())
                return "~";
            if (path[prefix.size()] == '/')
                return "~" + path.substr(prefix.size());
        }
    }
    return path;
}

const char *mode_icon(LauncherMode mode) {
    switch (mode) {
    case LauncherMode::Run:
        return icon::terminal;
    case LauncherMode::Google:
        return icon::brand_google;
    case LauncherMode::YouTube:
        return icon::brand_youtube;
    case LauncherMode::DuckDuckGo:
    case LauncherMode::Url:
        return icon::link;
    case LauncherMode::Drun:
    default:
        return icon::apps;
    }
}

struct Row {
    const char *icon;
    std::string label;
    std::string subtitle;
    const Texture *icon_tex = nullptr;
};

std::vector<Row> visible_rows(LauncherState &state, int &first) {
    std::vector<Row> rows;
    if (state.submenu.screen == SubmenuScreen::Search) {
        for (const DrunResult &r : state.results) {
            switch (r.kind) {
            case DrunResult::Kind::App: {
                Row row{icon::apps, r.app->name, ""};
                row.icon_tex =
                    launcher_icon_lookup(state, r.app->id, r.app->icon);
                rows.push_back(std::move(row));
                break;
            }
            case DrunResult::Kind::Dir:
                rows.push_back({icon::folder, r.file.name,
                                home_relative(dirname_of(r.file.path))});
                break;
            case DrunResult::Kind::File:
                rows.push_back({icon::edit, r.file.name,
                                home_relative(dirname_of(r.file.path))});
                break;
            }
        }
    } else {
        for (const SubmenuEntry &e : state.submenu.items) {
            std::string subtitle = e.action == SubmenuEntry::Action::None
                                       ? home_relative(dirname_of(e.path))
                                       : "";
            const char *row_icon =
                e.icon ? e.icon : (e.is_dir ? icon::folder : icon::edit);
            rows.push_back({row_icon, e.name, subtitle});
        }
    }

    first = 0;
    if (state.selected_index >= kLauncherMaxVisible)
        first = state.selected_index - kLauncherMaxVisible + 1;
    return rows;
}

const Texture *cached_text(TextureCache &cache, const std::string &s,
                           int32_t scale) {
    if (s.empty())
        return nullptr;
    return cache.get("t" + std::to_string(scale) + ":" + s,
                     [&] { return rasterize_text(s, scale); });
}

const Texture *cached_text_small(TextureCache &cache, const std::string &s,
                                 int32_t scale) {
    if (s.empty())
        return nullptr;
    return cache.get("s" + std::to_string(scale) + ":" + s,
                     [&] { return rasterize_text_small(s, scale); });
}

const Texture *cached_icon(TextureCache &cache, const char *codepoint,
                           int32_t scale) {
    return cache.get("i" + std::to_string(scale) + ":" + codepoint,
                     [&] { return rasterize_icon(codepoint, scale); });
}

constexpr float kLauncherListGap =
    kLauncherListTop - kLauncherSearchHeight - kLauncherPad;

int launcher_surface_height(int visible_rows) {
    float h = kLauncherPad * 2.0f + kLauncherSearchHeight;
    if (visible_rows > 0)
        h += kLauncherListGap + visible_rows * kLauncherRowHeight +
             (visible_rows - 1) * kLauncherRowSpacing;
    return static_cast<int>(h);
}

} // namespace

void launcher_paint(LauncherState &state) {
    if (state.egl_surface == EGL_NO_SURFACE)
        return;

    state.animations.tick(std::chrono::steady_clock::now());

    int first = 0;
    std::vector<Row> rows;
    if (state.open)
        rows = visible_rows(state, first);

    int visible_count =
        static_cast<int>(std::min<size_t>(rows.size(), kLauncherMaxVisible));
    int content_h = launcher_surface_height(visible_count);

    if (state.anim_height_target < 0.0f) {
        state.anim_height = static_cast<float>(content_h);
        state.anim_height_target = static_cast<float>(content_h);
    } else if (static_cast<float>(content_h) != state.anim_height_target) {
        state.anim_height_target = static_cast<float>(content_h);
        state.animations.animate(
            state.anim_height, state.anim_height_target, kLauncherHeightAnimMs,
            Easing::EaseInOutCubic,
            [&state](float v) { state.anim_height = v; }, {},
            kLauncherHeightOwner);
    }

    eglMakeCurrent(state.egl_display, state.egl_surface, state.egl_surface,
                   state.egl_context);
    int32_t scale = state.output_scale.scale;
    state.renderer->begin_frame(state.width, state.height, scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    state.scene.rebuild();
    Node *root = &state.scene.root;

    if (state.open) {
        const float *white = rgba(palette::text);
        const float *dim = rgba(palette::text_muted);

        float box_h = state.anim_height;
        float box_x =
            (static_cast<float>(state.width) - kLauncherSurfaceWidth) / 2.0f;
        float box_y = (static_cast<float>(state.height) - box_h) / 2.0f;
        state.box_rect = {box_x, box_y, kLauncherSurfaceWidth, box_h};

        node_add_rrect(root, box_x, box_y, kLauncherSurfaceWidth, box_h,
                       metrics::radius_md, kLauncherBorderWidth,
                       rgba(palette::overlay), rgba(palette::accent));

        float clip_inset = metrics::radius_md;
        Node *outer =
            node_add_group(root, box_x + clip_inset, box_y + clip_inset,
                           kLauncherSurfaceWidth - 2 * clip_inset,
                           box_h - 2 * clip_inset, true);
        auto orx = [&](float v) { return v - (box_x + clip_inset); };
        auto ory = [&](float v) { return v - (box_y + clip_inset); };

        constexpr float kTransparent[4] = {0, 0, 0, 0};
        float mode_box_x = box_x + kLauncherPad;
        float mode_box_w = kLauncherSearchHeight;
        node_add_rrect(outer, orx(mode_box_x), ory(box_y + kLauncherPad),
                       mode_box_w, kLauncherSearchHeight, metrics::radius_sm,
                       kLauncherBorderWidth, kTransparent,
                       rgba(palette::accent));
        const Texture *mode_tex =
            cached_icon(state.tcache, mode_icon(state.mode), scale);
        if (mode_tex) {
            node_add_texture(
                outer, orx(mode_box_x + (mode_box_w - mode_tex->width) / 2.0f),
                ory(box_y + kLauncherPad +
                    (kLauncherSearchHeight - mode_tex->height) / 2.0f),
                *mode_tex, white);
        }

        float field_box_x = mode_box_x + mode_box_w + kLauncherPad;
        float field_box_w =
            box_x + kLauncherSurfaceWidth - kLauncherPad - field_box_x;
        node_add_rrect(outer, orx(field_box_x), ory(box_y + kLauncherPad),
                       field_box_w, kLauncherSearchHeight, metrics::radius_sm,
                       kLauncherBorderWidth, kTransparent,
                       rgba(palette::accent));
        float text_x = field_box_x + kLauncherPad;
        float field_center_y =
            box_y + kLauncherPad + kLauncherSearchHeight / 2.0f;

        float cell_w = 0.0f;
        if (const Texture *ref_tex = cached_text(state.tcache, "M", scale))
            cell_w =
                static_cast<float>(ref_tex->width) /
                static_cast<float>(ref_tex->scale > 0 ? ref_tex->scale : 1);

        std::string display = elide(state.query);
        size_t char_index = 0;
        float cx = text_x;
        for (size_t i = 0; i < display.size();) {
            size_t len =
                std::min(utf8_char_len(static_cast<unsigned char>(display[i])),
                         display.size() - i);
            std::string ch = display.substr(i, len);
            i += len;

            bool animated = char_index < state.query_char_anim.size();
            const QueryCharAnim *anim =
                animated ? &state.query_char_anim[char_index] : nullptr;
            float glyph_scale = anim ? anim->scale : 1.0f;
            float slide = anim ? anim->slide_x : 0.0f;

            const Texture *ch_tex = cached_text(state.tcache, ch, scale);
            if (ch_tex && glyph_scale > 0.0f) {
                float inv = 1.0f / static_cast<float>(
                                       ch_tex->scale > 0 ? ch_tex->scale : 1);
                float w = static_cast<float>(ch_tex->width) * inv * glyph_scale;
                float h =
                    static_cast<float>(ch_tex->height) * inv * glyph_scale;
                float cell_center_x = cx + cell_w / 2.0f + slide;
                node_add_texture_rect(outer, orx(cell_center_x - w / 2.0f),
                                      ory(field_center_y - h / 2.0f), w, h,
                                      *ch_tex, white);
            }
            cx += cell_w;
            ++char_index;
        }

        if (state.cursor_blink_visible) {
            constexpr float kCaretW = 2.0f;
            float caret_h = kLauncherSearchHeight - 2.0f * kLauncherPad;
            node_add_rect(outer, orx(cx), ory(field_center_y - caret_h / 2.0f),
                          kCaretW, caret_h, rgba(palette::text));
        }

        float content_x = mode_box_x + mode_box_w + kLauncherBulletGap;
        float list_top = box_y + kLauncherListTop;
        float list_h = box_y + box_h - clip_inset - list_top;
        Node *list_clip = node_add_group(
            outer, orx(mode_box_x), ory(list_top),
            kLauncherSurfaceWidth - 2 * kLauncherPad, list_h, true);

        constexpr float kRowPitch = kLauncherRowHeight + kLauncherRowSpacing;
        float row_bg_x = content_x - mode_box_x;
        float row_bg_w =
            box_x + kLauncherSurfaceWidth - kLauncherPad - content_x;

        if (state.selected_index >= 0) {
            float highlight_target =
                static_cast<float>(state.selected_index) * kRowPitch;
            if (state.highlight_offset_target < 0.0f) {
                state.highlight_offset = highlight_target;
                state.highlight_offset_target = highlight_target;
            } else if (highlight_target != state.highlight_offset_target) {
                state.highlight_offset_target = highlight_target;
                state.animations.animate(
                    state.highlight_offset, state.highlight_offset_target,
                    kLauncherHighlightAnimMs, Easing::EaseOutCubic,
                    [&state](float v) { state.highlight_offset = v; }, {},
                    kLauncherHighlightOwner);
            }

            float scroll_target = static_cast<float>(first) * kRowPitch;
            if (state.scroll_offset_target < 0.0f) {
                state.scroll_offset = scroll_target;
                state.scroll_offset_target = scroll_target;
            } else if (scroll_target != state.scroll_offset_target) {
                state.scroll_offset_target = scroll_target;
                state.animations.animate(
                    state.scroll_offset, state.scroll_offset_target,
                    kLauncherHighlightAnimMs, Easing::EaseOutCubic,
                    [&state](float v) { state.scroll_offset = v; }, {},
                    kLauncherScrollOwner);
            }
        } else {
            state.highlight_offset_target = -1.0f;
            state.scroll_offset_target = -1.0f;
        }

        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            bool is_selected = i == state.selected_index;
            float y = static_cast<float>(i) * kRowPitch - state.scroll_offset;

            Node *rowg = node_add_group(
                list_clip, 0, y, kLauncherSurfaceWidth - 2 * kLauncherPad,
                kLauncherRowHeight, true);
            auto lrx = [&](float v) { return v - mode_box_x; };
            auto lry = [&](float v) { return v - y; };

            constexpr float kRowTransparent[4] = {0, 0, 0, 0};
            node_add_rrect(rowg, row_bg_x, 0, row_bg_w, kLauncherRowHeight,
                           metrics::radius_sm, 0.0f,
                           rgba(palette::text_alpha04), kRowTransparent);

            float rowx = content_x + kLauncherPad;
            if (rows[i].icon_tex) {
                const Texture &tex = *rows[i].icon_tex;
                node_add_texture_rect(
                    rowg, lrx(rowx),
                    lry(y + (kLauncherRowHeight - kIconTargetSize) / 2.0f),
                    kIconTargetSize, kIconTargetSize, tex, white);
            } else {
                const Texture *row_icon =
                    cached_icon(state.tcache, rows[i].icon, scale);
                if (row_icon) {
                    node_add_texture(
                        rowg,
                        lrx(rowx + (kIconTargetSize - row_icon->width) / 2.0f),
                        lry(y + (kLauncherRowHeight - row_icon->height) / 2.0f),
                        *row_icon, is_selected ? white : dim);
                }
            }
            rowx += kIconTargetSize + kLauncherPad;
            const Texture *label =
                cached_text(state.tcache, elide(rows[i].label), scale);
            if (!rows[i].subtitle.empty()) {
                const Texture *subtitle = cached_text_small(
                    state.tcache, elide(rows[i].subtitle), scale);
                constexpr float kTwoLineTopPad = 5.0f;
                constexpr float kTwoLineBottomPad = 5.0f;
                if (label)
                    node_add_texture(rowg, lrx(rowx), lry(y + kTwoLineTopPad),
                                     *label, white);
                if (subtitle)
                    node_add_texture(rowg, lrx(rowx),
                                     lry(y + kLauncherRowHeight -
                                         subtitle->height - kTwoLineBottomPad),
                                     *subtitle, dim);
            } else if (label) {
                node_add_texture(
                    rowg, lrx(rowx),
                    lry(y + (kLauncherRowHeight - label->height) / 2.0f),
                    *label, white);
            }
        }

        for (int slot = 0; slot < kLauncherMaxVisible; ++slot) {
            const Texture &bullet = state.bullet_tex[slot];
            if (!bullet.id)
                continue;
            float slot_y = static_cast<float>(slot) * kRowPitch;
            node_add_texture_rect(
                list_clip, (mode_box_w - kLauncherBulletSize) / 2.0f,
                slot_y + (kLauncherRowHeight - kLauncherBulletSize) / 2.0f,
                kLauncherBulletSize, kLauncherBulletSize, bullet, white);
        }

        if (state.selected_index >= 0) {
            constexpr float kTransparent2[4] = {0, 0, 0, 0};
            node_add_rrect(list_clip, content_x - mode_box_x,
                           state.highlight_offset - state.scroll_offset,
                           box_x + kLauncherSurfaceWidth - kLauncherPad -
                               content_x,
                           kLauncherRowHeight, metrics::radius_sm,
                           kLauncherHighlightBorderWidth, kTransparent2,
                           rgba(palette::accent_alt));
        }
    }

    state.renderer->set_opacity(state.opacity);
    state.scene.draw(*state.renderer);
    state.renderer->set_opacity(1.0f);
    eglSwapBuffers(state.egl_display, state.egl_surface);

    if (state.animations.hasActive())
        launcher_request_frame(state);
}

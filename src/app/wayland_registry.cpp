#include "app/wayland_registry.h"

#include "app/monitor_output.h"
#include "app/wayland_state.h"
#include "core/log.h"
#include "modules/bar.h"

#include <algorithm>
#include <cstring>

namespace {

void layer_surface_configure(void *data, zwlr_layer_surface_v1 *layer_surface,
                             uint32_t serial, uint32_t width, uint32_t) {
    auto *mon = static_cast<MonitorOutput *>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    mon->width = static_cast<int32_t>(width);
    if (mon->egl_window) {
        int32_t scale = mon->output_scale.scale;
        wl_egl_window_resize(mon->egl_window, mon->width * scale,
                             bar_detail::bar_current_height(*mon) * scale, 0,
                             0);
    }
    mon->configured = true;
}

void layer_surface_closed(void *data, zwlr_layer_surface_v1 *) {
    static_cast<MonitorOutput *>(data)->app->running = false;
}

namespace output_detail {
void geometry(void *, wl_output *, int32_t, int32_t, int32_t, int32_t, int32_t,
              const char *, const char *, int32_t) {}
void mode(void *, wl_output *, uint32_t, int32_t, int32_t, int32_t) {}
void scale_event(void *data, wl_output *, int32_t factor) {
    static_cast<MonitorOutput *>(data)->output.scale = factor;
}
void name_event(void *data, wl_output *, const char *name) {
    static_cast<MonitorOutput *>(data)->output.name = name;
}
void description(void *, wl_output *, const char *) {}
void done(void *data, wl_output *) {
    auto *mon = static_cast<MonitorOutput *>(data);
    mon->output.done = true;
    if (!mon->activated && mon->app->egl_context != EGL_NO_CONTEXT)
        monitor_output_activate(*mon->app, *mon);
}

const wl_output_listener &listener() {
    static constexpr wl_output_listener l{
        .geometry = geometry,
        .mode = mode,
        .done = done,
        .scale = scale_event,
        .name = name_event,
        .description = description,
    };
    return l;
}
} // namespace output_detail

namespace xdg_wm_base_listener_detail {
void ping(void *, xdg_wm_base *wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}
constexpr xdg_wm_base_listener listener{.ping = ping};
} // namespace xdg_wm_base_listener_detail

void registry_global(void *data, wl_registry *registry, uint32_t name,
                     const char *interface, uint32_t version) {
    auto *state = static_cast<WaylandState *>(data);
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = static_cast<wl_compositor *>(wl_registry_bind(
            registry, name, &wl_compositor_interface, std::min(version, 6u)));
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        state->layer_shell =
            static_cast<zwlr_layer_shell_v1 *>(wl_registry_bind(
                registry, name, &zwlr_layer_shell_v1_interface, 1));
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->wm_base = static_cast<xdg_wm_base *>(wl_registry_bind(
            registry, name, &xdg_wm_base_interface, std::min(version, 6u)));
        xdg_wm_base_add_listener(
            state->wm_base, &xdg_wm_base_listener_detail::listener, nullptr);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        auto mon = std::make_unique<MonitorOutput>();
        mon->app = state;
        mon->output.registry_name = name;
        mon->output.wl = static_cast<wl_output *>(wl_registry_bind(
            registry, name, &wl_output_interface, std::min(version, 4u)));
        wl_output_add_listener(mon->output.wl, &output_detail::listener(),
                               mon.get());
        state->outputs.push_back(std::move(mon));
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->idle.seat = static_cast<wl_seat *>(
            wl_registry_bind(registry, name, &wl_seat_interface, 3));
        state->seat_caps.keyboard = &state->keyboard;
        state->seat_caps.pointer = &state->pointer;
        keyboard_attach_seat(state->seat_caps, state->idle.seat);
    } else if (strcmp(interface, ext_idle_notifier_v1_interface.name) == 0) {
        state->idle.notifier =
            static_cast<ext_idle_notifier_v1 *>(wl_registry_bind(
                registry, name, &ext_idle_notifier_v1_interface, 1));
    } else if (strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) ==
               0) {
        state->idle.inhibit_manager =
            static_cast<zwp_idle_inhibit_manager_v1 *>(wl_registry_bind(
                registry, name, &zwp_idle_inhibit_manager_v1_interface, 1));
    }
}

void registry_global_remove(void *data, wl_registry *, uint32_t name) {
    auto *state = static_cast<WaylandState *>(data);
    auto it = std::find_if(state->outputs.begin(), state->outputs.end(),
                           [name](const std::unique_ptr<MonitorOutput> &m) {
                               return m->output.registry_name == name;
                           });
    if (it == state->outputs.end())
        return;
    klog("output: '%s' removed", (*it)->output.name.c_str());
    monitor_output_destroy(**it);
    state->outputs.erase(it);
}

} // namespace

const wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

const zwlr_layer_surface_v1_listener bar_layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

bool bootstrap_egl(WaylandState &state) {
    state.egl_display =
        eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(state.display));
    if (state.egl_display == EGL_NO_DISPLAY)
        return false;
    if (!eglInitialize(state.egl_display, nullptr, nullptr))
        return false;
    eglBindAPI(EGL_OPENGL_ES_API);

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,
        EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,
        8,
        EGL_GREEN_SIZE,
        8,
        EGL_BLUE_SIZE,
        8,
        EGL_ALPHA_SIZE,
        8,
        EGL_NONE,
    };
    EGLint num_configs = 0;
    if (!eglChooseConfig(state.egl_display, config_attribs, &state.egl_config,
                         1, &num_configs) ||
        num_configs == 0) {
        return false;
    }

    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    state.egl_context = eglCreateContext(state.egl_display, state.egl_config,
                                         EGL_NO_CONTEXT, context_attribs);
    return state.egl_context != EGL_NO_CONTEXT;
}

bool renderer_bootstrap_init(WaylandState &state) {
    const EGLint pbuffer_attribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLSurface pbuffer = eglCreatePbufferSurface(state.egl_display,
                                                 state.egl_config,
                                                 pbuffer_attribs);
    if (pbuffer == EGL_NO_SURFACE)
        return false;
    if (!eglMakeCurrent(state.egl_display, pbuffer, pbuffer,
                        state.egl_context)) {
        eglDestroySurface(state.egl_display, pbuffer);
        return false;
    }
    bool ok = state.renderer.init();
    eglDestroySurface(state.egl_display, pbuffer);
    return ok;
}

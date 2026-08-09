#pragma once

#include "../core/log.hpp"
#include "pointer.hpp"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <vector>

enum class KeyKind { Text, Up, Down, Left, Right, Enter, Escape, Backspace, Tab };

struct KeyEvent {
    KeyKind kind;
    std::string text;
};

struct KeyboardState {
    xkb_context *ctx = nullptr;
    xkb_keymap *keymap = nullptr;
    xkb_state *xkb = nullptr;
    wl_keyboard *keyboard = nullptr;
    std::vector<KeyEvent> pending;

    int repeat_timer_fd = -1;
    int32_t repeat_rate_hz = 25;
    int32_t repeat_delay_ms = 400;
    uint32_t repeat_keycode = 0;
    bool repeat_active = false;
};

struct SeatCapabilityState {
    KeyboardState *keyboard = nullptr;
    PointerState *pointer = nullptr;
};

inline std::optional<KeyEvent> translate_key(xkb_state *state,
                                             uint32_t keycode) {
    xkb_keycode_t xkb_code = keycode + 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(state, xkb_code);

    switch (sym) {
    case XKB_KEY_Up:
        return KeyEvent{KeyKind::Up, ""};
    case XKB_KEY_Down:
        return KeyEvent{KeyKind::Down, ""};
    case XKB_KEY_Left:
        return KeyEvent{KeyKind::Left, ""};
    case XKB_KEY_Right:
        return KeyEvent{KeyKind::Right, ""};
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        return KeyEvent{KeyKind::Enter, ""};
    case XKB_KEY_Escape:
        return KeyEvent{KeyKind::Escape, ""};
    case XKB_KEY_BackSpace:
        return KeyEvent{KeyKind::Backspace, ""};
    case XKB_KEY_Tab:
        return KeyEvent{KeyKind::Tab, ""};
    default:
        break;
    }

    char buf[32];
    int n = xkb_state_key_get_utf8(state, xkb_code, buf, sizeof(buf));
    if (n <= 0)
        return std::nullopt;
    return KeyEvent{KeyKind::Text, std::string(buf, static_cast<size_t>(n))};
}

namespace keyboard_detail {

inline void keymap_cb(void *data, wl_keyboard *, uint32_t format, int32_t fd,
                      uint32_t size) {
    auto *state = static_cast<KeyboardState *>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    void *map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED)
        return;

    if (state->keymap)
        xkb_keymap_unref(state->keymap);
    if (state->xkb)
        xkb_state_unref(state->xkb);
    state->xkb = nullptr;

    state->keymap = xkb_keymap_new_from_string(
        state->ctx, static_cast<const char *>(map), XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (!state->keymap) {
        klog("keyboard: failed to compile keymap");
        return;
    }
    state->xkb = xkb_state_new(state->keymap);
}

inline void enter_cb(void *, wl_keyboard *, uint32_t, wl_surface *,
                     wl_array *) {}

inline void set_repeat_timer(KeyboardState &state, bool armed) {
    if (state.repeat_timer_fd < 0)
        return;
    itimerspec spec{};
    if (armed && state.repeat_rate_hz > 0) {
        spec.it_value.tv_sec = state.repeat_delay_ms / 1000;
        spec.it_value.tv_nsec = (state.repeat_delay_ms % 1000) * 1000000L;
        int64_t interval_ns = 1000000000LL / state.repeat_rate_hz;
        spec.it_interval.tv_sec = interval_ns / 1000000000LL;
        spec.it_interval.tv_nsec = interval_ns % 1000000000LL;
    }

    timerfd_settime(state.repeat_timer_fd, 0, &spec, nullptr);
}

inline void leave_cb(void *data, wl_keyboard *, uint32_t, wl_surface *) {
    auto *state = static_cast<KeyboardState *>(data);
    if (state->repeat_active) {
        state->repeat_active = false;
        set_repeat_timer(*state, false);
    }
}

inline void key_cb(void *data, wl_keyboard *, uint32_t, uint32_t, uint32_t key,
                   uint32_t key_state) {
    auto *state = static_cast<KeyboardState *>(data);
    if (!state->xkb)
        return;
    xkb_keycode_t xkb_code = key + 8;

    if (key_state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        if (state->repeat_active && key == state->repeat_keycode) {
            state->repeat_active = false;
            set_repeat_timer(*state, false);
        }
        return;
    }
    if (key_state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;

    if (auto ev = translate_key(state->xkb, key))
        state->pending.push_back(*ev);

    if (state->keymap && xkb_keymap_key_repeats(state->keymap, xkb_code)) {
        state->repeat_keycode = key;
        state->repeat_active = true;
        set_repeat_timer(*state, true);
    }
}

inline void modifiers_cb(void *data, wl_keyboard *, uint32_t,
                         uint32_t mods_depressed, uint32_t mods_latched,
                         uint32_t mods_locked, uint32_t group) {
    auto *state = static_cast<KeyboardState *>(data);
    if (!state->xkb)
        return;
    xkb_state_update_mask(state->xkb, mods_depressed, mods_latched, mods_locked,
                          0, 0, group);
}

inline void repeat_info_cb(void *data, wl_keyboard *, int32_t rate,
                           int32_t delay) {
    auto *state = static_cast<KeyboardState *>(data);
    klog("keyboard: compositor repeat_info rate=%dHz delay=%dms", rate,
        delay);
    state->repeat_rate_hz = rate;
    state->repeat_delay_ms = delay;

    if (state->repeat_active)
        set_repeat_timer(*state, true);
}

inline constexpr wl_keyboard_listener kKeyboardListener = {
    .keymap = keymap_cb,
    .enter = enter_cb,
    .leave = leave_cb,
    .key = key_cb,
    .modifiers = modifiers_cb,
    .repeat_info = repeat_info_cb,
};

inline void seat_capabilities_cb(void *data, wl_seat *seat, uint32_t caps) {
    auto *seat_state = static_cast<SeatCapabilityState *>(data);

    KeyboardState *kb = seat_state->keyboard;
    bool has_keyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;
    if (has_keyboard && !kb->keyboard) {
        kb->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(kb->keyboard, &kKeyboardListener, kb);
    } else if (!has_keyboard && kb->keyboard) {
        wl_keyboard_release(kb->keyboard);
        kb->keyboard = nullptr;
    }

    PointerState *ptr = seat_state->pointer;
    bool has_pointer = caps & WL_SEAT_CAPABILITY_POINTER;
    if (has_pointer && !ptr->pointer) {
        pointer_bind(*ptr, seat);
    } else if (!has_pointer && ptr->pointer) {
        pointer_release(*ptr);
    }
}

inline void seat_name_cb(void *, wl_seat *, const char *) {}

inline constexpr wl_seat_listener kSeatListener = {
    .capabilities = seat_capabilities_cb,
    .name = seat_name_cb,
};

}

inline void keyboard_attach_seat(SeatCapabilityState &seat_state,
                                 wl_seat *seat) {
    if (!seat_state.keyboard->ctx)
        seat_state.keyboard->ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (seat_state.keyboard->repeat_timer_fd < 0) {
        seat_state.keyboard->repeat_timer_fd =
            timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    }
    wl_seat_add_listener(seat, &keyboard_detail::kSeatListener, &seat_state);
}

inline std::vector<KeyEvent> keyboard_drain_events(KeyboardState &state) {
    std::vector<KeyEvent> events = std::move(state.pending);
    state.pending.clear();
    return events;
}

inline void keyboard_repeat_tick(KeyboardState &state) {
    if (state.repeat_timer_fd < 0)
        return;
    uint64_t expirations = 0;
    ssize_t n = read(state.repeat_timer_fd, &expirations, sizeof(expirations));
    if (n != sizeof(expirations) || !state.repeat_active || !state.xkb)
        return;
    auto ev = translate_key(state.xkb, state.repeat_keycode);
    if (!ev)
        return;
    constexpr uint64_t kMaxCatchUp = 8;
    for (uint64_t i = 0; i < std::min(expirations, kMaxCatchUp); ++i)
        state.pending.push_back(*ev);
}


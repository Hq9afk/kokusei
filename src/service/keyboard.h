#pragma once

#include "service/pointer.h"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdint>
#include <optional>
#include <string>
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

std::optional<KeyEvent> translate_key(xkb_state *state, uint32_t keycode);

void keyboard_attach_seat(SeatCapabilityState &seat_state, wl_seat *seat);

std::vector<KeyEvent> keyboard_drain_events(KeyboardState &state);

void keyboard_repeat_tick(KeyboardState &state);

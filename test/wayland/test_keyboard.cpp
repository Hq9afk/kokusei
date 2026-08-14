#include "wayland/keyboard.h"

#include <cassert>

void test_keyboard() {
    xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    assert(ctx);

    xkb_rule_names names{};
    xkb_keymap *keymap =
        xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    assert(keymap);
    xkb_state *state = xkb_state_new(keymap);
    assert(state);

    auto ev = translate_key(state, 30);
    assert(ev.has_value());
    assert(ev->kind == KeyKind::Text);
    assert(ev->text == "a");

    auto enter_ev = translate_key(state, 28);
    assert(enter_ev.has_value());
    assert(enter_ev->kind == KeyKind::Enter);

    auto esc_ev = translate_key(state, 1);
    assert(esc_ev.has_value());
    assert(esc_ev->kind == KeyKind::Escape);

    auto bs_ev = translate_key(state, 14);
    assert(bs_ev.has_value());
    assert(bs_ev->kind == KeyKind::Backspace);

    auto shift_ev = translate_key(state, 42);
    assert(!shift_ev.has_value());

    xkb_state_unref(state);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);
}

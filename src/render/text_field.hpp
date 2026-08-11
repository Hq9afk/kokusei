#pragma once

#include "../wayland/keyboard.hpp"
#include <string>

inline void text_field_backspace(std::string &text) {
    while (!text.empty() &&
           (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80)
        text.pop_back();
    if (!text.empty())
        text.pop_back();
}

struct TextFieldState {
    std::string text;
    bool cursor_blink_visible = true;
};

enum class TextFieldResult { None, Changed, Committed, Cancelled };

inline TextFieldResult text_field_handle_key(TextFieldState &field,
                                             const KeyEvent &event) {
    switch (event.kind) {
    case KeyKind::Text:
        field.text += event.text;
        field.cursor_blink_visible = true;
        return TextFieldResult::Changed;
    case KeyKind::Backspace:
        text_field_backspace(field.text);
        field.cursor_blink_visible = true;
        return TextFieldResult::Changed;
    case KeyKind::Enter:
        return TextFieldResult::Committed;
    case KeyKind::Escape:
        return TextFieldResult::Cancelled;
    default:
        return TextFieldResult::None;
    }
}

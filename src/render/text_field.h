#pragma once

#include "service/keyboard.h"
#include <string>

void text_field_backspace(std::string &text);

struct TextFieldState {
    std::string text;
    bool cursor_blink_visible = true;
};

enum class TextFieldResult { None, Changed, Committed, Cancelled };

TextFieldResult text_field_handle_key(TextFieldState &field,
                                      const KeyEvent &event);

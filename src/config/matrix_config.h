#pragma once

#include "render/palette.h"

#include <cstdint>
#include <string>

constexpr float kMatrixFontPx = 20.0f;
constexpr float kMatrixCellWidth = 12.0f;
constexpr float kMatrixCellHeight = 24.0f;

constexpr float kMatrixBoldChance = 0.5f;
constexpr float kMatrixFadeAlpha = 0.05f;
constexpr float kMatrixFallIntervalMs = 45.0f;
constexpr float kMatrixResetChance = 0.025f;

constexpr int kMatrixDefaultWindowWidth = 480;
constexpr int kMatrixDefaultWindowHeight = 600;

inline constexpr Color kMatrixWindowBackground = {0.0f, 0.0f, 0.0f, 0.7f};

inline constexpr Color kMatrixHeadColor = palette::text;
inline constexpr Color kMatrixTailColor = palette::accent;

inline const std::u32string &matrix_glyph_pool() {
    static const std::u32string pool = [] {
        std::u32string s;
        for (uint32_t cp = 0xFF66; cp <= 0xFF9D; ++cp)
            s += static_cast<char32_t>(cp);
        for (int i = 0; i < 2; ++i)
            s += U"1234567890";
        for (int i = 0; i < 4; ++i)
            s += U"-=*_+|:<>\"";
        return s;
    }();
    return pool;
}

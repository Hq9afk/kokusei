#pragma once

#include <cstdint>
#include <string_view>

struct Color {
    float r, g, b, a;
};

inline const float *rgba(const Color &c) { return &c.r; }

constexpr uint8_t digit(char c) {
    return static_cast<uint8_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
}

constexpr Color color(std::string_view hex) {
    size_t i = hex.front() == '#' ? 1 : 0;
    auto byte = [&](size_t at) {
        return static_cast<float>(digit(hex[at]) * 16 + digit(hex[at + 1])) /
               255.0f;
    };
    float a = hex.size() - i > 6 ? byte(i + 6) : 1.0f;
    return {byte(i), byte(i + 2), byte(i + 4), a};
}

namespace palette {

inline constexpr Color accent = color("#9B57F4");
inline constexpr Color accent_alpha12 = color("#9B57F41F");
inline constexpr Color accent_alpha19 = color("#9B57F430");
inline constexpr Color accent_alpha25 = color("#9B57F440");
inline constexpr Color accent_container = color("#3C1877");

inline constexpr Color accent_alt = color("#DBAA24");
inline constexpr Color accent_alt_container = color("#2A1957");

inline constexpr Color base = color("#0A0614");
inline constexpr Color overlay = color("#0A0614EB");

inline constexpr Color lavender = color("#806FBE");
inline constexpr Color lavender_alpha20 = color("#806FBE33");

inline constexpr Color field_bg = color("#170D30");
inline constexpr Color surface_alt = color("#1D113B");

inline constexpr Color text = color("#F0ECF9");
inline constexpr Color text_alpha04 = color("#F0ECF90A");
inline constexpr Color text_alpha06 = color("#F0ECF90F");
inline constexpr Color text_alpha07 = color("#F0ECF912");
inline constexpr Color text_alpha08 = color("#F0ECF914");
inline constexpr Color text_alpha11 = color("#F0ECF91C");
inline constexpr Color text_alpha15 = color("#F0ECF926");
inline constexpr Color text_alpha20 = color("#F0ECF933");
inline constexpr Color text_alpha85 = color("#F0ECF9D9");
inline constexpr Color text_muted = color("#AB9DC8");
inline constexpr Color text_dim = color("#AB9DC899");

inline constexpr Color electro = color("#9D3EF2");

inline constexpr Color critical = color("#F44747");
inline constexpr Color critical_alpha15 = color("#F4474726");

} // namespace palette

namespace metrics {

inline constexpr float radius_md = 10.0f;
inline constexpr float radius_sm = 5.0f;
inline constexpr float border_thin = 2.0f;
inline constexpr float border_thick = 4.0f;

} // namespace metrics

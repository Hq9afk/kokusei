#pragma once

struct Color {
    float r, g, b, a;
};

inline const float *rgba(const Color &c) { return &c.r; }

inline constexpr Color with_alpha(Color c, float a) {
    return {c.r, c.g, c.b, a};
}

inline constexpr Color lerp_color(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
           a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

namespace palette {

inline constexpr Color accent = {0.6078f, 0.3412f, 0.9569f, 1.0f};
inline constexpr Color accent_alpha12 = with_alpha(accent, 0.12f);
inline constexpr Color accent_alpha15 = with_alpha(accent, 0.15f);
inline constexpr Color accent_alpha18 = with_alpha(accent, 0.18f);
inline constexpr Color accent_alpha20 = with_alpha(accent, 0.20f);
inline constexpr Color accent_alpha25 = with_alpha(accent, 0.25f);
inline constexpr Color accent_container = {0.2353f, 0.0941f, 0.4667f, 1.0f};

inline constexpr Color accent_alt = {0.8588f, 0.6667f, 0.1412f, 1.0f};
inline constexpr Color accent_alt_container = {0.1647f, 0.0980f, 0.3412f, 1.0f};

inline constexpr Color base = {0.0392f, 0.0235f, 0.0784f, 1.0f};
inline constexpr Color base_alpha45 = with_alpha(base, 0.45f);
inline constexpr Color overlay = with_alpha(base, 0.92f);

inline constexpr Color lavender = {0.5020f, 0.4353f, 0.7451f, 1.0f};
inline constexpr Color lavender_alpha20 = with_alpha(lavender, 0.20f);
inline constexpr Color lavender_alpha35 = with_alpha(lavender, 0.35f);
inline constexpr Color lavender_subtle = with_alpha(lavender, 0.15f);

inline constexpr Color field_bg = {0.0902f, 0.0510f, 0.1882f, 1.0f};
inline constexpr Color surface_alt = {0.1137f, 0.0667f, 0.2314f, 1.0f};

inline constexpr Color text = {0.9412f, 0.9255f, 0.9765f, 1.0f};
inline constexpr Color text_alpha03 = with_alpha(text, 0.03f);
inline constexpr Color text_alpha04 = with_alpha(text, 0.04f);
inline constexpr Color text_alpha05 = with_alpha(text, 0.05f);
inline constexpr Color text_alpha06 = with_alpha(text, 0.06f);
inline constexpr Color text_alpha07 = with_alpha(text, 0.07f);
inline constexpr Color text_alpha08 = with_alpha(text, 0.08f);
inline constexpr Color text_alpha10 = with_alpha(text, 0.10f);
inline constexpr Color text_alpha12 = with_alpha(text, 0.12f);
inline constexpr Color text_alpha13 = with_alpha(text, 0.13f);
inline constexpr Color text_alpha14 = with_alpha(text, 0.14f);
inline constexpr Color text_alpha15 = with_alpha(text, 0.15f);
inline constexpr Color text_alpha18 = with_alpha(text, 0.18f);
inline constexpr Color text_alpha20 = with_alpha(text, 0.20f);
inline constexpr Color text_alpha35 = with_alpha(text, 0.35f);
inline constexpr Color text_muted = {0.6706f, 0.6157f, 0.7843f, 1.0f};
inline constexpr Color text_dim = with_alpha(text_muted, 0.6f);

inline constexpr Color electro = {0.6157f, 0.2431f, 0.9490f, 1.0f};

inline constexpr Color critical = {0.9569f, 0.2784f, 0.2784f, 1.0f};
inline constexpr Color critical_alpha15 = with_alpha(critical, 0.15f);

}

namespace metrics {

inline constexpr float radius_md = 10.0f;
inline constexpr float radius_sm = 5.0f;
inline constexpr float border_thin = 2.0f;
inline constexpr float border_thick = 4.0f;

}

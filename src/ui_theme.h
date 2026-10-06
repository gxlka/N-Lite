#pragma once

#include <cstdint>

inline constexpr int kUiSpacingPx = 8;
inline constexpr int kUiEmojiSizePx = 20;
inline constexpr int kUiButtonHeightPx = 40;
inline constexpr int kUiProcessRowHeightPx = 44;
inline constexpr int kUiStartupRowHeightPx = 60;
inline constexpr int kUiCornerRadiusPx = 12;

struct UiPalette {
    bool dark = true;
    uint32_t background = 0;
    uint32_t surface = 0;
    uint32_t softSurface = 0;
    uint32_t border = 0;
    uint32_t text = 0;
    uint32_t muted = 0;
    uint32_t accent = 0;
    uint32_t accentSoft = 0;
    uint32_t green = 0;
    uint32_t greenSoft = 0;
    uint32_t amber = 0;
    uint32_t amberSoft = 0;
    uint32_t red = 0;
    uint32_t navHover = 0;
    uint32_t selected = 0;
    uint32_t field = 0;
    uint32_t row = 0;
    uint32_t track = 0;
};

constexpr uint32_t UiRgb(uint32_t red, uint32_t green, uint32_t blue) {
    return red | (green << 8) | (blue << 16);
}

constexpr UiPalette PaletteFor(bool dark) {
    if (dark) {
        return {true,
            UiRgb(17, 21, 29), UiRgb(26, 32, 42), UiRgb(21, 26, 35), UiRgb(43, 52, 66),
            UiRgb(233, 237, 245), UiRgb(151, 162, 178), UiRgb(130, 144, 255), UiRgb(37, 43, 73),
            UiRgb(114, 200, 164), UiRgb(27, 53, 45), UiRgb(228, 182, 108), UiRgb(53, 43, 29),
            UiRgb(239, 135, 144), UiRgb(31, 38, 52), UiRgb(38, 47, 73), UiRgb(16, 20, 29),
            UiRgb(23, 29, 42), UiRgb(57, 64, 81)};
    }
    return {false,
        UiRgb(243, 245, 249), UiRgb(255, 255, 255), UiRgb(247, 248, 251), UiRgb(223, 228, 236),
        UiRgb(32, 41, 56), UiRgb(102, 115, 134), UiRgb(88, 101, 218), UiRgb(238, 240, 255),
        UiRgb(24, 123, 88), UiRgb(231, 245, 239), UiRgb(166, 99, 19), UiRgb(255, 242, 220),
        UiRgb(184, 65, 76), UiRgb(238, 240, 245), UiRgb(229, 233, 252), UiRgb(247, 248, 251),
        UiRgb(249, 250, 252), UiRgb(220, 225, 233)};
}

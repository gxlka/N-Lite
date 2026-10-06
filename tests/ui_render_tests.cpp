#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ui_emoji.h"
#include "ui_icons.h"
#include "ui_paint.h"

#include <cstdint>
#include <iostream>

namespace {
void PaintColor(HDC dc, int width, int height, void*) {
    HBRUSH brush = CreateSolidBrush(RGB(114, 200, 164));
    RECT rect{0, 0, width, height};
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

HBITMAP FailBitmap(HDC, int, int, void*) { return nullptr; }

void ClearSurface(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

bool HasChangedPixels(const uint8_t* bytes, int width, int height, COLORREF background) {
    for (int i = 0; i < width * height; ++i) {
        const int offset = i * 4;
        if (bytes[offset] != GetBValue(background) || bytes[offset + 1] != GetGValue(background) ||
            bytes[offset + 2] != GetRValue(background)) return true;
    }
    return false;
}
}

int main() {
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 64;
    info.bmiHeader.biHeight = -48;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dc || !bitmap || !pixels) return 2;
    HGDIOBJ old = SelectObject(dc, bitmap);
    const COLORREF background = RGB(17, 21, 29);
    HBRUSH brush = CreateSolidBrush(background);
    RECT surface{0, 0, 64, 48};
    FillRect(dc, &surface, brush);
    DeleteObject(brush);

    InitializeUiEmojiRenderer();
    const bool drew = DrawUiEmoji(dc, UiRect{8, 8, 32, 32}, 96, L"\U0001F9E0", RGB(130, 144, 255));
    GdiFlush();
    const auto* bytes = static_cast<const uint8_t*>(pixels);
    bool changed = HasChangedPixels(bytes, 64, 48, background);
    ClearSurface(dc, surface, background);
    DrawUiIcon(dc, UiIcon::Delete, UiRect{8, 8, 32, 32}, RGB(239, 135, 144));
    GdiFlush();
    const bool iconChanged = HasChangedPixels(bytes, 64, 48, background);
    ClearSurface(dc, surface, background);
    const bool buffered = !PaintUiBuffered(dc, 64, 48, PaintColor, nullptr, FailBitmap);
    GdiFlush();
    const bool paintFallbackChanged = HasChangedPixels(bytes, 64, 48, background);
    ShutdownUiEmojiRenderer();
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    if (!drew || !changed) { std::cerr << "FAIL ui_emoji_draws_glyph_or_vector_fallback\n"; return 1; }
    if (!iconChanged) { std::cerr << "FAIL ui_vector_icon_draws_sharp_pixels\n"; return 1; }
    if (!buffered || !paintFallbackChanged) { std::cerr << "FAIL ui_paint_falls_back_when_bitmap_allocation_fails\n"; return 1; }
    std::cout << "PASS ui_emoji_draws_glyph_or_vector_fallback\n";
    std::cout << "PASS ui_vector_icon_draws_sharp_pixels\n";
    std::cout << "PASS ui_paint_falls_back_when_bitmap_allocation_fails\n";
    return 0;
}

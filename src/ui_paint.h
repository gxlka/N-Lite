#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using UiPaintCallback = void (*)(HDC, int, int, void*);
using UiBitmapFactory = HBITMAP (*)(HDC, int, int, void*);

inline bool PaintUiBuffered(HDC target, int widthPx, int heightPx, UiPaintCallback paint,
                           void* context, UiBitmapFactory factory) {
    if (!target || !paint || widthPx <= 0 || heightPx <= 0) return false;
    HDC buffer = CreateCompatibleDC(target);
    if (!buffer) {
        paint(target, widthPx, heightPx, context);
        return false;
    }
    HBITMAP bitmap = factory ? factory(target, widthPx, heightPx, context)
                             : CreateCompatibleBitmap(target, widthPx, heightPx);
    if (!bitmap) {
        DeleteDC(buffer);
        paint(target, widthPx, heightPx, context);
        return false;
    }
    HGDIOBJ old = SelectObject(buffer, bitmap);
    if (!old || old == HGDI_ERROR) {
        DeleteObject(bitmap);
        DeleteDC(buffer);
        paint(target, widthPx, heightPx, context);
        return false;
    }
    paint(buffer, widthPx, heightPx, context);
    const BOOL copied = BitBlt(target, 0, 0, widthPx, heightPx, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, old);
    DeleteObject(bitmap);
    DeleteDC(buffer);
    if (!copied) {
        paint(target, widthPx, heightPx, context);
        return false;
    }
    return true;
}

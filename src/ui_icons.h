#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "ui_layout.h"

#include <cmath>

enum class UiIcon {
    None,
    Memory,
    Processes,
    Startup,
    Settings,
    Refresh,
    Add,
    Clean,
    Delete
};

inline void DrawUiIcon(HDC dc, UiIcon icon, UiRect logicalBounds, COLORREF color) {
    if (!dc || icon == UiIcon::None || logicalBounds.right <= logicalBounds.left ||
        logicalBounds.bottom <= logicalBounds.top) return;
    const int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    const int left = logicalBounds.left, top = logicalBounds.top;
    const int width = logicalBounds.right - logicalBounds.left;
    const int height = logicalBounds.bottom - logicalBounds.top;
    const int right = logicalBounds.right - 1, bottom = logicalBounds.bottom - 1;
    const int cx = left + width / 2, cy = top + height / 2;
    switch (icon) {
    case UiIcon::Memory:
        RoundRect(dc, left + 5, top + 5, right - 5, bottom - 5, 3, 3);
        for (int i = 0; i < 3; ++i) {
            const int x = left + 8 + i * ((width - 16) / 2);
            MoveToEx(dc, x, top + 2, nullptr); LineTo(dc, x, top + 5);
            MoveToEx(dc, x, bottom - 4, nullptr); LineTo(dc, x, bottom - 1);
        }
        break;
    case UiIcon::Processes:
        RoundRect(dc, left + 4, top + 4, right - 3, bottom - 4, 3, 3);
        for (int i = 0; i < 3; ++i) {
            const int y = top + 8 + i * ((height - 16) / 2);
            Ellipse(dc, left + 7, y - 1, left + 10, y + 2);
            MoveToEx(dc, left + 13, y, nullptr); LineTo(dc, right - 6, y);
        }
        break;
    case UiIcon::Startup: {
        POINT triangle[3]{{left + 6, top + 4}, {right - 4, cy}, {left + 6, bottom - 4}};
        HBRUSH brush = CreateSolidBrush(color);
        HGDIOBJ old = SelectObject(dc, brush);
        Polygon(dc, triangle, 3);
        SelectObject(dc, old); DeleteObject(brush);
        break;
    }
    case UiIcon::Settings:
        Ellipse(dc, cx - 5, cy - 5, cx + 5, cy + 5);
        Ellipse(dc, cx - 1, cy - 1, cx + 1, cy + 1);
        for (int i = 0; i < 8; ++i) {
            const double a = 0.7853981633974483 * i;
            const int x1 = cx + static_cast<int>(7 * cos(a));
            const int y1 = cy + static_cast<int>(7 * sin(a));
            const int x2 = cx + static_cast<int>(10 * cos(a));
            const int y2 = cy + static_cast<int>(10 * sin(a));
            MoveToEx(dc, x1, y1, nullptr); LineTo(dc, x2, y2);
        }
        break;
    case UiIcon::Refresh:
        Arc(dc, left + 4, top + 4, right - 4, bottom - 4, cx + 8, cy - 5, cx - 6, cy - 8);
        MoveToEx(dc, right - 5, top + 3, nullptr); LineTo(dc, right - 4, top + 9);
        LineTo(dc, right - 10, top + 8);
        MoveToEx(dc, left + 4, bottom - 3, nullptr); LineTo(dc, left + 3, bottom - 9);
        LineTo(dc, left + 9, bottom - 8);
        break;
    case UiIcon::Add:
        MoveToEx(dc, cx, top + 4, nullptr); LineTo(dc, cx, bottom - 4);
        MoveToEx(dc, left + 4, cy, nullptr); LineTo(dc, right - 4, cy);
        break;
    case UiIcon::Clean:
        MoveToEx(dc, right - 5, top + 4, nullptr); LineTo(dc, left + 8, bottom - 6);
        MoveToEx(dc, left + 5, bottom - 9, nullptr); LineTo(dc, left + 13, bottom - 1);
        MoveToEx(dc, right - 10, top + 5, nullptr); LineTo(dc, right - 7, top + 2);
        MoveToEx(dc, right - 5, cy, nullptr); LineTo(dc, right - 2, cy);
        break;
    case UiIcon::Delete:
        MoveToEx(dc, left + 4, top + 6, nullptr); LineTo(dc, right - 4, top + 6);
        MoveToEx(dc, left + 7, top + 8, nullptr); LineTo(dc, left + 8, bottom - 3);
        LineTo(dc, right - 7, bottom - 3); LineTo(dc, right - 6, top + 8);
        MoveToEx(dc, cx - 3, top + 3, nullptr); LineTo(dc, cx + 3, top + 3);
        MoveToEx(dc, cx - 2, top + 10, nullptr); LineTo(dc, cx - 2, bottom - 6);
        MoveToEx(dc, cx + 2, top + 10, nullptr); LineTo(dc, cx + 2, bottom - 6);
        break;
    case UiIcon::None: break;
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    RestoreDC(dc, saved);
}

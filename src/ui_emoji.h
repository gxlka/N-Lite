#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "ui_layout.h"

bool InitializeUiEmojiRenderer();
bool DrawUiEmoji(HDC dc, UiRect logicalBounds, unsigned dpi, const wchar_t* glyph,
                 COLORREF fallbackColor);
void ShutdownUiEmojiRenderer();

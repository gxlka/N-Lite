#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dxgiformat.h>

#include "ui_emoji.h"
#include "ui_metrics.h"

#include <cwchar>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace {
ID2D1Factory* gD2dFactory = nullptr;
IDWriteFactory* gWriteFactory = nullptr;
IDWriteTextFormat* gEmojiFormat = nullptr;
ID2D1DCRenderTarget* gDcTarget = nullptr;
HFONT gFallbackFont = nullptr;
unsigned gTargetDpi = 96;

template <class T>
void ReleaseUiCom(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

void DrawEmojiFallback(HDC dc, UiRect bounds, const wchar_t* glyph, COLORREF color) {
    if (!gFallbackFont) {
        gFallbackFont = CreateFontW(-kUiEmojiSizePx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Emoji");
    }
    if (!gFallbackFont) {
        const int cx=(bounds.left+bounds.right)/2,cy=(bounds.top+bounds.bottom)/2;
        HPEN pen=CreatePen(PS_SOLID,2,color);HGDIOBJ old=SelectObject(dc,pen);
        MoveToEx(dc,cx,bounds.top+3,nullptr);LineTo(dc,cx,cy-3);
        MoveToEx(dc,cx,bounds.bottom-3,nullptr);LineTo(dc,cx,cy+3);
        MoveToEx(dc,bounds.left+3,cy,nullptr);LineTo(dc,cx-3,cy);
        MoveToEx(dc,bounds.right-3,cy,nullptr);LineTo(dc,cx+3,cy);
        MoveToEx(dc,cx-5,cy-5,nullptr);LineTo(dc,cx-2,cy-2);
        MoveToEx(dc,cx+5,cy-5,nullptr);LineTo(dc,cx+2,cy-2);
        SelectObject(dc,old);DeleteObject(pen);return;
    }
    const int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    HGDIOBJ old = SelectObject(dc, gFallbackFont);
    RECT rect{bounds.left, bounds.top, bounds.right, bounds.bottom};
    DrawTextW(dc, glyph, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
    RestoreDC(dc, saved);
}

bool CreateDcTarget() {
    if (!gD2dFactory) return false;
    D2D1_RENDER_TARGET_PROPERTIES props{};
    props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;
    props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
    return SUCCEEDED(gD2dFactory->CreateDCRenderTarget(&props, &gDcTarget));
}
}

bool InitializeUiEmojiRenderer() {
    D2D1_FACTORY_OPTIONS options{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory), &options, reinterpret_cast<void**>(&gD2dFactory)))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&gWriteFactory)))) return false;
    if (FAILED(gWriteFactory->CreateTextFormat(L"Segoe UI Emoji", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        static_cast<float>(kUiEmojiSizePx), L"", &gEmojiFormat))) return false;
    gEmojiFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    gEmojiFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    gEmojiFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return CreateDcTarget();
}

bool DrawUiEmoji(HDC dc, UiRect bounds, unsigned dpi, const wchar_t* glyph,
                 COLORREF fallbackColor) {
    if (!dc || bounds.right <= bounds.left || bounds.bottom <= bounds.top || !glyph || !*glyph)
        return false;
    if (dpi == 0) dpi = 96;
    POINT viewportOrigin{};
    GetViewportOrgEx(dc,&viewportOrigin);
    const RECT deviceBounds{
        UiLogicalToDevice(bounds.left, dpi)+viewportOrigin.x, UiLogicalToDevice(bounds.top, dpi)+viewportOrigin.y,
        UiLogicalToDevice(bounds.right, dpi)+viewportOrigin.x, UiLogicalToDevice(bounds.bottom, dpi)+viewportOrigin.y};
    if (gD2dFactory && gEmojiFormat && gDcTarget && deviceBounds.right > deviceBounds.left &&
        deviceBounds.bottom > deviceBounds.top) {
        const int saved = SaveDC(dc);
        SetMapMode(dc, MM_TEXT);
        HRESULT result = gDcTarget->BindDC(dc, &deviceBounds);
        if (SUCCEEDED(result)) {
            if (gTargetDpi != dpi) {
                gDcTarget->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
                gTargetDpi = dpi;
            }
            ID2D1SolidColorBrush* brush = nullptr;
            const D2D1_COLOR_F foreground{
                static_cast<float>(GetRValue(fallbackColor)) / 255.0f,
                static_cast<float>(GetGValue(fallbackColor)) / 255.0f,
                static_cast<float>(GetBValue(fallbackColor)) / 255.0f, 1.0f};
            result = gDcTarget->CreateSolidColorBrush(foreground, &brush);
            if (SUCCEEDED(result)) {
                gDcTarget->BeginDraw();
                const D2D1_RECT_F layout{0.0f, 0.0f,
                    static_cast<float>(bounds.right - bounds.left),
                    static_cast<float>(bounds.bottom - bounds.top)};
                gDcTarget->DrawText(glyph, static_cast<UINT32>(std::wcslen(glyph)),
                    gEmojiFormat, layout, brush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT,
                    DWRITE_MEASURING_MODE_NATURAL);
                result = gDcTarget->EndDraw();
                brush->Release();
            }
        }
        RestoreDC(dc, saved);
        if (SUCCEEDED(result)) return true;
        if (result == D2DERR_RECREATE_TARGET) {
            ReleaseUiCom(gDcTarget);
            CreateDcTarget();
        }
    }
    DrawEmojiFallback(dc, bounds, glyph, fallbackColor);
    return true;
}

void ShutdownUiEmojiRenderer() {
    if (gFallbackFont) { DeleteObject(gFallbackFont); gFallbackFont = nullptr; }
    ReleaseUiCom(gDcTarget);
    ReleaseUiCom(gEmojiFormat);
    ReleaseUiCom(gWriteFactory);
    ReleaseUiCom(gD2dFactory);
    gTargetDpi = 96;
}

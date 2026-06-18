// MinGW 下 objidl.h 必须在 gdiplus.h 之前,提供 PROPID
#include <windows.h>
#include <objidl.h>
#include "graphics_utils.h"
#include <algorithm>

HBITMAP CreateMenuIconBitmap(const wchar_t* iconChar, COLORREF color,
                             int verticalPadding) {
    const int width = 16;
    const int height = 16 + std::max(0, verticalPadding) * 2;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    Gdiplus::Graphics graphics(hdcMem);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    COLORREF menuBg = GetSysColor(COLOR_MENU);
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, GetRValue(menuBg), GetGValue(menuBg), GetBValue(menuBg)));
    graphics.FillRectangle(&bgBrush, 0, 0, width, height);

    Gdiplus::FontFamily fontFamily(L"Segoe MDL2 Assets");
    Gdiplus::Font font(&fontFamily, 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));

    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::RectF rect(0, 0, (float)width, (float)height);
    graphics.DrawString(iconChar, 1, &font, rect, &format, &textBrush);

    SelectObject(hdcMem, hOldBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    return hBitmap;
}

HBITMAP CreateMenuColorBitmap(COLORREF color) {
    const int width = 16;
    const int height = 16;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    Gdiplus::Graphics graphics(hdcMem);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // 用菜单背景色填充（避免透明残留）
    COLORREF menuBg = GetSysColor(COLOR_MENU);
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, GetRValue(menuBg), GetGValue(menuBg), GetBValue(menuBg)));
    graphics.FillRectangle(&bgBrush, 0, 0, width, height);

    // 绘制圆角颜色方块
    Gdiplus::GraphicsPath colorPath;
    CreateRoundRectPath(&colorPath, 1, 1, width - 2, height - 2, 3);
    Gdiplus::SolidBrush colorBrush(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    graphics.FillPath(&colorBrush, &colorPath);

    SelectObject(hdcMem, hOldBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    return hBitmap;
}

void CreateRoundRectPath(Gdiplus::GraphicsPath* path, int x, int y, int width, int height, int radius) {
    path->AddArc(x, y, radius * 2, radius * 2, 180, 90);
    path->AddArc(x + width - radius * 2, y, radius * 2, radius * 2, 270, 90);
    path->AddArc(x + width - radius * 2, y + height - radius * 2, radius * 2, radius * 2, 0, 90);
    path->AddArc(x, y + height - radius * 2, radius * 2, radius * 2, 90, 90);
    path->CloseFigure();
}

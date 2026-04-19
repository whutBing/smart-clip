#include "graphics_utils.h"

HBITMAP CreateMenuIconBitmap(const wchar_t* iconChar, COLORREF color) {
    const int size = 16;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
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
    graphics.FillRectangle(&bgBrush, 0, 0, size, size);

    Gdiplus::FontFamily fontFamily(L"Segoe MDL2 Assets");
    Gdiplus::Font font(&fontFamily, 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));

    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::RectF rect(0, 0, (float)size, (float)size);
    graphics.DrawString(iconChar, 1, &font, rect, &format, &textBrush);

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

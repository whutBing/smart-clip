// MinGW 下 objidl.h 必须在 gdiplus.h 之前,提供 PROPID
#include <objidl.h>
#include "graphics_utils.h"
#include <algorithm>
#include <shlobj.h>
#include <windows.h>

typedef UINT(WINAPI *GetDpiForWindowProc)(HWND);

static bool GetMonitorInfoForWindow(HWND hwnd, MONITORINFOEXW *mi) {
    if (!mi)
        return false;

    HMONITOR monitor = NULL;
    if (hwnd)
        monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor)
        monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (!monitor)
        return false;

    mi->cbSize = sizeof(MONITORINFOEXW);
    return GetMonitorInfoW(monitor, mi) != FALSE;
}

static void GetMonitorPixelSize(const MONITORINFOEXW &mi, int *width,
                                int *height) {
    if (!width || !height)
        return;

    *width = std::max(0, (int)(mi.rcMonitor.right - mi.rcMonitor.left));
    *height = std::max(0, (int)(mi.rcMonitor.bottom - mi.rcMonitor.top));

    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) &&
        dm.dmPelsWidth > 0 && dm.dmPelsHeight > 0) {
        *width = std::max(*width, (int)dm.dmPelsWidth);
        *height = std::max(*height, (int)dm.dmPelsHeight);
    }
}

static UINT GetNativeDpiForWindow(HWND hwnd) {
    static GetDpiForWindowProc s_getDpiForWindow = NULL;
    static bool s_loaded = false;
    if (!s_loaded) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
            s_getDpiForWindow =
                (GetDpiForWindowProc)GetProcAddress(user32, "GetDpiForWindow");
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        }
        s_loaded = true;
    }
    if (s_getDpiForWindow && hwnd) {
        UINT dpi = s_getDpiForWindow(hwnd);
        if (dpi > 0)
            return dpi;
    }

    HWND dcWindow = hwnd;
    HDC hdc = GetDC(dcWindow);
    if (!hdc) {
        dcWindow = NULL;
        hdc = GetDC(NULL);
    }
    UINT dpi = hdc ? (UINT)GetDeviceCaps(hdc, LOGPIXELSX) : 96;
    if (hdc)
        ReleaseDC(dcWindow, hdc);
    return dpi > 0 ? dpi : 96;
}

UINT GetSmartClipUiDpi(HWND hwnd) {
    UINT dpi = GetNativeDpiForWindow(hwnd);

    RECT rcWork = {};
    MONITORINFOEXW mi = {};
    int monitorW = 0;
    int monitorH = 0;
    if (GetMonitorInfoForWindow(hwnd, &mi)) {
        rcWork = mi.rcWork;
        GetMonitorPixelSize(mi, &monitorW, &monitorH);
    } else {
        rcWork = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        monitorW = rcWork.right - rcWork.left;
        monitorH = rcWork.bottom - rcWork.top;
    }

    int workW = rcWork.right - rcWork.left;
    int workH = rcWork.bottom - rcWork.top;
    int effectiveW = std::max(workW, monitorW);
    int effectiveH = std::max(workH, monitorH);

    if (effectiveW >= 3800 || effectiveH >= 2000) {
        dpi = std::max<UINT>(dpi, 192);
    } else if (effectiveW >= 2500 || effectiveH >= 1400) {
        dpi = std::max<UINT>(dpi, 144);
    }
    return dpi;
}

int ScaleForDpi(int value, UINT dpi) {
    return MulDiv(value, (int)dpi, 96);
}

int ScaleForWindowDpi(HWND hwnd, int value) {
    return ScaleForDpi(value, GetSmartClipUiDpi(hwnd));
}

HBITMAP CreateMenuIconBitmap(const wchar_t* iconChar, COLORREF color,
                             int verticalPadding) {
    UINT dpi = GetSmartClipUiDpi(NULL);
    const int width = ScaleForDpi(16, dpi);
    const int height =
        ScaleForDpi(16 + std::max(0, verticalPadding) * 2, dpi);

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
    Gdiplus::Font font(&fontFamily, (Gdiplus::REAL)ScaleForDpi(11, dpi),
                       Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
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
    UINT dpi = GetSmartClipUiDpi(NULL);
    const int width = ScaleForDpi(16, dpi);
    const int height = ScaleForDpi(16, dpi);

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
    int inset = std::max(1, ScaleForDpi(1, dpi));
    CreateRoundRectPath(&colorPath, inset, inset, width - inset * 2,
                        height - inset * 2, ScaleForDpi(3, dpi));
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

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

UINT GetWindowDpi(HWND hwnd) {
    return GetNativeDpiForWindow(hwnd);
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

// ==================== Direct2D + DirectWrite：彩色 emoji 渲染 ====================

#include <d2d1.h>
#include <dwrite.h>

// MinGW 旧版 d2d1.h 可能未定义此选项；手动补全
#ifndef D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
#define D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT 0x00000004
#endif

static ID2D1Factory *g_pD2DFactory = nullptr;
static IDWriteFactory *g_pDWriteFactory = nullptr;
static ID2D1DCRenderTarget *g_pDCRT = nullptr;
static ID2D1SolidColorBrush *g_pSolidBrush = nullptr;

static bool EnsureDirectWriteResources() {
    if (!g_pD2DFactory) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                          __uuidof(ID2D1Factory),
                          reinterpret_cast<void **>(&g_pD2DFactory));
    }
    if (!g_pDWriteFactory) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                            __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown **>(&g_pDWriteFactory));
    }
    if (!g_pDCRT && g_pD2DFactory) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED),
            0, 0);
        g_pD2DFactory->CreateDCRenderTarget(&props, &g_pDCRT);
    }
    if (!g_pSolidBrush && g_pDCRT) {
        g_pDCRT->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::Black, 1.0f), &g_pSolidBrush);
    }
    return g_pD2DFactory && g_pDWriteFactory && g_pDCRT && g_pSolidBrush;
}

bool TextContainsEmoji(const wchar_t *text, int len) {
    if (!text) return false;
    if (len < 0) len = (int)wcslen(text);
    for (int i = 0; i < len; ++i) {
        wchar_t c = text[i];
        // 代理对（非 BMP 字符，如 😀 等高码位 emoji，0x1F300+）
        if (c >= 0xD800 && c <= 0xDBFF) return true;
        // BMP 内常见 emoji 范围
        if (c >= 0x2600 && c <= 0x27BF) return true;  // Misc Symbols + Dingbats
        if (c >= 0x2300 && c <= 0x23FF) return true;  // Misc Technical
        if (c >= 0x25A0 && c <= 0x25FF) return true;  // Geometric Shapes
        if (c >= 0x2B00 && c <= 0x2BFF) return true;  // Misc Symbols & Arrows
        if (c >= 0x2190 && c <= 0x21FF) return true;  // Arrows
    }
    return false;
}

void DrawTextWithColorEmoji(HDC hdc, const wchar_t *text, int textLen,
                            const RECT &rcText,
                            HFONT referenceFont,
                            const wchar_t *fontFamily,
                            int fontWeight, COLORREF textColor,
                            int align, bool verticalCenter,
                            bool endEllipsis, float emojiScale) {
    if (!hdc || !text || !*text) return;
    if (!EnsureDirectWriteResources()) return;

    if (textLen < 0) textLen = (int)wcslen(text);
    if (textLen == 0) return;

    // 通过参考字体的 GDI TextMetrics 换算 DirectWrite em size：
    // GDI lfHeight 是 cell height（含 internal leading），而 DirectWrite
    // fontSize 是 em size（ascent + descent）。两者不一致会导致带 emoji
    // 的行渲染文字过大。em size = tmHeight - tmInternalLeading。
    float fontSize = 0.0f;
    if (referenceFont) {
        HFONT oldFont = (HFONT)SelectObject(hdc, referenceFont);
        TEXTMETRICW tm = {};
        if (GetTextMetricsW(hdc, &tm) && tm.tmHeight > 0) {
            int em = tm.tmHeight - tm.tmInternalLeading;
            if (em <= 0) em = tm.tmHeight;
            fontSize = (float)em;
        }
        SelectObject(hdc, oldFont);
    }
    if (fontSize <= 0.0f) {
        // 极端 fallback（理论上不会走到）
        fontSize = 16.0f;
    }
    // emoji 默认按 em box 满框绘制，视觉上比中文字大。对 emoji 字符
    // 单独设置较小的字号，文字保持原 em size 不变。
    float emojiFontSize = fontSize * emojiScale;

    // 绑定 HDC 的目标矩形到 DC Render Target（坐标系变为该矩形内局部坐标）
    RECT rcBind = rcText;
    if (FAILED(g_pDCRT->BindDC(hdc, &rcBind))) return;

    // 创建 TextFormat（默认字号用于文字部分）
    IDWriteTextFormat *pFormat = nullptr;
    if (FAILED(g_pDWriteFactory->CreateTextFormat(
            fontFamily, nullptr, (DWRITE_FONT_WEIGHT)fontWeight,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize, L"",
            &pFormat)) ||
        !pFormat) {
        return;
    }

    pFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TEXT_ALIGNMENT ta = DWRITE_TEXT_ALIGNMENT_LEADING;
    if (align == 1) ta = DWRITE_TEXT_ALIGNMENT_TRAILING;
    else if (align == 2) ta = DWRITE_TEXT_ALIGNMENT_CENTER;
    pFormat->SetTextAlignment(ta);
    pFormat->SetParagraphAlignment(verticalCenter
                                       ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                       : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    // 创建 TextLayout（宽高用 rcText 的逻辑尺寸）
    float width = (float)(rcText.right - rcText.left);
    float height = (float)(rcText.bottom - rcText.top);
    if (width <= 0 || height <= 0) {
        pFormat->Release();
        return;
    }
    IDWriteTextLayout *pLayout = nullptr;
    if (FAILED(g_pDWriteFactory->CreateTextLayout(text, (UINT32)textLen, pFormat,
                                                  width, height, &pLayout)) ||
        !pLayout) {
        pFormat->Release();
        return;
    }

    // 字符级省略号截断：MinGW 旧版 dwrite.h 缺少 CreateEllipsisTrimming，
    // 这里通过 SetTrimming + nullptr ellipsis 实现"硬截断"，
    // 然后用 GetMetrics 测量后判断是否需要追加省略号。
    if (endEllipsis) {
        DWRITE_TRIMMING trimming = {DWRITE_TRIMMING_GRANULARITY_CHARACTER,
                                     0, 0};
        pLayout->SetTrimming(&trimming, nullptr);
    }

    // 对每个 emoji 字符单独设置较小的字号，让 emoji 视觉上与文字匹配。
    // 不影响文字字符的大小（文字仍用 TextFormat 默认的 fontSize）。
    // 同时把紧跟 emoji 的变体选择符(U+FE0F/U+FE0E)与 ZWJ 序列(U+200D+字符)
    // 一并纳入同一 range，否则这些零宽修饰符会被当作独立文字字符渲染成
    // tofu(口字形)。例如 "⏸️" = U+23F8 + U+FE0F，"👨‍👩‍👧" 含 U+200D。
    for (int i = 0; i < textLen; ++i) {
        wchar_t c = text[i];
        bool isHighSurrogate = (c >= 0xD800 && c <= 0xDBFF);
        bool isEmojiBmp = (c >= 0x2600 && c <= 0x27BF) ||
                          (c >= 0x2300 && c <= 0x23FF) ||
                          (c >= 0x25A0 && c <= 0x25FF) ||
                          (c >= 0x2B00 && c <= 0x2BFF) ||
                          (c >= 0x2190 && c <= 0x21FF);
        if (!isHighSurrogate && !isEmojiBmp)
            continue;

        int start = i;
        int j = i;
        // 首个 emoji 字符：代理对占 2 个 wchar_t
        if (isHighSurrogate && j + 1 < textLen)
            j += 1;
        // 向后吞掉变体选择符与 ZWJ 序列，整体作为同一 emoji 簇
        while (j + 1 < textLen) {
            wchar_t next = text[j + 1];
            if (next == 0xFE0F || next == 0xFE0E) {
                j += 1;
                continue;
            }
            if (next == 0x200D && j + 2 < textLen) {
                j += 2;  // 跳过 ZWJ 及其后的一个字符
                if (text[j] >= 0xD800 && text[j] <= 0xDBFF && j + 1 < textLen)
                    j += 1;  // 该字符是高代理，再跳低位代理
                continue;
            }
            break;
        }

        DWRITE_TEXT_RANGE range = {(UINT32)start, (UINT32)(j - start + 1)};
        pLayout->SetFontSize(emojiFontSize, range);
        i = j;  // 跳过已纳入 range 的字符
    }

    pFormat->Release();

    // 设置画刷颜色
    g_pSolidBrush->SetColor(D2D1::ColorF(textColor, 1.0f));

    // 绘制（启用 color font，emoji 部分会自动 fallback 到 Segoe UI Emoji 渲染彩色）
    g_pDCRT->BeginDraw();
    g_pDCRT->DrawTextLayout(
        D2D1::Point2F(0, 0), pLayout, g_pSolidBrush,
        (D2D1_DRAW_TEXT_OPTIONS)D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    HRESULT endHr = g_pDCRT->EndDraw();

    pLayout->Release();

    // 设备丢失时重建 DCRT，避免后续绘制持续失败
    if (endHr == (HRESULT)D2DERR_RECREATE_TARGET) {
        if (g_pSolidBrush) { g_pSolidBrush->Release(); g_pSolidBrush = nullptr; }
        if (g_pDCRT) { g_pDCRT->Release(); g_pDCRT = nullptr; }
    }
}

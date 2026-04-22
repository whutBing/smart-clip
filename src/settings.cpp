#include "settings.h"
#include "hotkey.h"
#include "history.h"
#include "tray.h"
#include "graphics_utils.h"
#include "resource.h"
#include <commctrl.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <shlobj.h>

extern HWND g_hwndMain;

// 主窗口控件ID（用于刷新）
#define ID_SEARCH_BOX 104
#define ID_TOPMOST_BUTTON 1006

// ==================== 布局常量 ====================
#define SETTINGS_WIDTH 600
#define SETTINGS_HEIGHT 450
#define SETTINGS_TITLEBAR_H 36
#define SIDEBAR_W 180
#define CONTENT_PADDING 24
#define ROW_HEIGHT 60
#define TOGGLE_W 44
#define TOGGLE_H 24
#define TOGGLE_THUMB_R 9
#define SIDEBAR_ITEM_H 44
#define CATEGORY_HEADER_H 56

// ==================== 颜色辅助 ====================
inline COLORREF GetSettingsBgColor() { return g_isDarkMode ? RGB(32, 32, 36) : RGB(248, 248, 248); }
inline COLORREF GetSidebarBgColor() { return g_isDarkMode ? RGB(28, 28, 32) : RGB(238, 238, 238); }
inline COLORREF GetSidebarHoverColor() { return g_isDarkMode ? RGB(42, 42, 46) : RGB(225, 225, 225); }
inline COLORREF GetDescTextColor() { return g_isDarkMode ? RGB(140, 140, 145) : RGB(130, 130, 130); }
inline COLORREF GetSeparatorColor() { return g_isDarkMode ? RGB(55, 55, 58) : RGB(225, 225, 225); }
inline COLORREF GetToggleOffColor() { return g_isDarkMode ? RGB(85, 85, 85) : RGB(190, 190, 190); }
inline COLORREF GetSettingsTextColor() { return g_isDarkMode ? RGB(226, 222, 226) : RGB(60, 60, 60); }
inline COLORREF GetSettingsEditBg() { return g_isDarkMode ? RGB(46, 46, 48) : RGB(255, 255, 255); }
inline COLORREF GetTitlebarBgColor() { return g_isDarkMode ? RGB(24, 24, 28) : RGB(245, 245, 245); }
#define COLOR_ACCENT RGB(0, 120, 215)

// ==================== 全局变量 ====================
bool g_isStartupEnabled = false;
bool g_isSettingsDialogOpen = false;
bool g_isNotificationEnabled = false;
bool g_isSmoothScrollEnabled = true;
ImagePreviewQuality g_imagePreviewQuality = PREVIEW_HD;
std::wstring g_fontName = L"Microsoft YaHei";
int g_fontSize = 16;
int g_fontWeight = FW_NORMAL;
BYTE g_fontItalic = FALSE;
HWND g_hwndSettingsDlg = NULL;

// PLACEHOLDER_SETTINGS_PART2

// 设置对话框内部状态
static int g_currentSettingsTab = 0;
static int g_settingsHoverSidebar = -1;
static bool g_isRecordingHotkey = false;
static bool g_isRecordingSearchHotkey = false;
static WNDPROC g_oldEditProc = NULL;
static WNDPROC g_oldSearchEditProc = NULL;
static bool g_settingsClassRegistered = false;

// 控件句柄
static HWND g_hwndSettingsClose = NULL;
static HWND g_hwndToggleStartup = NULL;
static HWND g_hwndToggleNotification = NULL;
static HWND g_hwndToggleSmoothScroll = NULL;
static HWND g_hwndThemeCombo = NULL;
static HWND g_hwndImagePreviewCombo = NULL;
static HWND g_hwndHotkeyEdit = NULL;
static HWND g_hwndSearchHotkeyEdit = NULL;
static HWND g_hwndToggleQuickPaste = NULL;
static HWND g_hwndQuickPasteCombo = NULL;
static HWND g_hwndToggleCollapse = NULL;
static HWND g_hwndHistoryLimitEdit = NULL;

// === 数据分类控件 ===
static HWND g_hwndOpenDataBtn = NULL;
static HWND g_hwndSetDataDirBtn = NULL;
static HWND g_hwndClearNonFavBtn = NULL;
static std::wstring g_dataSizeText = L"计算中...";

// GDI 资源（WM_CREATE 创建，WM_DESTROY 释放）
static HFONT g_hTitleFont = NULL;
static HFONT g_hDescFont = NULL;
static HFONT g_hSidebarFont = NULL;
static HFONT g_hSidebarIconFont = NULL;
static HFONT g_hHeaderFont = NULL;
static HFONT g_hHeaderDescFont = NULL;
static HFONT g_hCloseIconFont = NULL;
static HBRUSH g_hSettingsBgBrush = NULL;
static HBRUSH g_hEditBgBrush = NULL;

// ==================== 辅助函数 ====================

void ToggleStartup() {
    g_isStartupEnabled = !g_isStartupEnabled;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_WRITE | KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        if (g_isStartupEnabled) {
            WCHAR szPath[MAX_PATH];
            GetModuleFileNameW(NULL, szPath, MAX_PATH);
            RegSetValueExW(hKey, L"SmartClip", 0, REG_SZ, (LPBYTE)szPath, (wcslen(szPath) + 1) * sizeof(wchar_t));
        } else {
            RegDeleteValueW(hKey, L"SmartClip");
        }
        RegCloseKey(hKey);
    }
}

bool CheckStartup() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwType = REG_SZ, dwSize = 0;
        bool exists = (RegQueryValueExW(hKey, L"SmartClip", NULL, &dwType, NULL, &dwSize) == ERROR_SUCCESS);
        RegCloseKey(hKey);
        return exists;
    }
    return false;
}

// 获取设置行控件的 Y 坐标
static int GetRowY(int rowIndex) {
    return SETTINGS_TITLEBAR_H + CATEGORY_HEADER_H + rowIndex * ROW_HEIGHT;
}

// 获取控件右对齐 X 坐标
static int GetControlX(int controlWidth) {
    return SETTINGS_WIDTH - CONTENT_PADDING - controlWidth;
}

// PLACEHOLDER_SETTINGS_PART3

// ==================== 快捷键编辑框子类 ====================

LRESULT CALLBACK HotkeyEditProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (g_isRecordingHotkey) {
                UINT vk = (UINT)wParam;
                if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN)
                    return 0;
                UINT mod = 0;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mod |= MOD_CONTROL;
                if (GetAsyncKeyState(VK_MENU) & 0x8000) mod |= MOD_ALT;
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mod |= MOD_SHIFT;
                if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) mod |= MOD_WIN;
                g_hotkeyModifiers = mod;
                g_hotkeyVirtualKey = vk;
                wchar_t text[128] = L"";
                if (mod & MOD_CONTROL) wcscat_s(text, L"Ctrl+");
                if (mod & MOD_ALT) wcscat_s(text, L"Alt+");
                if (mod & MOD_SHIFT) wcscat_s(text, L"Shift+");
                if (mod & MOD_WIN) wcscat_s(text, L"Win+");
                wchar_t kn[32];
                if (GetKeyNameTextW(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16, kn, 32))
                    wcscat_s(text, kn);
                else wcscat_s(text, L"?");
                SetWindowTextW(hwnd, text);
                g_isHotkeyEnabled = true;
                SaveHotkeySettings();
                RegisterHotkey(g_hwndMain);
                ShowTrayBalloon(g_hwndMain, L"设置已更新", L"快捷键设置已保存");
                g_isRecordingHotkey = false;
                return 0;
            }
            break;
        }
        case WM_CHAR: case WM_SYSCHAR:
            if (g_isRecordingHotkey) return 0;
            break;
    }
    return CallWindowProcW(g_oldEditProc, hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK SearchHotkeyEditProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (g_isRecordingSearchHotkey) {
                UINT vk = (UINT)wParam;
                if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN)
                    return 0;
                UINT mod = 0;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mod |= MOD_CONTROL;
                if (GetAsyncKeyState(VK_MENU) & 0x8000) mod |= MOD_ALT;
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mod |= MOD_SHIFT;
                if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) mod |= MOD_WIN;
                g_searchHotkeyModifiers = mod;
                g_searchHotkeyVirtualKey = vk;
                wchar_t text[128] = L"";
                if (mod & MOD_CONTROL) wcscat_s(text, L"Ctrl+");
                if (mod & MOD_ALT) wcscat_s(text, L"Alt+");
                if (mod & MOD_SHIFT) wcscat_s(text, L"Shift+");
                if (mod & MOD_WIN) wcscat_s(text, L"Win+");
                wchar_t kn[32];
                if (GetKeyNameTextW(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16, kn, 32))
                    wcscat_s(text, kn);
                else wcscat_s(text, L"Unknown");
                SetWindowTextW(hwnd, text);
                g_isSearchHotkeyEnabled = true;
                SaveHotkeySettings();
                RegisterHotkey(g_hwndMain);
                g_isRecordingSearchHotkey = false;
                return 0;
            }
            break;
        }
        case WM_CHAR: case WM_SYSCHAR:
            if (g_isRecordingSearchHotkey) return 0;
            break;
    }
    return CallWindowProcW(g_oldSearchEditProc, hwnd, uMsg, wParam, lParam);
}

// ==================== 分类切换 ====================

static void SwitchSettingsTab(int tab) {
    g_currentSettingsTab = tab;
    int showGen = (tab == 0) ? SW_SHOW : SW_HIDE;
    int showHk  = (tab == 1) ? SW_SHOW : SW_HIDE;
    int showTs  = (tab == 2) ? SW_SHOW : SW_HIDE;
    int showDt  = (tab == 3) ? SW_SHOW : SW_HIDE;

    if (g_hwndToggleStartup) ShowWindow(g_hwndToggleStartup, showGen);
    if (g_hwndToggleNotification) ShowWindow(g_hwndToggleNotification, showGen);
    if (g_hwndToggleSmoothScroll) ShowWindow(g_hwndToggleSmoothScroll, showGen);
    if (g_hwndThemeCombo) ShowWindow(g_hwndThemeCombo, showGen);
    if (g_hwndImagePreviewCombo) ShowWindow(g_hwndImagePreviewCombo, showGen);

    if (g_hwndHotkeyEdit) ShowWindow(g_hwndHotkeyEdit, showHk);
    if (g_hwndSearchHotkeyEdit) ShowWindow(g_hwndSearchHotkeyEdit, showHk);
    if (g_hwndToggleQuickPaste) ShowWindow(g_hwndToggleQuickPaste, showHk);
    if (g_hwndQuickPasteCombo) ShowWindow(g_hwndQuickPasteCombo, showHk);

    if (g_hwndToggleCollapse) ShowWindow(g_hwndToggleCollapse, showTs);
    if (g_hwndHistoryLimitEdit) ShowWindow(g_hwndHistoryLimitEdit, showTs);

    if (g_hwndOpenDataBtn) ShowWindow(g_hwndOpenDataBtn, showDt);
    if (g_hwndSetDataDirBtn) ShowWindow(g_hwndSetDataDirBtn, showDt);
    if (g_hwndClearNonFavBtn) ShowWindow(g_hwndClearNonFavBtn, showDt);

    // 切换到数据页时刷新磁盘空间
    if (tab == 3) {
        g_dataSizeText = FormatFileSize(GetDataDirSize());
    }

    if (g_hwndSettingsDlg) InvalidateRect(g_hwndSettingsDlg, NULL, TRUE);
}

// PLACEHOLDER_SETTINGS_PART4

// ==================== 侧边栏数据 ====================
struct SidebarItem {
    const wchar_t* icon;
    const wchar_t* label;
};
static const SidebarItem g_sidebarItems[] = {
    { L"\uE713", L"通用" },
    { L"\uE765", L"快捷键" },
    { L"\uE8F1", L"中转站" },

    { L"", L"数据" },
};
#define SIDEBAR_COUNT 4

// 设置行数据
struct SettingRowInfo {
    const wchar_t* title;
    const wchar_t* desc;
};

static const SettingRowInfo g_generalRows[] = {
    { L"开机自启", L"开机时自动启动 Smart Clip" },
    { L"消息通知", L"操作时显示系统通知" },
    { L"平滑滚动", L"列表滚动时使用平滑动画" },
    { L"主题模式", L"切换日间、夜间或跟随系统" },
    { L"图片预览质量", L"设置剪贴板图片的预览清晰度" },
};
static const SettingRowInfo g_hotkeyRows[] = {
    { L"切换快捷键", L"显示/隐藏 Smart Clip 窗口" },
    { L"搜索框快捷键", L"聚焦搜索框的快捷键" },
    { L"快捷粘贴", L"使用修饰键+数字快速粘贴" },
    { L"快捷粘贴修饰键", L"选择快捷粘贴使用的修饰键" },
};
static const SettingRowInfo g_transitRows[] = {
    { L"用完收起", L"粘贴后自动收起中转站卡片" },
    { L"历史记录数量", L"最多保存的剪贴板记录条数" },
};
static const SettingRowInfo g_dataRows[] = {
    { L"数据目录", L"在资源管理器中打开数据目录" },
    { L"设置数据目录", L"自定义数据存储位置" },
    { L"清理非收藏数据", L"删除所有未收藏的历史记录" },
    { L"磁盘空间占用", L"数据占用的磁盘空间" },
};

// 分类标题
struct CategoryHeader {
    const wchar_t* title;
    const wchar_t* desc;
    const SettingRowInfo* rows;
    int rowCount;
};
static const CategoryHeader g_categories[] = {
    { L"通用", L"基本设置和外观", g_generalRows, 5 },
    { L"快捷键", L"快捷键配置", g_hotkeyRows, 4 },
    { L"中转站", L"中转站行为设置", g_transitRows, 2 },
    { L"数据", L"数据存储与管理", g_dataRows, 4 },
};

// ==================== 绘制辅助 ====================

static void DrawToggleSwitch(HDC hdc, RECT rc, bool isOn) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    COLORREF pillColor = isOn ? COLOR_ACCENT : GetToggleOffColor();
    Gdiplus::GraphicsPath pillPath;
    int radius = TOGGLE_H / 2;
    CreateRoundRectPath(&pillPath, rc.left, rc.top, TOGGLE_W, TOGGLE_H, radius);
    Gdiplus::SolidBrush pillBrush(Gdiplus::Color(255, GetRValue(pillColor), GetGValue(pillColor), GetBValue(pillColor)));
    g.FillPath(&pillBrush, &pillPath);

    int thumbX = isOn ? (rc.left + TOGGLE_W - radius) : (rc.left + radius);
    int thumbY = rc.top + radius;
    Gdiplus::SolidBrush thumbBrush(Gdiplus::Color(255, 255, 255, 255));
    g.FillEllipse(&thumbBrush, thumbX - TOGGLE_THUMB_R, thumbY - TOGGLE_THUMB_R,
                  TOGGLE_THUMB_R * 2, TOGGLE_THUMB_R * 2);
}

static void UpdateSettingsBrushes() {
    if (g_hSettingsBgBrush) DeleteObject(g_hSettingsBgBrush);
    g_hSettingsBgBrush = CreateSolidBrush(GetSettingsBgColor());
    if (g_hEditBgBrush) DeleteObject(g_hEditBgBrush);
    g_hEditBgBrush = CreateSolidBrush(GetSettingsEditBg());
}

// ==================== 自定义下拉选择器 ====================

#define DROPDOWN_ITEM_H 34
#define DROPDOWN_PADDING 6
#define DROPDOWN_RADIUS 8

struct DropdownInfo {
    int ctlId;
    const wchar_t** items;
    int itemCount;
    int selectedIndex;
};

static HWND g_hwndDropdownPopup = NULL;
static DropdownInfo g_activeDropdown = {};
static int g_dropdownHoverIndex = -1;
static bool g_dropdownClassRegistered = false;

static const wchar_t* g_themeItems[] = { L"日间", L"夜间", L"跟随系统" };
static const wchar_t* g_previewItems[] = { L"关闭", L"模糊", L"标清", L"高清" };
static const wchar_t* g_quickPasteItems[] = { L"Alt", L"Ctrl", L"Shift", L"Ctrl+Alt", L"Ctrl+Shift", L"Alt+Shift" };

// 获取下拉按钮当前显示文字
static const wchar_t* GetDropdownText(int ctlId) {
    if (ctlId == IDC_THEME_COMBO) return g_themeItems[(int)g_themeMode];
    if (ctlId == IDC_IMAGE_PREVIEW_COMBO) return g_previewItems[(int)g_imagePreviewQuality];
    if (ctlId == IDC_QUICK_PASTE_COMBO) {
        if (g_quickPasteModifiers == MOD_ALT) return g_quickPasteItems[0];
        if (g_quickPasteModifiers == MOD_CONTROL) return g_quickPasteItems[1];
        if (g_quickPasteModifiers == MOD_SHIFT) return g_quickPasteItems[2];
        if (g_quickPasteModifiers == (MOD_CONTROL | MOD_ALT)) return g_quickPasteItems[3];
        if (g_quickPasteModifiers == (MOD_CONTROL | MOD_SHIFT)) return g_quickPasteItems[4];
        if (g_quickPasteModifiers == (MOD_ALT | MOD_SHIFT)) return g_quickPasteItems[5];
        return g_quickPasteItems[0];
    }
    return L"";
}

static int GetDropdownSelectedIndex(int ctlId) {
    if (ctlId == IDC_THEME_COMBO) return (int)g_themeMode;
    if (ctlId == IDC_IMAGE_PREVIEW_COMBO) return (int)g_imagePreviewQuality;
    if (ctlId == IDC_QUICK_PASTE_COMBO) {
        if (g_quickPasteModifiers == MOD_ALT) return 0;
        if (g_quickPasteModifiers == MOD_CONTROL) return 1;
        if (g_quickPasteModifiers == MOD_SHIFT) return 2;
        if (g_quickPasteModifiers == (MOD_CONTROL | MOD_ALT)) return 3;
        if (g_quickPasteModifiers == (MOD_CONTROL | MOD_SHIFT)) return 4;
        if (g_quickPasteModifiers == (MOD_ALT | MOD_SHIFT)) return 5;
        return 0;
    }
    return 0;
}

// 绘制下拉按钮（BS_OWNERDRAW）
static void DrawDropdownButton(HDC hdc, RECT rc, int ctlId) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // 圆角背景
    COLORREF bg = GetSettingsEditBg();
    Gdiplus::GraphicsPath path;
    CreateRoundRectPath(&path, 0, 0, rc.right - rc.left, rc.bottom - rc.top, DROPDOWN_RADIUS);
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
    g.FillPath(&bgBrush, &path);

    // 文字
    const wchar_t* text = GetDropdownText(ctlId);
    COLORREF tc = GetSettingsTextColor();
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, GetRValue(tc), GetGValue(tc), GetBValue(tc)));
    Gdiplus::Font font(L"Microsoft YaHei", 10.0f);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentNear);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::RectF textRect(10.0f, 0, (float)(rc.right - rc.left - 30), (float)(rc.bottom - rc.top));
    g.DrawString(text, -1, &font, textRect, &sf, &textBrush);

    // 下拉箭头
    Gdiplus::Font iconFont(L"Segoe MDL2 Assets", 8.0f);
    Gdiplus::StringFormat iconSf;
    iconSf.SetAlignment(Gdiplus::StringAlignmentCenter);
    iconSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    COLORREF ac = RGB(150, 150, 150);
    Gdiplus::SolidBrush arrowBrush(Gdiplus::Color(255, GetRValue(ac), GetGValue(ac), GetBValue(ac)));
    Gdiplus::RectF arrowRect((float)(rc.right - rc.left - 24), 0, 20.0f, (float)(rc.bottom - rc.top));
    g.DrawString(L"\uE70D", -1, &iconFont, arrowRect, &iconSf, &arrowBrush);
}

// 弹出下拉窗口过程
LRESULT CALLBACK DropdownPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        // 背景
        COLORREF bg = GetSettingsEditBg();
        Gdiplus::GraphicsPath bgPath;
        CreateRoundRectPath(&bgPath, 0, 0, rcClient.right, rcClient.bottom, DROPDOWN_RADIUS);
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
        g.FillPath(&bgBrush, &bgPath);

        // 边框
        COLORREF bc = GetSeparatorColor();
        Gdiplus::Pen borderPen(Gdiplus::Color(255, GetRValue(bc), GetGValue(bc), GetBValue(bc)), 1.0f);
        g.DrawPath(&borderPen, &bgPath);

        Gdiplus::Font font(L"Microsoft YaHei", 10.0f);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentNear);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        for (int i = 0; i < g_activeDropdown.itemCount; i++) {
            int y = DROPDOWN_PADDING + i * DROPDOWN_ITEM_H;
            Gdiplus::RectF itemRect((float)DROPDOWN_PADDING, (float)y,
                (float)(rcClient.right - DROPDOWN_PADDING * 2), (float)DROPDOWN_ITEM_H);

            // 悬浮高亮
            if (i == g_dropdownHoverIndex) {
                COLORREF hc = g_isDarkMode ? RGB(55, 55, 60) : RGB(230, 230, 230);
                Gdiplus::GraphicsPath hoverPath;
                CreateRoundRectPath(&hoverPath, (int)itemRect.X, (int)itemRect.Y,
                    (int)itemRect.Width, (int)itemRect.Height, 6);
                Gdiplus::SolidBrush hoverBrush(Gdiplus::Color(255, GetRValue(hc), GetGValue(hc), GetBValue(hc)));
                g.FillPath(&hoverBrush, &hoverPath);
            }

            // 选中标记
            bool selected = (i == g_activeDropdown.selectedIndex);
            if (selected) {
                Gdiplus::SolidBrush checkBrush(Gdiplus::Color(255, 0, 120, 215));
                g.FillEllipse(&checkBrush, (float)(DROPDOWN_PADDING + 6), (float)(y + DROPDOWN_ITEM_H / 2 - 4), 8.0f, 8.0f);
            }

            // 文字
            COLORREF tc = GetSettingsTextColor();
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, GetRValue(tc), GetGValue(tc), GetBValue(tc)));
            Gdiplus::RectF textRect((float)(DROPDOWN_PADDING + 22), (float)y,
                (float)(rcClient.right - DROPDOWN_PADDING * 2 - 22), (float)DROPDOWN_ITEM_H);
            g.DrawString(g_activeDropdown.items[i], -1, &font, textRect, &sf, &textBrush);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int y = GET_Y_LPARAM(lParam);
        int idx = (y - DROPDOWN_PADDING) / DROPDOWN_ITEM_H;
        if (idx < 0 || idx >= g_activeDropdown.itemCount) idx = -1;
        if (idx != g_dropdownHoverIndex) {
            g_dropdownHoverIndex = idx;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        g_dropdownHoverIndex = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        int y = GET_Y_LPARAM(lParam);
        int idx = (y - DROPDOWN_PADDING) / DROPDOWN_ITEM_H;
        if (idx >= 0 && idx < g_activeDropdown.itemCount) {
            g_activeDropdown.selectedIndex = idx;
            // 通知设置窗口
            PostMessageW(g_hwndSettingsDlg, WM_COMMAND, MAKEWPARAM(g_activeDropdown.ctlId, CBN_SELCHANGE), 0);
        }
        DestroyWindow(hwnd);
        g_hwndDropdownPopup = NULL;
        return 0;
    }

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            DestroyWindow(hwnd);
            g_hwndDropdownPopup = NULL;
        }
        return 0;

    case WM_DESTROY:
        g_hwndDropdownPopup = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void RegisterDropdownClass() {
    if (g_dropdownClassRegistered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wc.lpfnWndProc = DropdownPopupProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"SmartClipDropdown";
    RegisterClassExW(&wc);
    g_dropdownClassRegistered = true;
}

static void ShowDropdownPopup(HWND hwndBtn, int ctlId) {
    if (g_hwndDropdownPopup) {
        DestroyWindow(g_hwndDropdownPopup);
        g_hwndDropdownPopup = NULL;
    }

    g_activeDropdown.ctlId = ctlId;
    if (ctlId == IDC_THEME_COMBO) {
        g_activeDropdown.items = g_themeItems;
        g_activeDropdown.itemCount = 3;
    } else if (ctlId == IDC_IMAGE_PREVIEW_COMBO) {
        g_activeDropdown.items = g_previewItems;
        g_activeDropdown.itemCount = 4;
    } else if (ctlId == IDC_QUICK_PASTE_COMBO) {
        g_activeDropdown.items = g_quickPasteItems;
        g_activeDropdown.itemCount = 6;
    }
    g_activeDropdown.selectedIndex = GetDropdownSelectedIndex(ctlId);
    g_dropdownHoverIndex = -1;

    RegisterDropdownClass();

    RECT rcBtn;
    GetWindowRect(hwndBtn, &rcBtn);
    int popupW = rcBtn.right - rcBtn.left;
    if (popupW < 130) popupW = 130;
    int popupH = DROPDOWN_PADDING * 2 + g_activeDropdown.itemCount * DROPDOWN_ITEM_H;

    g_hwndDropdownPopup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"SmartClipDropdown", NULL, WS_POPUP,
        rcBtn.left, rcBtn.bottom + 2, popupW, popupH,
        g_hwndSettingsDlg, NULL, GetModuleHandleW(NULL), NULL);

    // 圆角区域
    HRGN hRgn = CreateRoundRectRgn(0, 0, popupW + 1, popupH + 1, DROPDOWN_RADIUS * 2, DROPDOWN_RADIUS * 2);
    SetWindowRgn(g_hwndDropdownPopup, hRgn, TRUE);

    ShowWindow(g_hwndDropdownPopup, SW_SHOW);
    UpdateWindow(g_hwndDropdownPopup);
}

// PLACEHOLDER_SETTINGS_PART5

// ==================== 窗口过程 ====================

LRESULT CALLBACK SettingsDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE: {
        g_hTitleFont = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        g_hDescFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        g_hSidebarFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        g_hSidebarIconFont = CreateFontW(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
        g_hHeaderFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        g_hHeaderDescFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        g_hCloseIconFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
        UpdateSettingsBrushes();
        return 0;
    }

    case WM_NCACTIVATE:
        return TRUE;

    case WM_NCCALCSIZE:
        if (wParam == TRUE) {
            NCCALCSIZE_PARAMS* p = (NCCALCSIZE_PARAMS*)lParam;
            p->rgrc[0].top += 1;
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
        if (hit == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            if (pt.y < SETTINGS_TITLEBAR_H) {
                if (g_hwndSettingsClose) {
                    RECT rc; GetWindowRect(g_hwndSettingsClose, &rc);
                    MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rc, 2);
                    if (PtInRect(&rc, pt)) return HTCLIENT;
                }
                return HTCAPTION;
            }
        }
        return hit;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        SetBkMode(hdc, TRANSPARENT);

        RECT rcClient;
        GetClientRect(hwnd, &rcClient);

        // 标题栏
        RECT rcTitlebar = { 0, 0, rcClient.right, SETTINGS_TITLEBAR_H };
        HBRUSH hTbBrush = CreateSolidBrush(GetTitlebarBgColor());
        FillRect(hdc, &rcTitlebar, hTbBrush);
        DeleteObject(hTbBrush);

        // 标题栏文字 "设置"
        HFONT hOld = (HFONT)SelectObject(hdc, g_hHeaderFont);
        SetTextColor(hdc, GetSettingsTextColor());
        RECT rcTitleText = { 16, 0, 200, SETTINGS_TITLEBAR_H };
        DrawTextW(hdc, L"设置", -1, &rcTitleText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // 侧边栏背景
        RECT rcSidebar = { 0, SETTINGS_TITLEBAR_H, SIDEBAR_W, rcClient.bottom };
        HBRUSH hSbBrush = CreateSolidBrush(GetSidebarBgColor());
        FillRect(hdc, &rcSidebar, hSbBrush);
        DeleteObject(hSbBrush);

        // 侧边栏项目
        for (int i = 0; i < SIDEBAR_COUNT; i++) {
            int itemY = SETTINGS_TITLEBAR_H + i * SIDEBAR_ITEM_H;
            RECT rcItem = { 0, itemY, SIDEBAR_W, itemY + SIDEBAR_ITEM_H };
            bool selected = (g_currentSettingsTab == i);
            bool hovered = (g_settingsHoverSidebar == i && !selected);

            if (selected) {
                HBRUSH hSelBrush = CreateSolidBrush(COLOR_ACCENT);
                FillRect(hdc, &rcItem, hSelBrush);
                DeleteObject(hSelBrush);
            } else if (hovered) {
                HBRUSH hHvBrush = CreateSolidBrush(GetSidebarHoverColor());
                FillRect(hdc, &rcItem, hHvBrush);
                DeleteObject(hHvBrush);
            }

            COLORREF textCol = selected ? RGB(255, 255, 255) : GetSettingsTextColor();
            SetTextColor(hdc, textCol);

            // 图标
            SelectObject(hdc, g_hSidebarIconFont);
            RECT rcIcon = { 20, itemY, 44, itemY + SIDEBAR_ITEM_H };
            DrawTextW(hdc, g_sidebarItems[i].icon, 1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // 文字
            SelectObject(hdc, g_hSidebarFont);
            RECT rcLabel = { 48, itemY, SIDEBAR_W - 8, itemY + SIDEBAR_ITEM_H };
            DrawTextW(hdc, g_sidebarItems[i].label, -1, &rcLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        // 内容区背景
        RECT rcContent = { SIDEBAR_W, SETTINGS_TITLEBAR_H, rcClient.right, rcClient.bottom };
        HBRUSH hCBrush = CreateSolidBrush(GetSettingsBgColor());
        FillRect(hdc, &rcContent, hCBrush);
        DeleteObject(hCBrush);

        // 分类标题
        const CategoryHeader& cat = g_categories[g_currentSettingsTab];
        int contentLeft = SIDEBAR_W + CONTENT_PADDING;
        int contentRight = rcClient.right - CONTENT_PADDING;

        SelectObject(hdc, g_hHeaderFont);
        SetTextColor(hdc, GetSettingsTextColor());
        RECT rcCatTitle = { contentLeft, SETTINGS_TITLEBAR_H + 10, contentRight, SETTINGS_TITLEBAR_H + 32 };
        DrawTextW(hdc, cat.title, -1, &rcCatTitle, DT_LEFT | DT_TOP | DT_SINGLELINE);

        SelectObject(hdc, g_hHeaderDescFont);
        SetTextColor(hdc, GetDescTextColor());
        RECT rcCatDesc = { contentLeft, SETTINGS_TITLEBAR_H + 34, contentRight, SETTINGS_TITLEBAR_H + 50 };
        DrawTextW(hdc, cat.desc, -1, &rcCatDesc, DT_LEFT | DT_TOP | DT_SINGLELINE);

        // 设置行
        for (int i = 0; i < cat.rowCount; i++) {
            int rowY = GetRowY(i);

            // 标题
            SelectObject(hdc, g_hTitleFont);
            SetTextColor(hdc, GetSettingsTextColor());
            RECT rcRowTitle = { contentLeft, rowY + 12, contentRight - 160, rowY + 30 };
            DrawTextW(hdc, cat.rows[i].title, -1, &rcRowTitle, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

            // 描述
            SelectObject(hdc, g_hDescFont);
            SetTextColor(hdc, GetDescTextColor());
            RECT rcRowDesc = { contentLeft, rowY + 32, contentRight - 160, rowY + 48 };
            DrawTextW(hdc, cat.rows[i].desc, -1, &rcRowDesc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

            // 分隔线
            if (i < cat.rowCount - 1) {
                HPEN hPen = CreatePen(PS_SOLID, 1, GetSeparatorColor());
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                MoveToEx(hdc, contentLeft, rowY + ROW_HEIGHT - 1, NULL);
                LineTo(hdc, contentRight, rowY + ROW_HEIGHT - 1);
                SelectObject(hdc, hOldPen);
                DeleteObject(hPen);
            }
        }

        // 数据分类：磁盘空间占用文字（第3行右侧）
        if (g_currentSettingsTab == 3) {
            int sizeRowY = GetRowY(3);
            SelectObject(hdc, g_hTitleFont);
            SetTextColor(hdc, COLOR_ACCENT);
            RECT rcSize = { contentRight - 150, sizeRowY + 12, contentRight, sizeRowY + 48 };
            DrawTextW(hdc, g_dataSizeText.c_str(), -1, &rcSize, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(hdc, hOld);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT lpDIS = (LPDRAWITEMSTRUCT)lParam;
        RECT rc = lpDIS->rcItem;

        // 关闭按钮
        if (lpDIS->CtlID == IDC_SETTINGS_CLOSE) {
            COLORREF bg = GetTitlebarBgColor();
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            RECT rcBtn; GetWindowRect(lpDIS->hwndItem, &rcBtn);
            MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rcBtn, 2);
            bool hover = PtInRect(&rcBtn, pt);
            if (hover) bg = RGB(232, 17, 35);
            HBRUSH hBr = CreateSolidBrush(bg);
            FillRect(lpDIS->hDC, &rc, hBr);
            DeleteObject(hBr);
            SetBkMode(lpDIS->hDC, TRANSPARENT);
            SetTextColor(lpDIS->hDC, hover ? RGB(255, 255, 255) : GetSettingsTextColor());
            HFONT hOldF = (HFONT)SelectObject(lpDIS->hDC, g_hCloseIconFont);
            DrawTextW(lpDIS->hDC, L"\uE8BB", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(lpDIS->hDC, hOldF);
            return TRUE;
        }

        // Toggle 开关
        if (lpDIS->CtlID == IDC_STARTUP_CHECK ||
            lpDIS->CtlID == IDC_NOTIFICATION_CHECK ||
            lpDIS->CtlID == IDC_SMOOTH_SCROLL_CHECK ||
            lpDIS->CtlID == IDC_QUICK_PASTE_CHECK ||
            lpDIS->CtlID == IDC_COLLAPSE_AFTER_PASTE_CHECK) {

            // 先填充背景
            HBRUSH hBgBr = CreateSolidBrush(GetSettingsBgColor());
            FillRect(lpDIS->hDC, &rc, hBgBr);
            DeleteObject(hBgBr);

            bool isOn = false;
            switch (lpDIS->CtlID) {
                case IDC_STARTUP_CHECK: isOn = g_isStartupEnabled; break;
                case IDC_NOTIFICATION_CHECK: isOn = g_isNotificationEnabled; break;
                case IDC_SMOOTH_SCROLL_CHECK: isOn = g_isSmoothScrollEnabled; break;
                case IDC_QUICK_PASTE_CHECK: isOn = g_isQuickPasteEnabled; break;
                case IDC_COLLAPSE_AFTER_PASTE_CHECK: isOn = g_isCollapseAfterPaste; break;
            }
            DrawToggleSwitch(lpDIS->hDC, rc, isOn);
            return TRUE;
        }

        // 下拉选择器按钮
        if (lpDIS->CtlID == IDC_THEME_COMBO ||
            lpDIS->CtlID == IDC_IMAGE_PREVIEW_COMBO ||
            lpDIS->CtlID == IDC_QUICK_PASTE_COMBO) {
            HBRUSH hBgBr = CreateSolidBrush(GetSettingsBgColor());
            FillRect(lpDIS->hDC, &rc, hBgBr);
            DeleteObject(hBgBr);
            DrawDropdownButton(lpDIS->hDC, rc, lpDIS->CtlID);
            return TRUE;
        }

        // iOS 风格操作按钮（蓝色圆角）
        if (lpDIS->CtlID == IDC_OPEN_DATA_FOLDER ||
            lpDIS->CtlID == IDC_SET_DATA_DIR ||
            lpDIS->CtlID == IDC_CLEAR_NON_FAV) {
            HBRUSH hBgBr = CreateSolidBrush(GetSettingsBgColor());
            FillRect(lpDIS->hDC, &rc, hBgBr);
            DeleteObject(hBgBr);

            Gdiplus::Graphics g(lpDIS->hDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            COLORREF btnColor = (lpDIS->CtlID == IDC_CLEAR_NON_FAV) ? RGB(220, 60, 60) : COLOR_ACCENT;
            Gdiplus::GraphicsPath btnPath;
            CreateRoundRectPath(&btnPath, 0, 0, rc.right - rc.left, rc.bottom - rc.top, 8);
            Gdiplus::SolidBrush btnBrush(Gdiplus::Color(255, GetRValue(btnColor), GetGValue(btnColor), GetBValue(btnColor)));
            g.FillPath(&btnBrush, &btnPath);

            const wchar_t* text = L"";
            if (lpDIS->CtlID == IDC_OPEN_DATA_FOLDER) text = L"打开";
            else if (lpDIS->CtlID == IDC_SET_DATA_DIR) text = L"选择";
            else if (lpDIS->CtlID == IDC_CLEAR_NON_FAV) text = L"清理";

            Gdiplus::Font font(L"Microsoft YaHei", 9.0f);
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::RectF textRect(0, 0, (float)(rc.right - rc.left), (float)(rc.bottom - rc.top));
            g.DrawString(text, -1, &font, textRect, &sf, &textBrush);
            return TRUE;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int newHover = -1;
        if (pt.x < SIDEBAR_W && pt.y >= SETTINGS_TITLEBAR_H) {
            int idx = (pt.y - SETTINGS_TITLEBAR_H) / SIDEBAR_ITEM_H;
            if (idx >= 0 && idx < SIDEBAR_COUNT) newHover = idx;
        }
        if (newHover != g_settingsHoverSidebar) {
            g_settingsHoverSidebar = newHover;
            RECT rcSb = { 0, SETTINGS_TITLEBAR_H, SIDEBAR_W, SETTINGS_TITLEBAR_H + SIDEBAR_COUNT * SIDEBAR_ITEM_H };
            InvalidateRect(hwnd, &rcSb, FALSE);
        }
        // 跟踪鼠标离开
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        break;
    }

    case WM_MOUSELEAVE:
        if (g_settingsHoverSidebar >= 0) {
            g_settingsHoverSidebar = -1;
            RECT rcSb = { 0, SETTINGS_TITLEBAR_H, SIDEBAR_W, SETTINGS_TITLEBAR_H + SIDEBAR_COUNT * SIDEBAR_ITEM_H };
            InvalidateRect(hwnd, &rcSb, FALSE);
        }
        break;

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (pt.x < SIDEBAR_W && pt.y >= SETTINGS_TITLEBAR_H) {
            int idx = (pt.y - SETTINGS_TITLEBAR_H) / SIDEBAR_ITEM_H;
            if (idx >= 0 && idx < SIDEBAR_COUNT && idx != g_currentSettingsTab) {
                SwitchSettingsTab(idx);
            }
        }
        break;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wParam;
        SetBkColor(hdcEdit, GetSettingsEditBg());
        SetTextColor(hdcEdit, GetSettingsTextColor());
        if (!g_hEditBgBrush) g_hEditBgBrush = CreateSolidBrush(GetSettingsEditBg());
        return (LRESULT)g_hEditBgBrush;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetBkColor(hdcStatic, GetSettingsBgColor());
        SetTextColor(hdcStatic, GetSettingsTextColor());
        if (!g_hSettingsBgBrush) g_hSettingsBgBrush = CreateSolidBrush(GetSettingsBgColor());
        return (LRESULT)g_hSettingsBgBrush;
    }

    // PLACEHOLDER_SETTINGS_PART6

    case WM_COMMAND: {
        WORD wID = LOWORD(wParam);
        WORD wNotify = HIWORD(wParam);

        // 关闭按钮
        if (wID == IDC_SETTINGS_CLOSE && wNotify == BN_CLICKED) {
            DestroyWindow(hwnd);
            return 0;
        }

        // Toggle 开关
        if (wNotify == BN_CLICKED) {
            if (wID == IDC_STARTUP_CHECK) {
                ToggleStartup();
                InvalidateRect(g_hwndToggleStartup, NULL, TRUE);
                if (g_isNotificationEnabled)
                    ShowTrayBalloon(g_hwndMain, L"设置已更新",
                        g_isStartupEnabled ? L"开机自启已启用" : L"开机自启已禁用");
                return 0;
            }
            if (wID == IDC_NOTIFICATION_CHECK) {
                g_isNotificationEnabled = !g_isNotificationEnabled;
                InvalidateRect(g_hwndToggleNotification, NULL, TRUE);
                SaveHotkeySettings();
                if (g_isNotificationEnabled)
                    ShowTrayBalloon(g_hwndMain, L"设置已更新", L"消息通知已启用");
                return 0;
            }
            if (wID == IDC_SMOOTH_SCROLL_CHECK) {
                g_isSmoothScrollEnabled = !g_isSmoothScrollEnabled;
                InvalidateRect(g_hwndToggleSmoothScroll, NULL, TRUE);
                SaveHotkeySettings();
                return 0;
            }
            if (wID == IDC_QUICK_PASTE_CHECK) {
                g_isQuickPasteEnabled = !g_isQuickPasteEnabled;
                InvalidateRect(g_hwndToggleQuickPaste, NULL, TRUE);
                HWND hwndMain = g_hwndMain;
                if (g_isQuickPasteEnabled) RegisterQuickPasteHotkeys(hwndMain);
                else UnregisterQuickPasteHotkeys(hwndMain);
                SaveHotkeySettings();
                return 0;
            }
            if (wID == IDC_COLLAPSE_AFTER_PASTE_CHECK) {
                g_isCollapseAfterPaste = !g_isCollapseAfterPaste;
                InvalidateRect(g_hwndToggleCollapse, NULL, TRUE);
                SaveHotkeySettings();
                return 0;
            }
            if (wID == IDC_OPEN_DATA_FOLDER) {
                std::wstring dataPath = GetDataFilePath();
                size_t lastSlash = dataPath.find_last_of(L"\\");
                if (lastSlash != std::wstring::npos) {
                    dataPath = dataPath.substr(0, lastSlash);
                    std::wstring cmd = L"/select,\"" + dataPath + L"\"";
                    ShellExecuteW(NULL, L"open", L"explorer.exe", cmd.c_str(), NULL, SW_SHOW);
                }
                return 0;
            }
            if (wID == IDC_SET_DATA_DIR) {
                BROWSEINFOW bi = {};
                bi.hwndOwner = hwnd;
                bi.lpszTitle = L"选择数据存储目录";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
                if (pidl) {
                    wchar_t path[MAX_PATH];
                    if (SHGetPathFromIDListW(pidl, path)) {
                        int result = MessageBoxW(hwnd,
                            (std::wstring(L"将数据迁移到:\n") + path + L"\\SmartClip\n\n确定迁移？").c_str(),
                            L"确认迁移", MB_YESNO | MB_ICONQUESTION);
                        if (result == IDYES) {
                            MigrateDataDir(path);
                        }
                    }
                    CoTaskMemFree(pidl);
                }
                return 0;
            }
            if (wID == IDC_CLEAR_NON_FAV) {
                int result = MessageBoxW(hwnd,
                    L"确定要删除所有未收藏的历史记录吗？\n此操作不可撤销。",
                    L"确认清理", MB_YESNO | MB_ICONWARNING);
                if (result == IDYES) {
                    ClearNonFavoriteHistory();
                    g_dataSizeText = FormatFileSize(GetDataDirSize());
                    InvalidateRect(hwnd, NULL, TRUE);
                    if (g_isNotificationEnabled)
                        ShowTrayBalloon(g_hwndMain, L"提示", L"非收藏数据已清理");
                }
                return 0;
            }
            // 下拉按钮点击 → 弹出选择器
            if (wID == IDC_THEME_COMBO) {
                ShowDropdownPopup(g_hwndThemeCombo, IDC_THEME_COMBO);
                return 0;
            }
            if (wID == IDC_IMAGE_PREVIEW_COMBO) {
                ShowDropdownPopup(g_hwndImagePreviewCombo, IDC_IMAGE_PREVIEW_COMBO);
                return 0;
            }
            if (wID == IDC_QUICK_PASTE_COMBO) {
                ShowDropdownPopup(g_hwndQuickPasteCombo, IDC_QUICK_PASTE_COMBO);
                return 0;
            }
        }

        // 主题选择（来自弹出窗口）
        if (wID == IDC_THEME_COMBO && wNotify == CBN_SELCHANGE) {
            int sel = g_activeDropdown.selectedIndex;
            switch (sel) {
                case 0: g_themeMode = THEME_LIGHT; break;
                case 1: g_themeMode = THEME_DARK; break;
                case 2: g_themeMode = THEME_SYSTEM; break;
            }
            ApplyTheme();
            SaveHotkeySettings();
            UpdateSettingsBrushes();
            InvalidateRect(hwnd, NULL, TRUE);
            if (g_hwndToggleStartup) InvalidateRect(g_hwndToggleStartup, NULL, TRUE);
            if (g_hwndToggleNotification) InvalidateRect(g_hwndToggleNotification, NULL, TRUE);
            if (g_hwndToggleSmoothScroll) InvalidateRect(g_hwndToggleSmoothScroll, NULL, TRUE);
            if (g_hwndToggleQuickPaste) InvalidateRect(g_hwndToggleQuickPaste, NULL, TRUE);
            if (g_hwndToggleCollapse) InvalidateRect(g_hwndToggleCollapse, NULL, TRUE);
            if (g_hwndSettingsClose) InvalidateRect(g_hwndSettingsClose, NULL, TRUE);
            if (g_hwndThemeCombo) InvalidateRect(g_hwndThemeCombo, NULL, TRUE);
            return 0;
        }

        // 图片预览选择
        if (wID == IDC_IMAGE_PREVIEW_COMBO && wNotify == CBN_SELCHANGE) {
            g_imagePreviewQuality = (ImagePreviewQuality)g_activeDropdown.selectedIndex;
            SaveHotkeySettings();
            extern HWND g_hwndListBox;
            if (g_hwndListBox) InvalidateRect(g_hwndListBox, NULL, TRUE);
            if (g_hwndImagePreviewCombo) InvalidateRect(g_hwndImagePreviewCombo, NULL, TRUE);
            return 0;
        }

        // 快捷粘贴修饰键选择
        if (wID == IDC_QUICK_PASTE_COMBO && wNotify == CBN_SELCHANGE) {
            int sel = g_activeDropdown.selectedIndex;
            HWND hwndMain = g_hwndMain;
            UnregisterQuickPasteHotkeys(hwndMain);
            switch (sel) {
                case 0: g_quickPasteModifiers = MOD_ALT; break;
                case 1: g_quickPasteModifiers = MOD_CONTROL; break;
                case 2: g_quickPasteModifiers = MOD_SHIFT; break;
                case 3: g_quickPasteModifiers = MOD_CONTROL | MOD_ALT; break;
                case 4: g_quickPasteModifiers = MOD_CONTROL | MOD_SHIFT; break;
                case 5: g_quickPasteModifiers = MOD_ALT | MOD_SHIFT; break;
            }
            if (g_isQuickPasteEnabled) RegisterQuickPasteHotkeys(hwndMain);
            SaveHotkeySettings();
            if (g_hwndQuickPasteCombo) InvalidateRect(g_hwndQuickPasteCombo, NULL, TRUE);
            return 0;
        }

        // 快捷键编辑框焦点
        if (wID == IDC_HOTKEY_EDIT) {
            if (wNotify == EN_SETFOCUS) {
                UnregisterHotkey(g_hwndMain);
                g_isRecordingHotkey = true;
                SetWindowTextW(g_hwndHotkeyEdit, L"请按下快捷键...");
            }
            if (wNotify == EN_KILLFOCUS && g_isRecordingHotkey) {
                g_isRecordingHotkey = false;
                if (g_isHotkeyEnabled) RegisterHotkey(g_hwndMain);
                wchar_t t[128] = L"";
                if (g_hotkeyModifiers & MOD_CONTROL) wcscat_s(t, L"Ctrl+");
                if (g_hotkeyModifiers & MOD_ALT) wcscat_s(t, L"Alt+");
                if (g_hotkeyModifiers & MOD_SHIFT) wcscat_s(t, L"Shift+");
                if (g_hotkeyModifiers & MOD_WIN) wcscat_s(t, L"Win+");
                wchar_t kn[32];
                if (GetKeyNameTextW(MapVirtualKeyW(g_hotkeyVirtualKey, MAPVK_VK_TO_VSC) << 16, kn, 32))
                    wcscat_s(t, kn);
                else wcscat_s(t, L"Space");
                SetWindowTextW(g_hwndHotkeyEdit, t);
            }
        }
        if (wID == IDC_SEARCH_HOTKEY_EDIT) {
            if (wNotify == EN_SETFOCUS) {
                UnregisterHotkey(g_hwndMain);
                g_isRecordingSearchHotkey = true;
                SetWindowTextW(g_hwndSearchHotkeyEdit, L"请按下快捷键...");
            }
            if (wNotify == EN_KILLFOCUS && g_isRecordingSearchHotkey) {
                g_isRecordingSearchHotkey = false;
                if (g_isHotkeyEnabled) RegisterHotkey(g_hwndMain);
                wchar_t t[128] = L"";
                if (g_searchHotkeyModifiers & MOD_CONTROL) wcscat_s(t, L"Ctrl+");
                if (g_searchHotkeyModifiers & MOD_ALT) wcscat_s(t, L"Alt+");
                if (g_searchHotkeyModifiers & MOD_SHIFT) wcscat_s(t, L"Shift+");
                if (g_searchHotkeyModifiers & MOD_WIN) wcscat_s(t, L"Win+");
                wchar_t kn[32];
                if (GetKeyNameTextW(MapVirtualKeyW(g_searchHotkeyVirtualKey, MAPVK_VK_TO_VSC) << 16, kn, 32))
                    wcscat_s(t, kn);
                else wcscat_s(t, L"F");
                SetWindowTextW(g_hwndSearchHotkeyEdit, t);
            }
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g_isRecordingHotkey = false;
        g_isRecordingSearchHotkey = false;
        if (g_hTitleFont) { DeleteObject(g_hTitleFont); g_hTitleFont = NULL; }
        if (g_hDescFont) { DeleteObject(g_hDescFont); g_hDescFont = NULL; }
        if (g_hSidebarFont) { DeleteObject(g_hSidebarFont); g_hSidebarFont = NULL; }
        if (g_hSidebarIconFont) { DeleteObject(g_hSidebarIconFont); g_hSidebarIconFont = NULL; }
        if (g_hHeaderFont) { DeleteObject(g_hHeaderFont); g_hHeaderFont = NULL; }
        if (g_hHeaderDescFont) { DeleteObject(g_hHeaderDescFont); g_hHeaderDescFont = NULL; }
        if (g_hCloseIconFont) { DeleteObject(g_hCloseIconFont); g_hCloseIconFont = NULL; }
        if (g_hSettingsBgBrush) { DeleteObject(g_hSettingsBgBrush); g_hSettingsBgBrush = NULL; }
        if (g_hEditBgBrush) { DeleteObject(g_hEditBgBrush); g_hEditBgBrush = NULL; }
        g_isSettingsDialogOpen = false;
        g_hwndSettingsDlg = NULL;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// PLACEHOLDER_SETTINGS_PART7

// ==================== 窗口类注册 ====================

static void RegisterSettingsClass() {
    if (g_settingsClassRegistered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = SettingsDialogProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(GetSettingsBgColor());
    wc.lpszClassName = L"SmartClipSettings";
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_ICON1));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);
    g_settingsClassRegistered = true;
}

// ==================== 加载字体设置 ====================

static void LoadFontSettings() {
    std::wstring filePath = GetDataFilePath();
    size_t lastSlash = filePath.find_last_of(L"\\");
    if (lastSlash == std::wstring::npos) return;
    std::wstring fontFilePath = filePath.substr(0, lastSlash) + L"\\font.txt";
    HANDLE hFile = CreateFileW(fontFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    DWORD sz = GetFileSize(hFile, NULL);
    if (sz > 0 && sz < 1024) {
        std::vector<BYTE> buf(sz + 1);
        DWORD read = 0;
        if (ReadFile(hFile, &buf[0], sz, &read, NULL)) {
            buf[sz] = 0;
            int uLen = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)&buf[0], sz, NULL, 0);
            if (uLen > 0) {
                std::vector<wchar_t> wbuf(uLen + 1);
                MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)&buf[0], sz, &wbuf[0], uLen);
                wbuf[uLen] = 0;
                std::wstring data = &wbuf[0];
                size_t p1 = data.find(L'\n');
                if (p1 != std::wstring::npos) {
                    g_fontName = data.substr(0, p1);
                    size_t p2 = data.find(L'\n', p1 + 1);
                    if (p2 != std::wstring::npos) {
                        g_fontSize = _wtoi(data.substr(p1 + 1, p2 - p1 - 1).c_str());
                        if (g_fontSize < 8 || g_fontSize > 72) g_fontSize = 16;
                        size_t p3 = data.find(L'\n', p2 + 1);
                        if (p3 != std::wstring::npos) {
                            g_fontWeight = _wtoi(data.substr(p2 + 1, p3 - p2 - 1).c_str());
                            g_fontItalic = (_wtoi(data.substr(p3 + 1).c_str()) != 0) ? TRUE : FALSE;
                        }
                    }
                }
            }
        }
    }
    CloseHandle(hFile);
}

// ==================== 创建控件辅助 ====================

static HWND CreateToggle(HWND parent, int rowIndex, int ctlId) {
    int y = GetRowY(rowIndex) + (ROW_HEIGHT - TOGGLE_H) / 2;
    int x = GetControlX(TOGGLE_W);
    return CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | BS_OWNERDRAW,
        x, y, TOGGLE_W, TOGGLE_H,
        parent, (HMENU)(INT_PTR)ctlId, GetModuleHandleW(NULL), NULL);
}

static HWND CreateSettingsCombo(HWND parent, int rowIndex, int ctlId, int width) {
    int y = GetRowY(rowIndex) + (ROW_HEIGHT - 32) / 2;
    int x = GetControlX(width);
    return CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | BS_OWNERDRAW,
        x, y, width, 32,
        parent, (HMENU)(INT_PTR)ctlId, GetModuleHandleW(NULL), NULL);
}

static HWND CreateHotkeyEditBox(HWND parent, int rowIndex, int ctlId) {
    int y = GetRowY(rowIndex) + (ROW_HEIGHT - 28) / 2;
    int x = GetControlX(180);
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
        WS_CHILD | WS_BORDER | WS_TABSTOP | ES_CENTER,
        x, y, 180, 28,
        parent, (HMENU)(INT_PTR)ctlId, GetModuleHandleW(NULL), NULL);
}

// ==================== 显示设置对话框 ====================

void ShowSettingsDialog(HWND hwndParent) {
    // 防御性检查：如果标记为打开但窗口已不存在，重置状态
    if (g_isSettingsDialogOpen) {
        if (g_hwndSettingsDlg && IsWindow(g_hwndSettingsDlg)) {
            ShowWindow(g_hwndSettingsDlg, SW_SHOW);
            SetForegroundWindow(g_hwndSettingsDlg);
            return;
        }
        g_isSettingsDialogOpen = false;
        g_hwndSettingsDlg = NULL;
    }

    LoadFontSettings();
    g_isSettingsDialogOpen = true;

    UINT oldMod = g_hotkeyModifiers;
    UINT oldVk = g_hotkeyVirtualKey;

    RegisterSettingsClass();

    RECT parentRect;
    GetWindowRect(hwndParent, &parentRect);
    int px = parentRect.left + (parentRect.right - parentRect.left - SETTINGS_WIDTH) / 2;
    int py = parentRect.top + (parentRect.bottom - parentRect.top - SETTINGS_HEIGHT) / 2;

    HWND hwndDlg = CreateWindowExW(0, L"SmartClipSettings", L"设置",
        WS_POPUP | WS_CLIPCHILDREN,
        px, py, SETTINGS_WIDTH, SETTINGS_HEIGHT,
        hwndParent, NULL, GetModuleHandleW(NULL), NULL);

    if (!hwndDlg) {
        g_isSettingsDialogOpen = false;
        return;
    }

    g_hwndSettingsDlg = hwndDlg;

    // 移除系统标题栏
    LONG_PTR style = GetWindowLongPtrW(hwndDlg, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    SetWindowLongPtrW(hwndDlg, GWL_STYLE, style);
    SetWindowPos(hwndDlg, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);

    HFONT hCtlFont = CreateFontW(19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");

    // 关闭按钮
    g_hwndSettingsClose = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        SETTINGS_WIDTH - 46, 0, 46, SETTINGS_TITLEBAR_H,
        hwndDlg, (HMENU)IDC_SETTINGS_CLOSE, GetModuleHandleW(NULL), NULL);

    // ===== 通用分类控件 =====
    g_hwndToggleStartup = CreateToggle(hwndDlg, 0, IDC_STARTUP_CHECK);
    g_hwndToggleNotification = CreateToggle(hwndDlg, 1, IDC_NOTIFICATION_CHECK);
    g_hwndToggleSmoothScroll = CreateToggle(hwndDlg, 2, IDC_SMOOTH_SCROLL_CHECK);

    g_hwndThemeCombo = CreateSettingsCombo(hwndDlg, 3, IDC_THEME_COMBO, 120);

    g_hwndImagePreviewCombo = CreateSettingsCombo(hwndDlg, 4, IDC_IMAGE_PREVIEW_COMBO, 100);

    // ===== 数据分类控件 =====
    auto CreateIosButton = [&](int rowIndex, int ctlId, int width) -> HWND {
        int y = GetRowY(rowIndex) + (ROW_HEIGHT - 32) / 2;
        int x = GetControlX(width);
        return CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | BS_OWNERDRAW,
            x, y, width, 32,
            hwndDlg, (HMENU)(INT_PTR)ctlId, GetModuleHandleW(NULL), NULL);
    };
    g_hwndOpenDataBtn = CreateIosButton(0, IDC_OPEN_DATA_FOLDER, 60);
    g_hwndSetDataDirBtn = CreateIosButton(1, IDC_SET_DATA_DIR, 60);
    g_hwndClearNonFavBtn = CreateIosButton(2, IDC_CLEAR_NON_FAV, 60);

    // ===== 快捷键分类控件 =====
    g_hwndHotkeyEdit = CreateHotkeyEditBox(hwndDlg, 0, IDC_HOTKEY_EDIT);
    SendMessageW(g_hwndHotkeyEdit, WM_SETFONT, (WPARAM)hCtlFont, TRUE);
    g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g_hwndHotkeyEdit, GWLP_WNDPROC, (LONG_PTR)HotkeyEditProc);
    wchar_t hkText[128] = L"";
    if (oldMod & MOD_CONTROL) wcscat_s(hkText, L"Ctrl+");
    if (oldMod & MOD_ALT) wcscat_s(hkText, L"Alt+");
    if (oldMod & MOD_SHIFT) wcscat_s(hkText, L"Shift+");
    if (oldMod & MOD_WIN) wcscat_s(hkText, L"Win+");
    wchar_t kn[32];
    if (GetKeyNameTextW(MapVirtualKeyW(oldVk, MAPVK_VK_TO_VSC) << 16, kn, 32)) wcscat_s(hkText, kn);
    else wcscat_s(hkText, L"Space");
    SetWindowTextW(g_hwndHotkeyEdit, hkText);

    g_hwndSearchHotkeyEdit = CreateHotkeyEditBox(hwndDlg, 1, IDC_SEARCH_HOTKEY_EDIT);
    SendMessageW(g_hwndSearchHotkeyEdit, WM_SETFONT, (WPARAM)hCtlFont, TRUE);
    g_oldSearchEditProc = (WNDPROC)SetWindowLongPtrW(g_hwndSearchHotkeyEdit, GWLP_WNDPROC, (LONG_PTR)SearchHotkeyEditProc);
    wchar_t shText[128] = L"";
    if (g_searchHotkeyModifiers & MOD_CONTROL) wcscat_s(shText, L"Ctrl+");
    if (g_searchHotkeyModifiers & MOD_ALT) wcscat_s(shText, L"Alt+");
    if (g_searchHotkeyModifiers & MOD_SHIFT) wcscat_s(shText, L"Shift+");
    if (g_searchHotkeyModifiers & MOD_WIN) wcscat_s(shText, L"Win+");
    wchar_t skn[32];
    if (GetKeyNameTextW(MapVirtualKeyW(g_searchHotkeyVirtualKey, MAPVK_VK_TO_VSC) << 16, skn, 32)) wcscat_s(shText, skn);
    else wcscat_s(shText, L"F");
    SetWindowTextW(g_hwndSearchHotkeyEdit, shText);

    g_hwndToggleQuickPaste = CreateToggle(hwndDlg, 2, IDC_QUICK_PASTE_CHECK);

    g_hwndQuickPasteCombo = CreateSettingsCombo(hwndDlg, 3, IDC_QUICK_PASTE_COMBO, 130);

    // ===== 中转站分类控件 =====
    g_hwndToggleCollapse = CreateToggle(hwndDlg, 0, IDC_COLLAPSE_AFTER_PASTE_CHECK);

    int limitY = GetRowY(1) + (ROW_HEIGHT - 28) / 2;
    g_hwndHistoryLimitEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"100",
        WS_CHILD | WS_BORDER | WS_TABSTOP | ES_CENTER | ES_NUMBER,
        GetControlX(80), limitY, 80, 28,
        hwndDlg, (HMENU)IDC_HISTORY_LIMIT_EDIT, GetModuleHandleW(NULL), NULL);
    SendMessageW(g_hwndHistoryLimitEdit, WM_SETFONT, (WPARAM)hCtlFont, TRUE);

    // 初始显示通用分类
    g_currentSettingsTab = 0;
    SwitchSettingsTab(0);

    ShowWindow(hwndDlg, SW_SHOW);
    SetForegroundWindow(hwndDlg);
    UpdateWindow(hwndDlg);
}

void ShowHotkeySettingsDialog(HWND hwndParent) {
    ShowSettingsDialog(hwndParent);
}

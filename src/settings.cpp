#include "settings.h"
#include "hotkey.h"
#include "history.h"
#include "tray.h"
#include <commctrl.h>
#include "resource.h"

// UI控件ID定义
#define ID_SEARCH_BOX 104
#define ID_TAB_CONTROL 103
#define ID_CLEAR_BUTTON 1002
#define ID_REFRESH_BUTTON 1003
#define ID_TOPMOST_BUTTON 1006
#define ID_DARKMODE_BUTTON 1007

bool g_isStartupEnabled = false;
// 添加全局变量，跟踪设置对话框是否已打开
bool g_isSettingsDialogOpen = false;

// 消息通知全局变量
bool g_isNotificationEnabled = false;  // 默认禁用

// 平滑滚动全局变量
bool g_isSmoothScrollEnabled = true;  // 默认启用

// 图片预览质量全局变量
ImagePreviewQuality g_imagePreviewQuality = PREVIEW_HD;  // 默认高清（最高画质）

// 字体设置全局变量
std::wstring g_fontName = L"Microsoft YaHei";
int g_fontSize = 16;
int g_fontWeight = FW_NORMAL;  // 字体粗细
BYTE g_fontItalic = FALSE;     // 是否斜体

void ToggleStartup() {
    g_isStartupEnabled = !g_isStartupEnabled;
    
    HKEY hKey;
    LPCWSTR lpSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    
    if (RegOpenKeyExW(HKEY_CURRENT_USER, lpSubKey, 0, KEY_WRITE | KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
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
    LPCWSTR lpSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    
    if (RegOpenKeyExW(HKEY_CURRENT_USER, lpSubKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwType = REG_SZ;
        DWORD dwSize = 0;
        
        if (RegQueryValueExW(hKey, L"SmartClip", NULL, &dwType, NULL, &dwSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }
        
        RegCloseKey(hKey);
    }
    return false;
}

#define IDC_TAB_CONTROL 100
#define IDC_STARTUP_CHECK 101
#define IDC_HOTKEY_EDIT 102
#define IDC_SEARCH_HOTKEY_EDIT 105
#define IDC_NOTIFICATION_CHECK 106
#define IDC_COLLAPSE_AFTER_PASTE_CHECK 107
#define IDC_QUICK_PASTE_COMBO 108
#define IDC_QUICK_PASTE_CHECK 109

// 分类按钮ID
#define IDC_TAB_GENERAL 110      // 通用
#define IDC_TAB_HOTKEY 111       // 快捷键
#define IDC_TAB_TRANSIT 112      // 中转站

// 中转站设置ID
#define IDC_HISTORY_LIMIT_EDIT 113   // 历史记录数量
#define IDC_AUTO_CLEAR_CHECK 114     // 自动清理

// 主题设置ID
#define IDC_THEME_LIGHT 115          // 日间模式
#define IDC_THEME_DARK 116           // 夜间模式
#define IDC_THEME_SYSTEM 117         // 跟随系统

// 平滑滚动设置ID
#define IDC_SMOOTH_SCROLL_CHECK 118  // 平滑滚动

// 图片预览质量设置ID
#define IDC_IMAGE_PREVIEW_COMBO 119  // 图片预览质量下拉框

// 当前选中的分类（0=通用, 1=快捷键, 2=中转站）
static int g_currentSettingsTab = 0;

// 分类按钮句柄
static HWND g_hwndTabGeneral = NULL;
static HWND g_hwndTabHotkey = NULL;
static HWND g_hwndTabTransit = NULL;

// 全局变量用于存储控件句柄

// === 通用分类控件 ===
static HWND g_hwndStartupBtn = NULL;        // 开机自启图标按钮
static HWND g_hwndStartupLabel = NULL;      // 开机自启标签
static HWND g_hwndNotificationBtn = NULL;   // 消息通知图标按钮
static HWND g_hwndNotificationLabel = NULL; // 消息通知标签
static HWND g_hwndCollapseBtn = NULL;       // 用完收起图标按钮
static HWND g_hwndCollapseLabel = NULL;     // 用完收起标签
static HWND g_hwndFontButton = NULL;        // 字体选择按钮
static HWND g_hwndFontLabel = NULL;         // 字体显示标签
static HWND g_hwndOpenLogButton = NULL;     // 打开日志按钮
static HWND g_hwndThemeLabel = NULL;        // 主题标签
static HWND g_hwndThemeLightBtn = NULL;     // 日间模式按钮
static HWND g_hwndThemeDarkBtn = NULL;      // 夜间模式按钮
static HWND g_hwndThemeSystemBtn = NULL;    // 跟随系统按钮
static HWND g_hwndSmoothScrollBtn = NULL;   // 平滑滚动图标按钮
static HWND g_hwndSmoothScrollLabel = NULL; // 平滑滚动标签
static HWND g_hwndImagePreviewLabel = NULL; // 图片预览质量标签
static HWND g_hwndImagePreviewCombo = NULL; // 图片预览质量下拉框

// === 快捷键分类控件 ===
static HWND g_hwndHotkeyLabel = NULL;
static HWND g_hwndHotkeyEdit = NULL;
static HWND g_hwndSearchHotkeyLabel = NULL;
static HWND g_hwndSearchHotkeyEdit = NULL;
static HWND g_hwndQuickPasteBtn = NULL;     // 快捷粘贴图标按钮
static HWND g_hwndQuickPasteLabel = NULL;   // 快捷粘贴标签
static HWND g_hwndQuickPasteCombo = NULL;   // 快捷粘贴修饰键下拉框

// === 中转站分类控件 ===
static HWND g_hwndHistoryLimitLabel = NULL; // 历史记录数量标签
static HWND g_hwndHistoryLimitEdit = NULL;  // 历史记录数量编辑框
static HWND g_hwndAutoClearBtn = NULL;      // 自动清理按钮
static HWND g_hwndAutoClearLabel = NULL;    // 自动清理标签
static bool g_isRecordingHotkey = false;
static bool g_isRecordingSearchHotkey = false;  // 是否正在录制搜索框快捷键
static WNDPROC g_oldEditProc = NULL;
static WNDPROC g_oldSearchEditProc = NULL;  // 搜索框快捷键编辑框的原始窗口过程
static HWND g_hwndSettingsDlg = NULL;
static bool g_wasHotkeyEnabledBeforeDialog = false;
static bool g_hotkeyWasModified = false;  // 标记快捷键是否被修改

// 编辑框子类窗口过程
LRESULT CALLBACK HotkeyEditProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (g_isRecordingHotkey) {
                UINT virtualKey = (UINT)wParam;

                // 忽略修饰键本身
                if (virtualKey == VK_CONTROL || virtualKey == VK_MENU || virtualKey == VK_SHIFT ||
                    virtualKey == VK_LWIN || virtualKey == VK_RWIN) {
                    return 0;
                }

                // 获取修饰键状态
                UINT modifiers = 0;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
                if (GetAsyncKeyState(VK_MENU) & 0x8000) modifiers |= MOD_ALT;
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) modifiers |= MOD_SHIFT;
                if (GetAsyncKeyState(VK_LWIN) & 0x8000 || GetAsyncKeyState(VK_RWIN) & 0x8000) modifiers |= MOD_WIN;

                // 保存新的快捷键
                g_hotkeyModifiers = modifiers;
                g_hotkeyVirtualKey = virtualKey;

                // 更新显示
                wchar_t hotkeyText[128] = L"";
                if (modifiers & MOD_CONTROL) wcscat_s(hotkeyText, L"Ctrl+");
                if (modifiers & MOD_ALT) wcscat_s(hotkeyText, L"Alt+");
                if (modifiers & MOD_SHIFT) wcscat_s(hotkeyText, L"Shift+");
                if (modifiers & MOD_WIN) wcscat_s(hotkeyText, L"Win+");

                wchar_t keyName[32];
                if (GetKeyNameTextW(MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC) << 16, keyName, 32)) {
                    wcscat_s(hotkeyText, keyName);
                } else {
                    wcscat_s(hotkeyText, L"?");
                }
                SetWindowTextW(hwnd, hotkeyText);

                // 标记快捷键已被修改
                g_hotkeyWasModified = true;

                // 保存快捷键设置（用户修改快捷键后自动启用）
                g_isHotkeyEnabled = true;  // 修改快捷键后自动启用
                SaveHotkeySettings();

                // 显示消息通知
                ShowTrayBalloon(g_hwndSettingsDlg, L"设置已更新", L"快捷键设置已保存");

                // 结束录制
                g_isRecordingHotkey = false;
                return 0;
            }
            break;
        }
        case WM_CHAR:
        case WM_SYSCHAR:
            // 阻止字符输入
            if (g_isRecordingHotkey) {
                return 0;
            }
            break;
    }
    return CallWindowProcW(g_oldEditProc, hwnd, uMsg, wParam, lParam);
}

// 搜索框快捷键编辑框子类窗口过程
LRESULT CALLBACK SearchHotkeyEditProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (g_isRecordingSearchHotkey) {
                UINT virtualKey = (UINT)wParam;

                // 忽略修饰键本身
                if (virtualKey == VK_CONTROL || virtualKey == VK_MENU || virtualKey == VK_SHIFT ||
                    virtualKey == VK_LWIN || virtualKey == VK_RWIN) {
                    return 0;
                }

                // 获取修饰键状态
                UINT modifiers = 0;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
                if (GetAsyncKeyState(VK_MENU) & 0x8000) modifiers |= MOD_ALT;
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) modifiers |= MOD_SHIFT;
                if (GetAsyncKeyState(VK_LWIN) & 0x8000 || GetAsyncKeyState(VK_RWIN) & 0x8000) modifiers |= MOD_WIN;

                // 保存新的快捷键
                g_searchHotkeyModifiers = modifiers;
                g_searchHotkeyVirtualKey = virtualKey;

                // 更新显示
                wchar_t hotkeyText[128] = L"";
                if (modifiers & MOD_CONTROL) wcscat_s(hotkeyText, L"Ctrl+");
                if (modifiers & MOD_ALT) wcscat_s(hotkeyText, L"Alt+");
                if (modifiers & MOD_SHIFT) wcscat_s(hotkeyText, L"Shift+");
                if (modifiers & MOD_WIN) wcscat_s(hotkeyText, L"Win+");

                wchar_t keyName[32];
                if (GetKeyNameTextW(MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC) << 16, keyName, 32)) {
                    wcscat_s(hotkeyText, keyName);
                } else {
                    wcscat_s(hotkeyText, L"Unknown");
                }

                SetWindowTextW(hwnd, hotkeyText);

                // 保存快捷键设置（用户修改快捷键后自动启用）
                g_isSearchHotkeyEnabled = true;
                SaveHotkeySettings();

                g_isRecordingSearchHotkey = false;
                return 0;
            }
            break;
        }
        case WM_CHAR:
        case WM_SYSCHAR:
            // 阻止字符输入
            if (g_isRecordingSearchHotkey) {
                return 0;
            }
            break;
    }
    return CallWindowProcW(g_oldSearchEditProc, hwnd, uMsg, wParam, lParam);
}

// 切换设置分类显示
void SwitchSettingsTab(int tabIndex) {
    g_currentSettingsTab = tabIndex;

    // 显示/隐藏通用分类控件
    int showGeneral = (tabIndex == 0) ? SW_SHOW : SW_HIDE;
    if (g_hwndStartupBtn) ShowWindow(g_hwndStartupBtn, showGeneral);
    if (g_hwndStartupLabel) ShowWindow(g_hwndStartupLabel, showGeneral);
    if (g_hwndNotificationBtn) ShowWindow(g_hwndNotificationBtn, showGeneral);
    if (g_hwndNotificationLabel) ShowWindow(g_hwndNotificationLabel, showGeneral);

    if (g_hwndOpenLogButton) ShowWindow(g_hwndOpenLogButton, showGeneral);
    if (g_hwndThemeLabel) ShowWindow(g_hwndThemeLabel, showGeneral);
    if (g_hwndThemeLightBtn) ShowWindow(g_hwndThemeLightBtn, showGeneral);
    if (g_hwndThemeDarkBtn) ShowWindow(g_hwndThemeDarkBtn, showGeneral);
    if (g_hwndThemeSystemBtn) ShowWindow(g_hwndThemeSystemBtn, showGeneral);
    if (g_hwndSmoothScrollBtn) ShowWindow(g_hwndSmoothScrollBtn, showGeneral);
    if (g_hwndSmoothScrollLabel) ShowWindow(g_hwndSmoothScrollLabel, showGeneral);
    if (g_hwndImagePreviewLabel) ShowWindow(g_hwndImagePreviewLabel, showGeneral);
    if (g_hwndImagePreviewCombo) ShowWindow(g_hwndImagePreviewCombo, showGeneral);

    // 显示/隐藏快捷键分类控件
    int showHotkey = (tabIndex == 1) ? SW_SHOW : SW_HIDE;
    if (g_hwndHotkeyLabel) ShowWindow(g_hwndHotkeyLabel, showHotkey);
    if (g_hwndHotkeyEdit) ShowWindow(g_hwndHotkeyEdit, showHotkey);
    if (g_hwndSearchHotkeyLabel) ShowWindow(g_hwndSearchHotkeyLabel, showHotkey);
    if (g_hwndSearchHotkeyEdit) ShowWindow(g_hwndSearchHotkeyEdit, showHotkey);
    if (g_hwndQuickPasteBtn) ShowWindow(g_hwndQuickPasteBtn, showHotkey);
    if (g_hwndQuickPasteLabel) ShowWindow(g_hwndQuickPasteLabel, showHotkey);
    if (g_hwndQuickPasteCombo) ShowWindow(g_hwndQuickPasteCombo, showHotkey);

    // 显示/隐藏中转站分类控件
    int showTransit = (tabIndex == 2) ? SW_SHOW : SW_HIDE;
    if (g_hwndCollapseBtn) ShowWindow(g_hwndCollapseBtn, showTransit);
    if (g_hwndCollapseLabel) ShowWindow(g_hwndCollapseLabel, showTransit);
    if (g_hwndHistoryLimitLabel) ShowWindow(g_hwndHistoryLimitLabel, showTransit);
    if (g_hwndHistoryLimitEdit) ShowWindow(g_hwndHistoryLimitEdit, showTransit);
    if (g_hwndAutoClearBtn) ShowWindow(g_hwndAutoClearBtn, showTransit);
    if (g_hwndAutoClearLabel) ShowWindow(g_hwndAutoClearLabel, showTransit);

    // 重绘分类按钮
    if (g_hwndTabGeneral) InvalidateRect(g_hwndTabGeneral, NULL, TRUE);
    if (g_hwndTabHotkey) InvalidateRect(g_hwndTabHotkey, NULL, TRUE);
    if (g_hwndTabTransit) InvalidateRect(g_hwndTabTransit, NULL, TRUE);

    // 重绘对话框内容区域
    HWND hwndDlg = GetParent(g_hwndTabGeneral);
    if (hwndDlg) {
        InvalidateRect(hwndDlg, NULL, TRUE);
    }
}

// 对话框过程函数
LRESULT CALLBACK SettingsDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HBRUSH hBgBrush = NULL;

    switch (uMsg) {
        case WM_INITDIALOG:
            hBgBrush = CreateSolidBrush(RGB(245, 245, 245));
            return TRUE;

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT: {
            HDC hdcStatic = (HDC)wParam;
            SetBkColor(hdcStatic, RGB(255, 255, 255));
            SetBkMode(hdcStatic, OPAQUE);
            if (!hBgBrush) hBgBrush = CreateSolidBrush(RGB(245, 245, 245));
            // 编辑框使用白色背景
            if (uMsg == WM_CTLCOLOREDIT) {
                static HBRUSH hEditBrush = CreateSolidBrush(RGB(255, 255, 255));
                return (LRESULT)hEditBrush;
            }
            // 静态控件和按钮使用对话框背景色
            SetBkColor(hdcStatic, RGB(245, 245, 245));
            return (LRESULT)hBgBrush;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT lpDIS = (LPDRAWITEMSTRUCT)lParam;
            HDC hdc = lpDIS->hDC;
            RECT rc = lpDIS->rcItem;

            // 处理分类按钮绘制（通用、快捷键、中转站）
            if (lpDIS->CtlID == IDC_TAB_GENERAL ||
                lpDIS->CtlID == IDC_TAB_HOTKEY ||
                lpDIS->CtlID == IDC_TAB_TRANSIT) {

                int tabIndex = (lpDIS->CtlID == IDC_TAB_GENERAL) ? 0 :
                               (lpDIS->CtlID == IDC_TAB_HOTKEY) ? 1 : 2;
                bool isSelected = (g_currentSettingsTab == tabIndex);

                // 背景色：选中时蓝色，未选中时浅灰
                COLORREF bgColor = isSelected ? RGB(0, 120, 215) : RGB(240, 240, 240);
                HBRUSH hBrush = CreateSolidBrush(bgColor);
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);

                // 绘制边框
                HPEN hPen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
                SelectObject(hdc, hOldPen);
                SelectObject(hdc, hOldBrush);
                DeleteObject(hPen);

                // 按钮文字
                const wchar_t* text = L"";
                switch (lpDIS->CtlID) {
                    case IDC_TAB_GENERAL: text = L"通用"; break;
                    case IDC_TAB_HOTKEY: text = L"快捷键"; break;
                    case IDC_TAB_TRANSIT: text = L"中转站"; break;
                }

                // 创建字体
                HFONT hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");

                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(60, 60, 60));

                HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
                DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, hOldFont);

                DeleteObject(hFont);
                return TRUE;
            }

            // 处理所有图标按钮绘制
            if (lpDIS->CtlID == IDC_STARTUP_CHECK ||
                lpDIS->CtlID == IDC_NOTIFICATION_CHECK ||
                lpDIS->CtlID == IDC_COLLAPSE_AFTER_PASTE_CHECK ||
                lpDIS->CtlID == IDC_QUICK_PASTE_CHECK ||
                lpDIS->CtlID == IDC_AUTO_CLEAR_CHECK ||
                lpDIS->CtlID == IDC_SMOOTH_SCROLL_CHECK) {

                // 设置背景色
                COLORREF bgColor = RGB(245, 245, 245);
                HBRUSH hBrush = CreateSolidBrush(bgColor);
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);

                // 根据按钮ID选择图标和状态
                const wchar_t* icon = L"";
                bool isEnabled = false;

                switch (lpDIS->CtlID) {
                    case IDC_STARTUP_CHECK:
                        icon = L"\uE7E8";  // Power
                        isEnabled = g_isStartupEnabled;
                        break;
                    case IDC_NOTIFICATION_CHECK:
                        icon = g_isNotificationEnabled ? L"\uEA8F" : L"\uE7ED";  // Ringer / RingerOff
                        isEnabled = g_isNotificationEnabled;
                        break;
                    case IDC_COLLAPSE_AFTER_PASTE_CHECK:
                        icon = L"\uE96D";  // CollapseContent
                        isEnabled = g_isCollapseAfterPaste;
                        break;
                    case IDC_QUICK_PASTE_CHECK:
                        icon = L"\uE945";  // Paste
                        isEnabled = g_isQuickPasteEnabled;
                        break;
                    case IDC_AUTO_CLEAR_CHECK:
                        icon = L"\uE74D";  // Delete
                        isEnabled = false;  // TODO: 添加自动清理状态变量
                        break;
                    case IDC_SMOOTH_SCROLL_CHECK:
                        icon = L"\uE74A";  // Scroll
                        isEnabled = g_isSmoothScrollEnabled;
                        break;
                }

                // 创建图标字体
                HFONT hIconFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");

                SetBkMode(hdc, TRANSPARENT);
                // 启用时蓝色，禁用时灰色
                SetTextColor(hdc, isEnabled ? RGB(0, 120, 215) : RGB(128, 128, 128));

                HFONT hOldFont = (HFONT)SelectObject(hdc, hIconFont);
                DrawTextW(hdc, icon, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, hOldFont);

                DeleteObject(hIconFont);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            WORD wID = LOWORD(wParam);
            WORD wNotifyCode = HIWORD(wParam);

            // 处理分类按钮点击
            if (wNotifyCode == BN_CLICKED) {
                if (wID == IDC_TAB_GENERAL) {
                    SwitchSettingsTab(0);
                    return TRUE;
                }
                if (wID == IDC_TAB_HOTKEY) {
                    SwitchSettingsTab(1);
                    return TRUE;
                }
                if (wID == IDC_TAB_TRANSIT) {
                    SwitchSettingsTab(2);
                    return TRUE;
                }
            }

            // 处理开机自启图标按钮
            if (wID == IDC_STARTUP_CHECK && wNotifyCode == BN_CLICKED) {
                ToggleStartup();
                InvalidateRect(g_hwndStartupBtn, NULL, TRUE);
                if (g_isNotificationEnabled) {
                    ShowTrayBalloon(GetParent(hwndDlg), L"设置已更新",
                        g_isStartupEnabled ? L"开机自启已启用" : L"开机自启已禁用");
                }
                return TRUE;
            }

            // 处理消息通知图标按钮
            if (wID == IDC_NOTIFICATION_CHECK && wNotifyCode == BN_CLICKED) {
                g_isNotificationEnabled = !g_isNotificationEnabled;
                InvalidateRect(g_hwndNotificationBtn, NULL, TRUE);
                if (g_isNotificationEnabled) {
                    ShowTrayBalloon(GetParent(hwndDlg), L"设置已更新", L"消息通知已启用");
                }
                return TRUE;
            }

            // 处理主题切换
            if (wID == IDC_THEME_LIGHT && wNotifyCode == BN_CLICKED) {
                g_themeMode = THEME_LIGHT;
                ApplyTheme();
                SaveHotkeySettings();
                return TRUE;
            }
            if (wID == IDC_THEME_DARK && wNotifyCode == BN_CLICKED) {
                g_themeMode = THEME_DARK;
                ApplyTheme();
                SaveHotkeySettings();
                return TRUE;
            }
            if (wID == IDC_THEME_SYSTEM && wNotifyCode == BN_CLICKED) {
                g_themeMode = THEME_SYSTEM;
                ApplyTheme();
                SaveHotkeySettings();
                return TRUE;
            }

            // 处理用完收起图标按钮
            if (wID == IDC_COLLAPSE_AFTER_PASTE_CHECK && wNotifyCode == BN_CLICKED) {
                g_isCollapseAfterPaste = !g_isCollapseAfterPaste;
                InvalidateRect(g_hwndCollapseBtn, NULL, TRUE);
                if (g_isNotificationEnabled) {
                    ShowTrayBalloon(GetParent(hwndDlg), L"设置已更新",
                        g_isCollapseAfterPaste ? L"用完收起已启用" : L"用完收起已禁用");
                }
                return TRUE;
            }

            // 处理快捷粘贴图标按钮
            if (wID == IDC_QUICK_PASTE_CHECK && wNotifyCode == BN_CLICKED) {
                g_isQuickPasteEnabled = !g_isQuickPasteEnabled;
                SaveHotkeySettings();
                InvalidateRect(g_hwndQuickPasteBtn, NULL, TRUE);
                // 重新注册或注销快捷键
                if (g_isQuickPasteEnabled) {
                    RegisterQuickPasteHotkeys(GetParent(hwndDlg));
                } else {
                    UnregisterQuickPasteHotkeys(GetParent(hwndDlg));
                }
                if (g_isNotificationEnabled) {
                    ShowTrayBalloon(GetParent(hwndDlg), L"设置已更新",
                        g_isQuickPasteEnabled ? L"快捷粘贴已启用" : L"快捷粘贴已禁用");
                }
                return TRUE;
            }

            // 处理平滑滚动图标按钮
            if (wID == IDC_SMOOTH_SCROLL_CHECK && wNotifyCode == BN_CLICKED) {
                g_isSmoothScrollEnabled = !g_isSmoothScrollEnabled;
                SaveHotkeySettings();
                InvalidateRect(g_hwndSmoothScrollBtn, NULL, TRUE);
                if (g_isNotificationEnabled) {
                    ShowTrayBalloon(GetParent(hwndDlg), L"设置已更新",
                        g_isSmoothScrollEnabled ? L"平滑滚动已启用" : L"平滑滚动已禁用");
                }
                return TRUE;
            }

            // 处理快捷粘贴修饰键下拉框
            if (wID == IDC_QUICK_PASTE_COMBO && wNotifyCode == CBN_SELCHANGE) {
                int sel = SendMessageW(g_hwndQuickPasteCombo, CB_GETCURSEL, 0, 0);
                UINT oldModifiers = g_quickPasteModifiers;
                switch (sel) {
                    case 0: g_quickPasteModifiers = MOD_ALT; break;
                    case 1: g_quickPasteModifiers = MOD_CONTROL; break;
                    case 2: g_quickPasteModifiers = MOD_SHIFT; break;
                    case 3: g_quickPasteModifiers = MOD_CONTROL | MOD_ALT; break;
                    case 4: g_quickPasteModifiers = MOD_CONTROL | MOD_SHIFT; break;
                    case 5: g_quickPasteModifiers = MOD_ALT | MOD_SHIFT; break;
                }
                if (oldModifiers != g_quickPasteModifiers) {
                    SaveHotkeySettings();
                    // 重新注册快捷键
                    if (g_isQuickPasteEnabled) {
                        RegisterQuickPasteHotkeys(GetParent(hwndDlg));
                    }
                    if (g_isNotificationEnabled) {
                        ShowTrayBalloon(GetParent(hwndDlg), L"设置已更新", L"快捷粘贴修饰键已更改");
                    }
                }
                return TRUE;
            }

            // 处理图片预览质量下拉框
            if (wID == IDC_IMAGE_PREVIEW_COMBO && wNotifyCode == CBN_SELCHANGE) {
                int sel = SendMessageW(g_hwndImagePreviewCombo, CB_GETCURSEL, 0, 0);
                ImagePreviewQuality oldQuality = g_imagePreviewQuality;
                switch (sel) {
                    case 0: g_imagePreviewQuality = PREVIEW_OFF; break;
                    case 1: g_imagePreviewQuality = PREVIEW_BLUR; break;
                    case 2: g_imagePreviewQuality = PREVIEW_SD; break;
                    case 3: g_imagePreviewQuality = PREVIEW_HD; break;
                }
                if (oldQuality != g_imagePreviewQuality) {
                    SaveHotkeySettings();
                    // 刷新列表显示
                    if (g_hwndListBox != NULL) {
                        InvalidateRect(g_hwndListBox, NULL, TRUE);
                    }
                    if (g_isNotificationEnabled) {
                        const wchar_t* qualityNames[] = {L"关闭", L"模糊", L"标清", L"高清"};
                        std::wstring msg = L"图片预览质量已设为：";
                        msg += qualityNames[sel];
                        ShowTrayBalloon(GetParent(hwndDlg), L"设置已更新", msg.c_str());
                    }
                }
                return TRUE;
            }

            // 处理打开日志文件夹按钮
            if (wID == IDC_OPEN_DATA_FOLDER && wNotifyCode == BN_CLICKED) {
                std::wstring dataPath = GetDataFilePath();
                // 获取文件夹路径（去掉文件名）
                size_t lastSlash = dataPath.find_last_of(L"\\");
                if (lastSlash != std::wstring::npos) {
                    std::wstring folderPath = dataPath.substr(0, lastSlash);
                    // 使用 explorer 打开文件夹并选中文件
                    std::wstring command = L"/select,\"" + dataPath + L"\"";
                    ShellExecuteW(NULL, L"open", L"explorer.exe", command.c_str(), NULL, SW_SHOW);
                }
                return TRUE;
            }


            // 处理快捷键编辑框焦点
            if (wID == IDC_HOTKEY_EDIT) {
                if (wNotifyCode == EN_SETFOCUS) {
                    // 开始录制快捷键
                    g_isRecordingHotkey = true;
                    SetWindowTextW(g_hwndHotkeyEdit, L"请按下新的快捷键组合...");
                    return TRUE;
                } else if (wNotifyCode == EN_KILLFOCUS) {
                    // 结束录制快捷键
                    if (g_isRecordingHotkey) {
                        g_isRecordingHotkey = false;
                    }
                    // 恢复显示当前快捷键
                    wchar_t hotkeyText[128] = L"";
                    if (g_hotkeyModifiers & MOD_CONTROL) wcscat_s(hotkeyText, L"Ctrl+");
                    if (g_hotkeyModifiers & MOD_ALT) wcscat_s(hotkeyText, L"Alt+");
                    if (g_hotkeyModifiers & MOD_SHIFT) wcscat_s(hotkeyText, L"Shift+");
                    if (g_hotkeyModifiers & MOD_WIN) wcscat_s(hotkeyText, L"Win+");

                    wchar_t keyName[32];
                    if (GetKeyNameTextW(MapVirtualKeyW(g_hotkeyVirtualKey, MAPVK_VK_TO_VSC) << 16, keyName, 32)) {
                        wcscat_s(hotkeyText, keyName);
                    } else {
                        wcscat_s(hotkeyText, L"Space");
                    }
                    SetWindowTextW(g_hwndHotkeyEdit, hotkeyText);
                    return TRUE;
                }
            }

            // 处理搜索框快捷键编辑框焦点
            if (wID == IDC_SEARCH_HOTKEY_EDIT) {
                if (wNotifyCode == EN_SETFOCUS) {
                    // 开始录制搜索框快捷键
                    g_isRecordingSearchHotkey = true;
                    SetWindowTextW(g_hwndSearchHotkeyEdit, L"请按下新的快捷键组合...");
                    return TRUE;
                } else if (wNotifyCode == EN_KILLFOCUS) {
                    // 结束录制搜索框快捷键
                    if (g_isRecordingSearchHotkey) {
                        g_isRecordingSearchHotkey = false;
                    }
                    // 恢复显示当前搜索框快捷键
                    wchar_t hotkeyText[128] = L"";
                    if (g_searchHotkeyModifiers & MOD_CONTROL) wcscat_s(hotkeyText, L"Ctrl+");
                    if (g_searchHotkeyModifiers & MOD_ALT) wcscat_s(hotkeyText, L"Alt+");
                    if (g_searchHotkeyModifiers & MOD_SHIFT) wcscat_s(hotkeyText, L"Shift+");
                    if (g_searchHotkeyModifiers & MOD_WIN) wcscat_s(hotkeyText, L"Win+");

                    wchar_t keyName[32];
                    if (GetKeyNameTextW(MapVirtualKeyW(g_searchHotkeyVirtualKey, MAPVK_VK_TO_VSC) << 16, keyName, 32)) {
                        wcscat_s(hotkeyText, keyName);
                    } else {
                        wcscat_s(hotkeyText, L"F");
                    }
                    SetWindowTextW(g_hwndSearchHotkeyEdit, hotkeyText);
                    return TRUE;
                }
            }
            break;
        }

        case WM_SYSCOMMAND:
            if (wParam == SC_CLOSE) {
                // 关闭对话框
                DestroyWindow(hwndDlg);
                return TRUE;
            }
            break;

        case WM_CLOSE:
            // 关闭对话框
            DestroyWindow(hwndDlg);
            return TRUE;

        case WM_DESTROY:
            // 对话框销毁时的处理
            g_isRecordingHotkey = false;
            g_isRecordingSearchHotkey = false;
            return TRUE;
    }
    return DefWindowProcW(hwndDlg, uMsg, wParam, lParam);
}

// 显示模态设置对话框
void ShowSettingsDialog(HWND hwndParent) {
    // 检查设置对话框是否已打开，避免多个窗口
    if (g_isSettingsDialogOpen) {
        return;
    }

    // 加载字体设置
    std::wstring filePath = GetDataFilePath();
    size_t lastSlash = filePath.find_last_of(L"\\");
    if (lastSlash != std::wstring::npos) {
        std::wstring fontFilePath = filePath.substr(0, lastSlash) + L"\\font.txt";
        HANDLE hFile = CreateFileW(fontFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD dwFileSize = GetFileSize(hFile, NULL);
            if (dwFileSize > 0 && dwFileSize < 1024) {
                std::vector<BYTE> fileContent(dwFileSize + 1);
                DWORD dwBytesRead = 0;
                if (ReadFile(hFile, &fileContent[0], dwFileSize, &dwBytesRead, NULL)) {
                    fileContent[dwFileSize] = 0;
                    int unicodeLength = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)(&fileContent[0]), dwFileSize, NULL, 0);
                    if (unicodeLength > 0) {
                        std::vector<wchar_t> unicodeContent(unicodeLength + 1);
                        MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)(&fileContent[0]), dwFileSize, &unicodeContent[0], unicodeLength);
                        unicodeContent[unicodeLength] = L'\0';

                        std::wstring fontData = &unicodeContent[0];
                        // 解析格式：字体名\n字号\n字重\n斜体
                        size_t pos1 = fontData.find(L'\n');
                        if (pos1 != std::wstring::npos) {
                            g_fontName = fontData.substr(0, pos1);
                            size_t pos2 = fontData.find(L'\n', pos1 + 1);
                            if (pos2 != std::wstring::npos) {
                                std::wstring sizeStr = fontData.substr(pos1 + 1, pos2 - pos1 - 1);
                                g_fontSize = _wtoi(sizeStr.c_str());
                                if (g_fontSize < 8) g_fontSize = 16;
                                if (g_fontSize > 72) g_fontSize = 16;

                                size_t pos3 = fontData.find(L'\n', pos2 + 1);
                                if (pos3 != std::wstring::npos) {
                                    std::wstring weightStr = fontData.substr(pos2 + 1, pos3 - pos2 - 1);
                                    g_fontWeight = _wtoi(weightStr.c_str());
                                    std::wstring italicStr = fontData.substr(pos3 + 1);
                                    g_fontItalic = (_wtoi(italicStr.c_str()) != 0) ? TRUE : FALSE;
                                }
                            } else {
                                // 兼容旧格式：字体名\n字号
                                std::wstring sizeStr = fontData.substr(pos1 + 1);
                                g_fontSize = _wtoi(sizeStr.c_str());
                                if (g_fontSize < 8) g_fontSize = 16;
                                if (g_fontSize > 72) g_fontSize = 16;
                            }
                        }
                    }
                }
            }
            CloseHandle(hFile);
        }
    }

    // 标记设置对话框已打开
    g_isSettingsDialogOpen = true;

    // 保存对话框句柄
    g_hwndSettingsDlg = hwndParent;

    bool wasHotkeyEnabled = g_isHotkeyEnabled;
    g_wasHotkeyEnabledBeforeDialog = wasHotkeyEnabled;  // 保存到静态变量
    g_hotkeyWasModified = false;  // 重置修改标记
    UINT oldModifiers = g_hotkeyModifiers;
    UINT oldVirtualKey = g_hotkeyVirtualKey;
    
    // 保存当前快捷键状态并注销
    UnregisterHotkey(hwndParent);
    g_isHotkeyEnabled = false;
    
    // 获取父窗口位置和大小，用于居中对话框
    RECT parentRect;
    GetWindowRect(hwndParent, &parentRect);
    int parentWidth = parentRect.right - parentRect.left;
    int parentHeight = parentRect.bottom - parentRect.top;
    
    const int dialogWidth = 350;
    const int dialogHeight = 320;
    
    int dialogX = parentRect.left + (parentWidth - dialogWidth) / 2;
    int dialogY = parentRect.top + (parentHeight - dialogHeight) / 2;
    
    // 创建模态对话框
    HWND hwndDlg = CreateWindowExW(
        WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME,
        L"#32770",  // 对话框类名
        L"设置",     // 对话框标题
        WS_VISIBLE | WS_CAPTION | WS_SYSMENU | WS_POPUP | DS_MODALFRAME | DS_SETFONT,
        dialogX, dialogY, dialogWidth, dialogHeight,
        hwndParent, NULL, NULL, NULL
    );
    
    if (!hwndDlg) {
        // 恢复快捷键状态
        if (wasHotkeyEnabled) {
            g_isHotkeyEnabled = true;
            RegisterHotkey(hwndParent);
        }
        g_isSettingsDialogOpen = false;
        return;
    }
    
    // 设置对话框图标
    HICON hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_ICON1));
    if (hIcon != NULL) {
        SendMessageW(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessageW(hwndDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }
    
    // 设置对话框背景色
    HBRUSH hBrush = CreateSolidBrush(RGB(245, 245, 245));
    SetClassLongPtrW(hwndDlg, GCLP_HBRBACKGROUND, (LONG_PTR)hBrush);
    
    // 设置对话框字体
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessageW(hwndDlg, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // 绘制背景
    RECT clientRect;
    GetClientRect(hwndDlg, &clientRect);
    HDC hDC = GetDC(hwndDlg);
    FillRect(hDC, &clientRect, hBrush);
    ReleaseDC(hwndDlg, hDC);
    
    // 初始化通用控件库
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    // ========== 分类按钮（通用、快捷键、中转站） ==========
    const int tabBtnWidth = 100;
    const int tabBtnHeight = 28;
    const int tabBtnSpacing = 5;
    int tabY = 10;
    int tabStartX = 15;

    // 通用按钮
    g_hwndTabGeneral = CreateWindowExW(0, L"BUTTON", L"通用",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        tabStartX, tabY, tabBtnWidth, tabBtnHeight,
        hwndDlg, (HMENU)IDC_TAB_GENERAL, NULL, NULL);

    // 快捷键按钮
    g_hwndTabHotkey = CreateWindowExW(0, L"BUTTON", L"快捷键",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        tabStartX + tabBtnWidth + tabBtnSpacing, tabY, tabBtnWidth, tabBtnHeight,
        hwndDlg, (HMENU)IDC_TAB_HOTKEY, NULL, NULL);

    // 中转站按钮
    g_hwndTabTransit = CreateWindowExW(0, L"BUTTON", L"中转站",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        tabStartX + (tabBtnWidth + tabBtnSpacing) * 2, tabY, tabBtnWidth, tabBtnHeight,
        hwndDlg, (HMENU)IDC_TAB_TRANSIT, NULL, NULL);

    // 内容区域起始Y坐标
    int contentY = tabY + tabBtnHeight + 15;
    const int btnSize = 32;
    const int labelWidth = 60;
    const int btnSpacing = 15;

    // ==================== 通用分类控件 ====================
    int col1X = 20;

    // 开机自启
    g_hwndStartupLabel = CreateWindowExW(0, L"STATIC", L"开机自启",
        WS_CHILD | SS_CENTER,
        col1X, contentY + btnSize + 2, labelWidth, 16, hwndDlg, NULL, NULL, NULL);
    SendMessageW(g_hwndStartupLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hwndStartupBtn = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | BS_OWNERDRAW,
        col1X + (labelWidth - btnSize) / 2, contentY, btnSize, btnSize,
        hwndDlg, (HMENU)IDC_STARTUP_CHECK, NULL, NULL);

    // 消息通知
    int col2X = col1X + labelWidth + btnSpacing;
    g_hwndNotificationLabel = CreateWindowExW(0, L"STATIC", L"消息通知",
        WS_CHILD | SS_CENTER,
        col2X, contentY + btnSize + 2, labelWidth, 16, hwndDlg, NULL, NULL, NULL);
    SendMessageW(g_hwndNotificationLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hwndNotificationBtn = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | BS_OWNERDRAW,
        col2X + (labelWidth - btnSize) / 2, contentY, btnSize, btnSize,
        hwndDlg, (HMENU)IDC_NOTIFICATION_CHECK, NULL, NULL);

    // 打开日志文件位置（通用分类第二行）
    int generalRow3Y = contentY + btnSize + 30;
    g_hwndOpenLogButton = CreateWindowExW(0, L"BUTTON", L"打开日志文件位置",
        WS_CHILD | BS_PUSHBUTTON,
        20, generalRow3Y, 130, 25, hwndDlg, (HMENU)IDC_OPEN_DATA_FOLDER, NULL, NULL);
    SendMessageW(g_hwndOpenLogButton, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 主题设置（通用分类第四行）
    int generalRow4Y = generalRow3Y + 35;
    g_hwndThemeLabel = CreateWindowExW(0, L"STATIC", L"主题：",
        WS_CHILD | SS_LEFT,
        20, generalRow4Y + 4, 40, 20, hwndDlg, NULL, NULL, NULL);
    SendMessageW(g_hwndThemeLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hwndThemeLightBtn = CreateWindowExW(0, L"BUTTON", L"日间",
        WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
        65, generalRow4Y, 60, 25, hwndDlg, (HMENU)IDC_THEME_LIGHT, NULL, NULL);
    SendMessageW(g_hwndThemeLightBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hwndThemeDarkBtn = CreateWindowExW(0, L"BUTTON", L"夜间",
        WS_CHILD | BS_AUTORADIOBUTTON,
        130, generalRow4Y, 60, 25, hwndDlg, (HMENU)IDC_THEME_DARK, NULL, NULL);
    SendMessageW(g_hwndThemeDarkBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hwndThemeSystemBtn = CreateWindowExW(0, L"BUTTON", L"跟随系统",
        WS_CHILD | BS_AUTORADIOBUTTON,
        195, generalRow4Y, 80, 25, hwndDlg, (HMENU)IDC_THEME_SYSTEM, NULL, NULL);
    SendMessageW(g_hwndThemeSystemBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 根据当前主题模式设置选中状态
    switch (g_themeMode) {
        case THEME_LIGHT:
            SendMessageW(g_hwndThemeLightBtn, BM_SETCHECK, BST_CHECKED, 0);
            break;
        case THEME_DARK:
            SendMessageW(g_hwndThemeDarkBtn, BM_SETCHECK, BST_CHECKED, 0);
            break;
        case THEME_SYSTEM:
            SendMessageW(g_hwndThemeSystemBtn, BM_SETCHECK, BST_CHECKED, 0);
            break;
    }

    // 平滑滚动（通用分类第五行）
    int generalRow5Y = generalRow4Y + 35;
    g_hwndSmoothScrollLabel = CreateWindowExW(0, L"STATIC", L"平滑滚动",
        WS_CHILD | SS_CENTER,
        col1X, generalRow5Y + btnSize + 2, labelWidth, 16, hwndDlg, NULL, NULL, NULL);
    SendMessageW(g_hwndSmoothScrollLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hwndSmoothScrollBtn = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | BS_OWNERDRAW,
        col1X + (labelWidth - btnSize) / 2, generalRow5Y, btnSize, btnSize,
        hwndDlg, (HMENU)IDC_SMOOTH_SCROLL_CHECK, NULL, NULL);

    // 图片预览质量（通用分类第五行，平滑滚动右侧）
    g_hwndImagePreviewLabel = CreateWindowExW(0, L"STATIC", L"图片预览：",
        WS_CHILD | SS_LEFT,
        col1X + labelWidth + 30, generalRow5Y + 8, 70, 20, hwndDlg, NULL, NULL, NULL);
    SendMessageW(g_hwndImagePreviewLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hwndImagePreviewCombo = CreateWindowExW(0, L"COMBOBOX", NULL,
        WS_CHILD | CBS_DROPDOWNLIST | WS_TABSTOP,
        col1X + labelWidth + 100, generalRow5Y + 5, 80, 200,
        hwndDlg, (HMENU)IDC_IMAGE_PREVIEW_COMBO, NULL, NULL);
    SendMessageW(g_hwndImagePreviewCombo, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(g_hwndImagePreviewCombo, CB_ADDSTRING, 0, (LPARAM)L"关闭");
    SendMessageW(g_hwndImagePreviewCombo, CB_ADDSTRING, 0, (LPARAM)L"模糊");
    SendMessageW(g_hwndImagePreviewCombo, CB_ADDSTRING, 0, (LPARAM)L"标清");
    SendMessageW(g_hwndImagePreviewCombo, CB_ADDSTRING, 0, (LPARAM)L"高清");
    SendMessageW(g_hwndImagePreviewCombo, CB_SETCURSEL, (int)g_imagePreviewQuality, 0);

    // ==================== 快捷键分类控件 ====================
    // 切换快捷键
    g_hwndHotkeyLabel = CreateWindowExW(0, L"STATIC", L"切换快捷键：",
        WS_CHILD | SS_LEFT,
        20, contentY, 100, 20, hwndDlg, NULL, NULL, NULL);
    SendMessageW(g_hwndHotkeyLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hwndHotkeyEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
        WS_CHILD | WS_BORDER | WS_TABSTOP | ES_CENTER,
        120, contentY - 3, 200, 25, hwndDlg, (HMENU)IDC_HOTKEY_EDIT, NULL, NULL);
    SendMessageW(g_hwndHotkeyEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g_hwndHotkeyEdit, GWLP_WNDPROC, (LONG_PTR)HotkeyEditProc);

    wchar_t hotkeyText[128] = L"";
    if (oldModifiers & MOD_CONTROL) wcscat_s(hotkeyText, L"Ctrl+");
    if (oldModifiers & MOD_ALT) wcscat_s(hotkeyText, L"Alt+");
    if (oldModifiers & MOD_SHIFT) wcscat_s(hotkeyText, L"Shift+");
    if (oldModifiers & MOD_WIN) wcscat_s(hotkeyText, L"Win+");
    wchar_t keyName[32];
    if (GetKeyNameTextW(MapVirtualKeyW(oldVirtualKey, MAPVK_VK_TO_VSC) << 16, keyName, 32)) {
        wcscat_s(hotkeyText, keyName);
    } else {
        wcscat_s(hotkeyText, L"Space");
    }
    SetWindowTextW(g_hwndHotkeyEdit, hotkeyText);

    // 搜索框快捷键
    int hotkeyRow2Y = contentY + 35;
    g_hwndSearchHotkeyLabel = CreateWindowExW(0, L"STATIC", L"搜索框快捷键：",
        WS_CHILD | SS_LEFT,
        20, hotkeyRow2Y, 100, 20, hwndDlg, NULL, NULL, NULL);
    SendMessageW(g_hwndSearchHotkeyLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hwndSearchHotkeyEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
        WS_CHILD | WS_BORDER | WS_TABSTOP | ES_CENTER,
        120, hotkeyRow2Y - 3, 200, 25, hwndDlg, (HMENU)IDC_SEARCH_HOTKEY_EDIT, NULL, NULL);
    SendMessageW(g_hwndSearchHotkeyEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_oldSearchEditProc = (WNDPROC)SetWindowLongPtrW(g_hwndSearchHotkeyEdit, GWLP_WNDPROC, (LONG_PTR)SearchHotkeyEditProc);

    wchar_t searchHotkeyText[128] = L"";
    if (g_searchHotkeyModifiers & MOD_CONTROL) wcscat_s(searchHotkeyText, L"Ctrl+");
    if (g_searchHotkeyModifiers & MOD_ALT) wcscat_s(searchHotkeyText, L"Alt+");
    if (g_searchHotkeyModifiers & MOD_SHIFT) wcscat_s(searchHotkeyText, L"Shift+");
    if (g_searchHotkeyModifiers & MOD_WIN) wcscat_s(searchHotkeyText, L"Win+");
    wchar_t searchKeyName[32];
    if (GetKeyNameTextW(MapVirtualKeyW(g_searchHotkeyVirtualKey, MAPVK_VK_TO_VSC) << 16, searchKeyName, 32)) {
        wcscat_s(searchHotkeyText, searchKeyName);
    } else {
        wcscat_s(searchHotkeyText, L"F");
    }
    SetWindowTextW(g_hwndSearchHotkeyEdit, searchHotkeyText);

    // 快捷粘贴
    int hotkeyRow3Y = hotkeyRow2Y + 40;
    g_hwndQuickPasteLabel = CreateWindowExW(0, L"STATIC", L"快捷粘贴",
        WS_CHILD | SS_CENTER,
        20, hotkeyRow3Y + btnSize + 2, labelWidth, 16, hwndDlg, NULL, NULL, NULL);
    SendMessageW(g_hwndQuickPasteLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hwndQuickPasteBtn = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | BS_OWNERDRAW,
        20 + (labelWidth - btnSize) / 2, hotkeyRow3Y, btnSize, btnSize,
        hwndDlg, (HMENU)IDC_QUICK_PASTE_CHECK, NULL, NULL);

    g_hwndQuickPasteCombo = CreateWindowExW(0, L"COMBOBOX", NULL,
        WS_CHILD | CBS_DROPDOWNLIST | WS_TABSTOP,
        20 + labelWidth + 15, hotkeyRow3Y + 4, 100, 200,
        hwndDlg, (HMENU)IDC_QUICK_PASTE_COMBO, NULL, NULL);
    SendMessageW(g_hwndQuickPasteCombo, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(g_hwndQuickPasteCombo, CB_ADDSTRING, 0, (LPARAM)L"Alt");
    SendMessageW(g_hwndQuickPasteCombo, CB_ADDSTRING, 0, (LPARAM)L"Ctrl");
    SendMessageW(g_hwndQuickPasteCombo, CB_ADDSTRING, 0, (LPARAM)L"Shift");
    SendMessageW(g_hwndQuickPasteCombo, CB_ADDSTRING, 0, (LPARAM)L"Ctrl+Alt");
    SendMessageW(g_hwndQuickPasteCombo, CB_ADDSTRING, 0, (LPARAM)L"Ctrl+Shift");
    SendMessageW(g_hwndQuickPasteCombo, CB_ADDSTRING, 0, (LPARAM)L"Alt+Shift");
    int comboSel = 0;
    if (g_quickPasteModifiers == MOD_ALT) comboSel = 0;
    else if (g_quickPasteModifiers == MOD_CONTROL) comboSel = 1;
    else if (g_quickPasteModifiers == MOD_SHIFT) comboSel = 2;
    else if (g_quickPasteModifiers == (MOD_CONTROL | MOD_ALT)) comboSel = 3;
    else if (g_quickPasteModifiers == (MOD_CONTROL | MOD_SHIFT)) comboSel = 4;
    else if (g_quickPasteModifiers == (MOD_ALT | MOD_SHIFT)) comboSel = 5;
    SendMessageW(g_hwndQuickPasteCombo, CB_SETCURSEL, comboSel, 0);

    // ==================== 中转站分类控件 ====================
    // 用完收起
    g_hwndCollapseLabel = CreateWindowExW(0, L"STATIC", L"用完收起",
        WS_CHILD | SS_CENTER,
        col1X, contentY + btnSize + 2, labelWidth, 16, hwndDlg, NULL, NULL, NULL);
    SendMessageW(g_hwndCollapseLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hwndCollapseBtn = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | BS_OWNERDRAW,
        col1X + (labelWidth - btnSize) / 2, contentY, btnSize, btnSize,
        hwndDlg, (HMENU)IDC_COLLAPSE_AFTER_PASTE_CHECK, NULL, NULL);

    // 历史记录数量
    int transitRow2Y = contentY + btnSize + 30;
    g_hwndHistoryLimitLabel = CreateWindowExW(0, L"STATIC", L"历史记录数量：",
        WS_CHILD | SS_LEFT,
        20, transitRow2Y, 100, 20, hwndDlg, NULL, NULL, NULL);
    SendMessageW(g_hwndHistoryLimitLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hwndHistoryLimitEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"100",
        WS_CHILD | WS_BORDER | WS_TABSTOP | ES_CENTER | ES_NUMBER,
        120, transitRow2Y - 3, 80, 25, hwndDlg, (HMENU)IDC_HISTORY_LIMIT_EDIT, NULL, NULL);
    SendMessageW(g_hwndHistoryLimitEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 初始化显示"通用"分类
    g_currentSettingsTab = 0;
    SwitchSettingsTab(0);

    // 设置对话框的窗口过程
    SetWindowLongPtrW(hwndDlg, GWLP_WNDPROC, (LONG_PTR)SettingsDialogProc);

    // 显示对话框
    ShowWindow(hwndDlg, SW_SHOW);
    SetForegroundWindow(hwndDlg);
    UpdateWindow(hwndDlg);

    MSG msg;
    BOOL bRet;
    bool dialogClosed = false;

    // 设置对话框为模态对话框
    EnableWindow(hwndParent, FALSE);

    // 消息循环
    while (!dialogClosed) {
        if ((bRet = GetMessageW(&msg, NULL, 0, 0)) == 0) {
            break;
        }

        if (bRet == -1) {
            break;
        }

        // 处理对话框消息
        if (!IsDialogMessageW(hwndDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // 检查对话框是否已被销毁
        if (!IsWindow(hwndDlg)) {
            dialogClosed = true;
            break;
        }
    }
    
    // 恢复父窗口
    EnableWindow(hwndParent, TRUE);
    SetForegroundWindow(hwndParent);

    // 恢复快捷键状态
    // 如果用户修改了快捷键，自动启用；否则恢复原来的状态
    if (g_hotkeyWasModified || g_wasHotkeyEnabledBeforeDialog) {
        g_isHotkeyEnabled = true;
        RegisterHotkey(hwndParent);
    }

    // 标记对话框已关闭
    g_isSettingsDialogOpen = false;
    
    // 强制重绘主窗口
    RedrawWindow(hwndParent, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void ShowHotkeySettingsDialog(HWND hwndParent) {
    // 这个函数不再使用，改为使用统一的设置对话框
    ShowSettingsDialog(hwndParent);
}

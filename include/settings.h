#pragma once

#include <windows.h>
#include <string>

// 设置对话框控件ID
#define IDC_HOTKEY_EDIT 102
#define IDC_OPEN_DATA_FOLDER 103
#define IDC_SEARCH_HOTKEY_EDIT 105
#define IDC_NOTIFICATION_CHECK 106
#define IDC_COLLAPSE_AFTER_PASTE_CHECK 107
#define IDC_QUICK_PASTE_COMBO 108
#define IDC_QUICK_PASTE_CHECK 109
#define IDC_FAVORITE_HOTKEY_COMBO 135
#define IDC_HISTORY_LIMIT_EDIT 113
#define IDC_SMOOTH_SCROLL_CHECK 118
#define IDC_IMAGE_PREVIEW_COMBO 119
#define IDC_THEME_COMBO 120
#define IDC_SETTINGS_CLOSE 121
#define IDC_SET_DATA_DIR 122
#define IDC_CLEAR_NON_FAV 123
#define IDC_HOTKEY_BTN 124
#define IDC_SEARCH_HOTKEY_BTN 125
#define IDC_CLEAN_INVALID_IMAGES 127
#define IDC_EXPORT_DATA 136
#define IDC_TEXT_SIZE_LIMIT 137
#define IDC_IMPORT_DATA 138
#define IDC_SCROLLBAR_CHECK 128
#define IDC_SCROLLBAR_TIMEOUT_EDIT 129
#define IDC_COLOR_DOT_CHECK 130
#define IDC_THEME_STYLE_COMBO 131
#define IDC_LANGUAGE_COMBO 132
#define IDC_TASKBAR_CHECK 133
#define IDC_STARTUP_CHECK 134
#define IDC_HOVER_SELECT_CHECK 139

// 图片预览质量枚举
enum ImagePreviewQuality {
    PREVIEW_OFF = 0,
    PREVIEW_BLUR = 1,
    PREVIEW_SD = 2,
    PREVIEW_HD = 3
};

// 全局变量
extern bool g_isNotificationEnabled;
extern bool g_isCollapseAfterPaste;
extern bool g_isQuickPasteEnabled;
extern UINT g_quickPasteModifiers;
extern bool g_isFavoriteHotkeyEnabled;
extern UINT g_favoriteHotkeyModifiers;
extern bool g_allHotkeysEnabled; // 托盘快捷键总开关（false 时禁用本 app 所有快捷键）
extern bool g_isSmoothScrollEnabled;
extern bool g_isCustomScrollbarEnabled;
extern bool g_isColorDotEnabled;
extern bool g_isTaskbarVisible;
extern bool g_isStartupEnabled;
extern bool g_isHoverSelectEnabled;
extern ImagePreviewQuality g_imagePreviewQuality;
extern int g_maxHistoryCount;
extern int g_customScrollbarHideDelayMs;
extern int g_maxTextSizeKB;
extern std::wstring g_fontName;
extern int g_fontSize;
extern bool g_isSettingsDialogOpen;
extern HWND g_hwndSettingsDlg;
extern int g_currentSettingsTab;

// 函数声明
extern void ShowSettingsDialog(HWND hwndParent);
extern void ShowHotkeySettingsDialog(HWND hwndParent);
extern void RegisterQuickPasteHotkeys(HWND hwnd);
extern void UnregisterQuickPasteHotkeys(HWND hwnd);
extern void RegisterFavoriteHotkeys(HWND hwnd);
extern void UnregisterFavoriteHotkeys(HWND hwnd);
// 托盘快捷键总开关：根据各独立开关与总开关状态注册/注销本 app 所有快捷键
extern void RegisterAllHotkeys(HWND hwnd);
extern void UnregisterAllHotkeys(HWND hwnd);
extern void ApplyTaskbarVisibility(HWND hwnd);
extern void ApplyStartupPreference(bool enable);
extern bool IsStartupEnabled();

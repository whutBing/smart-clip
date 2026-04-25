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
#define IDC_HISTORY_LIMIT_EDIT 113
#define IDC_SMOOTH_SCROLL_CHECK 118
#define IDC_IMAGE_PREVIEW_COMBO 119
#define IDC_THEME_COMBO 120
#define IDC_SETTINGS_CLOSE 121
#define IDC_SET_DATA_DIR 122
#define IDC_CLEAR_NON_FAV 123
#define IDC_HOTKEY_BTN 124
#define IDC_SEARCH_HOTKEY_BTN 125
#define IDC_SMART_ACTION_ADD 126
#define IDC_CLEAN_INVALID_IMAGES 127
#define IDC_SCROLLBAR_CHECK 128
#define IDC_SCROLLBAR_TIMEOUT_EDIT 129
#define IDC_COLOR_DOT_CHECK 130
#define IDC_SMART_ACTION_TOGGLE_BASE 200
#define IDC_SMART_ACTION_DEL_BASE 300

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
extern bool g_isSmoothScrollEnabled;
extern bool g_isCustomScrollbarEnabled;
extern bool g_isColorDotEnabled;
extern ImagePreviewQuality g_imagePreviewQuality;
extern int g_maxHistoryCount;
extern int g_customScrollbarHideDelayMs;
extern std::wstring g_fontName;
extern int g_fontSize;
extern bool g_isSettingsDialogOpen;
extern HWND g_hwndSettingsDlg;

// 函数声明
extern void ShowSettingsDialog(HWND hwndParent);
extern void ShowHotkeySettingsDialog(HWND hwndParent);

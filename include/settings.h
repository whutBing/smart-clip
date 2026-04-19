#pragma once

#include <windows.h>
#include <string>

// 设置对话框相关常量
#define IDC_TAB_CONTROL 100
#define IDC_STARTUP_CHECK 101
#define IDC_HOTKEY_EDIT 102
#define IDC_OPEN_DATA_FOLDER 103  // 打开数据文件夹按钮
#define IDC_FONT_BUTTON 104  // 字体选择按钮
#define IDC_NOTIFICATION_CHECK 106  // 消息通知复选框

// 图片预览质量枚举
enum ImagePreviewQuality {
    PREVIEW_OFF = 0,      // 关闭预览
    PREVIEW_BLUR = 1,     // 模糊（64px）
    PREVIEW_SD = 2,       // 标清（128px）
    PREVIEW_HD = 3        // 高清（256px）
};

// 开机自启相关全局变量
extern bool g_isStartupEnabled;

// 消息通知相关全局变量
extern bool g_isNotificationEnabled;

// 用完收起相关全局变量
extern bool g_isCollapseAfterPaste;

// 平滑滚动相关全局变量
extern bool g_isSmoothScrollEnabled;

// 图片预览质量全局变量
extern ImagePreviewQuality g_imagePreviewQuality;

// 字体设置相关全局变量
extern std::wstring g_fontName;
extern int g_fontSize;

// 设置对话框功能
extern void ToggleStartup();
extern bool CheckStartup();
extern void ShowSettingsDialog(HWND hwndParent);
extern void ShowHotkeySettingsDialog(HWND hwndParent);

#pragma once

#include <string>
#include <windows.h>

// 快捷键相关常量
#define ID_HOTKEY_TOGGLE 3001
#define ID_HOTKEY_SEARCH 3002

// 快捷键相关全局变量
extern int g_hotkeyId;
extern bool g_isHotkeyEnabled;
extern UINT g_hotkeyVirtualKey;
extern UINT g_hotkeyModifiers;

// 搜索框快捷键相关全局变量
extern bool g_isSearchHotkeyEnabled;
extern UINT g_searchHotkeyVirtualKey;
extern UINT g_searchHotkeyModifiers;

extern bool g_isCustomScrollbarEnabled;
extern int g_customScrollbarHideDelayMs;
extern bool g_isColorDotEnabled;
extern bool g_isTopmost;

// 主题模式枚举
enum ThemeMode {
  THEME_LIGHT = 0, // 日间模式
  THEME_DARK = 1,  // 夜间模式
  THEME_SYSTEM = 2 // 跟随系统
};

// 主题相关全局变量
extern ThemeMode g_themeMode; // 当前主题模式设置
extern bool g_isDarkMode;     // 实际是否为暗黑模式（跟随系统时由系统决定）

// 主题相关函数
extern void ApplyTheme();       // 应用主题
extern bool IsSystemDarkMode(); // 检测系统是否为暗黑模式

// 快捷键管理功能
extern void SaveHotkeySettings();
extern void LoadHotkeySettings();
extern bool RegisterHotkey(HWND hwnd);
extern void UnregisterHotkey(HWND hwnd);
extern void ToggleHotkey(HWND hwnd);

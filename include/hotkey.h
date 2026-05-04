#pragma once

#include <string>
#include <windows.h>

// 快捷键相关常量
#define ID_HOTKEY_TOGGLE 3001
#define ID_HOTKEY_SEARCH 3002
#define ID_HOTKEY_PASSWORD_GENERATOR 3003
#define ID_HOTKEY_PASTE_1 3010 // 快捷粘贴1-9，第10个用0
#define ID_HOTKEY_PASTE_2 3011
#define ID_HOTKEY_PASTE_3 3012
#define ID_HOTKEY_PASTE_4 3013
#define ID_HOTKEY_PASTE_5 3014
#define ID_HOTKEY_PASTE_6 3015
#define ID_HOTKEY_PASTE_7 3016
#define ID_HOTKEY_PASTE_8 3017
#define ID_HOTKEY_PASTE_9 3018
#define ID_HOTKEY_PASTE_10 3019 // 第10个，对应数字0

// 快捷键相关全局变量
extern int g_hotkeyId;
extern bool g_isHotkeyEnabled;
extern UINT g_hotkeyVirtualKey;
extern UINT g_hotkeyModifiers;

// 搜索框快捷键相关全局变量
extern bool g_isSearchHotkeyEnabled;
extern UINT g_searchHotkeyVirtualKey;
extern UINT g_searchHotkeyModifiers;

extern bool g_isPasswordGeneratorHotkeyEnabled;
extern UINT g_passwordGeneratorHotkeyVirtualKey;
extern UINT g_passwordGeneratorHotkeyModifiers;

// 快捷粘贴修饰键相关全局变量
extern bool g_isQuickPasteEnabled;
extern UINT g_quickPasteModifiers; // 快捷粘贴的修饰键（如 MOD_ALT）
extern bool g_isCustomScrollbarEnabled;
extern int g_customScrollbarHideDelayMs;
extern bool g_isColorDotEnabled;

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

// 快捷粘贴功能
extern bool RegisterQuickPasteHotkeys(HWND hwnd);
extern void UnregisterQuickPasteHotkeys(HWND hwnd);
extern std::wstring GetQuickPasteModifierText(); // 获取修饰键文本（如 "Alt+"）

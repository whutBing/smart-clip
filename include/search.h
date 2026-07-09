#pragma once

#include <windows.h>

// 搜索功能相关全局变量
extern HWND g_hwndSearchBox;
extern HWND g_hwndSearchButton;
extern HWND g_hwndTabControl;
extern HWND g_hwndFilterFavorite;

// 搜索功能
extern void PerformSearch(HWND hwnd);

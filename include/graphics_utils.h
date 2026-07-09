#pragma once

// MinGW 下 objidl.h 必须在 gdiplus.h 之前,提供 PROPID
#include <objidl.h>
#include <gdiplus.h>
#include <windows.h>

// 生成菜单图标位图(Segoe MDL2 Assets 字符)。调用方负责 DeleteObject。
HBITMAP CreateMenuIconBitmap(const wchar_t *iconChar,
                             COLORREF color = RGB(60, 60, 60),
                             int verticalPadding = 0);

// 生成菜单颜色方块位图（用于标签颜色标识）。调用方负责 DeleteObject。
HBITMAP CreateMenuColorBitmap(COLORREF color);

// 在给定路径上追加一个圆角矩形(GDI+)。
void CreateRoundRectPath(Gdiplus::GraphicsPath *path, int x, int y, int width,
                         int height, int radius);

// 获取当前窗口适用的 UI DPI。对高分辨率屏幕设置最低 UI 放大档位：
// 2K 至少 150%，4K 至少 200%，避免主界面显得过小。
UINT GetSmartClipUiDpi(HWND hwnd);

// 按 DPI 缩放设计像素。
int ScaleForDpi(int value, UINT dpi);

// 按当前窗口适用 UI DPI 缩放设计像素。
int ScaleForWindowDpi(HWND hwnd, int value);

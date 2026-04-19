#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

// 生成 16x16 的菜单图标位图(Segoe MDL2 Assets 字符)。调用方负责 DeleteObject。
HBITMAP CreateMenuIconBitmap(const wchar_t* iconChar, COLORREF color = RGB(60, 60, 60));

// 在给定路径上追加一个圆角矩形(GDI+)。
void CreateRoundRectPath(Gdiplus::GraphicsPath* path, int x, int y, int width, int height, int radius);

#pragma once
#include <windows.h>

// 显示文本编辑弹窗。itemRectScreen 为列表项的屏幕坐标矩形。
void ShowTextEditorPopup(HWND hwndParent, int actualIndex,
                         const RECT &itemRectScreen);

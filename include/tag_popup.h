#pragma once

#include <windows.h>

void ShowTagPopup(HWND hwndParent, int x, int y, int btnWidth, bool filterMode);
void CloseTagPopup();
bool IsTagPopupVisible();
HWND GetTagPopupWindow();

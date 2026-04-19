#pragma once

#include <windows.h>
#include <vector>
#include "history.h"

// 图像处理函数声明
bool IsImageFile(const wchar_t* filePath);
bool LoadImageFile(const wchar_t* filePath, std::vector<BYTE>& imageData, int& width, int& height);
void ShowImagePreview(HWND hwndParent, const ClipboardItem& item);
LRESULT CALLBACK ImagePreviewProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

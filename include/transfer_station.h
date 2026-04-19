#pragma once

#include <windows.h>
#include "history.h"

// 中转站全局变量声明
extern HWND g_hwndFlagpole;
extern bool g_isCollapseAfterPaste;
extern HWND g_transferStationPreviousWindow;
extern bool g_isTransferStationPasting;  // 标记中转站正在粘贴，防止剪贴板监控触发

// 中转站核心功能函数
void AddToTransferStation(int historyIndex);
void RemoveFromTransferStation(int historyIndex);
void ShowTransferStation();
void ExecuteQuickPaste(int historyIndex);
void RefreshTransferStationCards();
POINT CalculateCardPosition(int index);

// 中转站窗口过程函数
LRESULT CALLBACK TransferStationCardProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK TransferStationContainerProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK FlagpoleProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

// 中转站窗口类注册函数
ATOM RegisterTransferStationContainerClass(HINSTANCE hInstance);
ATOM RegisterTransferStationCardClass(HINSTANCE hInstance);
ATOM RegisterFlagpoleClass(HINSTANCE hInstance);

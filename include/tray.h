#pragma once

#include <windows.h>
#include <shellapi.h>   // NOTIFYICONDATAW, NIIF_INFO

#define WM_TRAYICON WM_USER + 100
#define ID_TRAYICON 1

// 系统托盘相关全局变量
extern NOTIFYICONDATAW g_nid;

// 系统托盘功能
extern void ShowTrayBalloon(HWND hwnd, const wchar_t* title, const wchar_t* text, DWORD iconType = NIIF_INFO, DWORD uTimeout = 3000);
extern void AddTrayIcon(HWND hwnd);
extern void RemoveTrayIcon();
extern void RefreshTrayTooltip();

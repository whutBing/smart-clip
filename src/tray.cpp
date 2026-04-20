#include "tray.h"
#include "resource.h"

// 全局变量定义
NOTIFYICONDATAW g_nid;

// 显示托盘气泡提示
void ShowTrayBalloon(HWND /*hwnd*/, const wchar_t* title, const wchar_t* text, DWORD iconType, DWORD uTimeout) {
    NOTIFYICONDATAW nid = g_nid;

    // 先清除当前显示的通知（如果有）
    nid.uFlags |= NIF_INFO;
    nid.szInfo[0] = L'\0';  // 清空消息内容
    nid.szInfoTitle[0] = L'\0';  // 清空标题
    Shell_NotifyIconW(NIM_MODIFY, &nid);

    // 然后立即显示新通知
    wcscpy_s(nid.szInfoTitle, title);
    wcscpy_s(nid.szInfo, text);
    nid.dwInfoFlags = iconType;
    nid.uTimeout = uTimeout;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// 添加系统托盘图标（修复托盘图标不见的问题）
void AddTrayIcon(HWND hwnd) {
    ZeroMemory(&g_nid, sizeof(NOTIFYICONDATAW));
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = ID_TRAYICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;

    // 使用资源文件中的自定义图标
    g_nid.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_ICON1));
    if (g_nid.hIcon == NULL) {
        // 如果加载自定义图标失败，使用默认图标
        g_nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    }

    wcscpy_s(g_nid.szTip, L"Smart Clip");

    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

// 移除系统托盘图标
void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_nid.hIcon != NULL) {
        DestroyIcon(g_nid.hIcon);
        g_nid.hIcon = NULL;
    }
}

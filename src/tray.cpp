#include "tray.h"
#include "hotkey.h"
#include "i18n.h"
#include "resource.h"
#include "version.h"
#include <string>

// 全局变量定义
NOTIFYICONDATAW g_nid;

// 显示托盘气泡提示
void ShowTrayBalloon(HWND /*hwnd*/, const wchar_t *title, const wchar_t *text,
                     DWORD iconType, DWORD uTimeout) {
  NOTIFYICONDATAW nid = g_nid;

  // 先清除当前显示的通知（如果有）
  nid.uFlags |= NIF_INFO;
  nid.szInfo[0] = L'\0';      // 清空消息内容
  nid.szInfoTitle[0] = L'\0'; // 清空标题
  Shell_NotifyIconW(NIM_MODIFY, &nid);

  // 然后立即显示新通知
  wcscpy_s(nid.szInfoTitle, title);
  wcscpy_s(nid.szInfo, text);
  nid.dwInfoFlags = iconType;
  nid.uTimeout = uTimeout;
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// 构建托盘提示文本：第一行 "SmartClip Free V" + APP_VERSION_STRING，
// 若已设置切换快捷键则另起一行显示 "切换快捷键: Ctrl+Alt+V"
// 若已启用收藏快捷键则另起一行显示 "收藏快捷键: Ctrl+Alt+1~9"
static std::wstring BuildTrayTooltip() {
  std::wstring tip = L"SmartClip Free V" + std::wstring(APP_VERSION_STRING);

  // 暂停状态指示
  extern bool g_isClipboardPaused;
  if (g_isClipboardPaused) {
    tip += L"\n";
    tip += T(STR_TRAY_PAUSED);
  }

  // 快捷键总开关关闭状态指示
  extern bool g_allHotkeysEnabled;
  if (!g_allHotkeysEnabled) {
    tip += L"\n";
    tip += T(STR_TRAY_QUICK_PASTE_DISABLED);
  }

  // 仅当切换快捷键已配置时才显示第二行
  if (g_isHotkeyEnabled && g_hotkeyModifiers != 0 && g_hotkeyVirtualKey != 0) {
    std::wstring hotkeyText;
    if (g_hotkeyModifiers & MOD_CONTROL)
      hotkeyText += L"Ctrl+";
    if (g_hotkeyModifiers & MOD_ALT)
      hotkeyText += L"Alt+";
    if (g_hotkeyModifiers & MOD_SHIFT)
      hotkeyText += L"Shift+";
    if (g_hotkeyModifiers & MOD_WIN)
      hotkeyText += L"Win+";

    wchar_t keyName[32] = {};
    if (GetKeyNameTextW(MapVirtualKeyW(g_hotkeyVirtualKey, MAPVK_VK_TO_VSC)
                            << 16,
                        keyName, _countof(keyName))) {
      hotkeyText += keyName;
    }

    if (!hotkeyText.empty()) {
      tip += L"\n";
      tip += T(STR_ROW_HOTKEY_TOGGLE);
      tip += L": ";
      tip += hotkeyText;
    }
  }

  // 收藏快捷键
  extern bool g_isFavoriteHotkeyEnabled;
  extern UINT g_favoriteHotkeyModifiers;
  if (g_isFavoriteHotkeyEnabled && g_favoriteHotkeyModifiers != 0) {
    std::wstring favHotkeyText;
    if (g_favoriteHotkeyModifiers & MOD_CONTROL)
      favHotkeyText += L"Ctrl+";
    if (g_favoriteHotkeyModifiers & MOD_ALT)
      favHotkeyText += L"Alt+";
    if (g_favoriteHotkeyModifiers & MOD_SHIFT)
      favHotkeyText += L"Shift+";
    if (g_favoriteHotkeyModifiers & MOD_WIN)
      favHotkeyText += L"Win+";
    favHotkeyText += L"1~9";
    tip += L"\n";
    tip += T(STR_TRAY_FAVORITE_HOTKEY);
    tip += L": ";
    tip += favHotkeyText;
  }

  return tip;
}

// 添加系统托盘图标（修复托盘图标不见的问题）
void AddTrayIcon(HWND hwnd) {
  ZeroMemory(&g_nid, sizeof(NOTIFYICONDATAW));
  g_nid.cbSize = sizeof(NOTIFYICONDATAW);
  g_nid.hWnd = hwnd;
  g_nid.uID = ID_TRAYICON;
  g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  g_nid.uCallbackMessage = WM_TRAYICON;

  g_nid.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_ICON1));
  if (g_nid.hIcon == NULL) {
    // 如果加载自定义图标失败，使用默认图标
    g_nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
  }

  std::wstring tip = BuildTrayTooltip();
  wcscpy_s(g_nid.szTip, tip.c_str());

  Shell_NotifyIconW(NIM_ADD, &g_nid);
}

// 切换快捷键变更后刷新托盘提示
void RefreshTrayTooltip() {
  if (g_nid.cbSize == 0)
    return;
  std::wstring tip = BuildTrayTooltip();
  wcscpy_s(g_nid.szTip, tip.c_str());
  g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// 移除系统托盘图标
void RemoveTrayIcon() { Shell_NotifyIconW(NIM_DELETE, &g_nid); }

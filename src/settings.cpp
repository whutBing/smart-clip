#include "settings.h"
#include "graphics_utils.h"
#include "history.h"
#include "hotkey.h"
#include "password_vault.h"
#include "resource.h"
#include "smart_action.h"
#include "tray.h"
#include <commctrl.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <windowsx.h>


extern HWND g_hwndMain;

// 主窗口控件ID（用于刷新）
#define ID_SEARCH_BOX 104
#define ID_TOPMOST_BUTTON 1006

// ==================== 布局常量 ====================
#define SETTINGS_WIDTH 600
#define SETTINGS_HEIGHT 550
#define SETTINGS_TITLEBAR_H 36
#define SIDEBAR_W 180
#define CONTENT_PADDING 24
#define ROW_HEIGHT 60
#define TOGGLE_W 44
#define TOGGLE_H 24
#define TOGGLE_THUMB_R 9
#define SIDEBAR_ITEM_H 44
#define CATEGORY_HEADER_H 56

// ==================== 颜色辅助 ====================
inline COLORREF GetSettingsBgColor() {
  return g_isDarkMode ? RGB(32, 32, 36) : RGB(248, 248, 248);
}
inline COLORREF GetSidebarBgColor() {
  return g_isDarkMode ? RGB(28, 28, 32) : RGB(238, 238, 238);
}
inline COLORREF GetSidebarHoverColor() {
  return g_isDarkMode ? RGB(42, 42, 46) : RGB(225, 225, 225);
}
inline COLORREF GetDescTextColor() {
  return g_isDarkMode ? RGB(140, 140, 145) : RGB(130, 130, 130);
}
inline COLORREF GetSeparatorColor() {
  return g_isDarkMode ? RGB(55, 55, 58) : RGB(225, 225, 225);
}
inline COLORREF GetToggleOffColor() {
  return g_isDarkMode ? RGB(85, 85, 85) : RGB(190, 190, 190);
}
inline COLORREF GetSettingsTextColor() {
  return g_isDarkMode ? RGB(226, 222, 226) : RGB(60, 60, 60);
}
inline COLORREF GetSettingsEditBg() {
  return g_isDarkMode ? RGB(46, 46, 48) : RGB(255, 255, 255);
}
inline COLORREF GetTitlebarBgColor() {
  return g_isDarkMode ? RGB(24, 24, 28) : RGB(245, 245, 245);
}
#define COLOR_ACCENT (g_isDarkMode ? RGB(104, 142, 196) : RGB(0, 120, 215))

// ==================== 全局变量 ====================
bool g_isSettingsDialogOpen = false;
bool g_isNotificationEnabled = false;
bool g_isSmoothScrollEnabled = false;
bool g_isCustomScrollbarEnabled = true;
bool g_isColorDotEnabled = true;
ImagePreviewQuality g_imagePreviewQuality = PREVIEW_HD;
int g_customScrollbarHideDelayMs = 1500;
std::wstring g_fontName = L"Microsoft YaHei";
int g_fontSize = 16;
int g_fontWeight = FW_NORMAL;
BYTE g_fontItalic = FALSE;
HWND g_hwndSettingsDlg = NULL;

// PLACEHOLDER_SETTINGS_PART2

// 设置对话框内部状态
static int g_currentSettingsTab = 0;
static int g_settingsHoverSidebar = -1;
static bool g_isRecordingHotkey = false;
static bool g_isRecordingSearchHotkey = false;
static WNDPROC g_oldEditProc = NULL;
static WNDPROC g_oldSearchEditProc = NULL;
static WNDPROC g_oldIosEditProc = NULL;
static bool g_settingsClassRegistered = false;
static bool g_hotkeyConflict = false;
static bool g_searchHotkeyConflict = false;

// 数据目录路径悬浮动画
static bool g_dataDirHovered = false;
static float g_dataDirUnderlineProgress = 0.0f;
#define ID_DATADIR_UNDERLINE_TIMER 301

// 控件句柄
static HWND g_hwndSettingsClose = NULL;
static HWND g_hwndToggleNotification = NULL;
static HWND g_hwndToggleSmoothScroll = NULL;
static HWND g_hwndToggleScrollbar = NULL;
static HWND g_hwndToggleColorDot = NULL;
static HWND g_hwndThemeCombo = NULL;
static HWND g_hwndImagePreviewCombo = NULL;
static HWND g_hwndHotkeyEdit = NULL;
static HWND g_hwndSearchHotkeyEdit = NULL;
static HWND g_hwndToggleQuickPaste = NULL;
static HWND g_hwndQuickPasteCombo = NULL;
static HWND g_hwndScrollbarTimeoutEdit = NULL;
static HWND g_hwndHistoryLimitEdit = NULL;

// === 数据分类控件 ===
static HWND g_hwndSetDataDirBtn = NULL;
static HWND g_hwndClearNonFavBtn = NULL;
static HWND g_hwndCleanInvalidImagesBtn = NULL;
static std::wstring g_dataSizeText = L"计算中...";

// === 智能操作分类控件 ===
static HWND g_hwndSmartAddBtn = NULL;
static std::vector<HWND> g_smartToggleHwnds;
static std::vector<HWND> g_smartDelHwnds;

// === 密码分类控件 ===
static HWND g_hwndToggleVaultProtection = NULL;
static HWND g_hwndAuthMethodCombo = NULL;
static HWND g_hwndResetPasswordBtn = NULL;
#define IDC_VAULT_PROTECTION_TOGGLE 400
#define IDC_AUTH_METHOD_COMBO 401
#define IDC_RESET_PASSWORD_BTN 402

static void UpdateScrollbarSettingsControls();

static int GetSmartSettingsTopY() {
  return SETTINGS_TITLEBAR_H + CATEGORY_HEADER_H;
}

static int GetSmartColorDotRowY() {
  return GetSmartSettingsTopY() + 18;
}

static int GetSmartActionListStartY() {
  return GetSmartColorDotRowY() + ROW_HEIGHT + 30;
}

static void DestroySmartActionControls() {
  for (auto h : g_smartToggleHwnds)
    if (h)
      DestroyWindow(h);
  for (auto h : g_smartDelHwnds)
    if (h)
      DestroyWindow(h);
  g_smartToggleHwnds.clear();
  g_smartDelHwnds.clear();
  if (g_hwndSmartAddBtn) {
    DestroyWindow(g_hwndSmartAddBtn);
    g_hwndSmartAddBtn = NULL;
  }
}

static void CreateSmartActionControls(HWND hwndDlg);
static void RefreshSmartActionControls(HWND hwndDlg);

// 前向声明（定义在智能操作下拉选择器区域）
static HWND g_hwndSmartEditName = NULL;
static HWND g_hwndSmartEditPattern = NULL;
static HWND g_hwndSmartEditCmd = NULL;
static HWND g_hwndSmartDropdown = NULL;

// GDI 资源（WM_CREATE 创建，WM_DESTROY 释放）
static HFONT g_hTitleFont = NULL;
static HFONT g_hDescFont = NULL;
static HFONT g_hSidebarFont = NULL;
static HFONT g_hSidebarIconFont = NULL;
static HFONT g_hHeaderFont = NULL;
static HFONT g_hHeaderDescFont = NULL;
static HFONT g_hCloseIconFont = NULL;
static HBRUSH g_hSettingsBgBrush = NULL;
static HBRUSH g_hEditBgBrush = NULL;

// ==================== 辅助函数 ====================

// 获取设置行控件的 Y 坐标
static int GetRowY(int rowIndex) {
  return SETTINGS_TITLEBAR_H + CATEGORY_HEADER_H + rowIndex * ROW_HEIGHT;
}

// 获取控件右对齐 X 坐标
static int GetControlX(int controlWidth) {
  return SETTINGS_WIDTH - CONTENT_PADDING - controlWidth;
}

static std::wstring FormatHotkeyText(UINT mod, UINT vk,
                                     const wchar_t *fallbackKeyName) {
  std::wstring text;
  if (mod & MOD_CONTROL)
    text += L"Ctrl+";
  if (mod & MOD_ALT)
    text += L"Alt+";
  if (mod & MOD_SHIFT)
    text += L"Shift+";
  if (mod & MOD_WIN)
    text += L"Win+";

  wchar_t keyName[32] = {};
  if (vk != 0 &&
      GetKeyNameTextW(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16, keyName,
                      _countof(keyName))) {
    text += keyName;
  } else if (fallbackKeyName && *fallbackKeyName) {
    text += fallbackKeyName;
  }
  return text;
}

static void SyncHotkeyEditTextRect(HWND hwnd) {
  if (!hwnd)
    return;
  SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
               MAKELONG(8, 8));
  InvalidateRect(hwnd, NULL, TRUE);
}

// PLACEHOLDER_SETTINGS_PART3

// ==================== iOS 风格编辑框子类 ====================

// 前向声明
LRESULT CALLBACK HotkeyEditProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                LPARAM lParam);
LRESULT CALLBACK SearchHotkeyEditProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                      LPARAM lParam);

static LRESULT CALLBACK IosEditProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                    LPARAM lParam) {
  WNDPROC origProc = g_oldIosEditProc;
  // 快捷键编辑框使用各自的原始 proc
  if (hwnd == g_hwndHotkeyEdit) {
    origProc = g_oldEditProc;
    // 转发按键事件给快捷键处理
    if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN || uMsg == WM_CHAR ||
        uMsg == WM_SYSCHAR) {
      return HotkeyEditProc(hwnd, uMsg, wParam, lParam);
    }
  } else if (hwnd == g_hwndSearchHotkeyEdit) {
    origProc = g_oldSearchEditProc;
    if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN || uMsg == WM_CHAR ||
        uMsg == WM_SYSCHAR) {
      return SearchHotkeyEditProc(hwnd, uMsg, wParam, lParam);
    }
  }

  switch (uMsg) {
  case WM_SETFONT:
  case WM_SIZE: {
    LRESULT result = CallWindowProcW(origProc, hwnd, uMsg, wParam, lParam);
    if (hwnd == g_hwndHotkeyEdit || hwnd == g_hwndSearchHotkeyEdit)
      SyncHotkeyEditTextRect(hwnd);
    return result;
  }
  case WM_NCPAINT: {
    // 用背景色填充非客户区（避免暗黑模式下白色残留）
    HDC hdc = GetWindowDC(hwnd);
    if (hdc) {
      RECT rcWin;
      GetWindowRect(hwnd, &rcWin);
      OffsetRect(&rcWin, -rcWin.left, -rcWin.top);
      HBRUSH hBr = CreateSolidBrush(GetSettingsEditBg());
      FillRect(hdc, &rcWin, hBr);
      DeleteObject(hBr);
      ReleaseDC(hwnd, hdc);
    }
    return 0;
  }
  case WM_SETFOCUS:
    // 获得焦点时显示光标
    CallWindowProcW(origProc, hwnd, uMsg, wParam, lParam);
    ShowCaret(hwnd);
    return 0;
  case WM_KILLFOCUS:
    // 失焦时隐藏光标
    HideCaret(hwnd);
    CallWindowProcW(origProc, hwnd, uMsg, wParam, lParam);
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  case WM_SETCURSOR:
    // 失焦状态下不显示竖条光标
    if (GetFocus() != hwnd) {
      SetCursor(LoadCursor(NULL, IDC_ARROW));
      return TRUE;
    }
    break;
  case WM_PAINT: {
    // 先让默认绘制完成
    LRESULT result = CallWindowProcW(origProc, hwnd, uMsg, wParam, lParam);

    // 在父窗口 DC 上绘制圆角边框
    HWND hParent = GetParent(hwnd);
    HDC hdc = GetDC(hParent);
    if (hdc) {
      RECT rcEdit;
      GetWindowRect(hwnd, &rcEdit);
      MapWindowPoints(HWND_DESKTOP, hParent, (LPPOINT)&rcEdit, 2);

      // 扩展 1px 绘制边框
      RECT rcBorder = {rcEdit.left - 1, rcEdit.top - 1, rcEdit.right + 1,
                       rcEdit.bottom + 1};
      int w = rcBorder.right - rcBorder.left;
      int h = rcBorder.bottom - rcBorder.top;

      Gdiplus::Graphics g(hdc);
      g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

      COLORREF borderColor = GetSettingsEditBg();
      Gdiplus::GraphicsPath path;
      CreateRoundRectPath(&path, rcBorder.left, rcBorder.top, w, h, 8);
      Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(borderColor),
                                      GetGValue(borderColor),
                                      GetBValue(borderColor)),
                       1.5f);
      g.DrawPath(&pen, &path);

      // 如果是快捷键编辑框且有冲突，绘制红色感叹号
      bool showConflict = false;
      if (hwnd == g_hwndHotkeyEdit && g_hotkeyConflict)
        showConflict = true;
      if (hwnd == g_hwndSearchHotkeyEdit && g_searchHotkeyConflict)
        showConflict = true;

      if (showConflict) {
        // 红色圆角边框
        Gdiplus::Pen redPen(Gdiplus::Color(255, 220, 60, 60), 1.5f);
        g.DrawPath(&redPen, &path);

        // 红色感叹号图标（编辑框右侧）
        int iconSize = 20;
        int iconX = rcEdit.right + 4;
        int iconY = rcEdit.top + (rcEdit.bottom - rcEdit.top - iconSize) / 2;

        // 红色圆底
        Gdiplus::SolidBrush redBrush(Gdiplus::Color(255, 220, 60, 60));
        g.FillEllipse(&redBrush, iconX, iconY, iconSize, iconSize);

        // 白色感叹号
        Gdiplus::Font iconFont(L"Microsoft YaHei", 10.0f,
                               Gdiplus::FontStyleBold);
        Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF iconRect((float)iconX, (float)iconY, (float)iconSize,
                                (float)iconSize);
        g.DrawString(L"!", -1, &iconFont, iconRect, &sf, &whiteBrush);
      }

      ReleaseDC(hParent, hdc);
    }
    return result;
  }
  }
  return CallWindowProcW(origProc, hwnd, uMsg, wParam, lParam);
}

// ==================== 快捷键冲突检测 ====================

// excludeType: 0=排除切换快捷键, 1=排除搜索快捷键
static bool CheckHotkeyConflict(UINT mod, UINT vk, int excludeType) {
  // 检查与另一个快捷键是否冲突
  if (excludeType != 0 && g_isHotkeyEnabled) {
    if (g_hotkeyModifiers == mod && g_hotkeyVirtualKey == vk)
      return true;
  }
  if (excludeType != 1 && g_isSearchHotkeyEnabled) {
    if (g_searchHotkeyModifiers == mod && g_searchHotkeyVirtualKey == vk)
      return true;
  }

  // 检查与快捷粘贴是否冲突
  if (g_isQuickPasteEnabled && mod == g_quickPasteModifiers) {
    if (vk >= '0' && vk <= '9')
      return true;
  }

  return false;
}

static void UpdateHotkeyConflictState() {
  g_hotkeyConflict =
      g_isHotkeyEnabled &&
      CheckHotkeyConflict(g_hotkeyModifiers, g_hotkeyVirtualKey, 0);
  g_searchHotkeyConflict =
      g_isSearchHotkeyEnabled &&
      CheckHotkeyConflict(g_searchHotkeyModifiers, g_searchHotkeyVirtualKey, 1);
}

static void ClearRecordedHotkey(HWND hwnd, bool isSearchHotkey) {
  if (isSearchHotkey) {
    g_isRecordingSearchHotkey = false;
    g_isSearchHotkeyEnabled = false;
    g_searchHotkeyModifiers = 0;
    g_searchHotkeyVirtualKey = 0;
    SetWindowTextW(hwnd, L"");
  } else {
    g_isRecordingHotkey = false;
    g_isHotkeyEnabled = false;
    g_hotkeyModifiers = 0;
    g_hotkeyVirtualKey = 0;
    UnregisterHotkey(g_hwndMain);
    SetWindowTextW(hwnd, L"");
  }
  SaveHotkeySettings();
  UpdateHotkeyConflictState();
  InvalidateRect(hwnd, NULL, TRUE);
  if (g_hwndHotkeyEdit)
    InvalidateRect(g_hwndHotkeyEdit, NULL, TRUE);
  if (g_hwndSearchHotkeyEdit)
    InvalidateRect(g_hwndSearchHotkeyEdit, NULL, TRUE);
  if (g_hwndSettingsDlg)
    SetFocus(g_hwndSettingsDlg);
}

// ==================== 快捷键编辑框子类 ====================

LRESULT CALLBACK HotkeyEditProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                LPARAM lParam) {
  switch (uMsg) {
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN: {
    if (g_isRecordingHotkey) {
      UINT vk = (UINT)wParam;
      if (vk == VK_ESCAPE) {
        ClearRecordedHotkey(hwnd, false);
        return 0;
      }
      if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT ||
          vk == VK_LWIN || vk == VK_RWIN)
        return 0;
      UINT mod = 0;
      if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
        mod |= MOD_CONTROL;
      if (GetAsyncKeyState(VK_MENU) & 0x8000)
        mod |= MOD_ALT;
      if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        mod |= MOD_SHIFT;
      if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000)
        mod |= MOD_WIN;
      g_hotkeyModifiers = mod;
      g_hotkeyVirtualKey = vk;
      std::wstring text = FormatHotkeyText(mod, vk, L"?");
      SetWindowTextW(hwnd, text.c_str());
      g_isHotkeyEnabled = true;
      SaveHotkeySettings();
      bool regOk = RegisterHotkey(g_hwndMain);
      UpdateHotkeyConflictState();
      if (!regOk)
        g_hotkeyConflict = true;
      if (g_hotkeyConflict) {
        if (g_isNotificationEnabled)
          ShowTrayBalloon(g_hwndMain, L"快捷键冲突", L"该快捷键与其他快捷键冲突");
      } else {
        if (g_isNotificationEnabled)
          ShowTrayBalloon(g_hwndMain, L"设置已更新", L"快捷键设置已保存");
      }
      InvalidateRect(hwnd, NULL, TRUE);
      if (g_hwndSearchHotkeyEdit)
        InvalidateRect(g_hwndSearchHotkeyEdit, NULL, TRUE);
      g_isRecordingHotkey = false;
      return 0;
    }
    break;
  }
  case WM_CHAR:
  case WM_SYSCHAR:
    if (g_isRecordingHotkey)
      return 0;
    break;
  }
  return CallWindowProcW(g_oldEditProc, hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK SearchHotkeyEditProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                      LPARAM lParam) {
  switch (uMsg) {
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN: {
    if (g_isRecordingSearchHotkey) {
      UINT vk = (UINT)wParam;
      if (vk == VK_ESCAPE) {
        ClearRecordedHotkey(hwnd, true);
        return 0;
      }
      if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT ||
          vk == VK_LWIN || vk == VK_RWIN)
        return 0;
      UINT mod = 0;
      if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
        mod |= MOD_CONTROL;
      if (GetAsyncKeyState(VK_MENU) & 0x8000)
        mod |= MOD_ALT;
      if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        mod |= MOD_SHIFT;
      if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000)
        mod |= MOD_WIN;
      g_searchHotkeyModifiers = mod;
      g_searchHotkeyVirtualKey = vk;
      wchar_t text[128] = L"";
      if (mod & MOD_CONTROL)
        wcscat_s(text, L"Ctrl+");
      if (mod & MOD_ALT)
        wcscat_s(text, L"Alt+");
      if (mod & MOD_SHIFT)
        wcscat_s(text, L"Shift+");
      if (mod & MOD_WIN)
        wcscat_s(text, L"Win+");
      wchar_t kn[32];
      if (GetKeyNameTextW(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16, kn, 32))
        wcscat_s(text, kn);
      else
        wcscat_s(text, L"Unknown");
      SetWindowTextW(hwnd, text);
      g_isSearchHotkeyEnabled = true;
      SaveHotkeySettings();
      RegisterHotkey(g_hwndMain);
      UpdateHotkeyConflictState();
      if (g_searchHotkeyConflict) {
        if (g_isNotificationEnabled)
          ShowTrayBalloon(g_hwndMain, L"快捷键冲突", L"该快捷键与其他快捷键冲突");
      }
      InvalidateRect(hwnd, NULL, TRUE);
      if (g_hwndHotkeyEdit)
        InvalidateRect(g_hwndHotkeyEdit, NULL, TRUE);
      g_isRecordingSearchHotkey = false;
      return 0;
    }
    break;
  }
  case WM_CHAR:
  case WM_SYSCHAR:
    if (g_isRecordingSearchHotkey)
      return 0;
    break;
  }
  return CallWindowProcW(g_oldSearchEditProc, hwnd, uMsg, wParam, lParam);
}

// ==================== 分类切换 ====================

static void SwitchSettingsTab(int tab) {
  g_currentSettingsTab = tab;

  // 销毁临时编辑框
  if (g_hwndSmartEditName) {
    DestroyWindow(g_hwndSmartEditName);
    g_hwndSmartEditName = NULL;
  }
  if (g_hwndSmartEditPattern) {
    DestroyWindow(g_hwndSmartEditPattern);
    g_hwndSmartEditPattern = NULL;
  }
  if (g_hwndSmartEditCmd) {
    DestroyWindow(g_hwndSmartEditCmd);
    g_hwndSmartEditCmd = NULL;
  }
  if (g_hwndSmartDropdown) {
    DestroyWindow(g_hwndSmartDropdown);
    g_hwndSmartDropdown = NULL;
  }

  int showGen = (tab == 0) ? SW_SHOW : SW_HIDE;
  int showHk = (tab == 1) ? SW_SHOW : SW_HIDE;
  int showDt = (tab == 2) ? SW_SHOW : SW_HIDE;

  if (g_hwndToggleNotification)
    ShowWindow(g_hwndToggleNotification, showGen);
  if (g_hwndToggleSmoothScroll)
    ShowWindow(g_hwndToggleSmoothScroll, showGen);
  if (g_hwndToggleScrollbar)
    ShowWindow(g_hwndToggleScrollbar, showGen);
  if (g_hwndToggleColorDot)
    ShowWindow(g_hwndToggleColorDot, (tab == 3) ? SW_SHOW : SW_HIDE);
  if (g_hwndScrollbarTimeoutEdit)
    ShowWindow(g_hwndScrollbarTimeoutEdit, showGen);
  if (g_hwndThemeCombo)
    ShowWindow(g_hwndThemeCombo, showGen);
  if (g_hwndImagePreviewCombo)
    ShowWindow(g_hwndImagePreviewCombo, showGen);
  if (g_hwndHistoryLimitEdit)
    ShowWindow(g_hwndHistoryLimitEdit, showGen);

  if (g_hwndHotkeyEdit)
    ShowWindow(g_hwndHotkeyEdit, showHk);
  if (g_hwndSearchHotkeyEdit)
    ShowWindow(g_hwndSearchHotkeyEdit, showHk);
  if (g_hwndToggleQuickPaste)
    ShowWindow(g_hwndToggleQuickPaste, showHk);
  if (g_hwndQuickPasteCombo)
    ShowWindow(g_hwndQuickPasteCombo, showHk);

  if (g_hwndSetDataDirBtn)
    ShowWindow(g_hwndSetDataDirBtn, showDt);
  if (g_hwndClearNonFavBtn)
    ShowWindow(g_hwndClearNonFavBtn, showDt);
  if (g_hwndCleanInvalidImagesBtn)
    ShowWindow(g_hwndCleanInvalidImagesBtn, showDt);

  // 智能操作控件
  int showSa = (tab == 3) ? SW_SHOW : SW_HIDE;
  for (auto h : g_smartToggleHwnds)
    if (h)
      ShowWindow(h, showSa);
  for (auto h : g_smartDelHwnds)
    if (h)
      ShowWindow(h, showSa);
  if (g_hwndSmartAddBtn)
    ShowWindow(g_hwndSmartAddBtn, showSa);

  // 切换到数据页时刷新磁盘空间
  if (tab == 2) {
    g_dataSizeText = FormatFileSize(GetDataDirSize());
  }

  // 切换到智能操作页时刷新控件
  if (tab == 3 && g_hwndSettingsDlg) {
    RefreshSmartActionControls(g_hwndSettingsDlg);
  }

  // 密码分类控件
  int showPw = (tab == 4) ? SW_SHOW : SW_HIDE;
  if (g_hwndToggleVaultProtection)
    ShowWindow(g_hwndToggleVaultProtection, showPw);
  if (g_hwndAuthMethodCombo) {
    ShowWindow(g_hwndAuthMethodCombo,
               (tab == 4 && g_vaultProtectionEnabled) ? SW_SHOW : SW_HIDE);
    EnableWindow(g_hwndAuthMethodCombo, g_vaultProtectionEnabled);
  }
  if (g_hwndResetPasswordBtn) {
    ShowWindow(g_hwndResetPasswordBtn,
               (tab == 4 && g_vaultProtectionEnabled) ? SW_SHOW : SW_HIDE);
    EnableWindow(g_hwndResetPasswordBtn,
                 g_vaultProtectionEnabled && IsMasterPasswordSet());
  }

  if (g_hwndSettingsDlg)
    InvalidateRect(g_hwndSettingsDlg, NULL, TRUE);
  UpdateScrollbarSettingsControls();
}

// PLACEHOLDER_SETTINGS_PART4

// ==================== 侧边栏数据 ====================
struct SidebarItem {
  const wchar_t *icon;
  const wchar_t *label;
};
static const SidebarItem g_sidebarItems[] = {
    {L"\uE713", L"通用"},
    {L"\uE765", L"快捷键"},

    {L"", L"数据"},
    {L"", L"智能操作"},
    {L"", L"密码"},
};
#define SIDEBAR_COUNT 5

// 设置行数据
struct SettingRowInfo {
  const wchar_t *title;
  const wchar_t *desc;
};

static const SettingRowInfo g_generalRows[] = {
    {L"消息通知", L"操作时显示系统通知"},
    {L"平滑滚动", L"列表滚动时使用平滑动画"},
    {L"显示滚动条", L"显示右侧悬浮滚动条并支持拖拽"},
    {L"停留时间", L"设置滚动条停止后的停留时长"},
    {L"主题模式", L"切换日间、夜间或跟随系统"},
    {L"图片预览质量", L"设置剪贴板图片的预览清晰度"},
    {L"历史记录数量", L"最多保存的剪贴板记录条数"},
};
static const SettingRowInfo g_hotkeyRows[] = {
    {L"切换快捷键", L"显示/隐藏 Smart Clip 窗口"},
    {L"搜索框快捷键", L"聚焦搜索框的快捷键"},
    {L"快捷粘贴", L"使用修饰键+数字快速粘贴"},
    {L"快捷粘贴修饰键", L"选择快捷粘贴使用的修饰键"},
};
static const SettingRowInfo g_dataRows[] = {
    {L"占用磁盘空间", L""},
    {L"粘贴次数", L""},
    {L"设置数据目录", L""},
    {L"清理非收藏数据", L"删除所有未收藏的历史记录"},
    {L"删除失效图片", L"清理原始图片已丢失的记录"},
};
static const SettingRowInfo g_passwordRows[] = {
    {L"开启密码保护", L"访问密码库时需要验证身份"},
    {L"认证方式", L"选择解锁密码库的验证方式"},
    {L"重置主密码", L"修改主密码（需验证旧密码）"},
};

// 分类标题
struct CategoryHeader {
  const wchar_t *title;
  const wchar_t *desc;
  const SettingRowInfo *rows;
  int rowCount;
};
static const CategoryHeader g_categories[] = {
    {L"通用", L"基本设置和外观", g_generalRows, 7},
    {L"快捷键", L"快捷键配置", g_hotkeyRows, 4},
    {L"数据", L"", g_dataRows, 5},
    {L"智能操作", L"根据内容自动执行操作", NULL, 0},
    {L"密码", L"密码库保护设置", g_passwordRows, 3},
};

// ==================== 绘制辅助 ====================

static void DrawToggleSwitch(HDC hdc, RECT rc, bool isOn) {
  Gdiplus::Graphics g(hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

  COLORREF pillColor = isOn ? COLOR_ACCENT : GetToggleOffColor();
  Gdiplus::GraphicsPath pillPath;
  int radius = TOGGLE_H / 2;
  CreateRoundRectPath(&pillPath, rc.left, rc.top, TOGGLE_W, TOGGLE_H, radius);
  Gdiplus::SolidBrush pillBrush(Gdiplus::Color(
      255, GetRValue(pillColor), GetGValue(pillColor), GetBValue(pillColor)));
  g.FillPath(&pillBrush, &pillPath);

  int thumbX = isOn ? (rc.left + TOGGLE_W - radius) : (rc.left + radius);
  int thumbY = rc.top + radius;
  Gdiplus::SolidBrush thumbBrush(Gdiplus::Color(255, 255, 255, 255));
  g.FillEllipse(&thumbBrush, thumbX - TOGGLE_THUMB_R, thumbY - TOGGLE_THUMB_R,
                TOGGLE_THUMB_R * 2, TOGGLE_THUMB_R * 2);
}

static void UpdateSettingsBrushes() {
  if (g_hSettingsBgBrush)
    DeleteObject(g_hSettingsBgBrush);
  g_hSettingsBgBrush = CreateSolidBrush(GetSettingsBgColor());
  if (g_hEditBgBrush)
    DeleteObject(g_hEditBgBrush);
  g_hEditBgBrush = CreateSolidBrush(GetSettingsEditBg());
}

static void UpdateScrollbarSettingsControls() {
  if (g_hwndScrollbarTimeoutEdit) {
    EnableWindow(g_hwndScrollbarTimeoutEdit, g_isCustomScrollbarEnabled);
    InvalidateRect(g_hwndScrollbarTimeoutEdit, NULL, TRUE);
  }
}

// ==================== 自定义下拉选择器 ====================

#define DROPDOWN_ITEM_H 34
#define DROPDOWN_PADDING 6
#define DROPDOWN_RADIUS 8

struct DropdownInfo {
  int ctlId;
  const wchar_t **items;
  int itemCount;
  int selectedIndex;
};

static HWND g_hwndDropdownPopup = NULL;
static DropdownInfo g_activeDropdown = {};
static int g_dropdownHoverIndex = -1;
static bool g_dropdownClassRegistered = false;

static const wchar_t *g_themeItems[] = {L"日间", L"夜间", L"跟随系统"};
static const wchar_t *g_previewItems[] = {L"关闭", L"模糊", L"标清", L"高清"};
static const wchar_t *g_quickPasteItems[] = {
    L"Alt", L"Ctrl", L"Shift", L"Ctrl+Alt", L"Ctrl+Shift", L"Alt+Shift"};
static const wchar_t *g_authMethodItems[] = {L"主密码", L"Windows Hello"};

// 获取下拉按钮当前显示文字
static const wchar_t *GetDropdownText(int ctlId) {
  if (ctlId == IDC_THEME_COMBO)
    return g_themeItems[(int)g_themeMode];
  if (ctlId == IDC_IMAGE_PREVIEW_COMBO)
    return g_previewItems[(int)g_imagePreviewQuality];
  if (ctlId == IDC_QUICK_PASTE_COMBO) {
    if (g_quickPasteModifiers == MOD_ALT)
      return g_quickPasteItems[0];
    if (g_quickPasteModifiers == MOD_CONTROL)
      return g_quickPasteItems[1];
    if (g_quickPasteModifiers == MOD_SHIFT)
      return g_quickPasteItems[2];
    if (g_quickPasteModifiers == (MOD_CONTROL | MOD_ALT))
      return g_quickPasteItems[3];
    if (g_quickPasteModifiers == (MOD_CONTROL | MOD_SHIFT))
      return g_quickPasteItems[4];
    if (g_quickPasteModifiers == (MOD_ALT | MOD_SHIFT))
      return g_quickPasteItems[5];
    return g_quickPasteItems[0];
  }
  if (ctlId == IDC_AUTH_METHOD_COMBO)
    return g_authMethodItems[g_vaultAuthMethod];
  return L"";
}

static int GetDropdownSelectedIndex(int ctlId) {
  if (ctlId == IDC_THEME_COMBO)
    return (int)g_themeMode;
  if (ctlId == IDC_IMAGE_PREVIEW_COMBO)
    return (int)g_imagePreviewQuality;
  if (ctlId == IDC_QUICK_PASTE_COMBO) {
    if (g_quickPasteModifiers == MOD_ALT)
      return 0;
    if (g_quickPasteModifiers == MOD_CONTROL)
      return 1;
    if (g_quickPasteModifiers == MOD_SHIFT)
      return 2;
    if (g_quickPasteModifiers == (MOD_CONTROL | MOD_ALT))
      return 3;
    if (g_quickPasteModifiers == (MOD_CONTROL | MOD_SHIFT))
      return 4;
    if (g_quickPasteModifiers == (MOD_ALT | MOD_SHIFT))
      return 5;
    return 0;
  }
  if (ctlId == IDC_AUTH_METHOD_COMBO)
    return g_vaultAuthMethod;
  return 0;
}

// 绘制下拉按钮（BS_OWNERDRAW）
static void DrawDropdownButton(HDC hdc, RECT rc, int ctlId) {
  Gdiplus::Graphics g(hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

  // 圆角背景
  COLORREF bg = GetSettingsEditBg();
  Gdiplus::GraphicsPath path;
  CreateRoundRectPath(&path, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                      DROPDOWN_RADIUS);
  Gdiplus::SolidBrush bgBrush(
      Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
  g.FillPath(&bgBrush, &path);

  // 文字
  const wchar_t *text = GetDropdownText(ctlId);
  COLORREF tc = GetSettingsTextColor();
  Gdiplus::SolidBrush textBrush(
      Gdiplus::Color(255, GetRValue(tc), GetGValue(tc), GetBValue(tc)));
  Gdiplus::Font font(L"Microsoft YaHei", 10.0f);
  Gdiplus::StringFormat sf;
  sf.SetAlignment(Gdiplus::StringAlignmentNear);
  sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  Gdiplus::RectF textRect(10.0f, 0, (float)(rc.right - rc.left - 30),
                          (float)(rc.bottom - rc.top));
  g.DrawString(text, -1, &font, textRect, &sf, &textBrush);

  // 下拉箭头
  Gdiplus::Font iconFont(L"Segoe MDL2 Assets", 8.0f);
  Gdiplus::StringFormat iconSf;
  iconSf.SetAlignment(Gdiplus::StringAlignmentCenter);
  iconSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  COLORREF ac = RGB(150, 150, 150);
  Gdiplus::SolidBrush arrowBrush(
      Gdiplus::Color(255, GetRValue(ac), GetGValue(ac), GetBValue(ac)));
  Gdiplus::RectF arrowRect((float)(rc.right - rc.left - 24), 0, 20.0f,
                           (float)(rc.bottom - rc.top));
  g.DrawString(L"\uE70D", -1, &iconFont, arrowRect, &iconSf, &arrowBrush);
}

// 弹出下拉窗口过程
LRESULT CALLBACK DropdownPopupProc(HWND hwnd, UINT msg, WPARAM wParam,
                                   LPARAM lParam) {
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // 背景
    COLORREF bg = GetSettingsEditBg();
    Gdiplus::GraphicsPath bgPath;
    CreateRoundRectPath(&bgPath, 0, 0, rcClient.right, rcClient.bottom,
                        DROPDOWN_RADIUS);
    Gdiplus::SolidBrush bgBrush(
        Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
    g.FillPath(&bgBrush, &bgPath);

    // 边框
    COLORREF bc = GetSeparatorColor();
    Gdiplus::Pen borderPen(
        Gdiplus::Color(255, GetRValue(bc), GetGValue(bc), GetBValue(bc)), 1.0f);
    g.DrawPath(&borderPen, &bgPath);

    Gdiplus::Font font(L"Microsoft YaHei", 10.0f);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentNear);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    for (int i = 0; i < g_activeDropdown.itemCount; i++) {
      int y = DROPDOWN_PADDING + i * DROPDOWN_ITEM_H;
      Gdiplus::RectF itemRect((float)DROPDOWN_PADDING, (float)y,
                              (float)(rcClient.right - DROPDOWN_PADDING * 2),
                              (float)DROPDOWN_ITEM_H);

      // 悬浮高亮
      if (i == g_dropdownHoverIndex) {
        COLORREF hc = g_isDarkMode ? RGB(55, 55, 60) : RGB(230, 230, 230);
        Gdiplus::GraphicsPath hoverPath;
        CreateRoundRectPath(&hoverPath, (int)itemRect.X, (int)itemRect.Y,
                            (int)itemRect.Width, (int)itemRect.Height, 6);
        Gdiplus::SolidBrush hoverBrush(
            Gdiplus::Color(255, GetRValue(hc), GetGValue(hc), GetBValue(hc)));
        g.FillPath(&hoverBrush, &hoverPath);
      }

      // 选中标记
      bool selected = (i == g_activeDropdown.selectedIndex);
      if (selected) {
        Gdiplus::SolidBrush checkBrush(Gdiplus::Color(255, 0, 120, 215));
        g.FillEllipse(&checkBrush, (float)(DROPDOWN_PADDING + 6),
                      (float)(y + DROPDOWN_ITEM_H / 2 - 4), 8.0f, 8.0f);
      }

      // 文字
      COLORREF tc = GetSettingsTextColor();
      Gdiplus::SolidBrush textBrush(
          Gdiplus::Color(255, GetRValue(tc), GetGValue(tc), GetBValue(tc)));
      Gdiplus::RectF textRect(
          (float)(DROPDOWN_PADDING + 22), (float)y,
          (float)(rcClient.right - DROPDOWN_PADDING * 2 - 22),
          (float)DROPDOWN_ITEM_H);
      g.DrawString(g_activeDropdown.items[i], -1, &font, textRect, &sf,
                   &textBrush);
    }

    EndPaint(hwnd, &ps);
    return 0;
  }

  case WM_MOUSEMOVE: {
    int y = GET_Y_LPARAM(lParam);
    int idx = (y - DROPDOWN_PADDING) / DROPDOWN_ITEM_H;
    if (idx < 0 || idx >= g_activeDropdown.itemCount)
      idx = -1;
    if (idx != g_dropdownHoverIndex) {
      g_dropdownHoverIndex = idx;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    return 0;
  }

  case WM_MOUSELEAVE:
    g_dropdownHoverIndex = -1;
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;

  case WM_LBUTTONDOWN: {
    int y = GET_Y_LPARAM(lParam);
    int idx = (y - DROPDOWN_PADDING) / DROPDOWN_ITEM_H;
    if (idx >= 0 && idx < g_activeDropdown.itemCount) {
      g_activeDropdown.selectedIndex = idx;
      // 通知设置窗口
      PostMessageW(g_hwndSettingsDlg, WM_COMMAND,
                   MAKEWPARAM(g_activeDropdown.ctlId, CBN_SELCHANGE), 0);
    }
    DestroyWindow(hwnd);
    g_hwndDropdownPopup = NULL;
    return 0;
  }

  case WM_ACTIVATE:
    if (LOWORD(wParam) == WA_INACTIVE) {
      DestroyWindow(hwnd);
      g_hwndDropdownPopup = NULL;
    }
    return 0;

  case WM_DESTROY:
    g_hwndDropdownPopup = NULL;
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void RegisterDropdownClass() {
  if (g_dropdownClassRegistered)
    return;
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
  wc.lpfnWndProc = DropdownPopupProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.lpszClassName = L"SmartClipDropdown";
  RegisterClassExW(&wc);
  g_dropdownClassRegistered = true;
}

static void ShowDropdownPopup(HWND hwndBtn, int ctlId) {
  if (g_hwndDropdownPopup) {
    DestroyWindow(g_hwndDropdownPopup);
    g_hwndDropdownPopup = NULL;
  }

  g_activeDropdown.ctlId = ctlId;
  if (ctlId == IDC_THEME_COMBO) {
    g_activeDropdown.items = g_themeItems;
    g_activeDropdown.itemCount = 3;
  } else if (ctlId == IDC_IMAGE_PREVIEW_COMBO) {
    g_activeDropdown.items = g_previewItems;
    g_activeDropdown.itemCount = 4;
  } else if (ctlId == IDC_QUICK_PASTE_COMBO) {
    g_activeDropdown.items = g_quickPasteItems;
    g_activeDropdown.itemCount = 6;
  } else if (ctlId == IDC_AUTH_METHOD_COMBO) {
    g_activeDropdown.items = g_authMethodItems;
    g_activeDropdown.itemCount = 2;
  }
  g_activeDropdown.selectedIndex = GetDropdownSelectedIndex(ctlId);
  g_dropdownHoverIndex = -1;

  RegisterDropdownClass();

  RECT rcBtn;
  GetWindowRect(hwndBtn, &rcBtn);
  int popupW = rcBtn.right - rcBtn.left;
  if (popupW < 130)
    popupW = 130;
  int popupH =
      DROPDOWN_PADDING * 2 + g_activeDropdown.itemCount * DROPDOWN_ITEM_H;

  g_hwndDropdownPopup = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"SmartClipDropdown", NULL, WS_POPUP,
      rcBtn.left, rcBtn.bottom + 2, popupW, popupH, g_hwndSettingsDlg, NULL,
      GetModuleHandleW(NULL), NULL);

  // 圆角区域
  HRGN hRgn = CreateRoundRectRgn(0, 0, popupW + 1, popupH + 1,
                                 DROPDOWN_RADIUS * 2, DROPDOWN_RADIUS * 2);
  SetWindowRgn(g_hwndDropdownPopup, hRgn, TRUE);

  ShowWindow(g_hwndDropdownPopup, SW_SHOW);
  UpdateWindow(g_hwndDropdownPopup);
}

// ==================== 智能操作下拉选择器 ====================

static int g_smartActionEditIndex = -1;
static int g_smartDropdownHover = -1;
static bool g_smartDropdownClassRegistered = false;

static int g_smartEditFieldIndex = -1;
static WNDPROC g_oldSmartEditProc = NULL;

// 构建扁平列表：分类标题(isSeparator=true) + 模板项
struct SmartDropdownItem {
  const wchar_t *text;
  int templateIndex; // -1 = 分类标题
  bool isSeparator;
};
static std::vector<SmartDropdownItem> g_smartDropdownItems;

static void BuildSmartDropdownItems() {
  g_smartDropdownItems.clear();
  const wchar_t *lastCat = NULL;
  for (int i = 0; i < g_actionTemplateCount; i++) {
    if (lastCat == NULL ||
        wcscmp(lastCat, g_actionTemplates[i].category) != 0) {
      lastCat = g_actionTemplates[i].category;
      SmartDropdownItem sep;
      sep.text = lastCat;
      sep.templateIndex = -1;
      sep.isSeparator = true;
      g_smartDropdownItems.push_back(sep);
    }
    SmartDropdownItem item;
    item.text = g_actionTemplates[i].name;
    item.templateIndex = i;
    item.isSeparator = false;
    g_smartDropdownItems.push_back(item);
  }
}

#define SMART_DD_ITEM_H 30
#define SMART_DD_SEP_H 24
#define SMART_DD_PAD 6

static LRESULT CALLBACK SmartEditProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                      LPARAM lParam);

LRESULT CALLBACK SmartDropdownProc(HWND hwnd, UINT msg, WPARAM wParam,
                                   LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

    // 双缓冲防闪烁
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hBmp = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

    Gdiplus::Graphics g(hdcMem);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    COLORREF bg = GetSettingsEditBg();
    Gdiplus::GraphicsPath bgPath;
    CreateRoundRectPath(&bgPath, 0, 0, rcClient.right, rcClient.bottom,
                        DROPDOWN_RADIUS);
    Gdiplus::SolidBrush bgBrush(
        Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
    g.FillPath(&bgBrush, &bgPath);
    COLORREF bc = GetSeparatorColor();
    Gdiplus::Pen borderPen(
        Gdiplus::Color(255, GetRValue(bc), GetGValue(bc), GetBValue(bc)), 1.0f);
    g.DrawPath(&borderPen, &bgPath);

    int y = SMART_DD_PAD;
    for (int i = 0; i < (int)g_smartDropdownItems.size(); i++) {
      const auto &di = g_smartDropdownItems[i];
      int h = di.isSeparator ? SMART_DD_SEP_H : SMART_DD_ITEM_H;

      if (di.isSeparator) {
        Gdiplus::Font catFont(L"Microsoft YaHei", 8.0f);
        COLORREF dc = GetDescTextColor();
        Gdiplus::SolidBrush catBrush(
            Gdiplus::Color(255, GetRValue(dc), GetGValue(dc), GetBValue(dc)));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentNear);
        sf.SetLineAlignment(Gdiplus::StringAlignmentFar);
        Gdiplus::RectF catRect((float)(SMART_DD_PAD + 8), (float)y,
                               (float)(rcClient.right - 20), (float)h);
        g.DrawString(di.text, -1, &catFont, catRect, &sf, &catBrush);
      } else {
        if (i == g_smartDropdownHover) {
          COLORREF hc = g_isDarkMode ? RGB(55, 55, 60) : RGB(230, 230, 230);
          Gdiplus::GraphicsPath hoverPath;
          CreateRoundRectPath(&hoverPath, SMART_DD_PAD, y,
                              rcClient.right - SMART_DD_PAD * 2, h, 6);
          Gdiplus::SolidBrush hoverBrush(
              Gdiplus::Color(255, GetRValue(hc), GetGValue(hc), GetBValue(hc)));
          g.FillPath(&hoverBrush, &hoverPath);
        }
        Gdiplus::Font font(L"Microsoft YaHei", 9.5f);
        COLORREF tc = GetSettingsTextColor();
        Gdiplus::SolidBrush textBrush(
            Gdiplus::Color(255, GetRValue(tc), GetGValue(tc), GetBValue(tc)));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentNear);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF textRect((float)(SMART_DD_PAD + 16), (float)y,
                                (float)(rcClient.right - 40), (float)h);
        g.DrawString(di.text, -1, &font, textRect, &sf, &textBrush);
      }
      y += h;
    }

    BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, hdcMem, 0, 0, SRCCOPY);
    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_MOUSEMOVE: {
    int my = GET_Y_LPARAM(lParam);
    int y = SMART_DD_PAD;
    int newHover = -1;
    for (int i = 0; i < (int)g_smartDropdownItems.size(); i++) {
      int h = g_smartDropdownItems[i].isSeparator ? SMART_DD_SEP_H
                                                  : SMART_DD_ITEM_H;
      if (my >= y && my < y + h && !g_smartDropdownItems[i].isSeparator) {
        newHover = i;
        break;
      }
      y += h;
    }
    if (newHover != g_smartDropdownHover) {
      g_smartDropdownHover = newHover;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    return 0;
  }
  case WM_MOUSELEAVE:
    g_smartDropdownHover = -1;
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  case WM_LBUTTONDOWN: {
    if (g_smartDropdownHover >= 0 &&
        g_smartDropdownHover < (int)g_smartDropdownItems.size()) {
      const auto &di = g_smartDropdownItems[g_smartDropdownHover];
      if (!di.isSeparator && di.templateIndex >= 0) {
        int ti = di.templateIndex;
        const auto &tmpl = g_actionTemplates[ti];
        if (g_smartActionEditIndex >= 0 &&
            g_smartActionEditIndex < (int)g_smartActions.size()) {
          auto &rule = g_smartActions[g_smartActionEditIndex];
          rule.action = tmpl.action;
          rule.customCmd = tmpl.cmdTemplate;

          // "自定义命令"或"自定义URL" — 弹出输入框
          if (wcscmp(tmpl.name, L"自定义命令") == 0 ||
              wcscmp(tmpl.name, L"自定义URL") == 0) {
            DestroyWindow(hwnd);
            g_hwndSmartDropdown = NULL;
            // 销毁已有的临时编辑框
            if (g_hwndSmartEditCmd) {
              DestroyWindow(g_hwndSmartEditCmd);
              g_hwndSmartEditCmd = NULL;
            }
            if (g_hwndSmartEditName) {
              DestroyWindow(g_hwndSmartEditName);
              g_hwndSmartEditName = NULL;
            }
            if (g_hwndSmartEditPattern) {
              DestroyWindow(g_hwndSmartEditPattern);
              g_hwndSmartEditPattern = NULL;
            }
            // 在动作区域创建临时编辑框
            int ruleRowH = 50;
            int startY = GetSmartActionListStartY();
            int rowY = startY + g_smartActionEditIndex * ruleRowH;
            int contentRight = SETTINGS_WIDTH - CONTENT_PADDING;
            int editX = contentRight - 170;
            int editW = 140;
            g_smartEditFieldIndex = g_smartActionEditIndex;
            g_hwndSmartEditCmd = CreateWindowExW(
                0, L"EDIT", rule.customCmd.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, editX,
                rowY + 10, editW, 20, g_hwndSettingsDlg, NULL,
                GetModuleHandleW(NULL), NULL);
            HFONT hFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                                      FALSE, DEFAULT_CHARSET, 0, 0,
                                      CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");
            SendMessageW(g_hwndSmartEditCmd, WM_SETFONT, (WPARAM)hFont, TRUE);
            g_oldSmartEditProc = (WNDPROC)SetWindowLongPtrW(
                g_hwndSmartEditCmd, GWLP_WNDPROC, (LONG_PTR)SmartEditProc);
            SetFocus(g_hwndSmartEditCmd);
            SendMessageW(g_hwndSmartEditCmd, EM_SETSEL, 0, -1);
            InvalidateRect(g_hwndSettingsDlg, NULL, TRUE);
            return 0;
          }

          SaveSmartActions();
          // 销毁已有的临时编辑框
          if (g_hwndSmartEditCmd) {
            DestroyWindow(g_hwndSmartEditCmd);
            g_hwndSmartEditCmd = NULL;
          }
          if (g_hwndSmartEditName) {
            DestroyWindow(g_hwndSmartEditName);
            g_hwndSmartEditName = NULL;
          }
          if (g_hwndSmartEditPattern) {
            DestroyWindow(g_hwndSmartEditPattern);
            g_hwndSmartEditPattern = NULL;
          }
        }
      }
    }
    DestroyWindow(hwnd);
    g_hwndSmartDropdown = NULL;
    if (g_hwndSettingsDlg) {
      SetForegroundWindow(g_hwndSettingsDlg);
      InvalidateRect(g_hwndSettingsDlg, NULL, TRUE);
    }
    return 0;
  }
  case WM_ACTIVATE:
    if (LOWORD(wParam) == WA_INACTIVE) {
      DestroyWindow(hwnd);
      g_hwndSmartDropdown = NULL;
    }
    return 0;
  case WM_DESTROY:
    g_hwndSmartDropdown = NULL;
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void RegisterSmartDropdownClass() {
  if (g_smartDropdownClassRegistered)
    return;
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
  wc.lpfnWndProc = SmartDropdownProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.lpszClassName = L"SmartClipSmartDropdown";
  RegisterClassExW(&wc);
  g_smartDropdownClassRegistered = true;
}

static void ShowSmartActionDropdown(int ruleIndex) {
  if (g_hwndSmartDropdown) {
    DestroyWindow(g_hwndSmartDropdown);
    g_hwndSmartDropdown = NULL;
  }
  g_smartActionEditIndex = ruleIndex;
  g_smartDropdownHover = -1;
  BuildSmartDropdownItems();
  RegisterSmartDropdownClass();

  // 计算弹窗位置（在动作文字右侧下方）
  int ruleRowH = 50;
  int startY = GetSmartActionListStartY();
  int rowY = startY + ruleIndex * ruleRowH;
  int contentRight = SETTINGS_WIDTH - CONTENT_PADDING;

  RECT rcDlg;
  GetWindowRect(g_hwndSettingsDlg, &rcDlg);
  int popupX = rcDlg.left + contentRight - 180;
  int popupY = rcDlg.top + rowY + 32;
  int popupW = 180;

  // 计算高度
  int totalH = SMART_DD_PAD * 2;
  for (const auto &di : g_smartDropdownItems) {
    totalH += di.isSeparator ? SMART_DD_SEP_H : SMART_DD_ITEM_H;
  }

  g_hwndSmartDropdown = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"SmartClipSmartDropdown", NULL,
      WS_POPUP, popupX, popupY, popupW, totalH, g_hwndSettingsDlg, NULL,
      GetModuleHandleW(NULL), NULL);
  ShowWindow(g_hwndSmartDropdown, SW_SHOW);
  SetForegroundWindow(g_hwndSmartDropdown);
}

// ==================== 智能操作临时编辑框 ====================

static LRESULT CALLBACK SmartEditProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                      LPARAM lParam) {
  if (uMsg == WM_KILLFOCUS) {
    // 保存内容
    wchar_t buf[512] = {};
    GetWindowTextW(hwnd, buf, 512);
    if (g_smartEditFieldIndex >= 0 &&
        g_smartEditFieldIndex < (int)g_smartActions.size()) {
      if (hwnd == g_hwndSmartEditName) {
        g_smartActions[g_smartEditFieldIndex].name = buf;
      } else if (hwnd == g_hwndSmartEditPattern) {
        g_smartActions[g_smartEditFieldIndex].pattern = buf;
      } else if (hwnd == g_hwndSmartEditCmd) {
        g_smartActions[g_smartEditFieldIndex].customCmd = buf;
      }
      SaveSmartActions();
    }
    // 延迟销毁
    PostMessageW(g_hwndSettingsDlg, WM_USER + 100, (WPARAM)hwnd, 0);
    if (g_hwndSettingsDlg)
      InvalidateRect(g_hwndSettingsDlg, NULL, TRUE);
    return 0;
  }
  if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
    SetFocus(g_hwndSettingsDlg);
    return 0;
  }
  if (uMsg == WM_KEYDOWN && wParam == VK_ESCAPE) {
    SetFocus(g_hwndSettingsDlg);
    return 0;
  }
  return CallWindowProcW(g_oldSmartEditProc, hwnd, uMsg, wParam, lParam);
}

static void ShowSmartEditField(int ruleIndex, int field) {
  // field: 0=name, 1=pattern
  if (g_hwndSmartEditName) {
    DestroyWindow(g_hwndSmartEditName);
    g_hwndSmartEditName = NULL;
  }
  if (g_hwndSmartEditPattern) {
    DestroyWindow(g_hwndSmartEditPattern);
    g_hwndSmartEditPattern = NULL;
  }

  if (ruleIndex < 0 || ruleIndex >= (int)g_smartActions.size())
    return;
  g_smartEditFieldIndex = ruleIndex;

  int ruleRowH = 50;
  int startY = GetSmartActionListStartY();
  int rowY = startY + ruleIndex * ruleRowH;
  int editX = SIDEBAR_W + CONTENT_PADDING + 50;
  int editW = SETTINGS_WIDTH - CONTENT_PADDING * 2 - SIDEBAR_W - 200;

  const wchar_t *text = L"";
  int editY = rowY + 6;
  if (field == 0) {
    text = g_smartActions[ruleIndex].name.c_str();
  } else {
    text = g_smartActions[ruleIndex].pattern.c_str();
    editY = rowY + 26;
  }

  HWND hEdit = CreateWindowExW(
      0, L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
      editX, editY, editW, 18, g_hwndSettingsDlg, NULL, GetModuleHandleW(NULL),
      NULL);
  HFONT hFont = CreateFontW(field == 0 ? 17 : 15, 0, 0, 0,
                            field == 0 ? FW_BOLD : FW_NORMAL, FALSE, FALSE,
                            FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                            L"Microsoft YaHei");
  SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
  g_oldSmartEditProc =
      (WNDPROC)SetWindowLongPtrW(hEdit, GWLP_WNDPROC, (LONG_PTR)SmartEditProc);
  SetFocus(hEdit);
  SendMessageW(hEdit, EM_SETSEL, 0, -1);

  if (field == 0)
    g_hwndSmartEditName = hEdit;
  else
    g_hwndSmartEditPattern = hEdit;
}

// PLACEHOLDER_SETTINGS_PART5

// ==================== 窗口过程 ====================

LRESULT CALLBACK SettingsDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam) {
  switch (msg) {

  case WM_CREATE: {
    g_hTitleFont =
        CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                    DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hDescFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hSidebarFont = CreateFontW(
        20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hSidebarIconFont = CreateFontW(
        22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
    g_hHeaderFont =
        CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                    DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hHeaderDescFont = CreateFontW(
        18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hCloseIconFont = CreateFontW(
        18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
    UpdateSettingsBrushes();
    return 0;
  }

  case WM_NCACTIVATE:
    return TRUE;

  case WM_NCCALCSIZE:
    if (wParam == TRUE) {
      NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lParam;
      p->rgrc[0].top += 1;
      return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);

  case WM_NCHITTEST: {
    LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
    if (hit == HTCLIENT) {
      POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      ScreenToClient(hwnd, &pt);
      if (pt.y < SETTINGS_TITLEBAR_H) {
        if (g_hwndSettingsClose) {
          RECT rc;
          GetWindowRect(g_hwndSettingsClose, &rc);
          MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rc, 2);
          if (PtInRect(&rc, pt))
            return HTCLIENT;
        }
        return HTCAPTION;
      }
    }
    return hit;
  }

  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    SetBkMode(hdc, TRANSPARENT);

    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

    // 标题栏
    RECT rcTitlebar = {0, 0, rcClient.right, SETTINGS_TITLEBAR_H};
    HBRUSH hTbBrush = CreateSolidBrush(GetTitlebarBgColor());
    FillRect(hdc, &rcTitlebar, hTbBrush);
    DeleteObject(hTbBrush);

    // 标题栏文字 "设置"
    HFONT hOld = (HFONT)SelectObject(hdc, g_hHeaderFont);
    SetTextColor(hdc, GetSettingsTextColor());
    RECT rcTitleText = {16, 0, 200, SETTINGS_TITLEBAR_H};
    DrawTextW(hdc, L"设置", -1, &rcTitleText,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 侧边栏背景
    RECT rcSidebar = {0, SETTINGS_TITLEBAR_H, SIDEBAR_W, rcClient.bottom};
    HBRUSH hSbBrush = CreateSolidBrush(GetSidebarBgColor());
    FillRect(hdc, &rcSidebar, hSbBrush);
    DeleteObject(hSbBrush);

    // 侧边栏项目
    for (int i = 0; i < SIDEBAR_COUNT; i++) {
      int itemY = SETTINGS_TITLEBAR_H + i * SIDEBAR_ITEM_H;
      RECT rcItem = {0, itemY, SIDEBAR_W, itemY + SIDEBAR_ITEM_H};
      bool selected = (g_currentSettingsTab == i);
      bool hovered = (g_settingsHoverSidebar == i && !selected);

      if (selected) {
        HBRUSH hSelBrush = CreateSolidBrush(COLOR_ACCENT);
        FillRect(hdc, &rcItem, hSelBrush);
        DeleteObject(hSelBrush);
      } else if (hovered) {
        HBRUSH hHvBrush = CreateSolidBrush(GetSidebarHoverColor());
        FillRect(hdc, &rcItem, hHvBrush);
        DeleteObject(hHvBrush);
      }

      COLORREF textCol = selected ? RGB(255, 255, 255) : GetSettingsTextColor();
      SetTextColor(hdc, textCol);

      // 图标
      SelectObject(hdc, g_hSidebarIconFont);
      RECT rcIcon = {20, itemY, 44, itemY + SIDEBAR_ITEM_H};
      DrawTextW(hdc, g_sidebarItems[i].icon, 1, &rcIcon,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      // 文字
      SelectObject(hdc, g_hSidebarFont);
      RECT rcLabel = {48, itemY, SIDEBAR_W - 8, itemY + SIDEBAR_ITEM_H};
      DrawTextW(hdc, g_sidebarItems[i].label, -1, &rcLabel,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // 内容区背景
    RECT rcContent = {SIDEBAR_W, SETTINGS_TITLEBAR_H, rcClient.right,
                      rcClient.bottom};
    HBRUSH hCBrush = CreateSolidBrush(GetSettingsBgColor());
    FillRect(hdc, &rcContent, hCBrush);
    DeleteObject(hCBrush);

    // 分类标题
    const CategoryHeader &cat = g_categories[g_currentSettingsTab];
    int contentLeft = SIDEBAR_W + CONTENT_PADDING;
    int contentRight = rcClient.right - CONTENT_PADDING;

    SelectObject(hdc, g_hHeaderFont);
    SetTextColor(hdc, GetSettingsTextColor());
    RECT rcCatTitle = {contentLeft, SETTINGS_TITLEBAR_H + 10, contentRight,
                       SETTINGS_TITLEBAR_H + 32};
    DrawTextW(hdc, cat.title, -1, &rcCatTitle,
              DT_LEFT | DT_TOP | DT_SINGLELINE);

    SelectObject(hdc, g_hHeaderDescFont);
    SetTextColor(hdc, GetDescTextColor());
    RECT rcCatDesc = {contentLeft, SETTINGS_TITLEBAR_H + 34, contentRight,
                      SETTINGS_TITLEBAR_H + 50};
    DrawTextW(hdc, cat.desc, -1, &rcCatDesc, DT_LEFT | DT_TOP | DT_SINGLELINE);

    // 设置行
    for (int i = 0; i < cat.rowCount; i++) {
      int rowY = GetRowY(i);

      // 标题
      SelectObject(hdc, g_hTitleFont);
      SetTextColor(hdc, GetSettingsTextColor());
      RECT rcRowTitle = {contentLeft, rowY + 12, contentRight - 160, rowY + 30};
      DrawTextW(hdc, cat.rows[i].title, -1, &rcRowTitle,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

      // 描述
      SelectObject(hdc, g_hDescFont);
      SetTextColor(hdc, GetDescTextColor());
      RECT rcRowDesc = {contentLeft, rowY + 32, contentRight - 160, rowY + 48};
      DrawTextW(hdc, cat.rows[i].desc, -1, &rcRowDesc,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

      // 分隔线
      bool skipSeparator = (g_currentSettingsTab == 0 && i == 2);
      if (i < cat.rowCount - 1 && !skipSeparator) {
        HPEN hPen = CreatePen(PS_SOLID, 1, GetSeparatorColor());
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, contentLeft, rowY + ROW_HEIGHT - 1, NULL);
        LineTo(hdc, contentRight, rowY + ROW_HEIGHT - 1);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);
      }
    }

    // 数据分类：动态内容
    if (g_currentSettingsTab == 2) {
      // 第0行右侧：蓝色磁盘占用大小
      int row0Y = GetRowY(0);
      SelectObject(hdc, g_hTitleFont);
      SetTextColor(hdc, COLOR_ACCENT);
      RECT rcSize = {contentRight - 150, row0Y + 12, contentRight, row0Y + 30};
      DrawTextW(hdc, g_dataSizeText.c_str(), -1, &rcSize,
                DT_RIGHT | DT_SINGLELINE);

      // 第1行右侧：蓝色粘贴次数
      extern int g_pasteCount;
      int row1Y = GetRowY(1);
      SelectObject(hdc, g_hTitleFont);
      SetTextColor(hdc, COLOR_ACCENT);
      wchar_t pasteBuf[32];
      _snwprintf_s(pasteBuf, 32, L"%d 次", g_pasteCount);
      RECT rcPaste = {contentRight - 150, row1Y + 12, contentRight, row1Y + 30};
      DrawTextW(hdc, pasteBuf, -1, &rcPaste, DT_RIGHT | DT_SINGLELINE);

      // 第2行：数据目录路径作为描述文字（按钮左侧）
      int row2Y = GetRowY(2);
      SelectObject(hdc, g_hDescFont);
      std::wstring dataPath = GetDataFilePath();
      size_t lastSlash = dataPath.find_last_of(L"\\");
      if (lastSlash != std::wstring::npos)
        dataPath = dataPath.substr(0, lastSlash);
      RECT rcPath = {contentLeft, row2Y + 32, contentRight - 70, row2Y + 48};

      // 悬浮时蓝色，否则灰色
      COLORREF pathColor = GetDescTextColor();
      if (g_dataDirUnderlineProgress > 0.0f) {
        int r = GetRValue(pathColor) + (int)((GetRValue(COLOR_ACCENT) - GetRValue(pathColor)) * g_dataDirUnderlineProgress);
        int g = GetGValue(pathColor) + (int)((GetGValue(COLOR_ACCENT) - GetGValue(pathColor)) * g_dataDirUnderlineProgress);
        int b = GetBValue(pathColor) + (int)((GetBValue(COLOR_ACCENT) - GetBValue(pathColor)) * g_dataDirUnderlineProgress);
        pathColor = RGB(r, g, b);
      }
      SetTextColor(hdc, pathColor);
      DrawTextW(hdc, dataPath.c_str(), -1, &rcPath,
                DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

      // 下划线动画
      if (g_dataDirUnderlineProgress > 0.0f) {
        SIZE textSize;
        GetTextExtentPoint32W(hdc, dataPath.c_str(), (int)dataPath.size(), &textSize);
        int maxW = rcPath.right - rcPath.left;
        int textW = (textSize.cx < maxW) ? textSize.cx : maxW;
        int lineW = (int)(textW * g_dataDirUnderlineProgress);
        HPEN hLinePen = CreatePen(PS_SOLID, 1, pathColor);
        HPEN hOldPen2 = (HPEN)SelectObject(hdc, hLinePen);
        MoveToEx(hdc, contentLeft, row2Y + 48, NULL);
        LineTo(hdc, contentLeft + lineW, row2Y + 48);
        SelectObject(hdc, hOldPen2);
        DeleteObject(hLinePen);
      }
    }

    // 智能操作分类：动态绘制规则列表
    if (g_currentSettingsTab == 3) {
      int smartRowY = GetSmartColorDotRowY();

      SelectObject(hdc, g_hTitleFont);
      SetTextColor(hdc, GetSettingsTextColor());
      RECT rcColorDotTitle = {contentLeft, smartRowY + 10, contentRight - 90,
                              smartRowY + 30};
      DrawTextW(hdc, L"颜色点提示", -1, &rcColorDotTitle,
                DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

      SelectObject(hdc, g_hDescFont);
      SetTextColor(hdc, GetDescTextColor());
      RECT rcColorDotDesc = {contentLeft, smartRowY + 32, contentRight - 90,
                             smartRowY + 50};
      DrawTextW(hdc, L"识别颜色代码后显示对应颜色圆点", -1, &rcColorDotDesc,
                DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

      HPEN hSmartTopPen = CreatePen(PS_SOLID, 1, GetSeparatorColor());
      HPEN hOldSmartTopPen = (HPEN)SelectObject(hdc, hSmartTopPen);
      MoveToEx(hdc, contentLeft, smartRowY + ROW_HEIGHT, NULL);
      LineTo(hdc, contentRight, smartRowY + ROW_HEIGHT);
      SelectObject(hdc, hOldSmartTopPen);
      DeleteObject(hSmartTopPen);

      // 列标题
      int headerY = GetSmartActionListStartY() - 30;
      SelectObject(hdc, g_hTitleFont);
      SetTextColor(hdc, g_isDarkMode ? RGB(188, 191, 198) : RGB(92, 98, 108));
      RECT rcColLeft = {contentLeft + 50, headerY, contentLeft + 200,
                        headerY + 24};
      DrawTextW(hdc, L"匹配规则", -1, &rcColLeft,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER);
      RECT rcColRight = {contentRight - 160, headerY, contentRight,
                         headerY + 24};
      DrawTextW(hdc, L"执行动作", -1, &rcColRight,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER);

      int ruleRowH = 50;
      int startY = GetSmartActionListStartY();
      for (int i = 0; i < (int)g_smartActions.size(); i++) {
        const auto &a = g_smartActions[i];
        int rowY = startY + i * ruleRowH;

        // 规则名称
        SelectObject(hdc, g_hTitleFont);
        SetTextColor(hdc, GetSettingsTextColor());
        RECT rcName = {contentLeft + 50, rowY + 6, contentRight - 170,
                       rowY + 24};
        DrawTextW(hdc, a.name.c_str(), -1, &rcName,
                  DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        // 正则表达式（灰色小字）
        SelectObject(hdc, g_hDescFont);
        SetTextColor(hdc, GetDescTextColor());
        RECT rcPattern = {contentLeft + 50, rowY + 26, contentRight - 170,
                          rowY + 42};
        DrawTextW(hdc, a.pattern.c_str(), -1, &rcPattern,
                  DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        // 动作文字 — browser/explorer 显示中文，cmd_template/url_template
        // 显示命令
        std::wstring actionStr;
        bool actionEmpty = false;
        if (a.action == L"browser") {
          actionStr = L"浏览器打开";
        } else if (a.action == L"explorer") {
          actionStr = L"资源管理器";
        } else if (a.action == L"cmd_template" || a.action == L"custom") {
          if (a.customCmd.empty()) {
            actionStr = L"未设置";
            actionEmpty = true;
          } else {
            actionStr = a.customCmd;
          }
        } else if (a.action == L"url_template") {
          if (a.customCmd.empty()) {
            actionStr = L"未设置";
            actionEmpty = true;
          } else {
            // 显示URL模板的名称（从模板列表中查找）
            bool found = false;
            for (int ti = 0; ti < g_actionTemplateCount; ti++) {
              if (g_actionTemplates[ti].action &&
                  wcscmp(g_actionTemplates[ti].action, L"url_template") == 0) {
                if (wcscmp(g_actionTemplates[ti].cmdTemplate,
                           a.customCmd.c_str()) == 0) {
                  actionStr = g_actionTemplates[ti].name;
                  found = true;
                  break;
                }
              }
            }
            if (!found) {
              actionStr = L"自定义URL";
            }
          }
        } else {
          actionStr = L"未设置";
          actionEmpty = true;
        }
        SelectObject(hdc, g_hDescFont);
        SetTextColor(hdc, actionEmpty ? RGB(180, 180, 180) : COLOR_ACCENT);
        RECT rcAction = {contentRight - 160, rowY + 12, contentRight - 30,
                         rowY + 32};
        DrawTextW(hdc, actionStr.c_str(), -1, &rcAction,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // 分隔线
        if (i < (int)g_smartActions.size() - 1) {
          HPEN hPen = CreatePen(PS_SOLID, 1, GetSeparatorColor());
          HPEN hOldPen2 = (HPEN)SelectObject(hdc, hPen);
          MoveToEx(hdc, contentLeft, rowY + ruleRowH - 1, NULL);
          LineTo(hdc, contentRight, rowY + ruleRowH - 1);
          SelectObject(hdc, hOldPen2);
          DeleteObject(hPen);
        }
      }
    }

    SelectObject(hdc, hOld);
    EndPaint(hwnd, &ps);
    return 0;
  }

  case WM_DRAWITEM: {
    LPDRAWITEMSTRUCT lpDIS = (LPDRAWITEMSTRUCT)lParam;
    RECT rc = lpDIS->rcItem;

    // 关闭按钮
    if (lpDIS->CtlID == IDC_SETTINGS_CLOSE) {
      COLORREF bg = GetTitlebarBgColor();
      POINT pt;
      GetCursorPos(&pt);
      ScreenToClient(hwnd, &pt);
      RECT rcBtn;
      GetWindowRect(lpDIS->hwndItem, &rcBtn);
      MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rcBtn, 2);
      bool hover = PtInRect(&rcBtn, pt);
      if (hover)
        bg = RGB(232, 17, 35);
      HBRUSH hBr = CreateSolidBrush(bg);
      FillRect(lpDIS->hDC, &rc, hBr);
      DeleteObject(hBr);
      SetBkMode(lpDIS->hDC, TRANSPARENT);
      SetTextColor(lpDIS->hDC,
                   hover ? RGB(255, 255, 255) : GetSettingsTextColor());
      HFONT hOldF = (HFONT)SelectObject(lpDIS->hDC, g_hCloseIconFont);
      DrawTextW(lpDIS->hDC, L"\uE8BB", -1, &rc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      SelectObject(lpDIS->hDC, hOldF);
      return TRUE;
    }

    // Toggle 开关
    if (lpDIS->CtlID == IDC_NOTIFICATION_CHECK ||
        lpDIS->CtlID == IDC_SMOOTH_SCROLL_CHECK ||
        lpDIS->CtlID == IDC_SCROLLBAR_CHECK ||
        lpDIS->CtlID == IDC_COLOR_DOT_CHECK ||
        lpDIS->CtlID == IDC_QUICK_PASTE_CHECK ||
        lpDIS->CtlID == IDC_VAULT_PROTECTION_TOGGLE) {

      // 先填充背景
      HBRUSH hBgBr = CreateSolidBrush(GetSettingsBgColor());
      FillRect(lpDIS->hDC, &rc, hBgBr);
      DeleteObject(hBgBr);

      bool isOn = false;
      switch (lpDIS->CtlID) {
      case IDC_NOTIFICATION_CHECK:
        isOn = g_isNotificationEnabled;
        break;
      case IDC_SMOOTH_SCROLL_CHECK:
        isOn = g_isSmoothScrollEnabled;
        break;
      case IDC_SCROLLBAR_CHECK:
        isOn = g_isCustomScrollbarEnabled;
        break;
      case IDC_COLOR_DOT_CHECK:
        isOn = g_isColorDotEnabled;
        break;
      case IDC_QUICK_PASTE_CHECK:
        isOn = g_isQuickPasteEnabled;
        break;
      case IDC_VAULT_PROTECTION_TOGGLE:
        isOn = g_vaultProtectionEnabled;
        break;
      }
      DrawToggleSwitch(lpDIS->hDC, rc, isOn);
      return TRUE;
    }

    // 智能操作 Toggle 开关
    if (lpDIS->CtlID >= IDC_SMART_ACTION_TOGGLE_BASE &&
        lpDIS->CtlID < IDC_SMART_ACTION_TOGGLE_BASE + 100) {
      HBRUSH hBgBr = CreateSolidBrush(GetSettingsBgColor());
      FillRect(lpDIS->hDC, &rc, hBgBr);
      DeleteObject(hBgBr);
      int idx = lpDIS->CtlID - IDC_SMART_ACTION_TOGGLE_BASE;
      bool isOn = (idx < (int)g_smartActions.size())
                      ? g_smartActions[idx].enabled
                      : false;
      DrawToggleSwitch(lpDIS->hDC, rc, isOn);
      return TRUE;
    }

    // 智能操作删除按钮
    if (lpDIS->CtlID >= IDC_SMART_ACTION_DEL_BASE &&
        lpDIS->CtlID < IDC_SMART_ACTION_DEL_BASE + 100) {
      HBRUSH hBgBr = CreateSolidBrush(GetSettingsBgColor());
      FillRect(lpDIS->hDC, &rc, hBgBr);
      DeleteObject(hBgBr);
      Gdiplus::Graphics g(lpDIS->hDC);
      g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
      g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
      Gdiplus::GraphicsPath btnPath;
      CreateRoundRectPath(&btnPath, 0, 0, rc.right - rc.left,
                          rc.bottom - rc.top, 6);
      Gdiplus::SolidBrush btnBrush(Gdiplus::Color(255, 220, 60, 60));
      g.FillPath(&btnBrush, &btnPath);
      Gdiplus::Font font(L"Microsoft YaHei", 8.0f);
      Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
      Gdiplus::StringFormat sf;
      sf.SetAlignment(Gdiplus::StringAlignmentCenter);
      sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
      Gdiplus::RectF textRect(0, 0, (float)(rc.right - rc.left),
                              (float)(rc.bottom - rc.top));
      g.DrawString(L"删除", -1, &font, textRect, &sf, &textBrush);
      return TRUE;
    }

    // 新建规则按钮
    if (lpDIS->CtlID == IDC_SMART_ACTION_ADD) {
      HBRUSH hBgBr = CreateSolidBrush(GetSettingsBgColor());
      FillRect(lpDIS->hDC, &rc, hBgBr);
      DeleteObject(hBgBr);
      Gdiplus::Graphics g(lpDIS->hDC);
      g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
      g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
      Gdiplus::GraphicsPath btnPath;
      CreateRoundRectPath(&btnPath, 0, 0, rc.right - rc.left,
                          rc.bottom - rc.top, 8);
      Gdiplus::SolidBrush btnBrush(Gdiplus::Color(255, GetRValue(COLOR_ACCENT),
                                                  GetGValue(COLOR_ACCENT),
                                                  GetBValue(COLOR_ACCENT)));
      g.FillPath(&btnBrush, &btnPath);
      Gdiplus::Font font(L"Microsoft YaHei", 9.0f);
      Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
      Gdiplus::StringFormat sf;
      sf.SetAlignment(Gdiplus::StringAlignmentCenter);
      sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
      Gdiplus::RectF textRect(0, 0, (float)(rc.right - rc.left),
                              (float)(rc.bottom - rc.top));
      g.DrawString(L"+ 新建规则", -1, &font, textRect, &sf, &textBrush);
      return TRUE;
    }

    // 下拉选择器按钮
    if (lpDIS->CtlID == IDC_THEME_COMBO ||
        lpDIS->CtlID == IDC_IMAGE_PREVIEW_COMBO ||
        lpDIS->CtlID == IDC_QUICK_PASTE_COMBO ||
        lpDIS->CtlID == IDC_AUTH_METHOD_COMBO) {
      HBRUSH hBgBr = CreateSolidBrush(GetSettingsBgColor());
      FillRect(lpDIS->hDC, &rc, hBgBr);
      DeleteObject(hBgBr);
      DrawDropdownButton(lpDIS->hDC, rc, lpDIS->CtlID);
      return TRUE;
    }

    // iOS 风格操作按钮（蓝色圆角）
    if (lpDIS->CtlID == IDC_SET_DATA_DIR || lpDIS->CtlID == IDC_CLEAR_NON_FAV ||
        lpDIS->CtlID == IDC_CLEAN_INVALID_IMAGES ||
        lpDIS->CtlID == IDC_RESET_PASSWORD_BTN) {
      HBRUSH hBgBr = CreateSolidBrush(GetSettingsBgColor());
      FillRect(lpDIS->hDC, &rc, hBgBr);
      DeleteObject(hBgBr);

      Gdiplus::Graphics g(lpDIS->hDC);
      g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
      g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

      COLORREF btnColor = (lpDIS->CtlID == IDC_CLEAR_NON_FAV ||
                           lpDIS->CtlID == IDC_CLEAN_INVALID_IMAGES)
                              ? RGB(220, 60, 60)
                              : COLOR_ACCENT;
      Gdiplus::GraphicsPath btnPath;
      CreateRoundRectPath(&btnPath, 0, 0, rc.right - rc.left,
                          rc.bottom - rc.top, 8);
      Gdiplus::SolidBrush btnBrush(Gdiplus::Color(
          255, GetRValue(btnColor), GetGValue(btnColor), GetBValue(btnColor)));
      g.FillPath(&btnBrush, &btnPath);

      const wchar_t *text = L"";
      if (lpDIS->CtlID == IDC_SET_DATA_DIR)
        text = L"选择";
      else if (lpDIS->CtlID == IDC_CLEAR_NON_FAV)
        text = L"清理";
      else if (lpDIS->CtlID == IDC_CLEAN_INVALID_IMAGES)
        text = L"清理";

      Gdiplus::Font font(L"Microsoft YaHei", 9.0f);
      Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
      Gdiplus::StringFormat sf;
      sf.SetAlignment(Gdiplus::StringAlignmentCenter);
      sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
      Gdiplus::RectF textRect(0, 0, (float)(rc.right - rc.left),
                              (float)(rc.bottom - rc.top));
      g.DrawString(text, -1, &font, textRect, &sf, &textBrush);
      return TRUE;
    }
    break;
  }

  case WM_MOUSEMOVE: {
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    int newHover = -1;
    if (pt.x < SIDEBAR_W && pt.y >= SETTINGS_TITLEBAR_H) {
      int idx = (pt.y - SETTINGS_TITLEBAR_H) / SIDEBAR_ITEM_H;
      if (idx >= 0 && idx < SIDEBAR_COUNT)
        newHover = idx;
    }
    if (newHover != g_settingsHoverSidebar) {
      g_settingsHoverSidebar = newHover;
      RECT rcSb = {0, SETTINGS_TITLEBAR_H, SIDEBAR_W,
                   SETTINGS_TITLEBAR_H + SIDEBAR_COUNT * SIDEBAR_ITEM_H};
      InvalidateRect(hwnd, &rcSb, FALSE);
    }
    // 跟踪鼠标离开
    TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);

    // 数据目录路径悬浮检测
    if (g_currentSettingsTab == 2 && pt.x > SIDEBAR_W) {
      int row2Y = GetRowY(2);
      bool overPath = (pt.y >= row2Y + 30 && pt.y <= row2Y + 50 &&
                        pt.x < SETTINGS_WIDTH - CONTENT_PADDING - 70);
      if (overPath && !g_dataDirHovered) {
        g_dataDirHovered = true;
        SetTimer(hwnd, ID_DATADIR_UNDERLINE_TIMER, 16, NULL);
        SetCursor(LoadCursorW(NULL, IDC_HAND));
      } else if (!overPath && g_dataDirHovered) {
        g_dataDirHovered = false;
        SetTimer(hwnd, ID_DATADIR_UNDERLINE_TIMER, 16, NULL);
      }
      if (overPath) SetCursor(LoadCursorW(NULL, IDC_HAND));
    } else if (g_dataDirHovered) {
      g_dataDirHovered = false;
      SetTimer(hwnd, ID_DATADIR_UNDERLINE_TIMER, 16, NULL);
    }
    break;
  }

  case WM_MOUSELEAVE:
    if (g_settingsHoverSidebar >= 0) {
      g_settingsHoverSidebar = -1;
      RECT rcSb = {0, SETTINGS_TITLEBAR_H, SIDEBAR_W,
                   SETTINGS_TITLEBAR_H + SIDEBAR_COUNT * SIDEBAR_ITEM_H};
      InvalidateRect(hwnd, &rcSb, FALSE);
    }
    if (g_dataDirHovered) {
      g_dataDirHovered = false;
      SetTimer(hwnd, ID_DATADIR_UNDERLINE_TIMER, 16, NULL);
    }
    break;

  case WM_TIMER:
    if (wParam == ID_DATADIR_UNDERLINE_TIMER) {
      float step = 0.08f;
      if (g_dataDirHovered) {
        g_dataDirUnderlineProgress += step;
        if (g_dataDirUnderlineProgress >= 1.0f) {
          g_dataDirUnderlineProgress = 1.0f;
          KillTimer(hwnd, ID_DATADIR_UNDERLINE_TIMER);
        }
      } else {
        g_dataDirUnderlineProgress -= step;
        if (g_dataDirUnderlineProgress <= 0.0f) {
          g_dataDirUnderlineProgress = 0.0f;
          KillTimer(hwnd, ID_DATADIR_UNDERLINE_TIMER);
        }
      }
      // 只重绘数据目录路径区域
      int row2Y = GetRowY(2);
      RECT rcPath = {SIDEBAR_W, row2Y + 28, SETTINGS_WIDTH, row2Y + 52};
      InvalidateRect(hwnd, &rcPath, FALSE);
      return 0;
    }
    break;

  case WM_LBUTTONDOWN: {
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    // 点击非输入框区域时，让输入框失焦
    HWND hFocus = GetFocus();
    if (hFocus && hFocus != hwnd) {
      HWND hParent = GetParent(hFocus);
      if (hParent == hwnd) {
        SetFocus(hwnd);
      }
    }
    if (pt.x < SIDEBAR_W && pt.y >= SETTINGS_TITLEBAR_H) {
      int idx = (pt.y - SETTINGS_TITLEBAR_H) / SIDEBAR_ITEM_H;
      if (idx >= 0 && idx < SIDEBAR_COUNT && idx != g_currentSettingsTab) {
        SwitchSettingsTab(idx);
      }
    }
    // 数据分类：点击数据目录路径打开资源管理器（第2行）
    if (g_currentSettingsTab == 2 && pt.x > SIDEBAR_W) {
      int row2Y = GetRowY(2);
      if (pt.y >= row2Y + 10 && pt.y <= row2Y + 45) {
        std::wstring dataPath = GetDataFilePath();
        size_t lastSlash = dataPath.find_last_of(L"\\");
        if (lastSlash != std::wstring::npos) {
          dataPath = dataPath.substr(0, lastSlash);
          ShellExecuteW(NULL, L"open", L"explorer.exe", dataPath.c_str(), NULL,
                        SW_SHOW);
        }
      }
    }
    // 智能操作面板点击
    if (g_currentSettingsTab == 3 && pt.x > SIDEBAR_W) {
      int smartRowY = GetSmartColorDotRowY();
      if (pt.y >= smartRowY && pt.y < smartRowY + ROW_HEIGHT) {
        if (pt.x >= GetControlX(TOGGLE_W) && pt.x < GetControlX(TOGGLE_W) + TOGGLE_W) {
          g_isColorDotEnabled = !g_isColorDotEnabled;
          InvalidateRect(g_hwndToggleColorDot, NULL, TRUE);
          SaveHotkeySettings();
          if (g_hwndListBox)
            InvalidateRect(g_hwndListBox, NULL, TRUE);
        }
        break;
      }

      int ruleRowH = 50;
      int startY = GetSmartActionListStartY();
      int contentLeft = SIDEBAR_W + CONTENT_PADDING;
      int contentRight = SETTINGS_WIDTH - CONTENT_PADDING;

      for (int i = 0; i < (int)g_smartActions.size(); i++) {
        int rowY = startY + i * ruleRowH;
        if (pt.y >= rowY && pt.y < rowY + ruleRowH) {
          // 点击动作区域（右侧）
          if (pt.x >= contentRight - 170) {
            ShowSmartActionDropdown(i);
          }
          // 点击名称区域
          else if (pt.y < rowY + 24 && pt.x >= contentLeft + 50) {
            ShowSmartEditField(i, 0);
          }
          // 点击正则区域
          else if (pt.y >= rowY + 24 && pt.x >= contentLeft + 50) {
            ShowSmartEditField(i, 1);
          }
          break;
        }
      }
    }
    break;
  }

  // 延迟销毁临时编辑框
  case WM_USER + 100: {
    HWND hEdit = (HWND)wParam;
    if (hEdit)
      DestroyWindow(hEdit);
    if (hEdit == g_hwndSmartEditName)
      g_hwndSmartEditName = NULL;
    if (hEdit == g_hwndSmartEditPattern)
      g_hwndSmartEditPattern = NULL;
    if (hEdit == g_hwndSmartEditCmd)
      g_hwndSmartEditCmd = NULL;
    return 0;
  }

  case WM_CTLCOLOREDIT: {
    HDC hdcEdit = (HDC)wParam;
    SetBkColor(hdcEdit, GetSettingsEditBg());
    SetTextColor(hdcEdit, GetSettingsTextColor());
    if (!g_hEditBgBrush)
      g_hEditBgBrush = CreateSolidBrush(GetSettingsEditBg());
    return (LRESULT)g_hEditBgBrush;
  }

  case WM_CTLCOLORSTATIC: {
    HDC hdcStatic = (HDC)wParam;
    SetBkColor(hdcStatic, GetSettingsBgColor());
    SetTextColor(hdcStatic, GetSettingsTextColor());
    if (!g_hSettingsBgBrush)
      g_hSettingsBgBrush = CreateSolidBrush(GetSettingsBgColor());
    return (LRESULT)g_hSettingsBgBrush;
  }

    // PLACEHOLDER_SETTINGS_PART6

  case WM_COMMAND: {
    WORD wID = LOWORD(wParam);
    WORD wNotify = HIWORD(wParam);

    // 关闭按钮
    if (wID == IDC_SETTINGS_CLOSE && wNotify == BN_CLICKED) {
      DestroyWindow(hwnd);
      return 0;
    }

    // Toggle 开关
    if (wNotify == BN_CLICKED) {
      if (wID == IDC_NOTIFICATION_CHECK) {
        g_isNotificationEnabled = !g_isNotificationEnabled;
        InvalidateRect(g_hwndToggleNotification, NULL, TRUE);
        SaveHotkeySettings();
        if (g_isNotificationEnabled)
          ShowTrayBalloon(g_hwndMain, L"设置已更新", L"消息通知已启用");
        return 0;
      }
      if (wID == IDC_SMOOTH_SCROLL_CHECK) {
        g_isSmoothScrollEnabled = !g_isSmoothScrollEnabled;
        InvalidateRect(g_hwndToggleSmoothScroll, NULL, TRUE);
        SaveHotkeySettings();
        return 0;
      }
      if (wID == IDC_SCROLLBAR_CHECK) {
        g_isCustomScrollbarEnabled = !g_isCustomScrollbarEnabled;
        InvalidateRect(g_hwndToggleScrollbar, NULL, TRUE);
        UpdateScrollbarSettingsControls();
        SaveHotkeySettings();
        if (g_hwndMain)
          InvalidateRect(g_hwndMain, NULL, FALSE);
        return 0;
      }
      if (wID == IDC_COLOR_DOT_CHECK) {
        g_isColorDotEnabled = !g_isColorDotEnabled;
        InvalidateRect(g_hwndToggleColorDot, NULL, TRUE);
        SaveHotkeySettings();
        if (g_hwndListBox)
          InvalidateRect(g_hwndListBox, NULL, TRUE);
        return 0;
      }
      if (wID == IDC_QUICK_PASTE_CHECK) {
        g_isQuickPasteEnabled = !g_isQuickPasteEnabled;
        InvalidateRect(g_hwndToggleQuickPaste, NULL, TRUE);
        HWND hwndMain = g_hwndMain;
        if (g_isQuickPasteEnabled)
          RegisterQuickPasteHotkeys(hwndMain);
        else
          UnregisterQuickPasteHotkeys(hwndMain);
        SaveHotkeySettings();
        return 0;
      }
      if (wID == IDC_VAULT_PROTECTION_TOGGLE) {
        if (!g_vaultProtectionEnabled && !IsMasterPasswordSet()) {
          // 首次开启，需要先设置主密码
          extern void ShowSetMasterPasswordDialog(HWND);
          ShowSetMasterPasswordDialog(hwnd);
          if (!IsMasterPasswordSet()) return 0;
        }
        g_vaultProtectionEnabled = !g_vaultProtectionEnabled;
        InvalidateRect(g_hwndToggleVaultProtection, NULL, TRUE);
        SaveVaultSettings();
        // 刷新认证方式和重置按钮的可见性
        SwitchSettingsTab(4);
        return 0;
      }
      if (wID == IDC_RESET_PASSWORD_BTN) {
        extern void ShowResetMasterPasswordDialog(HWND);
        ShowResetMasterPasswordDialog(hwnd);
        return 0;
      }
      if (wID == IDC_SET_DATA_DIR) {
        BROWSEINFOW bi = {};
        bi.hwndOwner = hwnd;
        bi.lpszTitle = L"选择数据存储目录";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (pidl) {
          wchar_t path[MAX_PATH];
          if (SHGetPathFromIDListW(pidl, path)) {
            int result = MessageBoxW(hwnd,
                                     (std::wstring(L"将数据迁移到:\n") + path +
                                      L"\\SmartClip\n\n确定迁移？")
                                         .c_str(),
                                     L"确认迁移", MB_YESNO | MB_ICONQUESTION);
            if (result == IDYES) {
              MigrateDataDir(path);
            }
          }
          CoTaskMemFree(pidl);
        }
        return 0;
      }
      if (wID == IDC_CLEAR_NON_FAV) {
        int result = MessageBoxW(
            hwnd, L"确定要删除所有未收藏的历史记录吗？\n此操作不可撤销。",
            L"确认清理", MB_YESNO | MB_ICONWARNING);
        if (result == IDYES) {
          ClearNonFavoriteHistory();
          g_dataSizeText = FormatFileSize(GetDataDirSize());
          InvalidateRect(hwnd, NULL, TRUE);
          if (g_isNotificationEnabled)
            ShowTrayBalloon(g_hwndMain, L"提示", L"非收藏数据已清理");
        }
        return 0;
      }
      if (wID == IDC_CLEAN_INVALID_IMAGES) {
        int result = MessageBoxW(
            hwnd,
            L"确定要删除所有原始图片已丢失的图片记录吗？\n此操作不可撤销。",
            L"确认清理", MB_YESNO | MB_ICONWARNING);
        if (result == IDYES) {
          extern void CleanInvalidImageRecords();
          CleanInvalidImageRecords();
          g_dataSizeText = FormatFileSize(GetDataDirSize());
          InvalidateRect(hwnd, NULL, TRUE);
          if (g_isNotificationEnabled)
            ShowTrayBalloon(g_hwndMain, L"提示", L"失效图片记录已清理");
        }
        return 0;
      }
      // 下拉按钮点击 → 弹出选择器
      if (wID == IDC_THEME_COMBO) {
        ShowDropdownPopup(g_hwndThemeCombo, IDC_THEME_COMBO);
        return 0;
      }
      if (wID == IDC_IMAGE_PREVIEW_COMBO) {
        ShowDropdownPopup(g_hwndImagePreviewCombo, IDC_IMAGE_PREVIEW_COMBO);
        return 0;
      }
      if (wID == IDC_QUICK_PASTE_COMBO) {
        ShowDropdownPopup(g_hwndQuickPasteCombo, IDC_QUICK_PASTE_COMBO);
        return 0;
      }
      if (wID == IDC_AUTH_METHOD_COMBO) {
        ShowDropdownPopup(g_hwndAuthMethodCombo, IDC_AUTH_METHOD_COMBO);
        return 0;
      }
    }

    // 主题选择（来自弹出窗口）
    if (wID == IDC_THEME_COMBO && wNotify == CBN_SELCHANGE) {
      int sel = g_activeDropdown.selectedIndex;
      switch (sel) {
      case 0:
        g_themeMode = THEME_LIGHT;
        break;
      case 1:
        g_themeMode = THEME_DARK;
        break;
      case 2:
        g_themeMode = THEME_SYSTEM;
        break;
      }
      ApplyTheme();
      SaveHotkeySettings();
      UpdateSettingsBrushes();
      // 更新窗口背景画刷
      SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND,
                       (LONG_PTR)CreateSolidBrush(GetSettingsBgColor()));
      // 强制重绘整个窗口和所有子控件
      RedrawWindow(hwnd, NULL, NULL,
                   RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN |
                       RDW_UPDATENOW);
      return 0;
    }

    // 图片预览选择
    if (wID == IDC_IMAGE_PREVIEW_COMBO && wNotify == CBN_SELCHANGE) {
      g_imagePreviewQuality =
          (ImagePreviewQuality)g_activeDropdown.selectedIndex;
      SaveHotkeySettings();
      extern HWND g_hwndListBox;
      if (g_hwndListBox)
        InvalidateRect(g_hwndListBox, NULL, TRUE);
      if (g_hwndImagePreviewCombo)
        InvalidateRect(g_hwndImagePreviewCombo, NULL, TRUE);
      return 0;
    }

    // 快捷粘贴修饰键选择
    if (wID == IDC_QUICK_PASTE_COMBO && wNotify == CBN_SELCHANGE) {
      int sel = g_activeDropdown.selectedIndex;
      HWND hwndMain = g_hwndMain;
      UnregisterQuickPasteHotkeys(hwndMain);
      switch (sel) {
      case 0:
        g_quickPasteModifiers = MOD_ALT;
        break;
      case 1:
        g_quickPasteModifiers = MOD_CONTROL;
        break;
      case 2:
        g_quickPasteModifiers = MOD_SHIFT;
        break;
      case 3:
        g_quickPasteModifiers = MOD_CONTROL | MOD_ALT;
        break;
      case 4:
        g_quickPasteModifiers = MOD_CONTROL | MOD_SHIFT;
        break;
      case 5:
        g_quickPasteModifiers = MOD_ALT | MOD_SHIFT;
        break;
      }
      if (g_isQuickPasteEnabled)
        RegisterQuickPasteHotkeys(hwndMain);
      SaveHotkeySettings();
      if (g_hwndQuickPasteCombo)
        InvalidateRect(g_hwndQuickPasteCombo, NULL, TRUE);
      return 0;
    }
    if (wID == IDC_AUTH_METHOD_COMBO && wNotify == CBN_SELCHANGE) {
      int sel = g_activeDropdown.selectedIndex;
      g_vaultAuthMethod = sel;
      SaveVaultSettings();
      if (g_hwndAuthMethodCombo)
        InvalidateRect(g_hwndAuthMethodCombo, NULL, TRUE);
      return 0;
    }

    // 快捷键编辑框焦点
    if (wID == IDC_HOTKEY_EDIT) {
      if (wNotify == EN_SETFOCUS) {
        UnregisterHotkey(g_hwndMain);
        g_isRecordingHotkey = true;
        SetWindowTextW(g_hwndHotkeyEdit, L"请按下快捷键...");
      }
      if (wNotify == EN_KILLFOCUS && g_isRecordingHotkey) {
        g_isRecordingHotkey = false;
        if (g_isHotkeyEnabled)
          RegisterHotkey(g_hwndMain);
        std::wstring text = FormatHotkeyText(g_hotkeyModifiers,
                                             g_hotkeyVirtualKey, L"Z");
        SetWindowTextW(g_hwndHotkeyEdit, text.c_str());
      }
    }
    if (wID == IDC_SEARCH_HOTKEY_EDIT) {
      if (wNotify == EN_SETFOCUS) {
        UnregisterHotkey(g_hwndMain);
        g_isRecordingSearchHotkey = true;
        SetWindowTextW(g_hwndSearchHotkeyEdit, L"请按下快捷键...");
      }
      if (wNotify == EN_KILLFOCUS && g_isRecordingSearchHotkey) {
        g_isRecordingSearchHotkey = false;
        if (g_isHotkeyEnabled)
          RegisterHotkey(g_hwndMain);
        std::wstring text = FormatHotkeyText(g_searchHotkeyModifiers,
                                             g_searchHotkeyVirtualKey, L"F");
        SetWindowTextW(g_hwndSearchHotkeyEdit, text.c_str());
      }
    }

    // 历史记录数量编辑框
    if (wID == IDC_HISTORY_LIMIT_EDIT && wNotify == EN_CHANGE) {
      wchar_t buf[16] = {};
      GetWindowTextW(g_hwndHistoryLimitEdit, buf, 16);
      int val = _wtoi(buf);
      if (val >= 10 && val <= 10000) {
        g_maxHistoryCount = val;
        SaveHotkeySettings();
      }
      return 0;
    }

    if (wID == IDC_SCROLLBAR_TIMEOUT_EDIT && wNotify == EN_CHANGE) {
      wchar_t buf[16] = {};
      GetWindowTextW(g_hwndScrollbarTimeoutEdit, buf, 16);
      int val = _wtoi(buf);
      if (val >= 600 && val <= 2000) {
        g_customScrollbarHideDelayMs = val;
        SaveHotkeySettings();
      }
      return 0;
    }

    // 智能操作 Toggle 开关
    if (wNotify == BN_CLICKED && wID >= IDC_SMART_ACTION_TOGGLE_BASE &&
        wID < IDC_SMART_ACTION_TOGGLE_BASE + 100) {
      int idx = wID - IDC_SMART_ACTION_TOGGLE_BASE;
      if (idx < (int)g_smartActions.size()) {
        g_smartActions[idx].enabled = !g_smartActions[idx].enabled;
        SaveSmartActions();
        // 重绘 toggle 按钮和整个面板
        if (idx < (int)g_smartToggleHwnds.size() && g_smartToggleHwnds[idx])
          InvalidateRect(g_smartToggleHwnds[idx], NULL, TRUE);
        InvalidateRect(hwnd, NULL, TRUE);
      }
      return 0;
    }

    // 智能操作删除按钮
    if (wNotify == BN_CLICKED && wID >= IDC_SMART_ACTION_DEL_BASE &&
        wID < IDC_SMART_ACTION_DEL_BASE + 100) {
      int idx = wID - IDC_SMART_ACTION_DEL_BASE;
      if (idx < (int)g_smartActions.size() && !g_smartActions[idx].isDefault) {
        g_smartActions.erase(g_smartActions.begin() + idx);
        SaveSmartActions();
        RefreshSmartActionControls(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
      }
      return 0;
    }

    // 新建规则按钮
    if (wNotify == BN_CLICKED && wID == IDC_SMART_ACTION_ADD) {
      SmartAction newAction;
      newAction.name = L"新规则";
      newAction.pattern = L"";
      newAction.action = L"custom";
      newAction.customCmd = L"";
      newAction.enabled = true;
      newAction.isDefault = false;
      g_smartActions.push_back(newAction);
      SaveSmartActions();
      RefreshSmartActionControls(hwnd);
      InvalidateRect(hwnd, NULL, TRUE);
      return 0;
    }
    break;
  }

  case WM_THEMECHANGED: {
    UpdateSettingsBrushes();
    SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND,
                     (LONG_PTR)CreateSolidBrush(GetSettingsBgColor()));
    RedrawWindow(hwnd, NULL, NULL,
                 RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    return 0;
  }

  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;

  case WM_DESTROY:
    g_isRecordingHotkey = false;
    g_isRecordingSearchHotkey = false;
    if (g_hTitleFont) {
      DeleteObject(g_hTitleFont);
      g_hTitleFont = NULL;
    }
    if (g_hDescFont) {
      DeleteObject(g_hDescFont);
      g_hDescFont = NULL;
    }
    if (g_hSidebarFont) {
      DeleteObject(g_hSidebarFont);
      g_hSidebarFont = NULL;
    }
    if (g_hSidebarIconFont) {
      DeleteObject(g_hSidebarIconFont);
      g_hSidebarIconFont = NULL;
    }
    if (g_hHeaderFont) {
      DeleteObject(g_hHeaderFont);
      g_hHeaderFont = NULL;
    }
    if (g_hHeaderDescFont) {
      DeleteObject(g_hHeaderDescFont);
      g_hHeaderDescFont = NULL;
    }
    if (g_hCloseIconFont) {
      DeleteObject(g_hCloseIconFont);
      g_hCloseIconFont = NULL;
    }
    if (g_hSettingsBgBrush) {
      DeleteObject(g_hSettingsBgBrush);
      g_hSettingsBgBrush = NULL;
    }
    if (g_hEditBgBrush) {
      DeleteObject(g_hEditBgBrush);
      g_hEditBgBrush = NULL;
    }
    DestroySmartActionControls();
    g_isSettingsDialogOpen = false;
    g_hwndSettingsDlg = NULL;
    return 0;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// PLACEHOLDER_SETTINGS_PART7

// ==================== 窗口类注册 ====================

static void RegisterSettingsClass() {
  if (g_settingsClassRegistered)
    return;
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = SettingsDialogProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = CreateSolidBrush(GetSettingsBgColor());
  wc.lpszClassName = L"SmartClipSettings";
  wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_ICON1));
  wc.hIconSm = wc.hIcon;
  RegisterClassExW(&wc);
  g_settingsClassRegistered = true;
}

// ==================== 加载字体设置 ====================

static void LoadFontSettings() {
  std::wstring filePath = GetDataFilePath();
  size_t lastSlash = filePath.find_last_of(L"\\");
  if (lastSlash == std::wstring::npos)
    return;
  std::wstring fontFilePath = filePath.substr(0, lastSlash) + L"\\font.txt";
  HANDLE hFile =
      CreateFileW(fontFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE)
    return;
  DWORD sz = GetFileSize(hFile, NULL);
  if (sz > 0 && sz < 1024) {
    std::vector<BYTE> buf(sz + 1);
    DWORD read = 0;
    if (ReadFile(hFile, &buf[0], sz, &read, NULL)) {
      buf[sz] = 0;
      int uLen = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)&buf[0], sz, NULL, 0);
      if (uLen > 0) {
        std::vector<wchar_t> wbuf(uLen + 1);
        MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)&buf[0], sz, &wbuf[0], uLen);
        wbuf[uLen] = 0;
        std::wstring data = &wbuf[0];
        size_t p1 = data.find(L'\n');
        if (p1 != std::wstring::npos) {
          g_fontName = data.substr(0, p1);
          size_t p2 = data.find(L'\n', p1 + 1);
          if (p2 != std::wstring::npos) {
            g_fontSize = _wtoi(data.substr(p1 + 1, p2 - p1 - 1).c_str());
            if (g_fontSize < 8 || g_fontSize > 72)
              g_fontSize = 16;
            size_t p3 = data.find(L'\n', p2 + 1);
            if (p3 != std::wstring::npos) {
              g_fontWeight = _wtoi(data.substr(p2 + 1, p3 - p2 - 1).c_str());
              g_fontItalic =
                  (_wtoi(data.substr(p3 + 1).c_str()) != 0) ? TRUE : FALSE;
            }
          }
        }
      }
    }
  }
  CloseHandle(hFile);
}

// ==================== 创建控件辅助 ====================

static HWND CreateToggle(HWND parent, int rowIndex, int ctlId) {
  int y = GetRowY(rowIndex) + (ROW_HEIGHT - TOGGLE_H) / 2;
  int x = GetControlX(TOGGLE_W);
  return CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW, x, y,
                         TOGGLE_W, TOGGLE_H, parent, (HMENU)(INT_PTR)ctlId,
                         GetModuleHandleW(NULL), NULL);
}

static HWND CreateSettingsCombo(HWND parent, int rowIndex, int ctlId,
                                int width) {
  int y = GetRowY(rowIndex) + (ROW_HEIGHT - 32) / 2;
  int x = GetControlX(width);
  return CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW, x, y,
                         width, 32, parent, (HMENU)(INT_PTR)ctlId,
                         GetModuleHandleW(NULL), NULL);
}

static void ConfigureSettingsEdit(HWND hwnd) {
  if (!hwnd)
    return;
  SetWindowTheme(hwnd, L"", L"");
  LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  style &= ~WS_BORDER;
  SetWindowLongPtrW(hwnd, GWL_STYLE, style);
  LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  exStyle &= ~WS_EX_CLIENTEDGE;
  SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
  SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

static HWND CreateHotkeyEditBox(HWND parent, int rowIndex, int ctlId) {
  int y = GetRowY(rowIndex) + (ROW_HEIGHT - 28) / 2;
  int x = GetControlX(180);
  return CreateWindowExW(0, L"EDIT", NULL,
                         WS_CHILD | WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL,
                         x, y, 180, 28, parent, (HMENU)(INT_PTR)ctlId,
                         GetModuleHandleW(NULL), NULL);
}

// ==================== 智能操作控件管理 ====================

static void CreateSmartActionControls(HWND hwndDlg) {
  DestroySmartActionControls();

  int ruleRowH = 50;
  int startY = GetSmartActionListStartY();
  int contentRight = SETTINGS_WIDTH - CONTENT_PADDING;

  for (int i = 0; i < (int)g_smartActions.size(); i++) {
    int rowY = startY + i * ruleRowH;

    // Toggle 开关
    int toggleY = rowY + (ruleRowH - TOGGLE_H) / 2;
    HWND hToggle = CreateWindowExW(
        0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        SIDEBAR_W + CONTENT_PADDING, toggleY, TOGGLE_W, TOGGLE_H, hwndDlg,
        (HMENU)(INT_PTR)(IDC_SMART_ACTION_TOGGLE_BASE + i),
        GetModuleHandleW(NULL), NULL);
    g_smartToggleHwnds.push_back(hToggle);

    // 删除按钮（仅非默认规则）
    if (!g_smartActions[i].isDefault) {
      int delY = rowY + (ruleRowH - 24) / 2;
      HWND hDel = CreateWindowExW(
          0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
          contentRight - 24, delY, 40, 24, hwndDlg,
          (HMENU)(INT_PTR)(IDC_SMART_ACTION_DEL_BASE + i),
          GetModuleHandleW(NULL), NULL);
      g_smartDelHwnds.push_back(hDel);
    } else {
      g_smartDelHwnds.push_back(NULL);
    }
  }

  // "+新建规则"按钮
  int addY = startY + (int)g_smartActions.size() * ruleRowH + 10;
  g_hwndSmartAddBtn = CreateWindowExW(
      0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
      SIDEBAR_W + CONTENT_PADDING, addY, 120, 32, hwndDlg,
      (HMENU)IDC_SMART_ACTION_ADD, GetModuleHandleW(NULL), NULL);
}

static void RefreshSmartActionControls(HWND hwndDlg) {
  CreateSmartActionControls(hwndDlg);
}

// ==================== 显示设置对话框 ====================

void ShowSettingsDialog(HWND hwndParent) {
  // 防御性检查：如果标记为打开但窗口已不存在，重置状态
  if (g_isSettingsDialogOpen) {
    if (g_hwndSettingsDlg && IsWindow(g_hwndSettingsDlg)) {
      ShowWindow(g_hwndSettingsDlg, SW_SHOW);
      SetForegroundWindow(g_hwndSettingsDlg);
      return;
    }
    g_isSettingsDialogOpen = false;
    g_hwndSettingsDlg = NULL;
  }

  LoadFontSettings();
  g_isSettingsDialogOpen = true;

  UINT oldMod = g_hotkeyModifiers;
  UINT oldVk = g_hotkeyVirtualKey;

  RegisterSettingsClass();

  RECT parentRect;
  GetWindowRect(hwndParent, &parentRect);
  int px = parentRect.left +
           (parentRect.right - parentRect.left - SETTINGS_WIDTH) / 2;
  int py = parentRect.top +
           (parentRect.bottom - parentRect.top - SETTINGS_HEIGHT) / 2;

  HWND hwndDlg = CreateWindowExW(0, L"SmartClipSettings", L"设置",
                                 WS_POPUP | WS_CLIPCHILDREN, px, py,
                                 SETTINGS_WIDTH, SETTINGS_HEIGHT, hwndParent,
                                 NULL, GetModuleHandleW(NULL), NULL);

  if (!hwndDlg) {
    g_isSettingsDialogOpen = false;
    return;
  }

  g_hwndSettingsDlg = hwndDlg;

  // 移除系统标题栏
  LONG_PTR style = GetWindowLongPtrW(hwndDlg, GWL_STYLE);
  style &= ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX |
             WS_MAXIMIZEBOX);
  SetWindowLongPtrW(hwndDlg, GWL_STYLE, style);
  SetWindowPos(hwndDlg, NULL, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);

  HFONT hCtlFont =
      CreateFontW(19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                  DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");

  // 关闭按钮
  g_hwndSettingsClose =
      CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                      SETTINGS_WIDTH - 46, 0, 46, SETTINGS_TITLEBAR_H, hwndDlg,
                      (HMENU)IDC_SETTINGS_CLOSE, GetModuleHandleW(NULL), NULL);

  // ===== 通用分类控件 =====
  g_hwndToggleNotification = CreateToggle(hwndDlg, 0, IDC_NOTIFICATION_CHECK);
  g_hwndToggleSmoothScroll = CreateToggle(hwndDlg, 1, IDC_SMOOTH_SCROLL_CHECK);
  g_hwndToggleScrollbar = CreateToggle(hwndDlg, 2, IDC_SCROLLBAR_CHECK);
  g_hwndToggleColorDot = CreateWindowExW(
      0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW, GetControlX(TOGGLE_W),
      GetSmartColorDotRowY() + (ROW_HEIGHT - TOGGLE_H) / 2, TOGGLE_W, TOGGLE_H,
      hwndDlg, (HMENU)IDC_COLOR_DOT_CHECK, GetModuleHandleW(NULL), NULL);

  {
    int timeoutY = GetRowY(3) + (ROW_HEIGHT - 28) / 2;
    wchar_t timeoutBuf[16];
    _snwprintf_s(timeoutBuf, _countof(timeoutBuf), L"%d",
                 g_customScrollbarHideDelayMs);
    g_hwndScrollbarTimeoutEdit = CreateWindowExW(
        0, L"EDIT", timeoutBuf,
        WS_CHILD | WS_TABSTOP | ES_CENTER | ES_NUMBER,
        GetControlX(80), timeoutY, 80, 28, hwndDlg,
        (HMENU)IDC_SCROLLBAR_TIMEOUT_EDIT, GetModuleHandleW(NULL), NULL);
    ConfigureSettingsEdit(g_hwndScrollbarTimeoutEdit);
    SendMessageW(g_hwndScrollbarTimeoutEdit, WM_SETFONT, (WPARAM)hCtlFont, TRUE);
    g_oldIosEditProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndScrollbarTimeoutEdit, GWLP_WNDPROC, (LONG_PTR)IosEditProc);
  }

  g_hwndThemeCombo = CreateSettingsCombo(hwndDlg, 4, IDC_THEME_COMBO, 120);

  g_hwndImagePreviewCombo =
      CreateSettingsCombo(hwndDlg, 5, IDC_IMAGE_PREVIEW_COMBO, 120);

  {
    int limitY = GetRowY(6) + (ROW_HEIGHT - 28) / 2;
    wchar_t limitBuf[16];
    _snwprintf_s(limitBuf, _countof(limitBuf), L"%d", g_maxHistoryCount);
    g_hwndHistoryLimitEdit = CreateWindowExW(
        0, L"EDIT", limitBuf, WS_CHILD | WS_TABSTOP | ES_CENTER | ES_NUMBER,
        GetControlX(80), limitY, 80, 28, hwndDlg, (HMENU)IDC_HISTORY_LIMIT_EDIT,
        GetModuleHandleW(NULL), NULL);
    ConfigureSettingsEdit(g_hwndHistoryLimitEdit);
    SendMessageW(g_hwndHistoryLimitEdit, WM_SETFONT, (WPARAM)hCtlFont, TRUE);
    g_oldIosEditProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndHistoryLimitEdit, GWLP_WNDPROC, (LONG_PTR)IosEditProc);
  }
  UpdateScrollbarSettingsControls();

  // ===== 数据分类控件 =====
  auto CreateIosButton = [&](int rowIndex, int ctlId, int width) -> HWND {
    int y = GetRowY(rowIndex) + (ROW_HEIGHT - 32) / 2;
    int x = GetControlX(width);
    return CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW, x, y,
                           width, 32, hwndDlg, (HMENU)(INT_PTR)ctlId,
                           GetModuleHandleW(NULL), NULL);
  };
  g_hwndSetDataDirBtn = CreateIosButton(2, IDC_SET_DATA_DIR, 60);
  g_hwndClearNonFavBtn = CreateIosButton(3, IDC_CLEAR_NON_FAV, 60);
  g_hwndCleanInvalidImagesBtn =
      CreateIosButton(4, IDC_CLEAN_INVALID_IMAGES, 60);

  // ===== 快捷键分类控件 =====
  g_hwndHotkeyEdit = CreateHotkeyEditBox(hwndDlg, 0, IDC_HOTKEY_EDIT);
  ConfigureSettingsEdit(g_hwndHotkeyEdit);
  SendMessageW(g_hwndHotkeyEdit, WM_SETFONT, (WPARAM)hCtlFont, TRUE);
  g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g_hwndHotkeyEdit, GWLP_WNDPROC,
                                             (LONG_PTR)IosEditProc);
  std::wstring hkText = FormatHotkeyText(oldMod, oldVk, L"");
  SetWindowTextW(g_hwndHotkeyEdit, hkText.c_str());

  g_hwndSearchHotkeyEdit =
      CreateHotkeyEditBox(hwndDlg, 1, IDC_SEARCH_HOTKEY_EDIT);
  ConfigureSettingsEdit(g_hwndSearchHotkeyEdit);
  SendMessageW(g_hwndSearchHotkeyEdit, WM_SETFONT, (WPARAM)hCtlFont, TRUE);
  g_oldSearchEditProc = (WNDPROC)SetWindowLongPtrW(
      g_hwndSearchHotkeyEdit, GWLP_WNDPROC, (LONG_PTR)IosEditProc);
  std::wstring shText =
      FormatHotkeyText(g_searchHotkeyModifiers, g_searchHotkeyVirtualKey, L"F");
  SetWindowTextW(g_hwndSearchHotkeyEdit, shText.c_str());

  g_hwndToggleQuickPaste = CreateToggle(hwndDlg, 2, IDC_QUICK_PASTE_CHECK);

  g_hwndQuickPasteCombo =
      CreateSettingsCombo(hwndDlg, 3, IDC_QUICK_PASTE_COMBO, 130);

  // ===== 智能操作分类控件 =====
  CreateSmartActionControls(hwndDlg);

  // ===== 密码分类控件 =====

  g_hwndToggleVaultProtection = CreateWindowExW(
      0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW,
      GetControlX(TOGGLE_W), GetRowY(0) + (ROW_HEIGHT - TOGGLE_H) / 2,
      TOGGLE_W, TOGGLE_H, hwndDlg, (HMENU)IDC_VAULT_PROTECTION_TOGGLE,
      GetModuleHandleW(NULL), NULL);

  g_hwndAuthMethodCombo = CreateSettingsCombo(hwndDlg, 1, IDC_AUTH_METHOD_COMBO, 150);

  g_hwndResetPasswordBtn = CreateWindowExW(
      0, L"BUTTON", L"重置主密码", WS_CHILD | BS_OWNERDRAW,
      GetControlX(120), GetRowY(2) + (ROW_HEIGHT - 32) / 2, 120, 32,
      hwndDlg, (HMENU)IDC_RESET_PASSWORD_BTN, GetModuleHandleW(NULL), NULL);

  // 初始显示通用分类
  g_currentSettingsTab = 0;
  SwitchSettingsTab(0);

  // 初始化冲突检测状态
  UpdateHotkeyConflictState();

  ShowWindow(hwndDlg, SW_SHOW);
  SetForegroundWindow(hwndDlg);
  UpdateWindow(hwndDlg);
}

void ShowHotkeySettingsDialog(HWND hwndParent) {
  ShowSettingsDialog(hwndParent);
}

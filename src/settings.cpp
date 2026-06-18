#include "settings.h"
#include "graphics_utils.h"
#include "history.h"
#include "hotkey.h"
#include "i18n.h"
#include "resource.h"
#include "themed_dialog.h"
#include "theme.h"
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
#define SETTINGS_WIDTH 660
#define SETTINGS_HEIGHT 610
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
  return GetThemeSurfaceAltColor();
}
inline COLORREF GetSidebarBgColor() {
  return GetThemeSidebarBgColor();
}
inline COLORREF GetSidebarHoverColor() {
  return GetThemeSidebarHoverColor();
}
inline COLORREF GetDescTextColor() {
  return GetThemeTextSecondaryColor();
}
inline COLORREF GetSeparatorColor() {
  return GetThemeSeparatorColor();
}
inline COLORREF GetToggleOffColor() {
  return GetThemeToggleOffColor();
}
inline COLORREF GetSettingsTextColor() {
  return GetThemeTextPrimaryColor();
}
inline COLORREF GetSettingsEditBg() {
  return GetThemeInputBgColor();
}
inline COLORREF GetTitlebarBgColor() {
  return GetThemeTitlebarBgColor();
}
#define COLOR_ACCENT (GetThemeAccentColor())

// ==================== 全局变量 ====================
bool g_isSettingsDialogOpen = false;
bool g_isNotificationEnabled = false;
bool g_isCollapseAfterPaste = true;
bool g_isQuickPasteEnabled = true;
UINT g_quickPasteModifiers = MOD_ALT;
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
static bool g_settingsCloseHover = false;
static WNDPROC g_oldSettingsCloseProc = NULL;
static HWND g_hwndToggleNotification = NULL;
static HWND g_hwndToggleSmoothScroll = NULL;
static HWND g_hwndToggleScrollbar = NULL;
static HWND g_hwndToggleColorDot = NULL;
static HWND g_hwndThemeCombo = NULL;
static HWND g_hwndThemeStyleCombo = NULL;
static HWND g_hwndLanguageCombo = NULL;
static HWND g_hwndImagePreviewCombo = NULL;
static HWND g_hwndHotkeyEdit = NULL;
static HWND g_hwndSearchHotkeyEdit = NULL;
static HWND g_hwndQuickPasteCombo = NULL;
static HWND g_hwndScrollbarTimeoutEdit = NULL;
static HWND g_hwndHistoryLimitEdit = NULL;

// === 数据分类控件 ===
static HWND g_hwndSetDataDirBtn = NULL;
static HWND g_hwndClearNonFavBtn = NULL;
static HWND g_hwndCleanInvalidImagesBtn = NULL;
static std::wstring g_dataSizeText = L"计算中...";

static void UpdateScrollbarSettingsControls();

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

static bool IsOverDataDirPath(POINT pt) {
  if (g_currentSettingsTab != 2)
    return false;

  int contentLeft = SIDEBAR_W + CONTENT_PADDING;
  int contentRight = SETTINGS_WIDTH - CONTENT_PADDING;
  int row2Y = GetRowY(3);
  RECT rcPath = {contentLeft, row2Y + 30, contentRight - 70, row2Y + 50};
  return PtInRect(&rcPath, pt) != FALSE;
}

static std::wstring BuildDataSizeText() {
  ULONGLONG usedBytes = GetDataDirSize();

  std::wstring filePath = GetDataFilePath();
  wchar_t volumePath[MAX_PATH] = {};
  bool hasVolumePath =
      GetVolumePathNameW(filePath.c_str(), volumePath, _countof(volumePath)) !=
      FALSE;

  ULARGE_INTEGER freeBytes = {};
  if (hasVolumePath &&
      GetDiskFreeSpaceExW(volumePath, NULL, NULL, &freeBytes) != FALSE) {
    return FormatFileSize(usedBytes) + L" / " +
           FormatFileSize(freeBytes.QuadPart);
  }

  return FormatFileSize(usedBytes);
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

static void SetHotkeyEditPlaceholder(HWND hwnd, const wchar_t *text) {
  if (!hwnd)
    return;
  SendMessageW(hwnd, 0x1501, TRUE, (LPARAM)text); // EM_SETCUEBANNER
}

static void SyncHotkeyEditTextRect(HWND hwnd) {
  if (!hwnd)
    return;
  RECT rcClient = {};
  GetClientRect(hwnd, &rcClient);
  if (rcClient.right <= rcClient.left || rcClient.bottom <= rcClient.top)
    return;

  HDC hdc = GetDC(hwnd);
  if (!hdc)
    return;

  HFONT hFont = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
  HFONT hOldFont = NULL;
  if (hFont)
    hOldFont = (HFONT)SelectObject(hdc, hFont);

  TEXTMETRICW tm = {};
  GetTextMetricsW(hdc, &tm);
  LOGFONTW lf = {};
  if (hFont)
    GetObjectW(hFont, sizeof(lf), &lf);
  if (hOldFont)
    SelectObject(hdc, hOldFont);
  ReleaseDC(hwnd, hdc);

  int fontHeight = lf.lfHeight != 0 ? abs(lf.lfHeight) : (int)tm.tmHeight;
  int textHeight = std::max(1, fontHeight + (int)tm.tmExternalLeading);
  int availableHeight = (int)(rcClient.bottom - rcClient.top);
  int topPadding = (availableHeight - textHeight) / 2;
  if (topPadding < 3)
    topPadding = 3;
  int bottomPadding = availableHeight - textHeight - topPadding;
  if (bottomPadding < 3) {
    bottomPadding = 3;
    topPadding = std::max(3, availableHeight - textHeight - bottomPadding);
  }

  RECT rcText = {10,
                 topPadding,
                 std::max(11, (int)rcClient.right - 10),
                 std::max(topPadding + textHeight + 1,
                          (int)rcClient.bottom - bottomPadding)};
  SendMessageW(hwnd, EM_SETRECT, 0, (LPARAM)&rcText);
  SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
               MAKELONG(0, 0));
  InvalidateRect(hwnd, NULL, TRUE);
}

static void SyncSettingsNumberEditTextRect(HWND hwnd) {
  if (!hwnd)
    return;
  RECT rcClient = {};
  GetClientRect(hwnd, &rcClient);
  if (rcClient.right <= rcClient.left || rcClient.bottom <= rcClient.top)
    return;

  HDC hdc = GetDC(hwnd);
  if (!hdc)
    return;

  HFONT hFont = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
  HFONT hOldFont = NULL;
  if (hFont)
    hOldFont = (HFONT)SelectObject(hdc, hFont);

  TEXTMETRICW tm = {};
  GetTextMetricsW(hdc, &tm);
  LOGFONTW lf = {};
  if (hFont)
    GetObjectW(hFont, sizeof(lf), &lf);
  if (hOldFont)
    SelectObject(hdc, hOldFont);
  ReleaseDC(hwnd, hdc);

  int fontHeight = lf.lfHeight != 0 ? abs(lf.lfHeight) : (int)tm.tmHeight;
  int textHeight = std::max(1, fontHeight + (int)tm.tmExternalLeading);
  int availableHeight = (int)(rcClient.bottom - rcClient.top);
  int topPadding = (availableHeight - textHeight) / 2;
  if (topPadding < 2)
    topPadding = 2;
  int bottomPadding = availableHeight - textHeight - topPadding;
  if (bottomPadding < 2) {
    bottomPadding = 2;
    topPadding = std::max(2, availableHeight - textHeight - bottomPadding);
  }

  RECT rcText = {0,
                 topPadding,
                 std::max(1, (int)rcClient.right),
                 std::max(topPadding + textHeight + 1,
                          (int)rcClient.bottom - bottomPadding)};
  SendMessageW(hwnd, EM_SETRECT, 0, (LPARAM)&rcText);
  SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
               MAKELONG(0, 0));
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
    else if (hwnd == g_hwndHistoryLimitEdit || hwnd == g_hwndScrollbarTimeoutEdit)
      SyncSettingsNumberEditTextRect(hwnd);
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

static void ClearRecordedHotkey(HWND hwnd, int hotkeyType) {
  if (hotkeyType == 1) {
    g_isRecordingSearchHotkey = false;
    g_isSearchHotkeyEnabled = false;
    g_searchHotkeyModifiers = 0;
    g_searchHotkeyVirtualKey = 0;
    SetWindowTextW(hwnd, L"");
    SetHotkeyEditPlaceholder(hwnd, L"请输入快捷键");
  } else {
    g_isRecordingHotkey = false;
    g_isHotkeyEnabled = false;
    g_hotkeyModifiers = 0;
    g_hotkeyVirtualKey = 0;
    UnregisterHotkey(g_hwndMain);
    SetWindowTextW(hwnd, L"");
    SetHotkeyEditPlaceholder(hwnd, L"请输入快捷键");
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
        ClearRecordedHotkey(hwnd, 0);
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
        ClearRecordedHotkey(hwnd, 1);
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
    ShowWindow(g_hwndToggleColorDot, SW_HIDE);
  if (g_hwndScrollbarTimeoutEdit)
    ShowWindow(g_hwndScrollbarTimeoutEdit, showGen);
  if (g_hwndThemeCombo)
    ShowWindow(g_hwndThemeCombo, showGen);
  if (g_hwndThemeStyleCombo)
    ShowWindow(g_hwndThemeStyleCombo, SW_HIDE);
  if (g_hwndLanguageCombo)
    ShowWindow(g_hwndLanguageCombo, showGen);
  if (g_hwndImagePreviewCombo)
    ShowWindow(g_hwndImagePreviewCombo, showGen);
  if (g_hwndHistoryLimitEdit)
    ShowWindow(g_hwndHistoryLimitEdit, showDt);

  if (g_hwndHotkeyEdit)
    ShowWindow(g_hwndHotkeyEdit, showHk);
  if (g_hwndSearchHotkeyEdit)
    ShowWindow(g_hwndSearchHotkeyEdit, showHk);
  if (g_hwndQuickPasteCombo)
    ShowWindow(g_hwndQuickPasteCombo, showHk);
  if (g_hwndSetDataDirBtn)
    ShowWindow(g_hwndSetDataDirBtn, showDt);
  if (g_hwndClearNonFavBtn)
    ShowWindow(g_hwndClearNonFavBtn, showDt);
  if (g_hwndCleanInvalidImagesBtn)
    ShowWindow(g_hwndCleanInvalidImagesBtn, showDt);

  // 切换到数据页时刷新磁盘空间
  if (tab == 2) {
    g_dataSizeText = BuildDataSizeText();
  }

  // free 版隐藏智能操作页

  if (g_hwndSettingsDlg)
    InvalidateRect(g_hwndSettingsDlg, NULL, TRUE);
  UpdateScrollbarSettingsControls();
}

// PLACEHOLDER_SETTINGS_PART4

// ==================== 侧边栏数据 ====================
struct SidebarItem {
  const wchar_t *icon;
  StringId labelId;
};
static const SidebarItem g_sidebarItems[] = {
    {L"\uE713", STR_SETTINGS_GENERAL},
    {L"\uE765", STR_SETTINGS_HOTKEY},
    {L"", STR_SETTINGS_DATA},
};
#define SIDEBAR_COUNT 3

// 设置行数据
struct SettingRowInfo {
  StringId titleId;
  StringId descId;
};

static const SettingRowInfo g_generalRows[] = {
    {STR_ROW_NOTIFICATION, STR_ROW_NOTIFICATION_DESC},
    {STR_ROW_SMOOTH_SCROLL, STR_ROW_SMOOTH_SCROLL_DESC},
    {STR_ROW_SCROLLBAR, STR_ROW_SCROLLBAR_DESC},
    {STR_ROW_SCROLLBAR_DELAY, STR_ROW_SCROLLBAR_DELAY_DESC},
    {STR_ROW_THEME_MODE, STR_ROW_THEME_MODE_DESC},
    {STR_ROW_LANGUAGE, STR_ROW_LANGUAGE_DESC},
    {STR_ROW_IMAGE_PREVIEW, STR_ROW_IMAGE_PREVIEW_DESC},
};
static const SettingRowInfo g_hotkeyRows[] = {
    {STR_ROW_HOTKEY_TOGGLE, STR_ROW_HOTKEY_TOGGLE_DESC},
    {STR_ROW_HOTKEY_SEARCH, STR_ROW_HOTKEY_SEARCH_DESC},
    {STR_ROW_QUICK_PASTE_MOD, STR_ROW_QUICK_PASTE_MOD_DESC},
};
static const SettingRowInfo g_dataRows[] = {
    {STR_ROW_DATA_SIZE, STR_COUNT},
    {STR_ROW_PASTE_COUNT, STR_COUNT},
    {STR_ROW_HISTORY_LIMIT, STR_ROW_HISTORY_LIMIT_DESC},
    {STR_ROW_SET_DATA_DIR, STR_COUNT},
    {STR_ROW_CLEAR_NON_FAV, STR_ROW_CLEAR_NON_FAV_DESC},
    {STR_ROW_CLEAN_INVALID_IMAGES, STR_ROW_CLEAN_INVALID_IMAGES_DESC},
};
// 分类标题
struct CategoryHeader {
  StringId titleId;
  StringId descId;
  const SettingRowInfo *rows;
  int rowCount;
};
static const CategoryHeader g_categories[] = {
    {STR_SETTINGS_GENERAL, STR_SETTINGS_GENERAL_DESC, g_generalRows, 7},
    {STR_SETTINGS_HOTKEY, STR_SETTINGS_HOTKEY_DESC, g_hotkeyRows, 3},
    {STR_SETTINGS_DATA, STR_COUNT, g_dataRows, 6},
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

static const StringId g_themeItemIds[] = {STR_THEME_LIGHT, STR_THEME_DARK,
                                          STR_THEME_SYSTEM};
static const StringId g_themeStyleItemIds[] = {
    STR_THEME_STYLE_CLASSIC, STR_THEME_STYLE_GRAPHITE,
    STR_THEME_STYLE_WARM, STR_THEME_STYLE_HIGH_CONTRAST};
static const StringId g_languageItemIds[] = {STR_LANGUAGE_ZH_CN,
                                             STR_LANGUAGE_EN_US};
static const StringId g_previewItemIds[] = {STR_PREVIEW_OFF, STR_PREVIEW_BLUR,
                                            STR_PREVIEW_SD, STR_PREVIEW_HD};
// 快捷粘贴修饰键选项（与 g_quickPasteModValues 一一对应）
static const wchar_t *g_quickPasteModItems[] = {
    L"Alt",      L"Ctrl",       L"Shift",
    L"Ctrl+Alt", L"Ctrl+Shift", L"Alt+Shift"};
static const UINT g_quickPasteModValues[] = {
    MOD_ALT,                   MOD_CONTROL,            MOD_SHIFT,
    MOD_CONTROL | MOD_ALT,     MOD_CONTROL | MOD_SHIFT, MOD_ALT | MOD_SHIFT};
#define QUICK_PASTE_MOD_COUNT 6
// 获取下拉按钮当前显示文字
static const wchar_t *GetDropdownText(int ctlId) {
  if (ctlId == IDC_THEME_COMBO)
    return T(g_themeItemIds[(int)g_themeMode]);
  if (ctlId == IDC_THEME_STYLE_COMBO)
    return T(STR_THEME_STYLE_HIGH_CONTRAST);
  if (ctlId == IDC_LANGUAGE_COMBO)
    return T(g_languageItemIds[(int)g_appLanguage]);
  if (ctlId == IDC_IMAGE_PREVIEW_COMBO)
    return T(g_previewItemIds[(int)g_imagePreviewQuality]);
  if (ctlId == IDC_QUICK_PASTE_COMBO) {
    for (int i = 0; i < QUICK_PASTE_MOD_COUNT; ++i) {
      if (g_quickPasteModValues[i] == g_quickPasteModifiers)
        return g_quickPasteModItems[i];
    }
    return g_quickPasteModItems[0];
  }
  return L"";
}

static int GetDropdownSelectedIndex(int ctlId) {
  if (ctlId == IDC_THEME_COMBO)
    return (int)g_themeMode;
  if (ctlId == IDC_THEME_STYLE_COMBO)
    return 3;
  if (ctlId == IDC_LANGUAGE_COMBO)
    return (int)g_appLanguage;
  if (ctlId == IDC_IMAGE_PREVIEW_COMBO)
    return (int)g_imagePreviewQuality;
  if (ctlId == IDC_QUICK_PASTE_COMBO) {
    for (int i = 0; i < QUICK_PASTE_MOD_COUNT; ++i) {
      if (g_quickPasteModValues[i] == g_quickPasteModifiers)
        return i;
    }
    return 0;
  }
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
        COLORREF hc = GetThemeDropdownHoverColor();
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
    static const wchar_t *s_themeItems[3];
    for (int i = 0; i < 3; ++i)
      s_themeItems[i] = T(g_themeItemIds[i]);
    g_activeDropdown.items = s_themeItems;
    g_activeDropdown.itemCount = 3;
  } else if (ctlId == IDC_THEME_STYLE_COMBO) {
    static const wchar_t *s_themeStyleItems[1] = {L"Black & White"};
    g_activeDropdown.items = s_themeStyleItems;
    g_activeDropdown.itemCount = 1;
  } else if (ctlId == IDC_LANGUAGE_COMBO) {
    static const wchar_t *s_languageItems[2];
    for (int i = 0; i < 2; ++i)
      s_languageItems[i] = T(g_languageItemIds[i]);
    g_activeDropdown.items = s_languageItems;
    g_activeDropdown.itemCount = 2;
  } else if (ctlId == IDC_IMAGE_PREVIEW_COMBO) {
    static const wchar_t *s_previewItems[4];
    for (int i = 0; i < 4; ++i)
      s_previewItems[i] = T(g_previewItemIds[i]);
    g_activeDropdown.items = s_previewItems;
    g_activeDropdown.itemCount = 4;
  } else if (ctlId == IDC_QUICK_PASTE_COMBO) {
    g_activeDropdown.items = g_quickPasteModItems;
    g_activeDropdown.itemCount = QUICK_PASTE_MOD_COUNT;
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
    DrawTextW(hdc, T(STR_SETTINGS_TITLE), -1, &rcTitleText,
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

      COLORREF textCol = GetSettingsTextColor();
      if (selected) {
        textCol = g_isDarkMode ? RGB(18, 20, 24) : RGB(48, 56, 68);
      }
      SetTextColor(hdc, textCol);

      // 图标
      SelectObject(hdc, g_hSidebarIconFont);
      RECT rcIcon = {20, itemY, 44, itemY + SIDEBAR_ITEM_H};
      DrawTextW(hdc, g_sidebarItems[i].icon, 1, &rcIcon,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      // 文字
      SelectObject(hdc, g_hSidebarFont);
      RECT rcLabel = {48, itemY, SIDEBAR_W - 8, itemY + SIDEBAR_ITEM_H};
      DrawTextW(hdc, T(g_sidebarItems[i].labelId), -1, &rcLabel,
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
    DrawTextW(hdc, T(cat.titleId), -1, &rcCatTitle,
              DT_LEFT | DT_TOP | DT_SINGLELINE);

    SelectObject(hdc, g_hHeaderDescFont);
    SetTextColor(hdc, GetDescTextColor());
    RECT rcCatDesc = {contentLeft, SETTINGS_TITLEBAR_H + 34, contentRight,
                      SETTINGS_TITLEBAR_H + 50};
    DrawTextW(hdc, cat.descId == STR_COUNT ? L"" : T(cat.descId), -1,
              &rcCatDesc, DT_LEFT | DT_TOP | DT_SINGLELINE);

    // 设置行
    for (int i = 0; i < cat.rowCount; i++) {
      int rowY = GetRowY(i);

      // 标题
      SelectObject(hdc, g_hTitleFont);
      SetTextColor(hdc, GetSettingsTextColor());
      RECT rcRowTitle = {contentLeft, rowY + 12, contentRight - 160, rowY + 30};
      DrawTextW(hdc, T(cat.rows[i].titleId), -1, &rcRowTitle,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

      // 描述
      SelectObject(hdc, g_hDescFont);
      SetTextColor(hdc, GetDescTextColor());
      RECT rcRowDesc = {contentLeft, rowY + 32, contentRight - 160, rowY + 48};
      DrawTextW(hdc, cat.rows[i].descId == STR_COUNT ? L"" : T(cat.rows[i].descId), -1,
                &rcRowDesc,
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
      RECT rcSize = {contentRight - 220, row0Y + 12, contentRight, row0Y + 30};
      DrawTextW(hdc, g_dataSizeText.c_str(), -1, &rcSize,
                DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS);

      // 第1行右侧：蓝色粘贴次数
      extern int g_pasteCount;
      int row1Y = GetRowY(1);
      SelectObject(hdc, g_hTitleFont);
      SetTextColor(hdc, COLOR_ACCENT);
      wchar_t pasteBuf[32];
      _snwprintf_s(pasteBuf, 32, L"%d 次", g_pasteCount);
      RECT rcPaste = {contentRight - 150, row1Y + 12, contentRight, row1Y + 30};
      DrawTextW(hdc, pasteBuf, -1, &rcPaste, DT_RIGHT | DT_SINGLELINE);

      // 第3行：数据目录路径作为描述文字（按钮左侧）
      int row2Y = GetRowY(3);
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
      if (g_settingsCloseHover)
        bg = RGB(232, 17, 35);
      HBRUSH hBr = CreateSolidBrush(bg);
      FillRect(lpDIS->hDC, &rc, hBr);
      DeleteObject(hBr);
      SetBkMode(lpDIS->hDC, TRANSPARENT);
      SetTextColor(lpDIS->hDC,
                   g_settingsCloseHover ? RGB(255, 255, 255) : GetSettingsTextColor());
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
        lpDIS->CtlID == IDC_COLOR_DOT_CHECK) {

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
      }
      DrawToggleSwitch(lpDIS->hDC, rc, isOn);
      return TRUE;
    }

    // 下拉选择器按钮
    if (lpDIS->CtlID == IDC_THEME_COMBO ||
        lpDIS->CtlID == IDC_THEME_STYLE_COMBO ||
        lpDIS->CtlID == IDC_LANGUAGE_COMBO ||
        lpDIS->CtlID == IDC_IMAGE_PREVIEW_COMBO ||
        lpDIS->CtlID == IDC_QUICK_PASTE_COMBO) {
      HBRUSH hBgBr = CreateSolidBrush(GetSettingsBgColor());
      FillRect(lpDIS->hDC, &rc, hBgBr);
      DeleteObject(hBgBr);
      DrawDropdownButton(lpDIS->hDC, rc, lpDIS->CtlID);
      return TRUE;
    }

    // iOS 风格操作按钮（蓝色圆角）
    if (lpDIS->CtlID == IDC_SET_DATA_DIR || lpDIS->CtlID == IDC_CLEAR_NON_FAV ||
        lpDIS->CtlID == IDC_CLEAN_INVALID_IMAGES) {
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

  case WM_SETCURSOR:
    if ((HWND)wParam == hwnd && LOWORD(lParam) == HTCLIENT) {
      POINT pt = {};
      if (GetCursorPos(&pt)) {
        ScreenToClient(hwnd, &pt);
        if (IsOverDataDirPath(pt)) {
          SetCursor(LoadCursorW(NULL, IDC_HAND));
          return TRUE;
        }
      }
    }
    break;

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
      bool overPath = IsOverDataDirPath(pt);
      if (overPath && !g_dataDirHovered) {
        g_dataDirHovered = true;
        SetTimer(hwnd, ID_DATADIR_UNDERLINE_TIMER, 16, NULL);
      } else if (!overPath && g_dataDirHovered) {
        g_dataDirHovered = false;
        SetTimer(hwnd, ID_DATADIR_UNDERLINE_TIMER, 16, NULL);
      }
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
      int row2Y = GetRowY(3);
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
    // 数据分类：点击数据目录路径打开资源管理器（第3行）
    if (g_currentSettingsTab == 2 && pt.x > SIDEBAR_W) {
      int row2Y = GetRowY(3);
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
    break;
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
              if (MigrateDataDir(path)) {
                g_dataSizeText = BuildDataSizeText();
                InvalidateRect(hwnd, NULL, TRUE);
              }
            }
          }
          CoTaskMemFree(pidl);
        }
        return 0;
      }
      if (wID == IDC_CLEAR_NON_FAV) {
        ThemedConfirmDialogConfig dialog = {
            L"清理非收藏数据",
            L"删除全部非收藏记录",
            L"仅保留你已收藏的内容",
            L"这会移除所有未收藏的历史记录，操作不可撤销。",
            L"立即清理",
            L"取消",
            424,
            236,
            {14, 78, 410, 170},
            true,
            false,
            true};
        if (ShowThemedConfirmDialog(hwnd, dialog)) {
          ClearNonFavoriteHistory();
          g_dataSizeText = BuildDataSizeText();
          InvalidateRect(hwnd, NULL, TRUE);
          if (g_isNotificationEnabled)
            ShowTrayBalloon(g_hwndMain, L"提示", L"非收藏数据已清理");
        }
        return 0;
      }
      if (wID == IDC_CLEAN_INVALID_IMAGES) {
        ThemedConfirmDialogConfig dialog = {
            L"删除失效图片记录",
            L"清理原图已丢失的记录",
            L"只删除已经失效的图片项",
            L"这会移除原始图片文件已不存在的记录，操作不可撤销。",
            L"立即清理",
            L"取消",
            424,
            236,
            {14, 78, 410, 170},
            true,
            false,
            true};
        if (ShowThemedConfirmDialog(hwnd, dialog)) {
          extern void CleanInvalidImageRecords();
          CleanInvalidImageRecords();
          g_dataSizeText = BuildDataSizeText();
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
      if (wID == IDC_THEME_STYLE_COMBO) {
        ShowDropdownPopup(g_hwndThemeStyleCombo, IDC_THEME_STYLE_COMBO);
        return 0;
      }
      if (wID == IDC_IMAGE_PREVIEW_COMBO) {
        ShowDropdownPopup(g_hwndImagePreviewCombo, IDC_IMAGE_PREVIEW_COMBO);
        return 0;
      }
      if (wID == IDC_LANGUAGE_COMBO) {
        ShowDropdownPopup(g_hwndLanguageCombo, IDC_LANGUAGE_COMBO);
        return 0;
      }
      if (wID == IDC_QUICK_PASTE_COMBO) {
        ShowDropdownPopup(g_hwndQuickPasteCombo, IDC_QUICK_PASTE_COMBO);
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

    if (wID == IDC_THEME_STYLE_COMBO && wNotify == CBN_SELCHANGE) {
      int sel = g_activeDropdown.selectedIndex;
      if (sel >= 0 && sel <= 3) {
        g_themeId = (ThemeId)sel;
        ApplyTheme();
        SaveHotkeySettings();
        UpdateSettingsBrushes();
        SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND,
                         (LONG_PTR)CreateSolidBrush(GetSettingsBgColor()));
        RedrawWindow(hwnd, NULL, NULL,
                     RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN |
                         RDW_UPDATENOW);
      }
      return 0;
    }

    if (wID == IDC_LANGUAGE_COMBO && wNotify == CBN_SELCHANGE) {
      int sel = g_activeDropdown.selectedIndex;
      if (sel >= 0 && sel <= 1) {
        g_appLanguage = (AppLanguage)sel;
        ApplyLanguage();
        SaveHotkeySettings();
        UpdateSettingsBrushes();
        SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND,
                         (LONG_PTR)CreateSolidBrush(GetSettingsBgColor()));
        RedrawWindow(hwnd, NULL, NULL,
                     RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN |
                         RDW_UPDATENOW);
      }
      return 0;
    }

    // 图片预览选择
    if (wID == IDC_IMAGE_PREVIEW_COMBO && wNotify == CBN_SELCHANGE) {
      g_imagePreviewQuality =
          (ImagePreviewQuality)g_activeDropdown.selectedIndex;
      SaveHotkeySettings();
      ApplyImagePreviewQualityChange();
      if (g_hwndImagePreviewCombo)
        InvalidateRect(g_hwndImagePreviewCombo, NULL, TRUE);
      return 0;
    }

    // 快捷粘贴修饰键选择
    if (wID == IDC_QUICK_PASTE_COMBO && wNotify == CBN_SELCHANGE) {
      int sel = g_activeDropdown.selectedIndex;
      if (sel >= 0 && sel < QUICK_PASTE_MOD_COUNT) {
        g_quickPasteModifiers = g_quickPasteModValues[sel];
        SaveHotkeySettings();
        // 重新注册快捷粘贴热键
        extern HWND g_hwndMain;
        if (g_hwndMain && g_isQuickPasteEnabled) {
          UnregisterQuickPasteHotkeys(g_hwndMain);
          RegisterQuickPasteHotkeys(g_hwndMain);
        }
        if (g_hwndQuickPasteCombo)
          InvalidateRect(g_hwndQuickPasteCombo, NULL, TRUE);
      }
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
        std::wstring text =
            FormatHotkeyText(g_hotkeyModifiers, g_hotkeyVirtualKey, L"");
        SetWindowTextW(g_hwndHotkeyEdit, text.c_str());
        SetHotkeyEditPlaceholder(g_hwndHotkeyEdit, L"请输入快捷键");
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
        SetHotkeyEditPlaceholder(g_hwndSearchHotkeyEdit, L"请输入快捷键");
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
    g_isSettingsDialogOpen = false;
    g_hwndSettingsDlg = NULL;
    g_settingsCloseHover = false;
    g_hwndSettingsClose = NULL;
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
  int y = GetRowY(rowIndex) + (ROW_HEIGHT - 32) / 2;
  int x = GetControlX(180);
  return CreateWindowExW(0, L"EDIT", NULL,
                         WS_CHILD | WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL |
                             ES_MULTILINE,
                         x, y, 180, 32, parent, (HMENU)(INT_PTR)ctlId,
                         GetModuleHandleW(NULL), NULL);
}

// 关闭按钮子类化窗口过程 - 处理悬浮效果
LRESULT CALLBACK SettingsCloseBtnProc(HWND hwnd, UINT message, WPARAM wParam,
                                      LPARAM lParam) {
  switch (message) {
  case WM_MOUSEMOVE: {
    TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    if (!g_settingsCloseHover) {
      g_settingsCloseHover = true;
      InvalidateRect(hwnd, NULL, TRUE);
    }
    break;
  }
  case WM_MOUSELEAVE:
    if (g_settingsCloseHover) {
      g_settingsCloseHover = false;
      InvalidateRect(hwnd, NULL, TRUE);
    }
    break;
  }
  return CallWindowProcW(g_oldSettingsCloseProc, hwnd, message, wParam, lParam);
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
  g_oldSettingsCloseProc = (WNDPROC)SetWindowLongPtrW(
      g_hwndSettingsClose, GWLP_WNDPROC, (LONG_PTR)SettingsCloseBtnProc);

  // ===== 通用分类控件 =====
  g_hwndToggleNotification = CreateToggle(hwndDlg, 0, IDC_NOTIFICATION_CHECK);
  g_hwndToggleSmoothScroll = CreateToggle(hwndDlg, 1, IDC_SMOOTH_SCROLL_CHECK);
  g_hwndToggleScrollbar = CreateToggle(hwndDlg, 2, IDC_SCROLLBAR_CHECK);
  g_hwndToggleColorDot = CreateWindowExW(
      0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW, GetControlX(TOGGLE_W),
      GetRowY(6) + (ROW_HEIGHT - TOGGLE_H) / 2, TOGGLE_W, TOGGLE_H,
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
    SyncSettingsNumberEditTextRect(g_hwndScrollbarTimeoutEdit);
  }

  g_hwndThemeCombo = CreateSettingsCombo(hwndDlg, 4, IDC_THEME_COMBO, 120);
  g_hwndThemeStyleCombo =
      CreateSettingsCombo(hwndDlg, 5, IDC_THEME_STYLE_COMBO, 120);
  g_hwndLanguageCombo =
      CreateSettingsCombo(hwndDlg, 5, IDC_LANGUAGE_COMBO, 140);

  g_hwndImagePreviewCombo =
      CreateSettingsCombo(hwndDlg, 6, IDC_IMAGE_PREVIEW_COMBO, 120);

  {
    int limitY = GetRowY(2) + (ROW_HEIGHT - 28) / 2;
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
    SyncSettingsNumberEditTextRect(g_hwndHistoryLimitEdit);
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
  g_hwndSetDataDirBtn = CreateIosButton(3, IDC_SET_DATA_DIR, 60);
  g_hwndClearNonFavBtn = CreateIosButton(4, IDC_CLEAR_NON_FAV, 60);
  g_hwndCleanInvalidImagesBtn =
      CreateIosButton(5, IDC_CLEAN_INVALID_IMAGES, 60);

  // ===== 快捷键分类控件 =====
  g_hwndHotkeyEdit = CreateHotkeyEditBox(hwndDlg, 0, IDC_HOTKEY_EDIT);
  ConfigureSettingsEdit(g_hwndHotkeyEdit);
  SendMessageW(g_hwndHotkeyEdit, WM_SETFONT, (WPARAM)hCtlFont, TRUE);
  g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g_hwndHotkeyEdit, GWLP_WNDPROC,
                                             (LONG_PTR)HotkeyEditProc);
  std::wstring hkText =
      (g_isHotkeyEnabled && oldMod != 0 && oldVk != 0)
          ? FormatHotkeyText(oldMod, oldVk, L"")
          : L"";
  SetWindowTextW(g_hwndHotkeyEdit, hkText.c_str());
  SetHotkeyEditPlaceholder(g_hwndHotkeyEdit, L"请输入快捷键");
  SyncHotkeyEditTextRect(g_hwndHotkeyEdit);

  g_hwndSearchHotkeyEdit =
      CreateHotkeyEditBox(hwndDlg, 1, IDC_SEARCH_HOTKEY_EDIT);
  ConfigureSettingsEdit(g_hwndSearchHotkeyEdit);
  SendMessageW(g_hwndSearchHotkeyEdit, WM_SETFONT, (WPARAM)hCtlFont, TRUE);
  g_oldSearchEditProc = (WNDPROC)SetWindowLongPtrW(
      g_hwndSearchHotkeyEdit, GWLP_WNDPROC, (LONG_PTR)SearchHotkeyEditProc);
  std::wstring shText =
      FormatHotkeyText(g_searchHotkeyModifiers, g_searchHotkeyVirtualKey, L"F");
  SetWindowTextW(g_hwndSearchHotkeyEdit, shText.c_str());
  SetHotkeyEditPlaceholder(g_hwndSearchHotkeyEdit, L"请输入快捷键");
  SyncHotkeyEditTextRect(g_hwndSearchHotkeyEdit);

  // 快捷粘贴修饰键下拉选择器
  g_hwndQuickPasteCombo =
      CreateSettingsCombo(hwndDlg, 2, IDC_QUICK_PASTE_COMBO, 120);

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

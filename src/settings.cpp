#include "settings.h"
#include "graphics_utils.h"
#include "history.h"
#include "hotkey.h"
#include "i18n.h"
#include "machine_info.h"
#include "resource.h"
#include "theme.h"
#include "themed_dialog.h"
#include "tray.h"
#include "version.h"
#include <algorithm>
#include <commctrl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <windowsx.h>

extern HWND g_hwndMain;

// 主窗口控件ID（用于刷新）
#define ID_SEARCH_BOX 104
#define ID_TOPMOST_BUTTON 1006

// ==================== 布局常量 ====================
#define SETTINGS_WIDTH 760
#define SETTINGS_HEIGHT 690
#define SETTINGS_TITLEBAR_H 36
#define SIDEBAR_W 180
#define CONTENT_PADDING 24
#define ROW_HEIGHT 60
#define TOGGLE_W 44
#define TOGGLE_H 24
#define TOGGLE_THUMB_R 9
#define SIDEBAR_ITEM_H 44
#define CATEGORY_HEADER_H 56
#define BACKUP_HEADER_H 56

// ==================== 颜色辅助 ====================
inline COLORREF GetSettingsBgColor() { return GetThemeSurfaceAltColor(); }
inline COLORREF GetSidebarBgColor() { return GetThemeSidebarBgColor(); }
inline COLORREF GetSidebarHoverColor() { return GetThemeSidebarHoverColor(); }
inline COLORREF GetDescTextColor() { return GetThemeTextSecondaryColor(); }
inline COLORREF GetSeparatorColor() { return GetThemeSeparatorColor(); }
inline COLORREF GetToggleOffColor() { return GetThemeToggleOffColor(); }
inline COLORREF GetSettingsTextColor() { return GetThemeTextPrimaryColor(); }
inline COLORREF GetSettingsEditBg() { return GetThemeInputBgColor(); }
inline COLORREF GetTitlebarBgColor() { return GetThemeTitlebarBgColor(); }
#define COLOR_ACCENT (GetThemeAccentColor())

// ==================== 全局变量 ====================
bool g_isSettingsDialogOpen = false;
bool g_isNotificationEnabled = false;
bool g_isCollapseAfterPaste = true;
bool g_isQuickPasteEnabled = true;
UINT g_quickPasteModifiers = MOD_ALT;
bool g_allHotkeysEnabled = true; // 托盘快捷键总开关（默认启用）
bool g_isSmoothScrollEnabled = false;
bool g_isCustomScrollbarEnabled = true;
bool g_isColorDotEnabled = true;
bool g_isTaskbarVisible = true;
bool g_isStartupEnabled = false;
bool g_isHoverSelectEnabled = true; // 悬浮选中（默认开启）
ImagePreviewQuality g_imagePreviewQuality = PREVIEW_HD;
int g_customScrollbarHideDelayMs = 1500;
int g_maxTextSizeKB = 50; // 默认50KB，超过此大小的文本不记录
std::wstring g_fontName = L"Microsoft YaHei";
int g_fontSize = 16;
int g_fontWeight = FW_NORMAL;
BYTE g_fontItalic = FALSE;
HWND g_hwndSettingsDlg = NULL;

// PLACEHOLDER_SETTINGS_PART2

// 设置对话框内部状态
static UINT g_settingsDpi = 96;
int g_currentSettingsTab = 0;
static int g_settingsHoverSidebar = -1;
static bool g_isRecordingHotkey = false;
static bool g_isRecordingSearchHotkey = false;
// 录制快捷键前保存旧值，用于用户取消时恢复
static UINT g_savedHotkeyMod = 0;
static UINT g_savedHotkeyVk = 0;
static bool g_savedHotkeyEnabled = false;
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
// GitHub 仓库链接悬浮动画
static bool g_githubHovered = false;
static float g_githubUnderlineProgress = 0.0f;
#define ID_GITHUB_UNDERLINE_TIMER 302

// 控件句柄
static HWND g_hwndSettingsClose = NULL;
static bool g_settingsCloseHover = false;
static WNDPROC g_oldSettingsCloseProc = NULL;
static HWND g_hwndToggleNotification = NULL;
static HWND g_hwndToggleSmoothScroll = NULL;
static HWND g_hwndToggleScrollbar = NULL;
static HWND g_hwndToggleColorDot = NULL;
static HWND g_hwndToggleTaskbar = NULL;
static HWND g_hwndToggleStartup = NULL;
static HWND g_hwndToggleHoverSelect = NULL;
static HWND g_hwndThemeCombo = NULL;
static HWND g_hwndThemeStyleCombo = NULL;
static HWND g_hwndLanguageCombo = NULL;
static HWND g_hwndImagePreviewCombo = NULL;
static HWND g_hwndHotkeyEdit = NULL;
static HWND g_hwndSearchHotkeyEdit = NULL;
static HWND g_hwndQuickPasteCombo = NULL;
static HWND g_hwndFavoriteHotkeyCombo = NULL;
static HWND g_hwndScrollbarTimeoutEdit = NULL;
static HWND g_hwndHistoryLimitEdit = NULL;

// === 数据分类控件 ===
static HWND g_hwndSetDataDirBtn = NULL;
static HWND g_hwndClearNonFavBtn = NULL;
static HWND g_hwndCleanInvalidImagesBtn = NULL;
static HWND g_hwndExportDataBtn = NULL;
static HWND g_hwndImportDataBtn = NULL;
static HWND g_hwndTextSizeLimitEdit = NULL;
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

static int SDpi(int value) { return ScaleForDpi(value, g_settingsDpi); }
static UINT SettingsCompactDpi() { return std::min<UINT>(g_settingsDpi, 144); }
static int SCDpi(int value) { return ScaleForDpi(value, SettingsCompactDpi()); }
static int SettingsWidth() { return SDpi(SETTINGS_WIDTH); }
static int SettingsHeight() { return SDpi(SETTINGS_HEIGHT); }
static int SettingsTitlebarH() { return SDpi(SETTINGS_TITLEBAR_H); }
static int SidebarW() { return SDpi(SIDEBAR_W); }
static int ContentPadding() { return SDpi(CONTENT_PADDING); }
static int RowHeight() { return SDpi(ROW_HEIGHT); }
static int ToggleW() { return SDpi(TOGGLE_W); }
static int ToggleH() { return SDpi(TOGGLE_H); }
static int ToggleThumbR() { return SDpi(TOGGLE_THUMB_R); }
static int SidebarItemH() { return SDpi(SIDEBAR_ITEM_H); }
static int CategoryHeaderH() { return SDpi(CATEGORY_HEADER_H); }

// 获取设置行控件的 Y 坐标
static int GetRowY(int rowIndex) {
  int y = SettingsTitlebarH() + CategoryHeaderH() + rowIndex * RowHeight();
  // data 标签页：导出/导入行（i >= 7）上方有"数据备份"分组标题
  if (g_currentSettingsTab == 2 && rowIndex >= 7)
    y += SDpi(BACKUP_HEADER_H);
  // 关于页：鸣谢行（第4行）有"标题+描述+名单"三行内容，
  // 其下的行下移，避免名单文字与分隔线/下行标题重叠
  if (g_currentSettingsTab == 3 && rowIndex >= 5)
    y += SDpi(28);
  return y;
}

// 获取控件右对齐 X 坐标
static int GetControlX(int controlWidth) {
  return SettingsWidth() - ContentPadding() - controlWidth;
}

static bool IsOverDataDirPath(POINT pt) {
  if (g_currentSettingsTab != 2)
    return false;

  int contentLeft = SidebarW() + ContentPadding();
  int contentRight = SettingsWidth() - ContentPadding();
  int row2Y = GetRowY(4);
  RECT rcPath = {contentLeft, row2Y + SDpi(30), contentRight - SDpi(70),
                 row2Y + SDpi(50)};
  return PtInRect(&rcPath, pt) != FALSE;
}

static bool IsOverAboutLink(POINT pt) {
  if (g_currentSettingsTab != 3)
    return false;

  int contentLeft = SidebarW() + ContentPadding();
  int row1Y = GetRowY(1);

  HDC hdc = GetDC(g_hwndSettingsDlg);
  if (!hdc)
    return false;
  HFONT oldFont = (HFONT)SelectObject(hdc, g_hDescFont);

  // 第1行协议链接区域：row1Y + 32 ~ row1Y + 50
  if (pt.y >= row1Y + SDpi(32) && pt.y <= row1Y + SDpi(50)) {
    const wchar_t *eulaText = T(STR_EULA);
    SIZE eulaSize = {0, 0};
    GetTextExtentPoint32W(hdc, eulaText, (int)wcslen(eulaText), &eulaSize);
    const wchar_t *privacyText = T(STR_PRIVACY_POLICY);
    SIZE privacySize = {0, 0};
    GetTextExtentPoint32W(hdc, privacyText, (int)wcslen(privacyText),
                          &privacySize);

    int eulaRight = contentLeft + eulaSize.cx;
    int privacyLeft = eulaRight + SDpi(24);
    int privacyRight = privacyLeft + privacySize.cx;

    SelectObject(hdc, oldFont);
    ReleaseDC(g_hwndSettingsDlg, hdc);

    if ((pt.x >= contentLeft && pt.x <= eulaRight) ||
        (pt.x >= privacyLeft && pt.x <= privacyRight))
      return true;
    return false;
  }

  // GitHub 仓库链接区域：第2行描述位置（row2Y + 32 ~ row2Y + 50）
  {
    int row2Y = GetRowY(1) + RowHeight(); // GetRowY(2)
    if (pt.y >= row2Y + SDpi(32) && pt.y <= row2Y + SDpi(50)) {
      const wchar_t *repoUrl = L"https://github.com/whutBing/smart-clip";
      SIZE urlSize = {0, 0};
      GetTextExtentPoint32W(hdc, repoUrl, (int)wcslen(repoUrl), &urlSize);

      int urlRight = contentLeft + urlSize.cx;

      SelectObject(hdc, oldFont);
      ReleaseDC(g_hwndSettingsDlg, hdc);

      return (pt.x >= contentLeft && pt.x <= urlRight);
    }
  }

  SelectObject(hdc, oldFont);
  ReleaseDC(g_hwndSettingsDlg, hdc);
  return false;
}

// 关于页机器码行（第3行）整行可点击区域，用于显示手型光标
static bool IsOverMachineInfoRow(POINT pt) {
  if (g_currentSettingsTab != 3)
    return false;
  int rowY = GetRowY(3);
  return (pt.y >= rowY && pt.y <= rowY + RowHeight());
}

static std::wstring BuildDataSizeText() {
  ULONGLONG usedBytes = GetDataDirSize();

  std::wstring filePath = GetDataFilePath();
  wchar_t volumePath[MAX_PATH] = {};
  bool hasVolumePath = GetVolumePathNameW(filePath.c_str(), volumePath,
                                          _countof(volumePath)) != FALSE;

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
  if (vk != 0 && GetKeyNameTextW(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16,
                                 keyName, _countof(keyName))) {
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

  RECT rcText = {10, topPadding, std::max(11, (int)rcClient.right - 10),
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

  RECT rcText = {0, topPadding, std::max(1, (int)rcClient.right),
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
    else if (hwnd == g_hwndHistoryLimitEdit ||
             hwnd == g_hwndScrollbarTimeoutEdit ||
             hwnd == g_hwndTextSizeLimitEdit)
      SyncSettingsNumberEditTextRect(hwnd);
    return result;
  }
  case WM_KEYDOWN:
    // 数字编辑框（ES_MULTILINE）禁止回车换行
    if ((hwnd == g_hwndHistoryLimitEdit || hwnd == g_hwndScrollbarTimeoutEdit ||
         hwnd == g_hwndTextSizeLimitEdit) &&
        wParam == VK_RETURN) {
      return 0;
    }
    break;
  case WM_CHAR:
    // 数字编辑框（ES_MULTILINE）禁止回车字符
    if ((hwnd == g_hwndHistoryLimitEdit || hwnd == g_hwndScrollbarTimeoutEdit ||
         hwnd == g_hwndTextSizeLimitEdit) &&
        (wParam == VK_RETURN || wParam == L'\n')) {
      return 0;
    }
    break;
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

      ReleaseDC(hParent, hdc);
    }
    return result;
  }
  }
  return CallWindowProcW(origProc, hwnd, uMsg, wParam, lParam);
}

// ==================== 快捷键 ====================

static void UpdateHotkeyConflictState() {
  g_hotkeyConflict = false;
  g_searchHotkeyConflict = false;
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
      // 检查是否为常用系统快捷键（Ctrl+C/V/X/Z/A/S 等）
      static const struct { UINT mod; UINT vk; const wchar_t *desc; } kSysShortcuts[] = {
        {MOD_CONTROL, 'C', L"复制"}, {MOD_CONTROL, 'V', L"粘贴"},
        {MOD_CONTROL, 'X', L"剪切"}, {MOD_CONTROL, 'Z', L"撤销"},
        {MOD_CONTROL, 'Y', L"重做"}, {MOD_CONTROL, 'A', L"全选"},
        {MOD_CONTROL, 'S', L"保存"}, {MOD_CONTROL, 'P', L"打印"},
        {MOD_CONTROL, 'F', L"查找"}, {MOD_CONTROL, 'N', L"新建"},
        {MOD_CONTROL, 'O', L"打开"}, {MOD_CONTROL, 'W', L"关闭"},
        {MOD_CONTROL, 'T', L"新建标签"}, {MOD_CONTROL, 'R', L"刷新"},
      };
      bool isSysShortcut = false;
      const wchar_t *sysDesc = NULL;
      if (mod == MOD_CONTROL) {
        for (const auto &s : kSysShortcuts) {
          if (s.vk == vk) { isSysShortcut = true; sysDesc = s.desc; break; }
        }
      }
      if (isSysShortcut) {
        wchar_t msg[256];
        wsprintfW(msg,
            L"该快捷键（Ctrl+%c）是系统「%s」快捷键，\n"
            L"设置后将导致系统「%s」功能失效。\n\n"
            L"确定要继续设置吗？", vk, sysDesc, sysDesc);
        if (MessageBoxW(g_hwndSettingsDlg, msg, L"快捷键冲突警告",
                        MB_YESNO | MB_ICONWARNING) != IDYES) {
          // 用户取消，恢复原设置
          g_hotkeyModifiers = g_savedHotkeyMod;
          g_hotkeyVirtualKey = g_savedHotkeyVk;
          g_isHotkeyEnabled = g_savedHotkeyEnabled;
          std::wstring oldText = FormatHotkeyText(g_savedHotkeyMod,
                                                   g_savedHotkeyVk, L"?");
          SetWindowTextW(hwnd, oldText.c_str());
          if (g_isHotkeyEnabled)
            RegisterHotkey(g_hwndMain);
          g_isRecordingHotkey = false;
          InvalidateRect(hwnd, NULL, TRUE);
          return 0;
        }
      }
      // 先注销旧快捷键，再保存和注册新快捷键
      UnregisterHotkey(g_hwndMain);
      SaveHotkeySettings();
      bool regOk = RegisterHotkey(g_hwndMain);
      if (!regOk) {
        if (g_isNotificationEnabled)
          ShowTrayBalloon(g_hwndMain, T(STR_TRAY_HOTKEY_FAILED),
                          L"该快捷键可能已被其他程序占用");
      } else {
        if (g_isNotificationEnabled)
          ShowTrayBalloon(g_hwndMain, T(STR_TRAY_SETTINGS_UPDATED),
                          T(STR_TRAY_HOTKEY_SAVED));
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
  DbSetSettingInt("settings_last_tab", g_currentSettingsTab);

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
  if (g_hwndToggleTaskbar)
    ShowWindow(g_hwndToggleTaskbar, showGen);
  if (g_hwndToggleStartup)
    ShowWindow(g_hwndToggleStartup, showGen);
  if (g_hwndToggleHoverSelect)
    ShowWindow(g_hwndToggleHoverSelect, showGen);
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
  if (g_hwndFavoriteHotkeyCombo)
    ShowWindow(g_hwndFavoriteHotkeyCombo, showHk);
  if (g_hwndSetDataDirBtn)
    ShowWindow(g_hwndSetDataDirBtn, showDt);
  if (g_hwndClearNonFavBtn)
    ShowWindow(g_hwndClearNonFavBtn, showDt);
  if (g_hwndCleanInvalidImagesBtn)
    ShowWindow(g_hwndCleanInvalidImagesBtn, showDt);
  if (g_hwndExportDataBtn)
    ShowWindow(g_hwndExportDataBtn, showDt);
  if (g_hwndImportDataBtn)
    ShowWindow(g_hwndImportDataBtn, showDt);
  if (g_hwndTextSizeLimitEdit)
    ShowWindow(g_hwndTextSizeLimitEdit, showDt);

  // 关于分类控件（tab == 3）— 无动态控件

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
    {L"\uE8B7", STR_SETTINGS_DATA},
    {L"\uE946", STR_SETTINGS_ABOUT},
};
#define SIDEBAR_COUNT 4

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
    {STR_ROW_TASKBAR, STR_ROW_TASKBAR_DESC},
    {STR_ROW_STARTUP, STR_ROW_STARTUP_DESC},
    {STR_ROW_HOVER_SELECT, STR_ROW_HOVER_SELECT_DESC},
};
static const SettingRowInfo g_hotkeyRows[] = {
    {STR_ROW_HOTKEY_TOGGLE, STR_ROW_HOTKEY_TOGGLE_DESC},
    {STR_ROW_HOTKEY_SEARCH, STR_ROW_HOTKEY_SEARCH_DESC},
    {STR_ROW_QUICK_PASTE_MOD, STR_ROW_QUICK_PASTE_MOD_DESC},
    {STR_ROW_FAVORITE_HOTKEY, STR_ROW_FAVORITE_HOTKEY_DESC},
};
static const SettingRowInfo g_dataRows[] = {
    {STR_ROW_DATA_SIZE, STR_COUNT},
    {STR_ROW_PASTE_COUNT, STR_COUNT},
    {STR_ROW_HISTORY_LIMIT, STR_ROW_HISTORY_LIMIT_DESC},
    {STR_ROW_TEXT_SIZE_LIMIT, STR_ROW_TEXT_SIZE_LIMIT_DESC},
    {STR_ROW_SET_DATA_DIR, STR_COUNT},
    {STR_ROW_CLEAR_NON_FAV, STR_ROW_CLEAR_NON_FAV_DESC},
    {STR_ROW_CLEAN_INVALID_IMAGES, STR_ROW_CLEAN_INVALID_IMAGES_DESC},
    {STR_ROW_EXPORT_DATA, STR_ROW_EXPORT_DATA_DESC},
    {STR_ROW_IMPORT_DATA, STR_ROW_IMPORT_DATA_DESC},
};
static const SettingRowInfo g_aboutRows[] = {
    {STR_ROW_VERSION, STR_ROW_VERSION_DESC},
    {STR_ROW_AGREEMENT, STR_COUNT},
    {STR_GITHUB_REPO, STR_COUNT},
    {STR_ROW_MACHINE_INFO, STR_ROW_MACHINE_INFO_DESC},
    {STR_ROW_CREDITS, STR_ROW_CREDITS_DESC},
};

// 致谢名单（扩展时只需在此数组添加即可）
static const wchar_t *g_creditsNames[] = {
    L"\u5317\u4e00", L"Sulla vetta la Saovia", L"Lion",
    L"Liu Hao",      L"\u9648\u968f\u6613",
};
static const int g_creditsCount = _countof(g_creditsNames);

// 构建致谢文本：中文用"、"分隔，英文用", "分隔
static std::wstring BuildCreditsText() {
  std::wstring result;
  const wchar_t *sep = (g_appLanguage == LANG_ZH_CN) ? L"\u3001" : L", ";
  for (int i = 0; i < g_creditsCount; ++i) {
    if (i > 0)
      result += sep;
    result += g_creditsNames[i];
  }
  return result;
}

// 分类标题
struct CategoryHeader {
  StringId titleId;
  StringId descId;
  const SettingRowInfo *rows;
  int rowCount;
};
static const CategoryHeader g_categories[] = {
    {STR_SETTINGS_GENERAL, STR_SETTINGS_GENERAL_DESC, g_generalRows, 10},
    {STR_SETTINGS_HOTKEY, STR_SETTINGS_HOTKEY_DESC, g_hotkeyRows, 4},
    {STR_SETTINGS_DATA, STR_COUNT, g_dataRows, 9},
    {STR_SETTINGS_ABOUT, STR_SETTINGS_ABOUT_DESC, g_aboutRows, 5},
};

// ==================== 绘制辅助 ====================

static void DrawToggleSwitch(HDC hdc, RECT rc, bool isOn) {
  Gdiplus::Graphics g(hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

  COLORREF pillColor = isOn ? COLOR_ACCENT : GetToggleOffColor();
  Gdiplus::GraphicsPath pillPath;
  int toggleW = ToggleW();
  int toggleH = ToggleH();
  int radius = toggleH / 2;
  CreateRoundRectPath(&pillPath, rc.left, rc.top, toggleW, toggleH, radius);
  Gdiplus::SolidBrush pillBrush(Gdiplus::Color(
      255, GetRValue(pillColor), GetGValue(pillColor), GetBValue(pillColor)));
  g.FillPath(&pillBrush, &pillPath);

  int thumbX = isOn ? (rc.left + toggleW - radius) : (rc.left + radius);
  int thumbY = rc.top + radius;
  int thumbR = ToggleThumbR();
  Gdiplus::SolidBrush thumbBrush(Gdiplus::Color(255, 255, 255, 255));
  g.FillEllipse(&thumbBrush, thumbX - thumbR, thumbY - thumbR, thumbR * 2,
                thumbR * 2);
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

void ApplyTaskbarVisibility(HWND hwnd) {
  if (!hwnd)
    return;
  LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if (g_isTaskbarVisible) {
    exStyle |= WS_EX_APPWINDOW;
    exStyle &= ~WS_EX_TOOLWINDOW;
  } else {
    exStyle |= WS_EX_TOOLWINDOW;
    exStyle &= ~WS_EX_APPWINDOW;
  }
  SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
  SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                   SWP_NOOWNERZORDER);
}

// ==================== 开机启动 ====================

// 检测当前是否运行在 MSIX/Store 容器中
static bool IsRunningInMSIX() {
  static int cached = -1;
  if (cached != -1)
    return cached == 1;
  // MSIX 应用其可执行文件路径位于 WindowsApps 目录下
  wchar_t exePath[MAX_PATH] = {};
  GetModuleFileNameW(NULL, exePath, MAX_PATH);
  std::wstring path(exePath);
  // 路径中包含 WindowsApps 即视为 MSIX 安装
  if (path.find(L"WindowsApps") != std::wstring::npos) {
    cached = 1;
    return true;
  }
  cached = 0;
  return false;
}

// 通过注册表设置桌面版开机启动
static bool SetDesktopStartup(bool enable) {
  HKEY hKey = NULL;
  LONG result = RegOpenKeyExW(
      HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
      0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey);
  if (result != ERROR_SUCCESS) {
    // 尝试以读写权限打开
    result = RegCreateKeyExW(
        HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, NULL, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, NULL, &hKey, NULL);
    if (result != ERROR_SUCCESS)
      return false;
  }

  bool ok = false;
  if (enable) {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    // 添加 -minimized 参数：开机自启动时主窗体隐藏，只在任务栏显示图标，
    // 用户点击任务栏图标即可显示主窗体。
    std::wstring value = std::wstring(L"\"") + exePath + L"\" -minimized";
    result = RegSetValueExW(hKey, L"SmartClip", 0, REG_SZ,
                            (const BYTE *)value.c_str(),
                            (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    ok = (result == ERROR_SUCCESS);
  } else {
    result = RegDeleteValueW(hKey, L"SmartClip");
    ok = (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
  }
  RegCloseKey(hKey);
  return ok;
}

// 查询桌面版开机启动是否已启用
static bool QueryDesktopStartup() {
  HKEY hKey = NULL;
  LONG result = RegOpenKeyExW(
      HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
      0, KEY_QUERY_VALUE, &hKey);
  if (result != ERROR_SUCCESS)
    return false;
  wchar_t buf[MAX_PATH] = {};
  DWORD bufSize = sizeof(buf);
  result =
      RegQueryValueExW(hKey, L"SmartClip", NULL, NULL, (LPBYTE)buf, &bufSize);
  RegCloseKey(hKey);
  return result == ERROR_SUCCESS;
}

// MSIX 启动任务 TaskId（与 AppxManifest.xml 中声明一致）
static const wchar_t *kMSIXStartupTaskId = L"SmartClipFreeStartup";
// 查询缓存（避免每次打开设置都调用 PowerShell，30 秒 TTL）
static bool g_msixStartupCacheValid = false;
static bool g_msixStartupCacheValue = false;
static DWORD g_msixStartupCacheTick = 0;

// 通过 PowerShell 调用 WinRT StartupTask API（MinGW 无 WinRT 头文件，用 PS
// 桥接） action: L"query" / L"enable" / L"disable" outState: query 返回
// StartupTaskState 枚举（0=Disabled,1=DisabledByUser,
//           2=Enabled,3=DisabledByPolicy,4=EnabledByPolicy）；enable
//           返回请求后的状态
// 返回: 脚本执行成功且未报错返回 true
static bool RunStartupTaskViaPowerShell(const std::wstring &action,
                                        const std::wstring &taskId,
                                        int &outState) {
  outState = -1;

  // 生成临时脚本文件路径
  wchar_t tempDir[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, tempDir);
  std::wstring scriptPath = std::wstring(tempDir) + L"sc_startup_task.ps1";

  // PowerShell 脚本：加载 WinRT 类型，调用
  // StartupTask.GetAsync/RequestEnableAsync/Disable 使用
  // System.Runtime.WindowsRuntime 的 AsTask 扩展等待异步操作完成
  std::wstring script =
      L"param([string]$action,[string]$taskId)\n"
      L"$ErrorActionPreference='Stop'\n"
      L"try{\n"
      L"  [Windows.ApplicationModel.StartupTask,Windows.ApplicationModel,"
      L"ContentType=WindowsRuntime]|Out-Null\n"
      L"  Add-Type -AssemblyName System.Runtime.WindowsRuntime\n"
      L"  $m=[System.WindowsRuntimeSystemExtensions].GetMethods()|Where-Object{"
      L"$_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and "
      L"$_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'}\n"
      L"  $asTask=$m[0]\n"
      L"  function AwaitOp($op,$rt){$t=$asTask.MakeGenericMethod($rt).Invoke("
      L"$null,@($op));$t.Wait(10000)|Out-Null;$t.Result}\n"
      L"  $task=AwaitOp ([Windows.ApplicationModel.StartupTask]::GetAsync("
      L"$taskId)) ([Windows.ApplicationModel.StartupTask])\n"
      L"  if($null -eq $task){Write-Output 'ERROR:TASK_NOT_FOUND';exit 1}\n"
      L"  if($action -eq 'query'){Write-Output ('STATE='+[int]$task.State)}\n"
      L"  elseif($action -eq 'enable'){"
      L"$r=AwaitOp $task.RequestEnableAsync() "
      L"([Windows.ApplicationModel.StartupTaskState]);"
      L"Write-Output ('RESULT='+[int]$r)}\n"
      L"  elseif($action -eq 'disable'){$task.Disable();"
      L"Write-Output 'RESULT=0'}\n"
      L"}catch{Write-Output ('ERROR:'+$_.Exception.Message);exit 1}\n";

  // 写入脚本文件（UTF-16 LE BOM + 内容，PowerShell 默认能识别）
  HANDLE hFile = CreateFileW(scriptPath.c_str(), GENERIC_WRITE, 0, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
  if (hFile == INVALID_HANDLE_VALUE)
    return false;
  // 写入 UTF-16 LE BOM
  DWORD written = 0;
  BYTE bom[] = {0xFF, 0xFE};
  WriteFile(hFile, bom, 2, &written, NULL);
  // 写入脚本内容（宽字符）
  WriteFile(hFile, script.c_str(), (DWORD)(script.size() * sizeof(wchar_t)),
            &written, NULL);
  CloseHandle(hFile);

  // 构造 PowerShell 命令行
  std::wstring cmd =
      L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"";
  cmd += scriptPath;
  cmd += L"\" -action ";
  cmd += action;
  cmd += L" -taskId ";
  cmd += taskId;

  // 创建管道捕获输出
  SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
  HANDLE hReadPipe = NULL, hWritePipe = NULL;
  CreatePipe(&hReadPipe, &hWritePipe, &sa, 0);
  SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = hWritePipe;
  si.hStdError = hWritePipe;
  PROCESS_INFORMATION pi = {};
  std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
  cmdBuf.push_back(0);

  BOOL ok = CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, TRUE,
                           CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  CloseHandle(hWritePipe);
  if (!ok) {
    CloseHandle(hReadPipe);
    DeleteFileW(scriptPath.c_str());
    return false;
  }

  // 等待进程结束（PowerShell 启动+执行通常 2-4 秒，给 12 秒上限）
  if (WaitForSingleObject(pi.hProcess, 12000) != WAIT_OBJECT_0) {
    // 超时则强制终止，避免 ReadFile 永久阻塞
    TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(hReadPipe);
    DeleteFileW(scriptPath.c_str());
    return false;
  }

  // 读取输出（进程已退出，管道有数据可读，不会阻塞）
  std::string output;
  char buf[4096];
  DWORD bytesRead = 0;
  while (ReadFile(hReadPipe, buf, sizeof(buf), &bytesRead, NULL) &&
         bytesRead > 0) {
    output.append(buf, bytesRead);
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  CloseHandle(hReadPipe);
  DeleteFileW(scriptPath.c_str());

  // 解析输出（输出为 UTF-16，转宽字符）
  // PowerShell 输出可能是 UTF-16 或 ANSI，统一处理
  std::wstring wout;
  // 尝试 UTF-16 解码（跳过可能的 BOM）
  const BYTE *p = (const BYTE *)output.data();
  size_t len = output.size();
  if (len >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
    p += 2;
    len -= 2;
    wout.assign((const wchar_t *)p, len / 2);
  } else if (len >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
    // UTF-16 BE，转 LE
    p += 2;
    len -= 2;
    wout.resize(len / 2);
    for (size_t i = 0; i < len / 2; i++) {
      wout[i] = (wchar_t)(p[i * 2] << 8 | p[i * 2 + 1]);
    }
  } else {
    // ANSI/UTF-8，按 UTF-8 解码
    int wlen = MultiByteToWideChar(CP_UTF8, 0, output.c_str(),
                                   (int)output.size(), NULL, 0);
    if (wlen > 0) {
      wout.resize(wlen);
      MultiByteToWideChar(CP_UTF8, 0, output.c_str(), (int)output.size(),
                          &wout[0], wlen);
    }
  }

  // 移除换行和空白
  while (!wout.empty() &&
         (wout.back() == L'\r' || wout.back() == L'\n' || wout.back() == L' '))
    wout.pop_back();

  if (wout.find(L"ERROR:") == 0)
    return false;

  if (wout.find(L"STATE=") == 0) {
    outState = _wtoi(wout.c_str() + 6);
    return true;
  }
  if (wout.find(L"RESULT=") == 0) {
    outState = _wtoi(wout.c_str() + 7);
    return true;
  }
  return false;
}

// 通过 MSIX StartupTask API 设置开机启动
static bool SetMSIXStartup(bool enable) {
  // 失效查询缓存
  g_msixStartupCacheValid = false;
  int state = -1;
  bool ok = RunStartupTaskViaPowerShell(enable ? L"enable" : L"disable",
                                        kMSIXStartupTaskId, state);
  if (!ok) {
    // PS 桥接失败（如未安装 PowerShell 或任务未注册），回退到打开系统设置
    if (enable) {
      MessageBoxW(g_hwndSettingsDlg,
                  L"请在系统设置 - 应用 - 启动 中启用 "
                  L"SmartClip。\n即将打开启动设置页面。",
                  L"提示", MB_OK | MB_ICONINFORMATION);
      ShellExecuteW(NULL, L"open", L"ms-settings:startupapps", NULL, NULL,
                    SW_SHOWNORMAL);
    }
    return false;
  }
  return true;
}

// 查询 MSIX 启动任务状态（带 30 秒缓存，避免频繁调用 PowerShell）
static bool QueryMSIXStartup() {
  DWORD now = GetTickCount();
  if (g_msixStartupCacheValid && (now - g_msixStartupCacheTick) < 30000) {
    return g_msixStartupCacheValue;
  }
  int state = -1;
  if (!RunStartupTaskViaPowerShell(L"query", kMSIXStartupTaskId, state))
    return false;
  // StartupTaskState: 2=Enabled, 4=EnabledByPolicy
  bool enabled = (state == 2 || state == 4);
  g_msixStartupCacheValue = enabled;
  g_msixStartupCacheTick = now;
  g_msixStartupCacheValid = true;
  return enabled;
}

void ApplyStartupPreference(bool enable) {
  g_isStartupEnabled = enable;
  if (IsRunningInMSIX()) {
    SetMSIXStartup(enable);
  } else {
    SetDesktopStartup(enable);
  }
}

bool IsStartupEnabled() {
  if (IsRunningInMSIX()) {
    return QueryMSIXStartup();
  }
  return QueryDesktopStartup();
}

// ==================== 自定义下拉选择器 ====================

#define DROPDOWN_ITEM_H 34
#define DROPDOWN_PADDING 6
#define DROPDOWN_RADIUS 8

static int DropdownScale(int /*ctlId*/, int value) { return SDpi(value); }

static int DropdownItemHFor(int ctlId) {
  return DropdownScale(ctlId, DROPDOWN_ITEM_H);
}

static int DropdownPaddingFor(int ctlId) {
  return DropdownScale(ctlId, DROPDOWN_PADDING);
}

static int DropdownRadiusFor(int ctlId) {
  return DropdownScale(ctlId, DROPDOWN_RADIUS);
}

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
    STR_THEME_STYLE_CLASSIC, STR_THEME_STYLE_GRAPHITE, STR_THEME_STYLE_WARM,
    STR_THEME_STYLE_HIGH_CONTRAST};
static const StringId g_languageItemIds[] = {
    STR_LANGUAGE_ZH_CN, STR_LANGUAGE_EN_US, STR_LANGUAGE_JA_JP,
    STR_LANGUAGE_KO_KR, STR_LANGUAGE_DE_DE, STR_LANGUAGE_AR_SA,
    STR_LANGUAGE_TR_TR};
static const StringId g_previewItemIds[] = {STR_PREVIEW_OFF, STR_PREVIEW_BLUR,
                                            STR_PREVIEW_SD, STR_PREVIEW_HD};
static const StringId g_quickPasteModItemIds[] = {
    STR_MOD_ALT,      STR_MOD_CTRL,       STR_MOD_SHIFT,
    STR_MOD_CTRL_ALT, STR_MOD_CTRL_SHIFT, STR_MOD_ALT_SHIFT};
static const UINT g_quickPasteModValues[] = {MOD_ALT,
                                             MOD_CONTROL,
                                             MOD_SHIFT,
                                             MOD_CONTROL | MOD_ALT,
                                             MOD_CONTROL | MOD_SHIFT,
                                             MOD_ALT | MOD_SHIFT};
#define QUICK_PASTE_MOD_COUNT 6

static const StringId g_favoriteHotkeyModItemIds[] = {
    STR_MOD_ALT,      STR_MOD_CTRL,       STR_MOD_SHIFT,
    STR_MOD_CTRL_ALT, STR_MOD_CTRL_SHIFT, STR_MOD_ALT_SHIFT};
static const UINT g_favoriteHotkeyModValues[] = {MOD_ALT,
                                                 MOD_CONTROL,
                                                 MOD_SHIFT,
                                                 MOD_CONTROL | MOD_ALT,
                                                 MOD_CONTROL | MOD_SHIFT,
                                                 MOD_ALT | MOD_SHIFT};
#define FAVORITE_HOTKEY_MOD_COUNT 6

static const wchar_t *g_quickPasteModItems[QUICK_PASTE_MOD_COUNT];
static const wchar_t *g_favoriteHotkeyModItems[FAVORITE_HOTKEY_MOD_COUNT];

static void UpdateModifierDropdownItems() {
  for (int i = 0; i < QUICK_PASTE_MOD_COUNT; ++i) {
    g_quickPasteModItems[i] = T(g_quickPasteModItemIds[i]);
    g_favoriteHotkeyModItems[i] = T(g_favoriteHotkeyModItemIds[i]);
  }
}

// 判断下拉框某项是否被禁用（灰色不可选）。
// 快捷粘贴与收藏快捷键都用数字键，修饰键相同时会冲突，
// 因此在对方下拉框中把对应修饰键显示为灰色禁用。
static bool IsModifierDropdownItemDisabled(int ctlId, int index) {
  if (index < 0 || index >= QUICK_PASTE_MOD_COUNT)
    return false;
  if (ctlId == IDC_QUICK_PASTE_COMBO)
    return g_quickPasteModValues[index] == g_favoriteHotkeyModifiers;
  if (ctlId == IDC_FAVORITE_HOTKEY_COMBO)
    return g_favoriteHotkeyModValues[index] == g_quickPasteModifiers;
  return false;
}

// 获取下拉按钮当前显示文字
static const wchar_t *GetDropdownText(int ctlId) {
  if (ctlId == IDC_THEME_COMBO)
    return T(g_themeItemIds[(int)g_themeMode]);
  if (ctlId == IDC_THEME_STYLE_COMBO)
    return T(STR_THEME_STYLE_HIGH_CONTRAST);
  if (ctlId == IDC_LANGUAGE_COMBO) {
    const auto &langs = GetAvailableLanguages();
    for (const auto &lang : langs) {
      if (lang.lang == g_appLanguage)
        return lang.name.c_str();
    }
    return T(STR_LANGUAGE_EN_US);
  }
  if (ctlId == IDC_IMAGE_PREVIEW_COMBO)
    return T(g_previewItemIds[(int)g_imagePreviewQuality]);
  if (ctlId == IDC_QUICK_PASTE_COMBO) {
    for (int i = 0; i < QUICK_PASTE_MOD_COUNT; ++i) {
      if (g_quickPasteModValues[i] == g_quickPasteModifiers)
        return g_quickPasteModItems[i];
    }
    return g_quickPasteModItems[0];
  }
  if (ctlId == IDC_FAVORITE_HOTKEY_COMBO) {
    for (int i = 0; i < FAVORITE_HOTKEY_MOD_COUNT; ++i) {
      if (g_favoriteHotkeyModValues[i] == g_favoriteHotkeyModifiers)
        return g_favoriteHotkeyModItems[i];
    }
    return g_favoriteHotkeyModItems[0];
  }
  return L"";
}

static int GetDropdownSelectedIndex(int ctlId) {
  if (ctlId == IDC_THEME_COMBO)
    return (int)g_themeMode;
  if (ctlId == IDC_THEME_STYLE_COMBO)
    return 3;
  if (ctlId == IDC_LANGUAGE_COMBO) {
    // 通过语言枚举值查找在列表中的索引
    const auto &langs = GetAvailableLanguages();
    for (int i = 0; i < (int)langs.size(); ++i) {
      if (langs[i].lang == g_appLanguage)
        return i;
    }
    return 0;
  }
  if (ctlId == IDC_IMAGE_PREVIEW_COMBO)
    return (int)g_imagePreviewQuality;
  if (ctlId == IDC_QUICK_PASTE_COMBO) {
    for (int i = 0; i < QUICK_PASTE_MOD_COUNT; ++i) {
      if (g_quickPasteModValues[i] == g_quickPasteModifiers)
        return i;
    }
    return 0;
  }
  if (ctlId == IDC_FAVORITE_HOTKEY_COMBO) {
    for (int i = 0; i < FAVORITE_HOTKEY_MOD_COUNT; ++i) {
      if (g_favoriteHotkeyModValues[i] == g_favoriteHotkeyModifiers)
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
  int radius = DropdownRadiusFor(ctlId);
  int textPad = DropdownScale(ctlId, 10);
  int arrowReserved = DropdownScale(ctlId, 30);
  int arrowLeftPad = DropdownScale(ctlId, 24);
  int arrowW = DropdownScale(ctlId, 20);

  // 圆角背景
  COLORREF bg = GetSettingsEditBg();
  Gdiplus::GraphicsPath path;
  CreateRoundRectPath(&path, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                      radius);
  Gdiplus::SolidBrush bgBrush(
      Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
  g.FillPath(&bgBrush, &path);

  // 文字
  const wchar_t *text = GetDropdownText(ctlId);
  COLORREF tc = GetSettingsTextColor();
  Gdiplus::SolidBrush textBrush(
      Gdiplus::Color(255, GetRValue(tc), GetGValue(tc), GetBValue(tc)));
  Gdiplus::Font font(L"Microsoft YaHei", (Gdiplus::REAL)SDpi(14),
                     Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::StringFormat sf;
  sf.SetAlignment(Gdiplus::StringAlignmentCenter);
  sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  Gdiplus::RectF textRect((Gdiplus::REAL)textPad, 0,
                          (float)(rc.right - rc.left - arrowReserved),
                          (float)(rc.bottom - rc.top));
  g.DrawString(text, -1, &font, textRect, &sf, &textBrush);

  // 下拉箭头
  Gdiplus::Font iconFont(L"Segoe MDL2 Assets", (Gdiplus::REAL)SDpi(11),
                         Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::StringFormat iconSf;
  iconSf.SetAlignment(Gdiplus::StringAlignmentCenter);
  iconSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  COLORREF ac = RGB(150, 150, 150);
  Gdiplus::SolidBrush arrowBrush(
      Gdiplus::Color(255, GetRValue(ac), GetGValue(ac), GetBValue(ac)));
  Gdiplus::RectF arrowRect((float)(rc.right - rc.left - arrowLeftPad), 0,
                           (Gdiplus::REAL)arrowW, (float)(rc.bottom - rc.top));
  g.DrawString(L"\uE70D", -1, &iconFont, arrowRect, &iconSf, &arrowBrush);
}

// 弹出下拉窗口过程
LRESULT CALLBACK DropdownPopupProc(HWND hwnd, UINT msg, WPARAM wParam,
                                   LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;

  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC screenDc = BeginPaint(hwnd, &ps);
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

    // 内存DC双缓冲，消除悬浮闪烁
    HDC hdc = CreateCompatibleDC(screenDc);
    HBITMAP memBmp =
        CreateCompatibleBitmap(screenDc, rcClient.right, rcClient.bottom);
    HBITMAP oldBmp = (HBITMAP)SelectObject(hdc, memBmp);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    int ctlId = g_activeDropdown.ctlId;
    int itemH = DropdownItemHFor(ctlId);
    int padding = DropdownPaddingFor(ctlId);
    int radius = DropdownRadiusFor(ctlId);

    // 背景
    COLORREF bg = GetSettingsEditBg();
    Gdiplus::GraphicsPath bgPath;
    CreateRoundRectPath(&bgPath, 0, 0, rcClient.right, rcClient.bottom, radius);
    Gdiplus::SolidBrush bgBrush(
        Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
    g.FillPath(&bgBrush, &bgPath);

    // 边框
    COLORREF bc = GetSeparatorColor();
    Gdiplus::Pen borderPen(
        Gdiplus::Color(255, GetRValue(bc), GetGValue(bc), GetBValue(bc)), 1.0f);
    g.DrawPath(&borderPen, &bgPath);

    Gdiplus::Font font(L"Microsoft YaHei", (Gdiplus::REAL)SDpi(14),
                       Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentNear);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    for (int i = 0; i < g_activeDropdown.itemCount; i++) {
      int y = padding + i * itemH;
      bool disabled = IsModifierDropdownItemDisabled(g_activeDropdown.ctlId, i);
      Gdiplus::RectF itemRect((float)padding, (float)y,
                              (float)(rcClient.right - padding * 2),
                              (float)itemH);

      // 悬浮高亮（禁用项不高亮）
      if (!disabled && i == g_dropdownHoverIndex) {
        COLORREF hc = GetThemeDropdownHoverColor();
        Gdiplus::GraphicsPath hoverPath;
        CreateRoundRectPath(&hoverPath, (int)itemRect.X, (int)itemRect.Y,
                            (int)itemRect.Width, (int)itemRect.Height,
                            DropdownScale(ctlId, 6));
        Gdiplus::SolidBrush hoverBrush(
            Gdiplus::Color(255, GetRValue(hc), GetGValue(hc), GetBValue(hc)));
        g.FillPath(&hoverBrush, &hoverPath);
      }

      // 选中标记
      bool selected = (i == g_activeDropdown.selectedIndex);
      if (selected) {
        Gdiplus::SolidBrush checkBrush(Gdiplus::Color(255, 0, 120, 215));
        g.FillEllipse(&checkBrush, (float)(padding + DropdownScale(ctlId, 6)),
                      (float)(y + itemH / 2 - DropdownScale(ctlId, 4)),
                      (Gdiplus::REAL)DropdownScale(ctlId, 8),
                      (Gdiplus::REAL)DropdownScale(ctlId, 8));
      }

      // 文字（禁用项灰色显示）
      COLORREF tc = disabled ? GetDescTextColor() : GetSettingsTextColor();
      Gdiplus::SolidBrush textBrush(Gdiplus::Color(
          disabled ? 120 : 255, GetRValue(tc), GetGValue(tc), GetBValue(tc)));
      Gdiplus::RectF textRect(
          (float)(padding + DropdownScale(ctlId, 22)), (float)y,
          (float)(rcClient.right - padding * 2 - DropdownScale(ctlId, 22)),
          (float)itemH);
      g.DrawString(g_activeDropdown.items[i], -1, &font, textRect, &sf,
                   &textBrush);
    }

    BitBlt(screenDc, 0, 0, rcClient.right, rcClient.bottom, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(hdc);

    EndPaint(hwnd, &ps);
    return 0;
  }

  case WM_MOUSEMOVE: {
    int y = GET_Y_LPARAM(lParam);
    int idx = (y - DropdownPaddingFor(g_activeDropdown.ctlId)) /
              DropdownItemHFor(g_activeDropdown.ctlId);
    if (idx < 0 || idx >= g_activeDropdown.itemCount)
      idx = -1;
    // 禁用项不响应悬浮
    if (IsModifierDropdownItemDisabled(g_activeDropdown.ctlId, idx))
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
    int idx = (y - DropdownPaddingFor(g_activeDropdown.ctlId)) /
              DropdownItemHFor(g_activeDropdown.ctlId);
    if (idx >= 0 && idx < g_activeDropdown.itemCount &&
        !IsModifierDropdownItemDisabled(g_activeDropdown.ctlId, idx)) {
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
    // 动态获取可用语言列表（内置 + 外部文件）
    const auto &langs = GetAvailableLanguages();
    static std::vector<std::wstring> s_languageItems;
    static std::vector<const wchar_t *> s_languageItemPtrs;
    s_languageItems.clear();
    s_languageItemPtrs.clear();
    // 先 reserve 避免 push_back 扩容导致 c_str() 指针失效
    s_languageItems.reserve(langs.size());
    s_languageItemPtrs.reserve(langs.size());
    for (const auto &lang : langs) {
      s_languageItems.push_back(lang.name);
      s_languageItemPtrs.push_back(s_languageItems.back().c_str());
    }
    g_activeDropdown.items = s_languageItemPtrs.data();
    g_activeDropdown.itemCount = (int)s_languageItemPtrs.size();
  } else if (ctlId == IDC_IMAGE_PREVIEW_COMBO) {
    static const wchar_t *s_previewItems[4];
    for (int i = 0; i < 4; ++i)
      s_previewItems[i] = T(g_previewItemIds[i]);
    g_activeDropdown.items = s_previewItems;
    g_activeDropdown.itemCount = 4;
  } else if (ctlId == IDC_QUICK_PASTE_COMBO) {
    g_activeDropdown.items = g_quickPasteModItems;
    g_activeDropdown.itemCount = QUICK_PASTE_MOD_COUNT;
  } else if (ctlId == IDC_FAVORITE_HOTKEY_COMBO) {
    g_activeDropdown.items = g_favoriteHotkeyModItems;
    g_activeDropdown.itemCount = FAVORITE_HOTKEY_MOD_COUNT;
  }
  g_activeDropdown.selectedIndex = GetDropdownSelectedIndex(ctlId);
  g_dropdownHoverIndex = -1;

  RegisterDropdownClass();

  RECT rcBtn;
  GetWindowRect(hwndBtn, &rcBtn);
  int popupW = rcBtn.right - rcBtn.left;
  if (popupW < DropdownScale(ctlId, 130))
    popupW = DropdownScale(ctlId, 130);
  int popupH = DropdownPaddingFor(ctlId) * 2 +
               g_activeDropdown.itemCount * DropdownItemHFor(ctlId);

  g_hwndDropdownPopup = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"SmartClipDropdown", NULL, WS_POPUP,
      rcBtn.left, rcBtn.bottom + DropdownScale(ctlId, 2), popupW, popupH,
      g_hwndSettingsDlg, NULL, GetModuleHandleW(NULL), NULL);

  // 圆角区域
  HRGN hRgn = CreateRoundRectRgn(0, 0, popupW + 1, popupH + 1,
                                 DropdownRadiusFor(ctlId) * 2,
                                 DropdownRadiusFor(ctlId) * 2);
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
    g_hTitleFont = CreateFontW(SDpi(20), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hDescFont = CreateFontW(SDpi(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hSidebarFont = CreateFontW(
        SDpi(20), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hSidebarIconFont = CreateFontW(
        SDpi(22), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
    g_hHeaderFont = CreateFontW(
        SDpi(24), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hHeaderDescFont = CreateFontW(
        SDpi(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hCloseIconFont = CreateFontW(
        SDpi(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
    UpdateSettingsBrushes();
    return 0;
  }

  case WM_ERASEBKGND:
    // 双缓冲模式下不需要系统擦除背景，避免闪烁
    return 1;

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
      if (pt.y < SettingsTitlebarH()) {
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
    HDC screenDc = BeginPaint(hwnd, &ps);

    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

    // 内存DC双缓冲，消除侧边栏悬浮等闪烁
    HDC hdc = CreateCompatibleDC(screenDc);
    HBITMAP memBmp =
        CreateCompatibleBitmap(screenDc, rcClient.right, rcClient.bottom);
    HBITMAP oldBmp = (HBITMAP)SelectObject(hdc, memBmp);
    SetBkMode(hdc, TRANSPARENT);

    // 标题栏
    RECT rcTitlebar = {0, 0, rcClient.right, SettingsTitlebarH()};
    HBRUSH hTbBrush = CreateSolidBrush(GetTitlebarBgColor());
    FillRect(hdc, &rcTitlebar, hTbBrush);
    DeleteObject(hTbBrush);

    // 标题栏文字 "设置"
    HFONT hOld = (HFONT)SelectObject(hdc, g_hHeaderFont);
    SetTextColor(hdc, GetSettingsTextColor());
    RECT rcTitleText = {SDpi(16), 0, SDpi(220), SettingsTitlebarH()};
    DrawTextW(hdc, T(STR_SETTINGS_TITLE), -1, &rcTitleText,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 侧边栏背景
    RECT rcSidebar = {0, SettingsTitlebarH(), SidebarW(), rcClient.bottom};
    HBRUSH hSbBrush = CreateSolidBrush(GetSidebarBgColor());
    FillRect(hdc, &rcSidebar, hSbBrush);
    DeleteObject(hSbBrush);

    // 侧边栏项目
    for (int i = 0; i < SIDEBAR_COUNT; i++) {
      int itemY = SettingsTitlebarH() + i * SidebarItemH();
      RECT rcItem = {0, itemY, SidebarW(), itemY + SidebarItemH()};
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
        textCol = RGB(255, 255, 255);
      }
      SetTextColor(hdc, textCol);

      // 图标
      SelectObject(hdc, g_hSidebarIconFont);
      RECT rcIcon = {SDpi(20), itemY, SDpi(48), itemY + SidebarItemH()};
      DrawTextW(hdc, g_sidebarItems[i].icon, 1, &rcIcon,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      // 文字
      SelectObject(hdc, g_hSidebarFont);
      RECT rcLabel = {SDpi(54), itemY, SidebarW() - SDpi(8),
                      itemY + SidebarItemH()};
      DrawTextW(hdc, T(g_sidebarItems[i].labelId), -1, &rcLabel,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // 内容区背景
    RECT rcContent = {SidebarW(), SettingsTitlebarH(), rcClient.right,
                      rcClient.bottom};
    HBRUSH hCBrush = CreateSolidBrush(GetSettingsBgColor());
    FillRect(hdc, &rcContent, hCBrush);
    DeleteObject(hCBrush);

    // 分类标题
    const CategoryHeader &cat = g_categories[g_currentSettingsTab];
    int contentLeft = SidebarW() + ContentPadding();
    int contentRight = rcClient.right - ContentPadding();

    SelectObject(hdc, g_hHeaderFont);
    SetTextColor(hdc, GetSettingsTextColor());
    RECT rcCatTitle = {contentLeft, SettingsTitlebarH() + SDpi(10),
                       contentRight, SettingsTitlebarH() + SDpi(32)};
    DrawTextW(hdc, T(cat.titleId), -1, &rcCatTitle,
              DT_LEFT | DT_TOP | DT_SINGLELINE);

    SelectObject(hdc, g_hHeaderDescFont);
    SetTextColor(hdc, GetDescTextColor());
    RECT rcCatDesc = {contentLeft, SettingsTitlebarH() + SDpi(34), contentRight,
                      SettingsTitlebarH() + SDpi(50)};
    DrawTextW(hdc, cat.descId == STR_COUNT ? L"" : T(cat.descId), -1,
              &rcCatDesc, DT_LEFT | DT_TOP | DT_SINGLELINE);

    // 设置行
    for (int i = 0; i < cat.rowCount; i++) {
      int rowY = GetRowY(i);

      // data 标签页：导出行（i == 7）上方绘制"数据备份"分组标题与分割线
      if (g_currentSettingsTab == 2 && i == 7) {
        int headerY = rowY - SDpi(BACKUP_HEADER_H);
        // 分组标题（与行标题字体一致）
        SelectObject(hdc, g_hTitleFont);
        SetTextColor(hdc, GetSettingsTextColor());
        RECT rcGrpTitle = {contentLeft, headerY + SDpi(10), contentRight,
                           headerY + SDpi(32)};
        DrawTextW(hdc, T(STR_DATA_BACKUP_TITLE), -1, &rcGrpTitle,
                  DT_LEFT | DT_TOP | DT_SINGLELINE);
        // 分组副标题（与行描述字体一致）
        SelectObject(hdc, g_hDescFont);
        SetTextColor(hdc, GetDescTextColor());
        RECT rcGrpDesc = {contentLeft, headerY + SDpi(34), contentRight,
                          headerY + SDpi(50)};
        DrawTextW(hdc, T(STR_DATA_BACKUP_DESC), -1, &rcGrpDesc,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        // 分组标题下方分割线
        HPEN hGrpPen = CreatePen(PS_SOLID, 1, GetSeparatorColor());
        HPEN hOldGrpPen = (HPEN)SelectObject(hdc, hGrpPen);
        MoveToEx(hdc, contentLeft, headerY + SDpi(BACKUP_HEADER_H) - 1, NULL);
        LineTo(hdc, contentRight, headerY + SDpi(BACKUP_HEADER_H) - 1);
        SelectObject(hdc, hOldGrpPen);
        DeleteObject(hGrpPen);
      }

      // 标题
      SelectObject(hdc, g_hTitleFont);
      SetTextColor(hdc, GetSettingsTextColor());
      RECT rcRowTitle = {contentLeft, rowY + SDpi(12), contentRight - SDpi(190),
                         rowY + SDpi(30)};
      DrawTextW(hdc, T(cat.rows[i].titleId), -1, &rcRowTitle,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

      // 描述
      SelectObject(hdc, g_hDescFont);
      // 关于页机器码行（i == 3）：描述文字用蓝色（可点击链接样式）
      if (g_currentSettingsTab == 3 && i == 3)
        SetTextColor(hdc, COLOR_ACCENT);
      else
        SetTextColor(hdc, GetDescTextColor());
      RECT rcRowDesc = {contentLeft, rowY + SDpi(32), contentRight - SDpi(190),
                        rowY + SDpi(48)};
      DrawTextW(
          hdc, cat.rows[i].descId == STR_COUNT ? L"" : T(cat.rows[i].descId),
          -1, &rcRowDesc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

      // 分隔线
      bool skipSeparator = (g_currentSettingsTab == 0 && i == 2);
      if (i < cat.rowCount - 1 && !skipSeparator) {
        int sepY = rowY + RowHeight() - 1;
        // 关于页鸣谢行（i == 4）：内容有三行（标题+描述+名单），
        // 分隔线下移与下一行对齐，避免穿过名单文字
        if (g_currentSettingsTab == 3 && i == 4)
          sepY += SDpi(28);
        HPEN hPen = CreatePen(PS_SOLID, 1, GetSeparatorColor());
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, contentLeft, sepY, NULL);
        LineTo(hdc, contentRight, sepY);
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
      RECT rcSize = {contentRight - SDpi(240), row0Y + SDpi(12), contentRight,
                     row0Y + SDpi(30)};
      DrawTextW(hdc, g_dataSizeText.c_str(), -1, &rcSize,
                DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS);

      // 第1行右侧：蓝色粘贴次数
      extern int g_pasteCount;
      int row1Y = GetRowY(1);
      SelectObject(hdc, g_hTitleFont);
      SetTextColor(hdc, COLOR_ACCENT);
      wchar_t pasteBuf[32];
      _snwprintf_s(pasteBuf, 32, L"%d%s", g_pasteCount,
                   T(STR_PASTE_COUNT_SUFFIX));
      RECT rcPaste = {contentRight - SDpi(170), row1Y + SDpi(12), contentRight,
                      row1Y + SDpi(30)};
      DrawTextW(hdc, pasteBuf, -1, &rcPaste, DT_RIGHT | DT_SINGLELINE);

      // 第3行：文本大小上限（输入框已水平居中文本，无需单位前缀）
      {
        // 不再绘制 "KB" 单位前缀
      }

      // 第4行：数据目录路径作为描述文字（按钮左侧）
      int row2Y = GetRowY(4);
      SelectObject(hdc, g_hDescFont);
      std::wstring dataPath = GetDataFilePath();
      size_t lastSlash = dataPath.find_last_of(L"\\");
      if (lastSlash != std::wstring::npos)
        dataPath = dataPath.substr(0, lastSlash);
      RECT rcPath = {contentLeft, row2Y + SDpi(32), contentRight - SDpi(80),
                     row2Y + SDpi(48)};

      // 悬浮时蓝色，否则灰色
      COLORREF pathColor = GetDescTextColor();
      if (g_dataDirUnderlineProgress > 0.0f) {
        int r = GetRValue(pathColor) +
                (int)((GetRValue(COLOR_ACCENT) - GetRValue(pathColor)) *
                      g_dataDirUnderlineProgress);
        int g = GetGValue(pathColor) +
                (int)((GetGValue(COLOR_ACCENT) - GetGValue(pathColor)) *
                      g_dataDirUnderlineProgress);
        int b = GetBValue(pathColor) +
                (int)((GetBValue(COLOR_ACCENT) - GetBValue(pathColor)) *
                      g_dataDirUnderlineProgress);
        pathColor = RGB(r, g, b);
      }
      SetTextColor(hdc, pathColor);
      DrawTextW(hdc, dataPath.c_str(), -1, &rcPath,
                DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

      // 下划线动画
      if (g_dataDirUnderlineProgress > 0.0f) {
        SIZE textSize;
        GetTextExtentPoint32W(hdc, dataPath.c_str(), (int)dataPath.size(),
                              &textSize);
        int maxW = rcPath.right - rcPath.left;
        int textW = (textSize.cx < maxW) ? textSize.cx : maxW;
        int lineW = (int)(textW * g_dataDirUnderlineProgress);
        HPEN hLinePen = CreatePen(PS_SOLID, 1, pathColor);
        HPEN hOldPen2 = (HPEN)SelectObject(hdc, hLinePen);
        MoveToEx(hdc, contentLeft, row2Y + SDpi(48), NULL);
        LineTo(hdc, contentLeft + lineW, row2Y + SDpi(48));
        SelectObject(hdc, hOldPen2);
        DeleteObject(hLinePen);
      }
    }

    // 关于分类：版本号 + 协议链接 + 鸣谢
    if (g_currentSettingsTab == 3) {
      // 第0行右侧：版本号
      int row0Y = GetRowY(0);
      SelectObject(hdc, g_hTitleFont);
      SetTextColor(hdc, COLOR_ACCENT);
      std::wstring versionText = L"v" + std::wstring(APP_VERSION_STRING);
      RECT rcVer = {contentRight - SDpi(220), row0Y + SDpi(12), contentRight,
                    row0Y + SDpi(30)};
      DrawTextW(hdc, versionText.c_str(), -1, &rcVer, DT_RIGHT | DT_SINGLELINE);

      // 第1行：协议链接（许可协议、隐私政策）
      int row1Y = GetRowY(1);
      SelectObject(hdc, g_hDescFont);
      SetBkMode(hdc, TRANSPARENT);

      // 最终用户许可协议（放在描述位置，避开行底分隔线）
      const wchar_t *eulaText = T(STR_EULA);
      SetTextColor(hdc, RGB(0, 116, 199)); // 蓝色链接色
      RECT rcEula = {contentLeft, row1Y + SDpi(32), contentLeft + SDpi(260),
                     row1Y + SDpi(50)};
      DrawTextW(hdc, eulaText, -1, &rcEula, DT_LEFT | DT_SINGLELINE);

      // 下划线
      SIZE eulaSize;
      GetTextExtentPoint32W(hdc, eulaText, (int)wcslen(eulaText), &eulaSize);
      HPEN hLinkPen = CreatePen(PS_SOLID, 1, RGB(0, 116, 199));
      HPEN hOldPen = (HPEN)SelectObject(hdc, hLinkPen);
      MoveToEx(hdc, rcEula.left, rcEula.bottom - SDpi(2), NULL);
      LineTo(hdc, rcEula.left + eulaSize.cx, rcEula.bottom - SDpi(2));
      SelectObject(hdc, hOldPen);
      DeleteObject(hLinkPen);

      // 隐私政策（紧随许可协议右侧，间距根据文字宽度自适应）
      const wchar_t *privacyText = T(STR_PRIVACY_POLICY);
      int privacyLeft = rcEula.left + eulaSize.cx + SDpi(24);
      RECT rcPrivacy = {privacyLeft, row1Y + SDpi(32), privacyLeft + SDpi(260),
                        row1Y + SDpi(50)};
      DrawTextW(hdc, privacyText, -1, &rcPrivacy, DT_LEFT | DT_SINGLELINE);

      // 下划线
      SIZE privacySize;
      GetTextExtentPoint32W(hdc, privacyText, (int)wcslen(privacyText),
                            &privacySize);
      hLinkPen = CreatePen(PS_SOLID, 1, RGB(0, 116, 199));
      hOldPen = (HPEN)SelectObject(hdc, hLinkPen);
      MoveToEx(hdc, rcPrivacy.left, rcPrivacy.bottom - SDpi(2), NULL);
      LineTo(hdc, rcPrivacy.left + privacySize.cx, rcPrivacy.bottom - SDpi(2));
      SelectObject(hdc, hOldPen);
      DeleteObject(hLinkPen);

      // 第2行：仓库标题（由通用行绘制逻辑自动绘制）+
      // 网址（蓝色字体，悬浮动画下划线）
      {
        int row2Y = GetRowY(2);
        const wchar_t *repoUrl = L"https://github.com/whutBing/smart-clip";

        // 绘制网址（蓝色字体，在描述位置）
        SetTextColor(hdc, RGB(0, 116, 199));
        RECT rcUrl = {contentLeft, row2Y + SDpi(32), contentLeft + SDpi(400),
                      row2Y + SDpi(50)};
        DrawTextW(hdc, repoUrl, -1, &rcUrl, DT_LEFT | DT_SINGLELINE);

        // 动画下划线（根据 g_githubUnderlineProgress 绘制宽度）
        SIZE urlSize = {0, 0};
        GetTextExtentPoint32W(hdc, repoUrl, (int)wcslen(repoUrl), &urlSize);
        if (g_githubUnderlineProgress > 0.0f) {
          int lineW = (int)(urlSize.cx * g_githubUnderlineProgress);
          hLinkPen = CreatePen(PS_SOLID, 1, RGB(0, 116, 199));
          hOldPen = (HPEN)SelectObject(hdc, hLinkPen);
          MoveToEx(hdc, rcUrl.left, rcUrl.bottom - SDpi(2), NULL);
          LineTo(hdc, rcUrl.left + lineW, rcUrl.bottom - SDpi(2));
          SelectObject(hdc, hOldPen);
          DeleteObject(hLinkPen);
        }
      }

      // 第4行：鸣谢——在描述"感谢为本项目提供帮助的人"下方另起一行显示名单
      int row4Y = GetRowY(4);
      std::wstring creditsText = BuildCreditsText();
      SelectObject(hdc, g_hDescFont);
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, COLOR_ACCENT);
      RECT rcCredits = {contentLeft, row4Y + SDpi(52), contentRight,
                        row4Y + SDpi(78)};
      DrawTextW(hdc, creditsText.c_str(), -1, &rcCredits,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, hOld);

    BitBlt(screenDc, 0, 0, rcClient.right, rcClient.bottom, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(hdc);

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
      SetTextColor(lpDIS->hDC, g_settingsCloseHover ? RGB(255, 255, 255)
                                                    : GetSettingsTextColor());
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
        lpDIS->CtlID == IDC_TASKBAR_CHECK ||
        lpDIS->CtlID == IDC_STARTUP_CHECK ||
        lpDIS->CtlID == IDC_HOVER_SELECT_CHECK) {

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
      case IDC_TASKBAR_CHECK:
        isOn = g_isTaskbarVisible;
        break;
      case IDC_STARTUP_CHECK:
        isOn = g_isStartupEnabled;
        break;
      case IDC_HOVER_SELECT_CHECK:
        isOn = g_isHoverSelectEnabled;
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
        lpDIS->CtlID == IDC_QUICK_PASTE_COMBO ||
        lpDIS->CtlID == IDC_FAVORITE_HOTKEY_COMBO) {
      HBRUSH hBgBr = CreateSolidBrush(GetSettingsBgColor());
      FillRect(lpDIS->hDC, &rc, hBgBr);
      DeleteObject(hBgBr);
      DrawDropdownButton(lpDIS->hDC, rc, lpDIS->CtlID);
      return TRUE;
    }

    // iOS 风格操作按钮（蓝色圆角）
    if (lpDIS->CtlID == IDC_SET_DATA_DIR || lpDIS->CtlID == IDC_CLEAR_NON_FAV ||
        lpDIS->CtlID == IDC_CLEAN_INVALID_IMAGES ||
        lpDIS->CtlID == IDC_EXPORT_DATA || lpDIS->CtlID == IDC_IMPORT_DATA) {
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
                          rc.bottom - rc.top, SCDpi(8));
      Gdiplus::SolidBrush btnBrush(Gdiplus::Color(
          255, GetRValue(btnColor), GetGValue(btnColor), GetBValue(btnColor)));
      g.FillPath(&btnBrush, &btnPath);

      const wchar_t *text = L"";
      if (lpDIS->CtlID == IDC_SET_DATA_DIR)
        text = T(STR_BTN_SELECT);
      else if (lpDIS->CtlID == IDC_CLEAR_NON_FAV)
        text = T(STR_BTN_CLEAN);
      else if (lpDIS->CtlID == IDC_CLEAN_INVALID_IMAGES)
        text = T(STR_BTN_CLEAN);
      else if (lpDIS->CtlID == IDC_EXPORT_DATA)
        text = T(STR_BTN_EXPORT);
      else if (lpDIS->CtlID == IDC_IMPORT_DATA)
        text = T(STR_BTN_IMPORT);

      Gdiplus::Font font(L"Microsoft YaHei", (Gdiplus::REAL)SDpi(13),
                         Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
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
        if (IsOverDataDirPath(pt) || IsOverAboutLink(pt) ||
            IsOverMachineInfoRow(pt)) {
          SetCursor(LoadCursorW(NULL, IDC_HAND));
          return TRUE;
        }
      }
    }
    break;

  case WM_MOUSEMOVE: {
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    int newHover = -1;
    if (pt.x < SidebarW() && pt.y >= SettingsTitlebarH()) {
      int idx = (pt.y - SettingsTitlebarH()) / SidebarItemH();
      if (idx >= 0 && idx < SIDEBAR_COUNT)
        newHover = idx;
    }
    if (newHover != g_settingsHoverSidebar) {
      g_settingsHoverSidebar = newHover;
      RECT rcSb = {0, SettingsTitlebarH(), SidebarW(),
                   SettingsTitlebarH() + SIDEBAR_COUNT * SidebarItemH()};
      InvalidateRect(hwnd, &rcSb, FALSE);
    }
    // 跟踪鼠标离开
    TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);

    // 数据目录路径悬浮检测
    if (g_currentSettingsTab == 2 && pt.x > SidebarW()) {
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

    // GitHub 仓库链接悬浮检测（关于页面）
    if (g_currentSettingsTab == 3 && pt.x > SidebarW()) {
      bool overLink = IsOverAboutLink(pt);
      if (overLink && !g_githubHovered) {
        g_githubHovered = true;
        SetTimer(hwnd, ID_GITHUB_UNDERLINE_TIMER, 16, NULL);
      } else if (!overLink && g_githubHovered) {
        g_githubHovered = false;
        SetTimer(hwnd, ID_GITHUB_UNDERLINE_TIMER, 16, NULL);
      }
    } else if (g_githubHovered) {
      g_githubHovered = false;
      SetTimer(hwnd, ID_GITHUB_UNDERLINE_TIMER, 16, NULL);
    }
    break;
  }

  case WM_MOUSELEAVE:
    if (g_settingsHoverSidebar >= 0) {
      g_settingsHoverSidebar = -1;
      RECT rcSb = {0, SettingsTitlebarH(), SidebarW(),
                   SettingsTitlebarH() + SIDEBAR_COUNT * SidebarItemH()};
      InvalidateRect(hwnd, &rcSb, FALSE);
    }
    if (g_dataDirHovered) {
      g_dataDirHovered = false;
      SetTimer(hwnd, ID_DATADIR_UNDERLINE_TIMER, 16, NULL);
    }
    if (g_githubHovered) {
      g_githubHovered = false;
      SetTimer(hwnd, ID_GITHUB_UNDERLINE_TIMER, 16, NULL);
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
      int row2Y = GetRowY(4);
      RECT rcPath = {SidebarW(), row2Y + SDpi(28), SettingsWidth(),
                     row2Y + SDpi(52)};
      InvalidateRect(hwnd, &rcPath, FALSE);
      return 0;
    }
    if (wParam == ID_GITHUB_UNDERLINE_TIMER) {
      float step = 0.08f;
      if (g_githubHovered) {
        g_githubUnderlineProgress += step;
        if (g_githubUnderlineProgress >= 1.0f) {
          g_githubUnderlineProgress = 1.0f;
          KillTimer(hwnd, ID_GITHUB_UNDERLINE_TIMER);
        }
      } else {
        g_githubUnderlineProgress -= step;
        if (g_githubUnderlineProgress <= 0.0f) {
          g_githubUnderlineProgress = 0.0f;
          KillTimer(hwnd, ID_GITHUB_UNDERLINE_TIMER);
        }
      }
      // 只重绘 GitHub 链接区域（第2行描述位置）
      int githubRowY = GetRowY(2);
      RECT rcLink = {SidebarW(), githubRowY + SDpi(28), SettingsWidth(),
                     githubRowY + SDpi(52)};
      InvalidateRect(hwnd, &rcLink, FALSE);
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
    if (pt.x < SidebarW() && pt.y >= SettingsTitlebarH()) {
      int idx = (pt.y - SettingsTitlebarH()) / SidebarItemH();
      if (idx >= 0 && idx < SIDEBAR_COUNT && idx != g_currentSettingsTab) {
        SwitchSettingsTab(idx);
      }
    }
    // 数据分类：点击数据目录路径打开资源管理器（第4行）
    if (g_currentSettingsTab == 2 && pt.x > SidebarW()) {
      int row2Y = GetRowY(4);
      if (pt.y >= row2Y + SDpi(10) && pt.y <= row2Y + SDpi(45)) {
        std::wstring dataPath = GetDataFilePath();
        size_t lastSlash = dataPath.find_last_of(L"\\");
        if (lastSlash != std::wstring::npos) {
          dataPath = dataPath.substr(0, lastSlash);
          ShellExecuteW(NULL, L"open", L"explorer.exe", dataPath.c_str(), NULL,
                        SW_SHOW);
        }
      }
    }
    // 关于分类：点击协议链接（第1行）
    if (g_currentSettingsTab == 3 && pt.x > SidebarW()) {
      int row1Y = GetRowY(1);
      int contentLeft = SidebarW() + ContentPadding();
      // 协议链接位于第1行描述位置（row1Y + 32 ~ row1Y + 50）
      if (pt.y >= row1Y + SDpi(32) && pt.y <= row1Y + SDpi(50)) {
        // 计算许可协议链接区域宽度
        HDC hdc = GetDC(hwnd);
        SIZE eulaSize = {0, 0};
        if (hdc) {
          HFONT oldFont = (HFONT)SelectObject(hdc, g_hDescFont);
          const wchar_t *eulaText = T(STR_EULA);
          GetTextExtentPoint32W(hdc, eulaText, (int)wcslen(eulaText),
                                &eulaSize);
          SelectObject(hdc, oldFont);
          ReleaseDC(hwnd, hdc);
        }
        int eulaRight = contentLeft + eulaSize.cx;
        int privacyLeft = eulaRight + SDpi(24);
        // 点击许可协议
        if (pt.x >= contentLeft && pt.x <= eulaRight) {
          MessageBoxW(hwnd, T(STR_EULA_BODY), T(STR_EULA_TITLE),
                      MB_OK | MB_ICONINFORMATION);
        }
        // 点击隐私政策
        else if (pt.x >= privacyLeft) {
          MessageBoxW(hwnd, T(STR_PRIVACY_BODY), T(STR_PRIVACY_TITLE),
                      MB_OK | MB_ICONINFORMATION);
        }
      }
      // GitHub 仓库链接位于第2行描述位置（row2Y + 32 ~ row2Y + 50）
      int row2Y = GetRowY(2);
      if (pt.y >= row2Y + SDpi(32) && pt.y <= row2Y + SDpi(50)) {
        ShellExecuteW(NULL, L"open", L"https://github.com/whutBing/smart-clip",
                      NULL, NULL, SW_SHOWNORMAL);
      }
      // 机器码行（第3行）：点击打开机器码弹窗
      int row3Y = GetRowY(3);
      if (pt.y >= row3Y && pt.y <= row3Y + RowHeight()) {
        ShowMachineInfoDialog(hwnd);
      }
    }
    break;
  }

  case WM_RBUTTONUP: {
    // 右键点击侧边栏也切换分类（与左键一致），避免右键无响应
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (pt.x < SidebarW() && pt.y >= SettingsTitlebarH()) {
      int idx = (pt.y - SettingsTitlebarH()) / SidebarItemH();
      if (idx >= 0 && idx < SIDEBAR_COUNT && idx != g_currentSettingsTab) {
        SwitchSettingsTab(idx);
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
          ShowTrayBalloon(g_hwndMain, T(STR_TRAY_SETTINGS_UPDATED),
                          T(STR_TRAY_NOTIFY_ENABLED));
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
      if (wID == IDC_TASKBAR_CHECK) {
        g_isTaskbarVisible = !g_isTaskbarVisible;
        InvalidateRect(g_hwndToggleTaskbar, NULL, TRUE);
        SaveHotkeySettings();
        if (g_hwndMain)
          ApplyTaskbarVisibility(g_hwndMain);
        return 0;
      }
      if (wID == IDC_STARTUP_CHECK) {
        g_isStartupEnabled = !g_isStartupEnabled;
        ApplyStartupPreference(g_isStartupEnabled);
        InvalidateRect(g_hwndToggleStartup, NULL, TRUE);
        return 0;
      }
      if (wID == IDC_HOVER_SELECT_CHECK) {
        g_isHoverSelectEnabled = !g_isHoverSelectEnabled;
        InvalidateRect(g_hwndToggleHoverSelect, NULL, TRUE);
        SaveHotkeySettings();
        return 0;
      }
      if (wID == IDC_SET_DATA_DIR) {
        BROWSEINFOW bi = {};
        bi.hwndOwner = hwnd;
        bi.lpszTitle = T(STR_DLG_SELECT_DATA_DIR);
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (pidl) {
          wchar_t path[MAX_PATH];
          if (SHGetPathFromIDListW(pidl, path)) {
            wchar_t migrateMsg[MAX_PATH + 100];
            _snwprintf_s(migrateMsg, _countof(migrateMsg),
                         T(STR_DLG_CONFIRM_MIGRATE_MSG), path);
            int result =
                MessageBoxW(hwnd, migrateMsg, T(STR_DLG_CONFIRM_MIGRATE_TITLE),
                            MB_YESNO | MB_ICONQUESTION);
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
        ThemedConfirmDialogConfig dialog = {T(STR_DLG_CLEAR_NON_FAV_TITLE),
                                            T(STR_DLG_CLEAR_NON_FAV_SUBTITLE),
                                            T(STR_DLG_CLEAR_NON_FAV_SUB_DESC),
                                            T(STR_DLG_CLEAR_NON_FAV_DESC),
                                            T(STR_BTN_CONFIRM_CLEAR),
                                            T(STR_DLG_CONFIRM_CANCEL),
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
            ShowTrayBalloon(g_hwndMain, T(STR_TRAY_HINT),
                            T(STR_TRAY_CLEARED_NON_FAV));
        }
        return 0;
      }
      if (wID == IDC_CLEAN_INVALID_IMAGES) {
        ThemedConfirmDialogConfig dialog = {
            T(STR_DLG_CLEAN_INVALID_IMAGES_TITLE),
            T(STR_DLG_CLEAN_INVALID_IMAGES_SUBTITLE),
            T(STR_DLG_CLEAN_INVALID_IMAGES_SUB_DESC),
            T(STR_DLG_CLEAN_INVALID_IMAGES_DESC),
            T(STR_BTN_CONFIRM_CLEAR),
            T(STR_DLG_CONFIRM_CANCEL),
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
            ShowTrayBalloon(g_hwndMain, T(STR_TRAY_HINT),
                            T(STR_TRAY_CLEARED_INVALID_IMAGES));
        }
        return 0;
      }
      if (wID == IDC_EXPORT_DATA) {
        // 导出数据为 ZIP 压缩包
        wchar_t szFile[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"ZIP Files (*.zip)\0*.zip\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"zip";
        std::wstring defaultName =
            L"smartclip_export_" + GetCurrentTimeString();
        for (auto &c : defaultName)
          if (c == L' ' || c == L':' || c == L'-')
            c = L'_';
        wcscpy_s(szFile, defaultName.c_str());
        ofn.lpstrTitle = T(STR_ROW_EXPORT_DATA);
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

        if (GetSaveFileNameW(&ofn)) {
          extern bool ExportData(const std::wstring &outputPath);
          std::wstring path = szFile;
          if (ExportData(path)) {
            MessageBoxW(hwnd, T(STR_ROW_EXPORT_DATA_DESC), T(STR_TRAY_HINT),
                        MB_OK | MB_ICONINFORMATION);
          } else {
            MessageBoxW(hwnd, L"Export failed", T(STR_TRAY_HINT),
                        MB_OK | MB_ICONERROR);
          }
        }
        return 0;
      }
      if (wID == IDC_IMPORT_DATA) {
        wchar_t szFile[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"ZIP Files (*.zip)\0*.zip\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"zip";
        ofn.lpstrTitle = T(STR_ROW_IMPORT_DATA);
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

        if (GetOpenFileNameW(&ofn)) {
          std::wstring path = szFile;

          int choice = MessageBoxW(
              hwnd,
              L"选择导入方式：\n\n是 - 覆盖当前数据（备份当前数据）\n否 - "
              L"追加合并（保留现有数据）\n取消 - 取消导入",
              T(STR_ROW_IMPORT_DATA), MB_YESNOCANCEL | MB_ICONQUESTION);

          if (choice == IDCANCEL) {
            return 0;
          }

          bool overwrite = (choice == IDYES);
          extern bool ImportData(const std::wstring &zipPath, bool overwrite);
          if (ImportData(path, overwrite)) {
            MessageBoxW(hwnd, T(STR_ROW_IMPORT_DATA_DESC), T(STR_TRAY_HINT),
                        MB_OK | MB_ICONINFORMATION);
            extern void UpdateListBox();
            UpdateListBox();
          } else {
            std::wstring message = L"Import failed";
            std::wstring detail = GetLastDataImportError();
            if (!detail.empty()) message += L"\n\n" + detail;
            MessageBoxW(hwnd, message.c_str(), T(STR_TRAY_HINT),
                        MB_OK | MB_ICONERROR);
          }
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
      if (wID == IDC_FAVORITE_HOTKEY_COMBO) {
        ShowDropdownPopup(g_hwndFavoriteHotkeyCombo, IDC_FAVORITE_HOTKEY_COMBO);
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
      const auto &langs = GetAvailableLanguages();
      if (sel >= 0 && sel < (int)langs.size()) {
        g_appLanguage = langs[sel].lang;
        ApplyLanguage();
        UpdateModifierDropdownItems();
        UpdateListBox();
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
        // 刷新主面板列表，使快捷键提示立即更新
        if (g_hwndListBox)
          InvalidateRect(g_hwndListBox, NULL, FALSE);
      }
      return 0;
    }
    // 收藏快捷键修饰键选择
    if (wID == IDC_FAVORITE_HOTKEY_COMBO && wNotify == CBN_SELCHANGE) {
      int sel = g_activeDropdown.selectedIndex;
      if (sel >= 0 && sel < FAVORITE_HOTKEY_MOD_COUNT) {
        g_favoriteHotkeyModifiers = g_favoriteHotkeyModValues[sel];
        SaveHotkeySettings();
        extern HWND g_hwndMain;
        if (g_hwndMain && g_isFavoriteHotkeyEnabled) {
          UnregisterFavoriteHotkeys(g_hwndMain);
          RegisterFavoriteHotkeys(g_hwndMain);
        }
        if (g_hwndFavoriteHotkeyCombo)
          InvalidateRect(g_hwndFavoriteHotkeyCombo, NULL, TRUE);
      }
      return 0;
    }
    // 快捷键编辑框焦点
    if (wID == IDC_HOTKEY_EDIT) {
      if (wNotify == EN_SETFOCUS) {
        UnregisterHotkey(g_hwndMain);
        g_savedHotkeyMod = g_hotkeyModifiers;
        g_savedHotkeyVk = g_hotkeyVirtualKey;
        g_savedHotkeyEnabled = g_isHotkeyEnabled;
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
        SetHotkeyEditPlaceholder(g_hwndHotkeyEdit, T(STR_HOTKEY_PLACEHOLDER));
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
        SetHotkeyEditPlaceholder(g_hwndSearchHotkeyEdit,
                                 T(STR_HOTKEY_PLACEHOLDER));
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

    if (wID == IDC_TEXT_SIZE_LIMIT && wNotify == EN_CHANGE) {
      wchar_t buf[16] = {};
      GetWindowTextW(g_hwndTextSizeLimitEdit, buf, 16);
      int val = _wtoi(buf);
      if (val >= 1 && val <= 10240) {
        g_maxTextSizeKB = val;
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
        extern void StartScrollbarHideTimer(HWND hwnd);
        if (g_hwndListBox) {
          StartScrollbarHideTimer(g_hwndListBox);
        }
      }
      return 0;
    }

    // 失焦时若为空，显示默认值
    if (wID == IDC_SCROLLBAR_TIMEOUT_EDIT && wNotify == EN_KILLFOCUS) {
      wchar_t buf[16] = {};
      GetWindowTextW(g_hwndScrollbarTimeoutEdit, buf, 16);
      std::wstring trimmed = buf;
      while (!trimmed.empty() &&
             (trimmed.back() == L' ' || trimmed.back() == L'\r' ||
              trimmed.back() == L'\n'))
        trimmed.pop_back();
      if (trimmed.empty()) {
        SetWindowTextW(g_hwndScrollbarTimeoutEdit, L"0");
        SyncSettingsNumberEditTextRect(g_hwndScrollbarTimeoutEdit);
      }
      return 0;
    }

    if (wID == IDC_HISTORY_LIMIT_EDIT && wNotify == EN_KILLFOCUS) {
      wchar_t buf[16] = {};
      GetWindowTextW(g_hwndHistoryLimitEdit, buf, 16);
      std::wstring trimmed = buf;
      while (!trimmed.empty() &&
             (trimmed.back() == L' ' || trimmed.back() == L'\r' ||
              trimmed.back() == L'\n'))
        trimmed.pop_back();
      if (trimmed.empty()) {
        SetWindowTextW(g_hwndHistoryLimitEdit, L"100");
        SyncSettingsNumberEditTextRect(g_hwndHistoryLimitEdit);
      }
      return 0;
    }

    if (wID == IDC_TEXT_SIZE_LIMIT && wNotify == EN_KILLFOCUS) {
      wchar_t buf[16] = {};
      GetWindowTextW(g_hwndTextSizeLimitEdit, buf, 16);
      std::wstring trimmed = buf;
      while (!trimmed.empty() &&
             (trimmed.back() == L' ' || trimmed.back() == L'\r' ||
              trimmed.back() == L'\n'))
        trimmed.pop_back();
      if (trimmed.empty()) {
        SetWindowTextW(g_hwndTextSizeLimitEdit, L"50");
        SyncSettingsNumberEditTextRect(g_hwndTextSizeLimitEdit);
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

  case WM_CLOSE: {
    // 关闭前最终读取滚动条隐藏时间编辑框的值，确保即使用户刚修改未触发
    // EN_CHANGE 也能保存
    if (g_hwndScrollbarTimeoutEdit) {
      wchar_t buf[16] = {};
      GetWindowTextW(g_hwndScrollbarTimeoutEdit, buf, 16);
      int val = _wtoi(buf);
      if (val >= 600 && val <= 2000) {
        g_customScrollbarHideDelayMs = val;
        SaveHotkeySettings();
      }
    }
    // 同样读取历史记录数量
    if (g_hwndHistoryLimitEdit) {
      wchar_t buf[16] = {};
      GetWindowTextW(g_hwndHistoryLimitEdit, buf, 16);
      int val = _wtoi(buf);
      if (val >= 10 && val <= 10000) {
        g_maxHistoryCount = val;
        SaveHotkeySettings();
      }
    }
    DestroyWindow(hwnd);
    return 0;
  }

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
  int y = GetRowY(rowIndex) + (RowHeight() - ToggleH()) / 2;
  int x = GetControlX(ToggleW());
  return CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW, x, y,
                         ToggleW(), ToggleH(), parent, (HMENU)(INT_PTR)ctlId,
                         GetModuleHandleW(NULL), NULL);
}

static HWND CreateSettingsCombo(HWND parent, int rowIndex, int ctlId,
                                int width) {
  int w = SDpi(width);
  int h = SDpi(32);
  int y = GetRowY(rowIndex) + (RowHeight() - h) / 2;
  int x = GetControlX(w);
  return CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW, x, y, w, h,
                         parent, (HMENU)(INT_PTR)ctlId, GetModuleHandleW(NULL),
                         NULL);
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
  int w = SDpi(180);
  int h = SDpi(32);
  int y = GetRowY(rowIndex) + (RowHeight() - h) / 2;
  int x = GetControlX(w);
  return CreateWindowExW(
      0, L"EDIT", NULL,
      WS_CHILD | WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL | ES_MULTILINE, x, y,
      w, h, parent, (HMENU)(INT_PTR)ctlId, GetModuleHandleW(NULL), NULL);
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
      // 恢复窗口（可能被最小化）并强制切到前台
      ShowWindow(g_hwndSettingsDlg, SW_RESTORE);
      // 使用 AttachThreadInput 绕过 Windows 前台窗口限制
      DWORD foreThread = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
      DWORD curThread = GetCurrentThreadId();
      if (foreThread != curThread)
        AttachThreadInput(curThread, foreThread, TRUE);
      SetForegroundWindow(g_hwndSettingsDlg);
      if (foreThread != curThread)
        AttachThreadInput(curThread, foreThread, FALSE);
      return;
    }
    g_isSettingsDialogOpen = false;
    g_hwndSettingsDlg = NULL;
  }

  LoadFontSettings();
  g_isSettingsDialogOpen = true;
  UpdateModifierDropdownItems();

  UINT oldMod = g_hotkeyModifiers;
  UINT oldVk = g_hotkeyVirtualKey;

  RegisterSettingsClass();
  g_settingsDpi = GetSmartClipUiDpi(hwndParent);
  int settingsW = SettingsWidth();
  int settingsH = SettingsHeight();

  RECT parentRect;
  GetWindowRect(hwndParent, &parentRect);
  int px =
      parentRect.left + (parentRect.right - parentRect.left - settingsW) / 2;
  int py =
      parentRect.top + (parentRect.bottom - parentRect.top - settingsH) / 2;

  HWND hwndDlg = CreateWindowExW(
      0, L"SmartClipSettings", L"设置", WS_POPUP | WS_CLIPCHILDREN, px, py,
      settingsW, settingsH, hwndParent, NULL, GetModuleHandleW(NULL), NULL);

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

  HFONT hCtlFont = CreateFontW(SDpi(19), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                               FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");

  // 关闭按钮
  g_hwndSettingsClose = CreateWindowExW(
      0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
      SettingsWidth() - SDpi(46), 0, SDpi(46), SettingsTitlebarH(), hwndDlg,
      (HMENU)IDC_SETTINGS_CLOSE, GetModuleHandleW(NULL), NULL);
  g_oldSettingsCloseProc = (WNDPROC)SetWindowLongPtrW(
      g_hwndSettingsClose, GWLP_WNDPROC, (LONG_PTR)SettingsCloseBtnProc);

  // ===== 通用分类控件 =====
  // GetRowY 会根据 g_currentSettingsTab 加页面对应偏移（tab==2/3），
  // 而通用控件按 tab==0 布局。若进入时停留在他页（如上次停在关于页），
  // 会导致通用控件（尤其 row>=4 的 toggle）被错误下移。故临时锁定 tab==0。
  int savedGeneralTab = g_currentSettingsTab;
  g_currentSettingsTab = 0;
  g_hwndToggleNotification = CreateToggle(hwndDlg, 0, IDC_NOTIFICATION_CHECK);
  g_hwndToggleSmoothScroll = CreateToggle(hwndDlg, 1, IDC_SMOOTH_SCROLL_CHECK);
  g_hwndToggleScrollbar = CreateToggle(hwndDlg, 2, IDC_SCROLLBAR_CHECK);
  g_hwndToggleColorDot = CreateWindowExW(
      0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW, GetControlX(ToggleW()),
      GetRowY(6) + (RowHeight() - ToggleH()) / 2, ToggleW(), ToggleH(), hwndDlg,
      (HMENU)IDC_COLOR_DOT_CHECK, GetModuleHandleW(NULL), NULL);
  g_hwndToggleTaskbar = CreateToggle(hwndDlg, 7, IDC_TASKBAR_CHECK);
  g_hwndToggleStartup = CreateToggle(hwndDlg, 8, IDC_STARTUP_CHECK);
  g_hwndToggleHoverSelect = CreateToggle(hwndDlg, 9, IDC_HOVER_SELECT_CHECK);
  // 启动项状态从系统读取
  g_isStartupEnabled = IsStartupEnabled();

  {
    int editW = SDpi(120);
    int editH = SDpi(28);
    int timeoutY = GetRowY(3) + (RowHeight() - editH) / 2;
    wchar_t timeoutBuf[16];
    _snwprintf_s(timeoutBuf, _countof(timeoutBuf), L"%d",
                 g_customScrollbarHideDelayMs);
    g_hwndScrollbarTimeoutEdit = CreateWindowExW(
        0, L"EDIT", timeoutBuf,
        WS_CHILD | WS_TABSTOP | ES_CENTER | ES_NUMBER | ES_MULTILINE,
        GetControlX(editW), timeoutY, editW, editH, hwndDlg,
        (HMENU)IDC_SCROLLBAR_TIMEOUT_EDIT, GetModuleHandleW(NULL), NULL);
    ConfigureSettingsEdit(g_hwndScrollbarTimeoutEdit);
    SendMessageW(g_hwndScrollbarTimeoutEdit, WM_SETFONT, (WPARAM)hCtlFont,
                 TRUE);
    g_oldIosEditProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndScrollbarTimeoutEdit, GWLP_WNDPROC, (LONG_PTR)IosEditProc);
    SyncSettingsNumberEditTextRect(g_hwndScrollbarTimeoutEdit);
  }

  g_hwndThemeCombo = CreateSettingsCombo(hwndDlg, 4, IDC_THEME_COMBO, 120);
  g_hwndThemeStyleCombo =
      CreateSettingsCombo(hwndDlg, 5, IDC_THEME_STYLE_COMBO, 120);
  g_hwndLanguageCombo =
      CreateSettingsCombo(hwndDlg, 5, IDC_LANGUAGE_COMBO, 120);

  g_hwndImagePreviewCombo =
      CreateSettingsCombo(hwndDlg, 6, IDC_IMAGE_PREVIEW_COMBO, 120);

  {
    int editW = SDpi(80);
    int editH = SDpi(28);
    int limitY = GetRowY(2) + (RowHeight() - editH) / 2;
    wchar_t limitBuf[16];
    _snwprintf_s(limitBuf, _countof(limitBuf), L"%d", g_maxHistoryCount);
    g_hwndHistoryLimitEdit = CreateWindowExW(
        0, L"EDIT", limitBuf,
        WS_CHILD | WS_TABSTOP | ES_CENTER | ES_NUMBER | ES_MULTILINE,
        GetControlX(editW), limitY, editW, editH, hwndDlg,
        (HMENU)IDC_HISTORY_LIMIT_EDIT, GetModuleHandleW(NULL), NULL);
    ConfigureSettingsEdit(g_hwndHistoryLimitEdit);
    SendMessageW(g_hwndHistoryLimitEdit, WM_SETFONT, (WPARAM)hCtlFont, TRUE);
    g_oldIosEditProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndHistoryLimitEdit, GWLP_WNDPROC, (LONG_PTR)IosEditProc);
    SyncSettingsNumberEditTextRect(g_hwndHistoryLimitEdit);
  }
  UpdateScrollbarSettingsControls();
  // 恢复进入时的分类，后续数据控件块会自行临时切换
  g_currentSettingsTab = savedGeneralTab;

  // ===== 数据分类控件 =====
  // 按钮宽度与历史记录数量输入框(SDpi(80))保持一致
  auto CreateIosButton = [&](int rowIndex, int ctlId, int width) -> HWND {
    int w = SDpi(width);
    int h = SDpi(32);
    int y = GetRowY(rowIndex) + (RowHeight() - h) / 2;
    int x = GetControlX(w);
    return CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW, x, y, w,
                           h, hwndDlg, (HMENU)(INT_PTR)ctlId,
                           GetModuleHandleW(NULL), NULL);
  };
  // 创建数据分类按钮时，临时切换到数据标签页，确保 GetRowY 正确计算偏移
  int savedTab = g_currentSettingsTab;
  g_currentSettingsTab = 2;
  g_hwndSetDataDirBtn = CreateIosButton(4, IDC_SET_DATA_DIR, 80);
  g_hwndClearNonFavBtn = CreateIosButton(5, IDC_CLEAR_NON_FAV, 80);
  g_hwndCleanInvalidImagesBtn =
      CreateIosButton(6, IDC_CLEAN_INVALID_IMAGES, 80);
  g_hwndExportDataBtn = CreateIosButton(7, IDC_EXPORT_DATA, 80);
  g_hwndImportDataBtn = CreateIosButton(8, IDC_IMPORT_DATA, 80);
  g_currentSettingsTab = savedTab;

  // 文本大小上限输入框（与历史数量输入框一致：ES_CENTER + ES_MULTILINE
  // 实现水平居中）
  {
    int w = SDpi(80);
    int h = SDpi(28);
    int y = GetRowY(3) + SDpi(16);
    int x = GetControlX(w);
    g_hwndTextSizeLimitEdit = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_TABSTOP | ES_CENTER | ES_NUMBER | ES_MULTILINE, x, y, w,
        h, hwndDlg, (HMENU)(INT_PTR)IDC_TEXT_SIZE_LIMIT, GetModuleHandleW(NULL),
        NULL);
    ConfigureSettingsEdit(g_hwndTextSizeLimitEdit);
    SendMessageW(g_hwndTextSizeLimitEdit, WM_SETFONT, (WPARAM)hCtlFont, TRUE);
    g_oldIosEditProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndTextSizeLimitEdit, GWLP_WNDPROC, (LONG_PTR)IosEditProc);
    wchar_t buf[16];
    _snwprintf_s(buf, _countof(buf), L"%d", g_maxTextSizeKB);
    SetWindowTextW(g_hwndTextSizeLimitEdit, buf);
    SyncSettingsNumberEditTextRect(g_hwndTextSizeLimitEdit);
  }

  // ===== 快捷键分类控件 =====
  g_hwndHotkeyEdit = CreateHotkeyEditBox(hwndDlg, 0, IDC_HOTKEY_EDIT);
  ConfigureSettingsEdit(g_hwndHotkeyEdit);
  SendMessageW(g_hwndHotkeyEdit, WM_SETFONT, (WPARAM)hCtlFont, TRUE);
  g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g_hwndHotkeyEdit, GWLP_WNDPROC,
                                             (LONG_PTR)HotkeyEditProc);
  std::wstring hkText = (g_isHotkeyEnabled && oldMod != 0 && oldVk != 0)
                            ? FormatHotkeyText(oldMod, oldVk, L"")
                            : L"";
  SetWindowTextW(g_hwndHotkeyEdit, hkText.c_str());
  SetHotkeyEditPlaceholder(g_hwndHotkeyEdit, T(STR_HOTKEY_PLACEHOLDER));
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
  SetHotkeyEditPlaceholder(g_hwndSearchHotkeyEdit, T(STR_HOTKEY_PLACEHOLDER));
  SyncHotkeyEditTextRect(g_hwndSearchHotkeyEdit);

  // 快捷粘贴修饰键下拉选择器（与快捷键输入框宽度一致）
  g_hwndQuickPasteCombo =
      CreateSettingsCombo(hwndDlg, 2, IDC_QUICK_PASTE_COMBO, 180);
  // 收藏快捷键修饰键下拉选择器（与快捷键输入框宽度一致）
  g_hwndFavoriteHotkeyCombo =
      CreateSettingsCombo(hwndDlg, 3, IDC_FAVORITE_HOTKEY_COMBO, 180);

  // 初始显示最后一次使用的分类
  int initialTab =
      (g_currentSettingsTab >= 0 && g_currentSettingsTab < SIDEBAR_COUNT)
          ? g_currentSettingsTab
          : 0;
  g_currentSettingsTab = initialTab;
  SwitchSettingsTab(initialTab);

  // 初始化冲突检测状态
  UpdateHotkeyConflictState();

  ShowWindow(hwndDlg, SW_SHOW);
  // 使用 AttachThreadInput 绕过 Windows
  // 前台窗口限制，防止从托盘菜单调用时无法前台显示
  {
    DWORD foreThread = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
    DWORD curThread = GetCurrentThreadId();
    if (foreThread != curThread)
      AttachThreadInput(curThread, foreThread, TRUE);
    SetForegroundWindow(hwndDlg);
    if (foreThread != curThread)
      AttachThreadInput(curThread, foreThread, FALSE);
  }
  UpdateWindow(hwndDlg);
}

void ShowHotkeySettingsDialog(HWND hwndParent) {
  ShowSettingsDialog(hwndParent);
}

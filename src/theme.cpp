#include "theme.h"
#include "history.h"
#include "search.h"
#include "tag_popup.h"
#include "settings.h"
#include <windows.h>

extern HWND g_hwndMain;
extern HWND g_hwndActiveThemedDialog;

bool g_isDarkMode = false;
ThemeMode g_themeMode = THEME_LIGHT;
ThemeId g_themeId = APP_THEME_HIGH_CONTRAST;

static const ThemePalette kMonoLight = {
    RGB(241, 239, 235), RGB(250, 249, 246), RGB(244, 242, 238),
    RGB(38, 38, 38),    RGB(108, 108, 108), RGB(0, 102, 204),
    RGB(0, 82, 184),    RGB(241, 239, 235), RGB(236, 234, 230),
    RGB(226, 223, 218), RGB(214, 211, 206), RGB(150, 150, 150),
    RGB(252, 251, 248), RGB(238, 235, 231), RGB(248, 247, 244),
    RGB(250, 249, 246), RGB(250, 249, 246)};

static const ThemePalette kMonoDark = {
    RGB(24, 25, 28),    RGB(34, 36, 40),    RGB(29, 31, 35),
    RGB(232, 233, 236), RGB(154, 158, 166), RGB(132, 160, 198),
    RGB(164, 189, 222), RGB(26, 28, 32),    RGB(27, 29, 33),
    RGB(39, 42, 48),    RGB(58, 62, 70),    RGB(98, 103, 112),
    RGB(36, 38, 43),    RGB(46, 49, 56),    RGB(24, 25, 28),
    RGB(29, 31, 35),    RGB(29, 31, 35)};

static const ThemePalette *ResolvePalette() {
  return g_isDarkMode ? &kMonoDark : &kMonoLight;
}

const ThemePalette &GetCurrentThemePalette() { return *ResolvePalette(); }
COLORREF GetThemeWindowBgColor() { return ResolvePalette()->windowBg; }
COLORREF GetThemeSurfaceColor() { return ResolvePalette()->surface; }
COLORREF GetThemeSurfaceAltColor() { return ResolvePalette()->surfaceAlt; }
COLORREF GetThemeTextPrimaryColor() { return ResolvePalette()->textPrimary; }
COLORREF GetThemeTextSecondaryColor() { return ResolvePalette()->textSecondary; }
COLORREF GetThemeAccentColor() { return ResolvePalette()->accent; }
COLORREF GetThemeAccentStrongColor() { return ResolvePalette()->accentStrong; }
COLORREF GetThemeTitlebarBgColor() { return ResolvePalette()->titlebarBg; }
COLORREF GetThemeSidebarBgColor() { return ResolvePalette()->sidebarBg; }
COLORREF GetThemeSidebarHoverColor() { return ResolvePalette()->sidebarHover; }
COLORREF GetThemeSeparatorColor() { return ResolvePalette()->separator; }
COLORREF GetThemeToggleOffColor() { return ResolvePalette()->toggleOff; }
COLORREF GetThemeInputBgColor() { return ResolvePalette()->inputBg; }
COLORREF GetThemeDropdownHoverColor() { return ResolvePalette()->dropdownHover; }
COLORREF GetThemeDialogBgColor() { return ResolvePalette()->dialogBg; }
COLORREF GetThemeDialogCardBgColor() { return ResolvePalette()->dialogCardBg; }
COLORREF GetThemeDialogEditBgColor() { return ResolvePalette()->dialogEditBg; }

bool IsSystemDarkMode() {
  HKEY hKey;
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegOpenKeyExW(
          HKEY_CURRENT_USER,
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
          0, KEY_READ, &hKey) == ERROR_SUCCESS) {
    RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&value,
                     &size);
    RegCloseKey(hKey);
  }
  return value == 0;
}

void ApplyTheme() {
  bool newDarkMode = false;
  switch (g_themeMode) {
  case THEME_LIGHT:
    newDarkMode = false;
    break;
  case THEME_DARK:
    newDarkMode = true;
    break;
  case THEME_SYSTEM:
    newDarkMode = IsSystemDarkMode();
    break;
  }

  g_isDarkMode = newDarkMode;

  if (g_hwndMain) {
    SetClassLongPtrW(g_hwndMain, GCLP_HBRBACKGROUND,
                     (LONG_PTR)CreateSolidBrush(GetThemeWindowBgColor()));
    if (g_hwndListBox) {
      InvalidateRect(g_hwndListBox, NULL, TRUE);
    }
    if (g_hwndSearchBox) {
      InvalidateRect(g_hwndSearchBox, NULL, TRUE);
    }
    InvalidateRect(g_hwndMain, NULL, TRUE);
    UpdateWindow(g_hwndMain);
  }

  if (g_hwndSettingsDlg && IsWindow(g_hwndSettingsDlg)) {
    SendMessageW(g_hwndSettingsDlg, WM_THEMECHANGED, 0, 0);
    InvalidateRect(g_hwndSettingsDlg, NULL, TRUE);
  }
  if (g_hwndActiveThemedDialog && IsWindow(g_hwndActiveThemedDialog)) {
    SendMessageW(g_hwndActiveThemedDialog, WM_THEMECHANGED, 0, 0);
  }
  if (IsTagPopupVisible()) {
    HWND hwndTagPopup = GetTagPopupWindow();
    if (hwndTagPopup && IsWindow(hwndTagPopup)) {
      SendMessageW(hwndTagPopup, WM_THEMECHANGED, 0, 0);
    }
  }
}

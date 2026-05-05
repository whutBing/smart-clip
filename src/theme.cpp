#include "theme.h"
#include "history.h"
#include "search.h"
#include "settings.h"
#include <windows.h>

extern HWND g_hwndMain;
extern HWND g_hwndActiveThemedDialog;

bool g_isDarkMode = false;
ThemeMode g_themeMode = THEME_LIGHT;
ThemeId g_themeId = APP_THEME_HIGH_CONTRAST;

static const ThemePalette kMonoLight = {
    RGB(245, 245, 245), RGB(255, 255, 255), RGB(248, 248, 248),
    RGB(0, 0, 0),       RGB(90, 90, 90),    RGB(0, 0, 0),
    RGB(32, 32, 32),    RGB(245, 245, 245), RGB(245, 245, 245),
    RGB(230, 230, 230), RGB(220, 220, 220), RGB(140, 140, 140),
    RGB(255, 255, 255), RGB(235, 235, 235), RGB(255, 255, 255),
    RGB(255, 255, 255), RGB(255, 255, 255)};

static const ThemePalette kMonoDark = {
    RGB(12, 12, 12),    RGB(24, 24, 24),    RGB(18, 18, 18),
    RGB(255, 255, 255), RGB(170, 170, 170), RGB(255, 255, 255),
    RGB(220, 220, 220), RGB(12, 12, 12),    RGB(12, 12, 12),
    RGB(28, 28, 28),    RGB(50, 50, 50),    RGB(90, 90, 90),
    RGB(24, 24, 24),    RGB(36, 36, 36),    RGB(12, 12, 12),
    RGB(18, 18, 18),    RGB(18, 18, 18)};

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
}

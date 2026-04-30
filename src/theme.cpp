#include "theme.h"
#include "history.h"
#include "search.h"
#include "settings.h"
#include <windows.h>

extern HWND g_hwndMain;
extern HWND g_hwndActiveThemedDialog;

bool g_isDarkMode = false;
ThemeMode g_themeMode = THEME_LIGHT;
ThemeId g_themeId = APP_THEME_CLASSIC;

static const ThemePalette kClassicLight = {
    RGB(245, 245, 245), RGB(255, 255, 255), RGB(248, 248, 248),
    RGB(60, 60, 60),    RGB(130, 130, 130), RGB(0, 120, 215),
    RGB(66, 133, 244),  RGB(245, 245, 245), RGB(238, 238, 238),
    RGB(225, 225, 225), RGB(225, 225, 225), RGB(190, 190, 190),
    RGB(255, 255, 255), RGB(230, 230, 230), RGB(247, 248, 250),
    RGB(255, 255, 255), RGB(255, 255, 255)};

static const ThemePalette kClassicDark = {
    RGB(23, 23, 26),    RGB(46, 46, 48),    RGB(32, 32, 36),
    RGB(226, 222, 226), RGB(140, 140, 145), RGB(104, 142, 196),
    RGB(124, 160, 210), RGB(24, 24, 28),    RGB(28, 28, 32),
    RGB(42, 42, 46),    RGB(55, 55, 58),    RGB(85, 85, 85),
    RGB(46, 46, 48),    RGB(55, 55, 60),    RGB(30, 31, 35),
    RGB(35, 36, 40),    RGB(24, 26, 30)};

static const ThemePalette kGraphiteLight = {
    RGB(241, 243, 246), RGB(252, 253, 255), RGB(245, 247, 250),
    RGB(52, 59, 68),    RGB(112, 120, 132), RGB(48, 103, 178),
    RGB(63, 123, 204),  RGB(240, 242, 246), RGB(232, 235, 240),
    RGB(217, 222, 228), RGB(220, 224, 230), RGB(182, 188, 196),
    RGB(255, 255, 255), RGB(226, 230, 235), RGB(244, 246, 249),
    RGB(255, 255, 255), RGB(255, 255, 255)};

static const ThemePalette kGraphiteDark = {
    RGB(22, 26, 32),    RGB(34, 39, 46),    RGB(28, 33, 40),
    RGB(224, 228, 234), RGB(142, 149, 160), RGB(110, 149, 206),
    RGB(132, 170, 226), RGB(24, 28, 34),    RGB(26, 31, 38),
    RGB(39, 45, 54),    RGB(54, 60, 70),    RGB(84, 90, 100),
    RGB(34, 39, 46),    RGB(50, 56, 66),    RGB(26, 30, 36),
    RGB(31, 36, 42),    RGB(22, 26, 32)};

static const ThemePalette kWarmLight = {
    RGB(246, 242, 235), RGB(255, 252, 246), RGB(250, 246, 240),
    RGB(82, 68, 54),    RGB(140, 122, 102), RGB(178, 110, 52),
    RGB(203, 128, 60),  RGB(245, 240, 232), RGB(238, 232, 223),
    RGB(228, 220, 208), RGB(227, 220, 211), RGB(198, 184, 166),
    RGB(255, 252, 246), RGB(236, 228, 216), RGB(248, 244, 238),
    RGB(255, 252, 246), RGB(255, 252, 246)};

static const ThemePalette kWarmDark = {
    RGB(34, 29, 25),    RGB(48, 42, 37),    RGB(43, 36, 32),
    RGB(232, 222, 210), RGB(170, 152, 136), RGB(198, 132, 74),
    RGB(224, 150, 91),  RGB(37, 31, 27),    RGB(40, 34, 30),
    RGB(55, 47, 42),    RGB(70, 61, 55),    RGB(102, 90, 80),
    RGB(48, 42, 37),    RGB(64, 55, 49),    RGB(38, 33, 29),
    RGB(42, 37, 33),    RGB(34, 29, 25)};

static const ThemePalette kHighContrastLight = {
    RGB(255, 255, 255), RGB(255, 255, 255), RGB(248, 248, 248),
    RGB(0, 0, 0),       RGB(70, 70, 70),    RGB(0, 102, 204),
    RGB(0, 82, 184),    RGB(255, 255, 255), RGB(245, 245, 245),
    RGB(230, 230, 230), RGB(0, 0, 0),       RGB(120, 120, 120),
    RGB(255, 255, 255), RGB(225, 225, 225), RGB(255, 255, 255),
    RGB(255, 255, 255), RGB(255, 255, 255)};

static const ThemePalette kHighContrastDark = {
    RGB(0, 0, 0),       RGB(18, 18, 18),    RGB(12, 12, 12),
    RGB(255, 255, 255), RGB(186, 186, 186), RGB(72, 159, 255),
    RGB(122, 191, 255), RGB(0, 0, 0),       RGB(8, 8, 8),
    RGB(30, 30, 30),    RGB(255, 255, 255), RGB(92, 92, 92),
    RGB(18, 18, 18),    RGB(42, 42, 42),    RGB(0, 0, 0),
    RGB(18, 18, 18),    RGB(18, 18, 18)};

static const ThemePalette *ResolvePalette() {
  switch (g_themeId) {
  case APP_THEME_GRAPHITE:
    return g_isDarkMode ? &kGraphiteDark : &kGraphiteLight;
  case APP_THEME_WARM:
    return g_isDarkMode ? &kWarmDark : &kWarmLight;
  case APP_THEME_HIGH_CONTRAST:
    return g_isDarkMode ? &kHighContrastDark : &kHighContrastLight;
  case APP_THEME_CLASSIC:
  default:
    return g_isDarkMode ? &kClassicDark : &kClassicLight;
  }
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

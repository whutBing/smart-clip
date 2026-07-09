#pragma once

#include "hotkey.h"
#include <windows.h>

enum ThemeId {
  APP_THEME_CLASSIC = 0,
  APP_THEME_GRAPHITE = 1,
  APP_THEME_WARM = 2,
  APP_THEME_HIGH_CONTRAST = 3
};

struct ThemePalette {
  COLORREF windowBg;
  COLORREF surface;
  COLORREF surfaceAlt;
  COLORREF textPrimary;
  COLORREF textSecondary;
  COLORREF accent;
  COLORREF accentStrong;
  COLORREF titlebarBg;
  COLORREF sidebarBg;
  COLORREF sidebarHover;
  COLORREF separator;
  COLORREF toggleOff;
  COLORREF inputBg;
  COLORREF dropdownHover;
  COLORREF dialogBg;
  COLORREF dialogCardBg;
  COLORREF dialogEditBg;
};

extern ThemeId g_themeId;

const ThemePalette &GetCurrentThemePalette();
COLORREF GetThemeWindowBgColor();
COLORREF GetThemeSurfaceColor();
COLORREF GetThemeSurfaceAltColor();
COLORREF GetThemeTextPrimaryColor();
COLORREF GetThemeTextSecondaryColor();
COLORREF GetThemeAccentColor();
COLORREF GetThemeAccentStrongColor();
COLORREF GetThemeTitlebarBgColor();
COLORREF GetThemeSidebarBgColor();
COLORREF GetThemeSidebarHoverColor();
COLORREF GetThemeSeparatorColor();
COLORREF GetThemeToggleOffColor();
COLORREF GetThemeInputBgColor();
COLORREF GetThemeDropdownHoverColor();
COLORREF GetThemeDialogBgColor();
COLORREF GetThemeDialogCardBgColor();
COLORREF GetThemeDialogEditBgColor();


#include "hotkey.h"
#include "history.h"
#include "i18n.h"
#include "settings.h"
#include "theme.h"
#include "tray.h"
#include <string>

// 全局变量定义
int g_hotkeyId = 1;
bool g_isHotkeyEnabled = false;
UINT g_hotkeyVirtualKey = 0; // 默认未设置
UINT g_hotkeyModifiers = 0;  // 默认未设置

// 搜索框快捷键全局变量定义
bool g_isSearchHotkeyEnabled = true;        // 默认启用
UINT g_searchHotkeyVirtualKey = 'F';        // 默认F键
UINT g_searchHotkeyModifiers = MOD_CONTROL; // 默认Ctrl

// 历史记录数量限制
int g_maxHistoryCount = 100; // 默认100条

// 保存快捷键设置到 SQLite
void SaveHotkeySettings() {
  DbSetSettingInt("hotkey_enabled", g_isHotkeyEnabled ? 1 : 0);
  DbSetSettingInt("hotkey_modifiers", (int)g_hotkeyModifiers);
  DbSetSettingInt("hotkey_vk", (int)g_hotkeyVirtualKey);
  DbSetSettingInt("notification_enabled", g_isNotificationEnabled ? 1 : 0);
  DbSetSettingInt("search_hotkey_enabled", g_isSearchHotkeyEnabled ? 1 : 0);
  DbSetSettingInt("search_hotkey_modifiers", (int)g_searchHotkeyModifiers);
  DbSetSettingInt("search_hotkey_vk", (int)g_searchHotkeyVirtualKey);
  DbSetSettingInt("theme_mode", (int)g_themeMode);
  DbSetSettingInt("smooth_scroll", g_isSmoothScrollEnabled ? 1 : 0);
  DbSetSettingInt("image_preview_quality", (int)g_imagePreviewQuality);
  DbSetSettingInt("max_history_count", g_maxHistoryCount);
  DbSetSettingInt("custom_scrollbar", g_isCustomScrollbarEnabled ? 1 : 0);
  DbSetSettingInt("scrollbar_hide_delay", g_customScrollbarHideDelayMs);
  DbSetSettingInt("color_dot", g_isColorDotEnabled ? 1 : 0);
  DbSetSettingInt("theme_id", (int)g_themeId);
  DbSetSettingInt("language", (int)g_appLanguage);
  DbSetSettingInt("quick_paste_modifiers", (int)g_quickPasteModifiers);
  DbSetSettingInt("taskbar_visible", g_isTaskbarVisible ? 1 : 0);
  DbSetSettingInt("favorite_hotkey_modifiers",
                  (int)g_favoriteHotkeyModifiers);
  DbSetSettingInt("max_text_size_kb", g_maxTextSizeKB);
  DbSetSettingInt("quick_paste_enabled", g_isQuickPasteEnabled ? 1 : 0);
  DbSetSettingInt("all_hotkeys_enabled", g_allHotkeysEnabled ? 1 : 0);
  DbSetSettingInt("window_topmost", g_isTopmost ? 1 : 0);
  DbSetSettingInt("settings_last_tab", g_currentSettingsTab);

  RefreshTrayTooltip();
}

// 根据系统区域检测语言（首次运行或旧配置无 language 行时使用）
static void DetectSystemLanguage() {
  wchar_t localeName[LOCALE_NAME_MAX_LENGTH] = {};
  bool gotLocale = false;
  LANGID uiLang = GetUserDefaultUILanguage();
  wchar_t uiLocaleName[LOCALE_NAME_MAX_LENGTH] = {};
  if (LCIDToLocaleName(MAKELCID(uiLang, SORT_DEFAULT), uiLocaleName,
                       LOCALE_NAME_MAX_LENGTH, 0) > 0) {
    wcscpy_s(localeName, uiLocaleName);
    gotLocale = true;
  } else if (GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH) > 0) {
    gotLocale = true;
  }
  if (gotLocale) {
    if (_wcsicmp(localeName, L"zh-CN") == 0 ||
        _wcsicmp(localeName, L"zh-Hans-CN") == 0 ||
        _wcsicmp(localeName, L"zh-SG") == 0 ||
        _wcsicmp(localeName, L"zh-Hans-SG") == 0 ||
        _wcsicmp(localeName, L"zh-Hans") == 0 ||
        _wcsicmp(localeName, L"zh") == 0) {
      g_appLanguage = LANG_ZH_CN;
    } else if (_wcsicmp(localeName, L"ja-JP") == 0 ||
               _wcsicmp(localeName, L"ja") == 0) {
      g_appLanguage = LANG_JA_JP;
    } else if (_wcsicmp(localeName, L"ko-KR") == 0 ||
               _wcsicmp(localeName, L"ko") == 0) {
      g_appLanguage = LANG_KO_KR;
    } else if (_wcsicmp(localeName, L"de-DE") == 0 ||
               _wcsicmp(localeName, L"de-AT") == 0 ||
               _wcsicmp(localeName, L"de-CH") == 0 ||
               _wcsicmp(localeName, L"de") == 0) {
      g_appLanguage = LANG_DE_DE;
    } else if (_wcsicmp(localeName, L"ar-SA") == 0 ||
               _wcsicmp(localeName, L"ar-EG") == 0 ||
               _wcsicmp(localeName, L"ar-AE") == 0 ||
               _wcsicmp(localeName, L"ar") == 0) {
      g_appLanguage = LANG_AR_SA;
    } else if (_wcsicmp(localeName, L"tr-TR") == 0 ||
               _wcsicmp(localeName, L"tr") == 0) {
      g_appLanguage = LANG_TR_TR;
    } else {
      g_appLanguage = LANG_EN_US;
    }
  } else {
    g_appLanguage = LANG_EN_US;
  }
}

// 从旧版 _hotkey.txt 迁移到 SQLite（仅解析设置全局变量，不保存/删除文件）
static bool MigrateHotkeyFromTxt(bool &languageLoaded) {
  languageLoaded = false;

  std::wstring filePath = GetDataFilePath();
  size_t dotPos = filePath.rfind(L'.');
  if (dotPos != std::wstring::npos)
    filePath = filePath.substr(0, dotPos) + L"_hotkey.txt";

  HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE)
    return false;

  DWORD dwFileSize = GetFileSize(hFile, NULL);
  if (dwFileSize == 0 || dwFileSize == INVALID_FILE_SIZE) {
    CloseHandle(hFile);
    return false;
  }

  std::vector<BYTE> fileContent(dwFileSize);
  DWORD dwBytesRead = 0;
  bool ok = false;
  if (ReadFile(hFile, &fileContent[0], dwFileSize, &dwBytesRead, NULL)) {
    int unicodeLength = MultiByteToWideChar(
        CP_UTF8, 0, (LPCSTR)(&fileContent[0]), dwFileSize, NULL, 0);
    if (unicodeLength > 0) {
      std::vector<wchar_t> unicodeContent(unicodeLength + 1);
      MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)(&fileContent[0]), dwFileSize,
                          &unicodeContent[0], unicodeLength);
      unicodeContent[unicodeLength] = L'\0';

      wchar_t *pLine = &unicodeContent[0];
      wchar_t *pNextLine = wcsstr(pLine, L"\n");
      if (pNextLine) { *pNextLine = L'\0'; pNextLine++; }

      // 第一行：enabled|modifiers|virtualKey
      wchar_t *pDelim = wcsstr(pLine, L"|");
      if (pDelim) {
        *pDelim = L'\0';
        g_isHotkeyEnabled = (wcstol(pLine, NULL, 10) != 0);
        pLine = pDelim + 1;
        pDelim = wcsstr(pLine, L"|");
        if (pDelim) {
          *pDelim = L'\0';
          g_hotkeyModifiers = (UINT)wcstol(pLine, NULL, 10);
          pLine = pDelim + 1;
          g_hotkeyVirtualKey = (UINT)wcstol(pLine, NULL, 10);
          if (g_hotkeyModifiers == 0 || g_hotkeyVirtualKey == 0) {
            g_isHotkeyEnabled = false;
            g_hotkeyModifiers = 0;
            g_hotkeyVirtualKey = 0;
          }
          ok = true;
        }
      }

      // 第二行：searchEnabled|searchModifiers|searchVirtualKey
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        pNextLine = wcsstr(pLine, L"\n");
        if (pNextLine) { *pNextLine = L'\0'; pNextLine++; }
        pDelim = wcsstr(pLine, L"|");
        if (pDelim) {
          *pDelim = L'\0';
          g_isSearchHotkeyEnabled = (wcstol(pLine, NULL, 10) != 0);
          pLine = pDelim + 1;
          pDelim = wcsstr(pLine, L"|");
          if (pDelim) {
            *pDelim = L'\0';
            g_searchHotkeyModifiers = (UINT)wcstol(pLine, NULL, 10);
            pLine = pDelim + 1;
            g_searchHotkeyVirtualKey = (UINT)wcstol(pLine, NULL, 10);
          }
        }
      }

      // 第三行：兼容占位
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        pNextLine = wcsstr(pLine, L"\n");
        if (pNextLine) { *pNextLine = L'\0'; pNextLine++; }
      }

      // 第四行：themeMode
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        wchar_t *pEnd = wcsstr(pLine, L"\n");
        if (pEnd) { *pEnd = L'\0'; pNextLine = pEnd + 1; } else pNextLine = NULL;
        pEnd = wcsstr(pLine, L"\r"); if (pEnd) *pEnd = L'\0';
        int tv = (int)wcstol(pLine, NULL, 10);
        if (tv >= 0 && tv <= 2) g_themeMode = (ThemeMode)tv;
      }

      // 第五行：smoothScrollEnabled
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        wchar_t *pEnd = wcsstr(pLine, L"\n");
        if (pEnd) { *pEnd = L'\0'; pNextLine = pEnd + 1; } else pNextLine = NULL;
        pEnd = wcsstr(pLine, L"\r"); if (pEnd) *pEnd = L'\0';
        g_isSmoothScrollEnabled = (wcstol(pLine, NULL, 10) != 0);
      }

      // 第六行：imagePreviewQuality
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        wchar_t *pEnd = wcsstr(pLine, L"\n");
        if (pEnd) { *pEnd = L'\0'; pNextLine = pEnd + 1; } else pNextLine = NULL;
        pEnd = wcsstr(pLine, L"\r"); if (pEnd) *pEnd = L'\0';
        int qv = (int)wcstol(pLine, NULL, 10);
        if (qv >= 0 && qv <= 3) g_imagePreviewQuality = (ImagePreviewQuality)qv;
      }

      // 第七行：maxHistoryCount
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        wchar_t *pEnd = wcsstr(pLine, L"\n");
        if (pEnd) { *pEnd = L'\0'; pNextLine = pEnd + 1; } else pNextLine = NULL;
        pEnd = wcsstr(pLine, L"\r"); if (pEnd) *pEnd = L'\0';
        int hc = (int)wcstol(pLine, NULL, 10);
        if (hc >= 10 && hc <= 10000) g_maxHistoryCount = hc;
      }

      // 第八行：customScrollbarEnabled
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        wchar_t *pEnd = wcsstr(pLine, L"\n");
        if (pEnd) { *pEnd = L'\0'; pNextLine = pEnd + 1; } else pNextLine = NULL;
        pEnd = wcsstr(pLine, L"\r"); if (pEnd) *pEnd = L'\0';
        g_isCustomScrollbarEnabled = (wcstol(pLine, NULL, 10) != 0);
      }

      // 第九行：customScrollbarHideDelayMs
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        wchar_t *pEnd = wcsstr(pLine, L"\n");
        if (pEnd) { *pEnd = L'\0'; pNextLine = pEnd + 1; } else pNextLine = NULL;
        pEnd = wcsstr(pLine, L"\r"); if (pEnd) *pEnd = L'\0';
        int hd = (int)wcstol(pLine, NULL, 10);
        if (hd >= 600 && hd <= 2000) g_customScrollbarHideDelayMs = hd;
      }

      // 第十行：colorDotEnabled
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        wchar_t *pEnd = wcsstr(pLine, L"\n");
        if (pEnd) { *pEnd = L'\0'; pNextLine = pEnd + 1; } else pNextLine = NULL;
        pEnd = wcsstr(pLine, L"\r"); if (pEnd) *pEnd = L'\0';
        g_isColorDotEnabled = (wcstol(pLine, NULL, 10) != 0);
      }

      // 第十一行：themeId
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        wchar_t *pEnd = wcsstr(pLine, L"\n");
        if (pEnd) { *pEnd = L'\0'; pNextLine = pEnd + 1; } else pNextLine = NULL;
        pEnd = wcsstr(pLine, L"\r"); if (pEnd) *pEnd = L'\0';
        int tid = (int)wcstol(pLine, NULL, 10);
        if (tid >= 0 && tid <= 3) g_themeId = (ThemeId)tid;
      }

      // 第十二行：language
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        wchar_t *pEnd = wcsstr(pLine, L"\n");
        if (pEnd) { *pEnd = L'\0'; pNextLine = pEnd + 1; } else pNextLine = NULL;
        pEnd = wcsstr(pLine, L"\r"); if (pEnd) *pEnd = L'\0';
        int lv = (int)wcstol(pLine, NULL, 10);
        if (lv >= 0 && lv < (int)LANG_COUNT) {
          g_appLanguage = (AppLanguage)lv;
          languageLoaded = true;
        }
      }

      // 第十三行：quickPasteModifiers
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        wchar_t *pEnd = wcsstr(pLine, L"\n");
        if (pEnd) { *pEnd = L'\0'; pNextLine = pEnd + 1; } else pNextLine = NULL;
        pEnd = wcsstr(pLine, L"\r"); if (pEnd) *pEnd = L'\0';
        UINT mv = (UINT)wcstol(pLine, NULL, 10);
        if (mv != 0) g_quickPasteModifiers = mv;
      }

      // 第十四行：taskbarVisible
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        pNextLine = wcsstr(pLine, L"\n");
        if (pNextLine) { *pNextLine = L'\0'; pNextLine++; }
        wchar_t *pCR = wcsstr(pLine, L"\r"); if (pCR) *pCR = L'\0';
        if (wcsstr(pLine, L"|") == NULL)
          g_isTaskbarVisible = (wcstol(pLine, NULL, 10) != 0);
      }

      // 第十五行：favoriteHotkeyModifiers
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        wchar_t *pEnd = wcsstr(pLine, L"\n");
        if (pEnd) { *pEnd = L'\0'; pNextLine = pEnd + 1; } else pNextLine = NULL;
        pEnd = wcsstr(pLine, L"\r"); if (pEnd) *pEnd = L'\0';
        UINT fv = (UINT)wcstol(pLine, NULL, 10);
        if (fv != 0) g_favoriteHotkeyModifiers = fv;
      }

      // 第十六行：maxTextSizeKB
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        wchar_t *pEnd = wcsstr(pLine, L"\n");
        if (pEnd) { *pEnd = L'\0'; pNextLine = pEnd + 1; } else pNextLine = NULL;
        pEnd = wcsstr(pLine, L"\r"); if (pEnd) *pEnd = L'\0';
        int sk = (int)wcstol(pLine, NULL, 10);
        if (sk >= 1 && sk <= 10240) g_maxTextSizeKB = sk;
      }

      // 第十七行：isQuickPasteEnabled
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        wchar_t *pEnd = wcsstr(pLine, L"\n");
        if (pEnd) { *pEnd = L'\0'; pNextLine = pEnd + 1; } else pNextLine = NULL;
        pEnd = wcsstr(pLine, L"\r"); if (pEnd) *pEnd = L'\0';
        g_isQuickPasteEnabled = (wcstol(pLine, NULL, 10) != 0);
      }

      // 第十八行：allHotkeysEnabled
      if (pNextLine && *pNextLine) {
        pLine = pNextLine;
        wchar_t *pEnd = wcsstr(pLine, L"\n");
        if (pEnd) { *pEnd = L'\0'; pNextLine = pEnd + 1; } else pNextLine = NULL;
        pEnd = wcsstr(pLine, L"\r"); if (pEnd) *pEnd = L'\0';
        g_allHotkeysEnabled = (wcstol(pLine, NULL, 10) != 0);
      }
    }
  }

  CloseHandle(hFile);
  return ok;
}

// 从 SQLite 加载快捷键设置
void LoadHotkeySettings() {
  // 检查数据库中是否有设置
  std::wstring dummy;
  bool dbHasSettings = DbGetSetting("hotkey_enabled", dummy);

  if (!dbHasSettings) {
    // 数据库无设置，尝试从旧 _hotkey.txt 迁移
    bool languageLoaded = false;
    bool migrated = MigrateHotkeyFromTxt(languageLoaded);

    if (!migrated) {
      // 无旧文件也无数据库设置，使用默认值
      g_isHotkeyEnabled = false;
      g_hotkeyModifiers = 0;
      g_hotkeyVirtualKey = 0;
      g_isSearchHotkeyEnabled = true;
      g_searchHotkeyModifiers = MOD_CONTROL;
      g_searchHotkeyVirtualKey = 'F';
    }

    // 若语言未从配置加载，根据系统区域检测
    if (!languageLoaded)
      DetectSystemLanguage();

    // 保存到数据库
    SaveHotkeySettings();

    // 迁移成功则删除旧文件
    if (migrated) {
      std::wstring filePath = GetDataFilePath();
      size_t dotPos = filePath.rfind(L'.');
      if (dotPos != std::wstring::npos)
        filePath = filePath.substr(0, dotPos) + L"_hotkey.txt";
      DeleteFileW(filePath.c_str());
    }
    return;
  }

  // 从数据库加载
  g_isHotkeyEnabled = DbGetSettingInt("hotkey_enabled", 0) != 0;
  g_hotkeyModifiers = (UINT)DbGetSettingInt("hotkey_modifiers", 0);
  g_hotkeyVirtualKey = (UINT)DbGetSettingInt("hotkey_vk", 0);
  g_isNotificationEnabled = DbGetSettingInt("notification_enabled", 0) != 0;
  if (g_hotkeyModifiers == 0 || g_hotkeyVirtualKey == 0) {
    g_isHotkeyEnabled = false;
    g_hotkeyModifiers = 0;
    g_hotkeyVirtualKey = 0;
  }

  g_isSearchHotkeyEnabled = DbGetSettingInt("search_hotkey_enabled", 1) != 0;
  g_searchHotkeyModifiers =
      (UINT)DbGetSettingInt("search_hotkey_modifiers", MOD_CONTROL);
  g_searchHotkeyVirtualKey = (UINT)DbGetSettingInt("search_hotkey_vk", 'F');

  int tv = DbGetSettingInt("theme_mode", (int)THEME_SYSTEM);
  if (tv >= 0 && tv <= 2)
    g_themeMode = (ThemeMode)tv;
  g_isSmoothScrollEnabled = DbGetSettingInt("smooth_scroll", 0) != 0;
  int qv = DbGetSettingInt("image_preview_quality", (int)PREVIEW_HD);
  if (qv >= 0 && qv <= 3)
    g_imagePreviewQuality = (ImagePreviewQuality)qv;
  g_maxHistoryCount = DbGetSettingInt("max_history_count", 100);
  g_isCustomScrollbarEnabled = DbGetSettingInt("custom_scrollbar", 1) != 0;
  g_customScrollbarHideDelayMs = DbGetSettingInt("scrollbar_hide_delay", 1500);
  g_isColorDotEnabled = DbGetSettingInt("color_dot", 1) != 0;
  int tid = DbGetSettingInt("theme_id", (int)APP_THEME_HIGH_CONTRAST);
  if (tid >= 0 && tid <= 3)
    g_themeId = (ThemeId)tid;

  int lv = DbGetSettingInt("language", (int)LANG_ZH_CN);
  if (lv >= 0 && lv < (int)LANG_COUNT)
    g_appLanguage = (AppLanguage)lv;

  g_quickPasteModifiers =
      (UINT)DbGetSettingInt("quick_paste_modifiers", MOD_ALT);
  g_isTaskbarVisible = DbGetSettingInt("taskbar_visible", 1) != 0;
  g_favoriteHotkeyModifiers =
      (UINT)DbGetSettingInt("favorite_hotkey_modifiers",
                            MOD_CONTROL | MOD_ALT);
  g_maxTextSizeKB = DbGetSettingInt("max_text_size_kb", 50);
  g_isQuickPasteEnabled = DbGetSettingInt("quick_paste_enabled", 1) != 0;
  g_allHotkeysEnabled = DbGetSettingInt("all_hotkeys_enabled", 1) != 0;
  g_isTopmost = DbGetSettingInt("window_topmost", 0) != 0;

  int savedSettingsTab = DbGetSettingInt("settings_last_tab", 0);
  if (savedSettingsTab >= 0 && savedSettingsTab < 4)
    g_currentSettingsTab = savedSettingsTab;
}

// 注册快捷键
bool RegisterHotkey(HWND hwnd) {
  bool success = true;
  if (g_isHotkeyEnabled) {
    if (RegisterHotKey(hwnd, ID_HOTKEY_TOGGLE, g_hotkeyModifiers,
                       g_hotkeyVirtualKey)) {
      success = true;
    } else {
      success = false;
    }
  }
  return success;
}

// 注销快捷键
void UnregisterHotkey(HWND hwnd) { ::UnregisterHotKey(hwnd, ID_HOTKEY_TOGGLE); }

// 切换快捷键状态
void ToggleHotkey(HWND hwnd) {
  g_isHotkeyEnabled = !g_isHotkeyEnabled;
  if (g_isHotkeyEnabled) {
    RegisterHotkey(hwnd);
  } else {
    UnregisterHotkey(hwnd);
  }

  // 保存快捷键设置
  SaveHotkeySettings();

  // 显示提示
  ShowTrayBalloon(hwnd, T(STR_TRAY_HOTKEY_SETTINGS),
                  g_isHotkeyEnabled ? L"快捷键已启用" : L"快捷键已禁用",
                  g_isHotkeyEnabled ? NIIF_INFO : NIIF_WARNING);
}

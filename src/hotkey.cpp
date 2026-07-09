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
UINT g_hotkeyVirtualKey = 0;                    // 默认未设置
UINT g_hotkeyModifiers = 0;                     // 默认未设置

// 搜索框快捷键全局变量定义
bool g_isSearchHotkeyEnabled = true;        // 默认启用
UINT g_searchHotkeyVirtualKey = 'F';        // 默认F键
UINT g_searchHotkeyModifiers = MOD_CONTROL; // 默认Ctrl

// 历史记录数量限制
int g_maxHistoryCount = 100; // 默认100条

// 保存快捷键设置
void SaveHotkeySettings() {
  std::wstring filePath = GetDataFilePath();
  // 用不同的文件名保存快捷键设置
  size_t dotPos = filePath.rfind(L'.');
  if (dotPos != std::wstring::npos) {
    filePath = filePath.substr(0, dotPos) + L"_hotkey.txt";
  }

  // 保存快捷键设置到文件
  std::wstring content = std::to_wstring(g_isHotkeyEnabled) + L"|" +
                         std::to_wstring(g_hotkeyModifiers) + L"|" +
                         std::to_wstring(g_hotkeyVirtualKey) + L"\n" +
                         std::to_wstring(g_isSearchHotkeyEnabled) + L"|" +
                         std::to_wstring(g_searchHotkeyModifiers) + L"|" +
                         std::to_wstring(g_searchHotkeyVirtualKey) + L"\n" +
                         L"0|0\n" +
                         std::to_wstring((int)g_themeMode) + L"\n" +
                         std::to_wstring(g_isSmoothScrollEnabled) + L"\n" +
                         std::to_wstring((int)g_imagePreviewQuality) + L"\n" +
                         std::to_wstring(g_maxHistoryCount) + L"\n" +
                         std::to_wstring(g_isCustomScrollbarEnabled) + L"\n" +
                         std::to_wstring(g_customScrollbarHideDelayMs) + L"\n" +
                         std::to_wstring(g_isColorDotEnabled) + L"\n" +
                         std::to_wstring((int)g_themeId) + L"\n" +
                         std::to_wstring((int)g_appLanguage) + L"\n" +
                         std::to_wstring((int)g_quickPasteModifiers) + L"\n" +
                         std::to_wstring(g_isTaskbarVisible) + L"\n" +
                         std::to_wstring((int)g_favoriteHotkeyModifiers) + L"\n";

  // 转换为UTF-8
  int utf8Length = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, NULL,
                                       0, NULL, NULL);
  if (utf8Length <= 0)
    return;

  std::vector<char> utf8Content(utf8Length);
  WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, &utf8Content[0],
                      utf8Length, NULL, NULL);

  // 确保数据目录存在（递归创建，处理用户手动删除目录的情况）
  size_t slashPos = filePath.find_last_of(L"\\");
  if (slashPos != std::wstring::npos) {
    std::wstring dir = filePath.substr(0, slashPos);
    // 递归创建目录链
    std::wstring curPath;
    size_t start = 0;
    while (start < dir.length()) {
      size_t next = dir.find(L'\\', start);
      if (next == std::wstring::npos) {
        curPath = dir;
      } else {
        curPath = dir.substr(0, next);
      }
      if (!curPath.empty() && curPath.length() > 2) {
        CreateDirectoryW(curPath.c_str(), NULL);
      }
      if (next == std::wstring::npos)
        break;
      start = next + 1;
    }
    // 最后确保完整目录存在
    CreateDirectoryW(dir.c_str(), NULL);
  }

  HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile != INVALID_HANDLE_VALUE) {
    DWORD dwBytesWritten = 0;
    WriteFile(hFile, &utf8Content[0], utf8Length - 1, &dwBytesWritten, NULL);
    FlushFileBuffers(hFile);
    CloseHandle(hFile);
  }

  // 快捷键变更后刷新托盘提示（显示/隐藏切换快捷键行）
  RefreshTrayTooltip();
}

// 加载快捷键设置
void LoadHotkeySettings() {
  std::wstring filePath = GetDataFilePath();
  // 用不同的文件名加载快捷键设置
  size_t dotPos = filePath.rfind(L'.');
  if (dotPos != std::wstring::npos) {
    filePath = filePath.substr(0, dotPos) + L"_hotkey.txt";
  }

  // 读取保存的设置
  bool settingsLoaded = false;
  bool searchSettingsLoaded = false;
  HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile != INVALID_HANDLE_VALUE) {
    DWORD dwFileSize = GetFileSize(hFile, NULL);
    if (dwFileSize > 0) {
      std::vector<BYTE> fileContent(dwFileSize);
      DWORD dwBytesRead = 0;
      if (ReadFile(hFile, &fileContent[0], dwFileSize, &dwBytesRead, NULL)) {
        // 转换为Unicode
        int unicodeLength = MultiByteToWideChar(
            CP_UTF8, 0, (LPCSTR)(&fileContent[0]), dwFileSize, NULL, 0);
        if (unicodeLength > 0) {
          std::vector<wchar_t> unicodeContent(unicodeLength + 1);
          MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)(&fileContent[0]), dwFileSize,
                              &unicodeContent[0], unicodeLength);
          unicodeContent[unicodeLength] = L'\0';

          // 解析第一行：enabled|modifiers|virtualKey
          wchar_t *pLine = &unicodeContent[0];
          wchar_t *pNextLine = wcsstr(pLine, L"\n");
          if (pNextLine != NULL) {
            *pNextLine = L'\0';
            pNextLine++;
          }

          wchar_t *pDelim = wcsstr(pLine, L"|");
          if (pDelim != NULL) {
            *pDelim = L'\0';
            g_isHotkeyEnabled = (wcstol(pLine, NULL, 10) != 0);

            pLine = pDelim + 1;
            pDelim = wcsstr(pLine, L"|");
            if (pDelim != NULL) {
              *pDelim = L'\0';
              g_hotkeyModifiers = (UINT)wcstol(pLine, NULL, 10);

              pLine = pDelim + 1;
              g_hotkeyVirtualKey = (UINT)wcstol(pLine, NULL, 10);
              // 仅当修饰键或虚拟键无效时才重置（保留禁用状态的快捷键配置）
              if (g_hotkeyModifiers == 0 || g_hotkeyVirtualKey == 0) {
                g_isHotkeyEnabled = false;
                g_hotkeyModifiers = 0;
                g_hotkeyVirtualKey = 0;
              }
              settingsLoaded = true;
            }
          }

          // 解析第二行：searchEnabled|searchModifiers|searchVirtualKey
          if (pNextLine != NULL && *pNextLine != L'\0') {
            pLine = pNextLine;
            pNextLine = wcsstr(pLine, L"\n");
            if (pNextLine != NULL) {
              *pNextLine = L'\0';
              pNextLine++;
            }

            pDelim = wcsstr(pLine, L"|");
            if (pDelim != NULL) {
              *pDelim = L'\0';
              g_isSearchHotkeyEnabled = (wcstol(pLine, NULL, 10) != 0);

              pLine = pDelim + 1;
              pDelim = wcsstr(pLine, L"|");
              if (pDelim != NULL) {
                *pDelim = L'\0';
                g_searchHotkeyModifiers = (UINT)wcstol(pLine, NULL, 10);

                pLine = pDelim + 1;
                g_searchHotkeyVirtualKey = (UINT)wcstol(pLine, NULL, 10);
                searchSettingsLoaded = true;
              }
            }
          }

          // 解析第三行：保留兼容占位
          if (pNextLine != NULL && *pNextLine != L'\0') {
            pLine = pNextLine;
            pNextLine = wcsstr(pLine, L"\n");
            if (pNextLine != NULL) {
              *pNextLine = L'\0';
              pNextLine++;
            }

          }

          // 解析第四行：themeMode
          if (pNextLine != NULL && *pNextLine != L'\0') {
            pLine = pNextLine;
            // 去除可能的换行符
            wchar_t *pEnd = wcsstr(pLine, L"\n");
            if (pEnd != NULL) {
              *pEnd = L'\0';
              pNextLine = pEnd + 1;
            } else {
              pNextLine = NULL;
            }
            pEnd = wcsstr(pLine, L"\r");
            if (pEnd != NULL)
              *pEnd = L'\0';

            int themeValue = (int)wcstol(pLine, NULL, 10);
            if (themeValue >= 0 && themeValue <= 2) {
              g_themeMode = (ThemeMode)themeValue;
            }
          }

          // 解析第五行：smoothScrollEnabled
          if (pNextLine != NULL && *pNextLine != L'\0') {
            pLine = pNextLine;
            // 去除可能的换行符
            wchar_t *pEnd = wcsstr(pLine, L"\n");
            if (pEnd != NULL) {
              *pEnd = L'\0';
              pNextLine = pEnd + 1;
            } else {
              pNextLine = NULL;
            }
            pEnd = wcsstr(pLine, L"\r");
            if (pEnd != NULL)
              *pEnd = L'\0';

            g_isSmoothScrollEnabled = (wcstol(pLine, NULL, 10) != 0);
          }

          // 解析第六行：imagePreviewQuality
          if (pNextLine != NULL && *pNextLine != L'\0') {
            pLine = pNextLine;
            // 去除可能的换行符
            wchar_t *pEnd = wcsstr(pLine, L"\n");
            if (pEnd != NULL) {
              *pEnd = L'\0';
              pNextLine = pEnd + 1;
            } else {
              pNextLine = NULL;
            }
            pEnd = wcsstr(pLine, L"\r");
            if (pEnd != NULL)
              *pEnd = L'\0';

            int qualityValue = (int)wcstol(pLine, NULL, 10);
            if (qualityValue >= 0 && qualityValue <= 3) {
              g_imagePreviewQuality = (ImagePreviewQuality)qualityValue;
            }
          }

          // 解析第七行：maxHistoryCount
          if (pNextLine != NULL && *pNextLine != L'\0') {
            pLine = pNextLine;
            wchar_t *pEnd = wcsstr(pLine, L"\n");
            if (pEnd != NULL) {
              *pEnd = L'\0';
              pNextLine = pEnd + 1;
            } else {
              pNextLine = NULL;
            }
            pEnd = wcsstr(pLine, L"\r");
            if (pEnd != NULL)
              *pEnd = L'\0';

            int historyCount = (int)wcstol(pLine, NULL, 10);
            if (historyCount >= 10 && historyCount <= 10000) {
              g_maxHistoryCount = historyCount;
            }
          }

          // 解析第八行：customScrollbarEnabled
          if (pNextLine != NULL && *pNextLine != L'\0') {
            pLine = pNextLine;
            wchar_t *pEnd = wcsstr(pLine, L"\n");
            if (pEnd != NULL) {
              *pEnd = L'\0';
              pNextLine = pEnd + 1;
            } else {
              pNextLine = NULL;
            }
            pEnd = wcsstr(pLine, L"\r");
            if (pEnd != NULL)
              *pEnd = L'\0';

            g_isCustomScrollbarEnabled = (wcstol(pLine, NULL, 10) != 0);
          }

          // 解析第九行：customScrollbarHideDelayMs
          if (pNextLine != NULL && *pNextLine != L'\0') {
            pLine = pNextLine;
            wchar_t *pEnd = wcsstr(pLine, L"\n");
            if (pEnd != NULL) {
              *pEnd = L'\0';
              pNextLine = pEnd + 1;
            } else {
              pNextLine = NULL;
            }
            pEnd = wcsstr(pLine, L"\r");
            if (pEnd != NULL)
              *pEnd = L'\0';

            int hideDelay = (int)wcstol(pLine, NULL, 10);
            if (hideDelay >= 600 && hideDelay <= 2000) {
              g_customScrollbarHideDelayMs = hideDelay;
            }
          }

          // 解析第十行：colorDotEnabled
          if (pNextLine != NULL && *pNextLine != L'\0') {
            pLine = pNextLine;
            wchar_t *pEnd = wcsstr(pLine, L"\n");
            if (pEnd != NULL) {
              *pEnd = L'\0';
              pNextLine = pEnd + 1;
            } else {
              pNextLine = NULL;
            }
            pEnd = wcsstr(pLine, L"\r");
            if (pEnd != NULL)
              *pEnd = L'\0';
            g_isColorDotEnabled = (wcstol(pLine, NULL, 10) != 0);
          }

          // 解析第十一行：themeId（旧配置可能不存在）
          if (pNextLine != NULL && *pNextLine != L'\0') {
            pLine = pNextLine;
            wchar_t *pEnd = wcsstr(pLine, L"\n");
            if (pEnd != NULL) {
              *pEnd = L'\0';
              pNextLine = pEnd + 1;
            } else {
              pNextLine = NULL;
            }
            pEnd = wcsstr(pLine, L"\r");
            if (pEnd != NULL)
              *pEnd = L'\0';
            g_themeId = APP_THEME_HIGH_CONTRAST;
          }

          // 解析第十二行：language（旧配置可能不存在）
          if (pNextLine != NULL && *pNextLine != L'\0') {
            pLine = pNextLine;
            wchar_t *pEnd = wcsstr(pLine, L"\n");
            if (pEnd != NULL) {
              *pEnd = L'\0';
              pNextLine = pEnd + 1;
            } else {
              pNextLine = NULL;
            }
            pEnd = wcsstr(pLine, L"\r");
            if (pEnd != NULL)
              *pEnd = L'\0';
            int languageValue = (int)wcstol(pLine, NULL, 10);
            if (languageValue >= 0 && languageValue <= 1) {
              g_appLanguage = (AppLanguage)languageValue;
            }
          }

          // 解析第十三行：quickPasteModifiers（旧配置可能不存在）
          if (pNextLine != NULL && *pNextLine != L'\0') {
            pLine = pNextLine;
            wchar_t *pEnd = wcsstr(pLine, L"\n");
            if (pEnd != NULL) {
              *pEnd = L'\0';
              pNextLine = pEnd + 1;
            } else {
              pNextLine = NULL;
            }
            pEnd = wcsstr(pLine, L"\r");
            if (pEnd != NULL)
              *pEnd = L'\0';
            UINT modValue = (UINT)wcstol(pLine, NULL, 10);
            if (modValue != 0) {
              g_quickPasteModifiers = modValue;
            }
          }

          // 解析第十四行：taskbarVisible（旧配置为兼容占位 0|0|0）
          if (pNextLine != NULL && *pNextLine != L'\0') {
            pLine = pNextLine;
            pNextLine = wcsstr(pLine, L"\n");
            if (pNextLine != NULL) {
              *pNextLine = L'\0';
              pNextLine++;
            }
            // 去除回车
            wchar_t *pCR = wcsstr(pLine, L"\r");
            if (pCR != NULL)
              *pCR = L'\0';
            // 兼容旧配置的 "0|0|0" 占位格式：如果包含 "|"，则为旧格式，保持默认值
            if (wcsstr(pLine, L"|") == NULL) {
              g_isTaskbarVisible = (wcstol(pLine, NULL, 10) != 0);
            }
          }

          // 解析第十五行：favoriteHotkeyModifiers（旧配置可能不存在）
          if (pNextLine != NULL && *pNextLine != L'\0') {
            pLine = pNextLine;
            wchar_t *pEnd = wcsstr(pLine, L"\n");
            if (pEnd != NULL) {
              *pEnd = L'\0';
              pNextLine = pEnd + 1;
            } else {
              pNextLine = NULL;
            }
            pEnd = wcsstr(pLine, L"\r");
            if (pEnd != NULL)
              *pEnd = L'\0';
            UINT favModValue = (UINT)wcstol(pLine, NULL, 10);
            if (favModValue != 0) {
              g_favoriteHotkeyModifiers = favModValue;
            }
          }
        }
      }
    }

    CloseHandle(hFile);
  } else {
    // 首次运行（配置文件不存在）：非中国大陆地区默认英语
    wchar_t localeName[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH) > 0) {
      if (_wcsicmp(localeName, L"zh-CN") != 0) {
        g_appLanguage = LANG_EN_US;
      }
    }
  }

  // 如果没有加载到设置，使用默认值
  if (!settingsLoaded) {
    g_isHotkeyEnabled = false; // 默认关闭，用户手动开启
    g_hotkeyModifiers = 0;
    g_hotkeyVirtualKey = 0;
  }

  if (!searchSettingsLoaded) {
    g_isSearchHotkeyEnabled = true; // 默认启用
    g_searchHotkeyModifiers = MOD_CONTROL;
    g_searchHotkeyVirtualKey = 'F';
  }

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
void UnregisterHotkey(HWND hwnd) {
  ::UnregisterHotKey(hwnd, ID_HOTKEY_TOGGLE);
}

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
  ShowTrayBalloon(hwnd, L"快捷键设置",
                  g_isHotkeyEnabled ? L"快捷键已启用" : L"快捷键已禁用",
                  g_isHotkeyEnabled ? NIIF_INFO : NIIF_WARNING);
}

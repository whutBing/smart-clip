#include "smart_action.h"
#include "history.h"
#include <fstream>
#include <regex>
#include <shlwapi.h>

std::vector<SmartAction> g_smartActions;

void InitDefaultActions() {
  g_smartActions.clear();

  SmartAction urlRule;
  urlRule.name = L"网址";
  urlRule.pattern = L"^(https?://|ftp://|www\\.)";
  urlRule.action = L"browser";
  urlRule.enabled = true;
  urlRule.isDefault = true;
  g_smartActions.push_back(urlRule);

  SmartAction ipRule;
  ipRule.name = L"IP地址";
  ipRule.pattern = L"^\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}";
  ipRule.action = L"custom";
  ipRule.customCmd = L"";
  ipRule.enabled = true;
  ipRule.isDefault = true;
  g_smartActions.push_back(ipRule);

  SmartAction fileRule;
  fileRule.name = L"文件/文件夹";
  fileRule.pattern = L"^[A-Za-z]:[/\\\\]";
  fileRule.action = L"explorer";
  fileRule.enabled = true;
  fileRule.isDefault = true;
  g_smartActions.push_back(fileRule);
}

// PLACEHOLDER_SMART_ACTION_PERSIST

static std::wstring GetSmartActionsPath() {
  std::wstring filePath = GetDataFilePath();
  size_t lastSlash = filePath.find_last_of(L"\\");
  if (lastSlash == std::wstring::npos)
    return L"";
  return filePath.substr(0, lastSlash) + L"\\smart_actions.txt";
}

void SaveSmartActions() {
  std::wstring path = GetSmartActionsPath();
  if (path.empty())
    return;

  HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE)
    return;

  std::wstring content;
  for (const auto &a : g_smartActions) {
    // format: name|pattern|action|customCmd|enabled|isDefault
    content += a.name + L"|" + a.pattern + L"|" + a.action + L"|" +
               a.customCmd + L"|" + std::to_wstring(a.enabled ? 1 : 0) + L"|" +
               std::to_wstring(a.isDefault ? 1 : 0) + L"\n";
  }

  int utf8Len =
      WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, NULL, 0, NULL, NULL);
  if (utf8Len > 0) {
    std::vector<char> utf8(utf8Len);
    WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, &utf8[0], utf8Len,
                        NULL, NULL);
    DWORD written = 0;
    WriteFile(hFile, &utf8[0], utf8Len - 1, &written, NULL);
  }
  CloseHandle(hFile);
}

void LoadSmartActions() {
  InitDefaultActions();

  std::wstring path = GetSmartActionsPath();
  if (path.empty())
    return;

  HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE)
    return;

  DWORD fileSize = GetFileSize(hFile, NULL);
  if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
    CloseHandle(hFile);
    return;
  }

  std::vector<BYTE> buf(fileSize);
  DWORD bytesRead = 0;
  if (!ReadFile(hFile, &buf[0], fileSize, &bytesRead, NULL)) {
    CloseHandle(hFile);
    return;
  }
  CloseHandle(hFile);

  int wLen =
      MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)&buf[0], fileSize, NULL, 0);
  if (wLen <= 0)
    return;
  std::vector<wchar_t> wBuf(wLen + 1);
  MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)&buf[0], fileSize, &wBuf[0], wLen);
  wBuf[wLen] = L'\0';

  g_smartActions.clear();
  std::wstring data = &wBuf[0];
  size_t pos = 0;
  while (pos < data.size()) {
    size_t eol = data.find(L'\n', pos);
    if (eol == std::wstring::npos)
      eol = data.size();
    std::wstring line = data.substr(pos, eol - pos);
    pos = eol + 1;
    if (line.empty())
      continue;
    // remove \r
    if (!line.empty() && line.back() == L'\r')
      line.pop_back();

    // parse: name|pattern|action|customCmd|enabled|isDefault
    std::vector<std::wstring> parts;
    size_t p = 0;
    for (int i = 0; i < 6; i++) {
      size_t d = line.find(L'|', p);
      if (d == std::wstring::npos) {
        parts.push_back(line.substr(p));
        break;
      }
      parts.push_back(line.substr(p, d - p));
      p = d + 1;
    }
    if (parts.size() >= 6) {
      SmartAction a;
      a.name = parts[0];
      a.pattern = parts[1];
      a.action = parts[2];
      a.customCmd = parts[3];
      a.enabled = (parts[4] == L"1");
      a.isDefault = (parts[5] == L"1");
      g_smartActions.push_back(a);
    }
  }

  // Ensure defaults exist
  if (g_smartActions.empty())
    InitDefaultActions();
}

bool MatchAndExecute(const std::wstring &text) {
  for (const auto &a : g_smartActions) {
    if (!a.enabled)
      continue;
    try {
      std::wregex re(a.pattern, std::regex_constants::icase);
      if (!std::regex_search(text, re))
        continue;
    } catch (...) {
      continue;
    }

    if (a.action == L"browser") {
      std::wstring url = text;
      if (url.find(L"://") == std::wstring::npos &&
          _wcsnicmp(url.c_str(), L"www.", 4) == 0) {
        url = L"http://" + url;
      }
      ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
      return true;
    } else if (a.action == L"explorer") {
      DWORD attrs = GetFileAttributesW(text.c_str());
      if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
          ShellExecuteW(NULL, L"open", text.c_str(), NULL, NULL, SW_SHOWNORMAL);
        } else {
          // Select file in explorer
          std::wstring cmd = L"/select,\"" + text + L"\"";
          ShellExecuteW(NULL, NULL, L"explorer.exe", cmd.c_str(), NULL,
                        SW_SHOWNORMAL);
        }
      }
      return true;
    } else if (a.action == L"custom" && !a.customCmd.empty()) {
      std::wstring cmd = a.customCmd;
      // Replace {0} with matched text
      size_t placeholder = cmd.find(L"{0}");
      if (placeholder != std::wstring::npos) {
        cmd.replace(placeholder, 3, text);
      }
      ShellExecuteW(NULL, L"open", L"cmd.exe",
                     (L"/c " + cmd).c_str(), NULL, SW_SHOWNORMAL);
      return true;
    }
  }
  return false;
}

#include "smart_action.h"
#include "history.h"
#include "themed_dialog.h"
#include <fstream>
#include <regex>
#include <shlwapi.h>

std::vector<SmartAction> g_smartActions;

// ==================== 预设动作模板 ====================

const ActionTemplate g_actionTemplates[] = {
    // 常用
    {L"常用", L"浏览器打开", L"browser", L""},
    {L"常用", L"资源管理器", L"explorer", L""},
    {L"常用", L"打开CMD", L"cmd_template", L"cmd /k {0}"},
    // 搜索（url_template：用浏览器打开，{0:url} 会 URL 编码）
    {L"搜索", L"百度搜索", L"url_template",
     L"https://www.baidu.com/s?wd={0:url}"},
    {L"搜索", L"豆包搜索", L"url_template",
     L"https://www.doubao.com/chat/url-action?action={\"pluginId\":\"Send_"
     L"Message\",\"payload\":{\"text\":\"{0:url}\"}}"},
    // 网络工具
    {L"网络工具", L"Ping", L"cmd_template", L"cmd /k ping {0}"},
    {L"网络工具", L"远程桌面", L"cmd_template", L"mstsc /v:{0}"},
    {L"网络工具", L"远程桌面（账号密码）", L"cmd_template",
     L"cmdkey /generic:TERMSRV/{0} /user:\"{user}\" /pass:\"{pass}\" & mstsc "
     L"/v:{0}"},
    {L"网络工具", L"SSH", L"cmd_template", L"cmd /k ssh {0}"},
    // 开发工具
    {L"开发工具", L"VS Code 打开", L"cmd_template", L"code \"{0}\""},
    {L"开发工具", L"记事本打开", L"cmd_template", L"notepad \"{0}\""},
    // 自定义
    {L"其他", L"自定义命令", L"cmd_template", L""},
    {L"其他", L"自定义URL", L"url_template",
     L"https://www.doubao.com/chat/url-action?action={\"pluginId\":\"Send_"
     L"Message\",\"payload\":{\"text\":\"{0:url}\"}}"},
};
const int g_actionTemplateCount =
    sizeof(g_actionTemplates) / sizeof(g_actionTemplates[0]);

// ==================== URL 编码 ====================

std::wstring UrlEncode(const std::wstring &text) {
  int utf8Len =
      WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, NULL, 0, NULL, NULL);
  if (utf8Len <= 0)
    return text;
  std::vector<char> utf8(utf8Len);
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &utf8[0], utf8Len, NULL,
                      NULL);

  std::wstring result;
  for (int i = 0; i < utf8Len - 1; i++) {
    unsigned char c = (unsigned char)utf8[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      result += (wchar_t)c;
    } else {
      wchar_t hex[4];
      _snwprintf_s(hex, 4, L"%%%02X", c);
      result += hex;
    }
  }
  return result;
}

struct SmartActionParamField {
  std::wstring token;
  std::wstring label;
  std::wstring value;
  bool password;
  int labelId;
  int editId;
  int toggleId;
};

struct SmartActionPromptState {
  bool done;
  bool accepted;
  std::wstring subtitle;
  std::wstring body;
  std::vector<SmartActionParamField> fields;
  std::vector<int> labelIds;
  std::vector<PasswordToggleBinding> toggles;
};

static void ReplaceAll(std::wstring &text, const std::wstring &from,
                       const std::wstring &to) {
  if (from.empty())
    return;
  size_t pos = 0;
  while ((pos = text.find(from, pos)) != std::wstring::npos) {
    text.replace(pos, from.size(), to);
    pos += to.size();
  }
}

static bool IsPasswordPlaceholder(const std::wstring &token) {
  return token == L"pass" || token == L"password" || token == L"pwd";
}

static std::wstring GetPlaceholderLabel(const std::wstring &token) {
  if (token == L"user" || token == L"username")
    return L"用户名";
  if (token == L"pass" || token == L"password" || token == L"pwd")
    return L"密码";
  if (token == L"arg1")
    return L"参数 1";
  if (token == L"arg2")
    return L"参数 2";
  if (token == L"arg3")
    return L"参数 3";
  return token;
}

static std::vector<SmartActionParamField>
CollectTemplateParams(const std::wstring &templateText) {
  std::vector<SmartActionParamField> fields;
  try {
    std::wregex re(L"\\{([A-Za-z][A-Za-z0-9_]*)(:url)?\\}");
    auto begin =
        std::wsregex_iterator(templateText.begin(), templateText.end(), re);
    auto end = std::wsregex_iterator();
    for (auto it = begin; it != end; ++it) {
      std::wstring token = (*it)[1].str();
      bool exists = false;
      for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].token == token) {
          exists = true;
          break;
        }
      }
      if (exists)
        continue;

      SmartActionParamField field = {};
      field.token = token;
      field.label = GetPlaceholderLabel(token);
      field.password = IsPasswordPlaceholder(token);
      field.labelId = 5100 + (int)fields.size();
      field.editId = 5200 + (int)fields.size();
      field.toggleId = 5300 + (int)fields.size();
      fields.push_back(field);
    }
  } catch (...) {
  }
  return fields;
}

static bool PromptSmartActionParams(HWND hwndParent, const SmartAction &action,
                                    const std::wstring &text,
                                    std::vector<SmartActionParamField> *fields) {
  if (!fields || fields->empty())
    return true;

  HINSTANCE hInst = GetModuleHandleW(NULL);
  const int closeBtnId = 5400;

  SmartActionPromptState state = {};
  state.done = false;
  state.accepted = false;
  state.subtitle = L"为“" + action.name + L"”补充执行参数";
  state.body = L"匹配内容：" + text;
  state.fields = *fields;
  for (size_t i = 0; i < state.fields.size(); ++i) {
    state.labelIds.push_back(state.fields[i].labelId);
    if (state.fields[i].password) {
      PasswordToggleBinding binding = {state.fields[i].editId,
                                       state.fields[i].toggleId, false};
      state.toggles.push_back(binding);
    }
  }

  ThemedDialogConfig config = {};
  config.windowTitle = L"智能操作参数";
  config.title = L"智能操作参数";
  config.subtitle = state.subtitle.c_str();
  config.bodyText = state.body.c_str();
  config.primaryButtonText = L"执行";
  config.secondaryButtonText = L"取消";
  config.dlgW = 424;
  config.dlgH = 194 + (int)state.fields.size() * 58;
  config.closeBtnId = closeBtnId;
  config.bodyFontDelta = 1;
  config.cardRect = {14, 78, 410, config.dlgH - 66};
  config.fieldLabelIds =
      state.labelIds.empty() ? NULL : &state.labelIds[0];
  config.fieldLabelCount = (int)state.labelIds.size();
  config.doneFlag = &state.done;
  config.userData = &state;

  HWND hDlg = CreateThemedDialog(hwndParent, hInst, &config);
  if (!hDlg)
    return false;

  const int labelX = 34;
  const int editX = 34;
  const int editW = 356;
  const int pwEditW = GetDialogPasswordEditWidth(editW);
  const int firstY = 92;
  const int blockGap = 58;

  for (size_t i = 0; i < state.fields.size(); ++i) {
    SmartActionParamField &field = state.fields[i];
    int y = firstY + (int)i * blockGap;
    CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", field.label.c_str(),
                    WS_CHILD | WS_VISIBLE, labelX, y, 180, 20, hDlg,
                    (HMENU)(INT_PTR)field.labelId, hInst, NULL);
    DWORD editStyle = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_MULTILINE;
    if (field.password)
      editStyle |= ES_PASSWORD;
    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", editStyle,
                                 editX, y + 24,
                                 field.password ? pwEditW : editW, 32, hDlg,
                                 (HMENU)(INT_PTR)field.editId, hInst, NULL);
    if (field.password) {
      ApplyDialogPasswordMask(hEdit, false);
      CreateDialogPasswordToggleButton(
          hDlg, hInst, GetDialogPasswordToggleX(editX, editW), y + 24,
          field.toggleId);
    }
    if (i == 0) {
      config.initialFocus = hEdit;
    }
  }

  const int btnW = 65;
  const int btnH = 25;
  const int btnGap = 10;
  const int btnY = config.dlgH - 42;
  const int cancelBtnX = config.dlgW - 24 - btnW;
  const int okBtnX = cancelBtnX - btnGap - btnW;
  CreateWindowExW(0, L"BUTTON", L"执行",
                  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                  okBtnX, btnY, btnW, btnH, hDlg, (HMENU)IDOK, hInst, NULL);
  CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                  cancelBtnX, btnY, btnW, btnH, hDlg, (HMENU)IDCANCEL, hInst,
                  NULL);

  config.onOk = +[](HWND hw, void *userData) -> bool {
    SmartActionPromptState *state =
        reinterpret_cast<SmartActionPromptState *>(userData);
    if (!state)
      return false;
    for (size_t i = 0; i < state->fields.size(); ++i) {
      wchar_t buf[512] = {};
      GetDlgItemTextW(hw, state->fields[i].editId, buf, _countof(buf));
      state->fields[i].value = buf;
    }
    state->accepted = true;
    return true;
  };

  ShowThemedDialog(hwndParent, hDlg, &config);
  RunThemedDialogLoop(hDlg, &state.done);
  CloseThemedDialog(hwndParent, hDlg, &config);

  if (state.accepted) {
    *fields = state.fields;
  }
  return state.accepted;
}

static bool ExpandActionTemplate(HWND hwndParent, const SmartAction &action,
                                 const std::wstring &sourceText,
                                 const std::wstring &templateText,
                                 std::wstring *expandedText) {
  if (!expandedText)
    return false;

  std::wstring output = templateText;
  std::wstring encoded = UrlEncode(sourceText);
  ReplaceAll(output, L"{0:url}", encoded);
  ReplaceAll(output, L"{0}", sourceText);

  std::vector<SmartActionParamField> fields = CollectTemplateParams(output);
  if (!fields.empty() &&
      !PromptSmartActionParams(hwndParent, action, sourceText, &fields)) {
    return false;
  }

  for (size_t i = 0; i < fields.size(); ++i) {
    ReplaceAll(output, L"{" + fields[i].token + L":url}",
               UrlEncode(fields[i].value));
    ReplaceAll(output, L"{" + fields[i].token + L"}", fields[i].value);
  }

  *expandedText = output;
  return true;
}

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
  ipRule.action = L"cmd_template";
  ipRule.customCmd = L"cmd /k ping {0}";
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
    // format: name\tenabled\tisDefault\taction\tpattern\tcustomCmd
    // 用 tab 分隔，pattern/customCmd 放最后避免包含分隔符的问题
    content += a.name + L"\t" + std::to_wstring(a.enabled ? 1 : 0) + L"\t" +
               std::to_wstring(a.isDefault ? 1 : 0) + L"\t" + a.action + L"\t" +
               a.pattern + L"\t" + a.customCmd + L"\n";
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

    // parse: name\tenabled\tisDefault\taction\tpattern\tcustomCmd
    // 只分割前 5 个 tab，第 6 个字段（customCmd）取剩余全部
    std::vector<std::wstring> parts;
    size_t p = 0;
    for (int i = 0; i < 5; i++) {
      size_t d = line.find(L'\t', p);
      if (d == std::wstring::npos) {
        parts.push_back(line.substr(p));
        p = line.size();
        break;
      }
      parts.push_back(line.substr(p, d - p));
      p = d + 1;
    }
    if (p < line.size())
      parts.push_back(line.substr(p));
    else if (parts.size() == 5)
      parts.push_back(L"");

    if (parts.size() >= 6) {
      SmartAction a;
      a.name = parts[0];
      a.enabled = (parts[1] == L"1");
      a.isDefault = (parts[2] == L"1");
      a.action = parts[3];
      a.pattern = parts[4];
      a.customCmd = parts[5];
      g_smartActions.push_back(a);
    }
  }

  // Ensure defaults exist
  if (g_smartActions.empty())
    InitDefaultActions();
}

bool HasEnabledMatch(const std::wstring &text) {
  for (const auto &a : g_smartActions) {
    if (!a.enabled)
      continue;
    if (a.pattern.empty())
      continue;
    try {
      std::wregex re(a.pattern, std::regex_constants::icase);
      if (std::regex_search(text, re))
        return true;
    } catch (...) {
      continue;
    }
  }
  return false;
}

bool MatchAndExecute(HWND hwndParent, const std::wstring &text) {
  for (const auto &a : g_smartActions) {
    if (!a.enabled)
      continue;
    if (a.pattern.empty())
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
          std::wstring cmd = L"/select,\"" + text + L"\"";
          ShellExecuteW(NULL, NULL, L"explorer.exe", cmd.c_str(), NULL,
                        SW_SHOWNORMAL);
        }
      } else {
        // 非本地路径（如 FTP URL），用资源管理器打开
        ShellExecuteW(NULL, NULL, L"explorer.exe", text.c_str(), NULL,
                      SW_SHOWNORMAL);
      }
      return true;
    } else if (a.action == L"url_template" && !a.customCmd.empty()) {
      std::wstring url;
      if (!ExpandActionTemplate(hwndParent, a, text, a.customCmd, &url))
        return true;
      ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
      return true;
    } else if (a.action == L"cmd_template" && !a.customCmd.empty()) {
      std::wstring cmd;
      if (!ExpandActionTemplate(hwndParent, a, text, a.customCmd, &cmd))
        return true;
      // VS Code 的 code 命令需要特殊处理，直接执行而不是通过 cmd.exe
      if (cmd.find(L"code ") == 0) {
        ShellExecuteW(NULL, L"open", cmd.c_str(), NULL, NULL, SW_SHOWNORMAL);
        return true;
      }
      // cmd /k 需要显示窗口，其他隐藏
      int showFlag = (cmd.find(L"cmd /k") == 0 || cmd.find(L"cmd /K") == 0)
                         ? SW_SHOWNORMAL
                         : SW_HIDE;
      ShellExecuteW(NULL, L"open", L"cmd.exe", (L"/c " + cmd).c_str(), NULL,
                    showFlag);
      return true;
    }
    // custom (旧格式兼容)
    else if (a.action == L"custom" && !a.customCmd.empty()) {
      std::wstring cmd;
      if (!ExpandActionTemplate(hwndParent, a, text, a.customCmd, &cmd))
        return true;
      int showFlag = (cmd.find(L"cmd /k") == 0 || cmd.find(L"cmd /K") == 0)
                         ? SW_SHOWNORMAL
                         : SW_HIDE;
      ShellExecuteW(NULL, L"open", L"cmd.exe", (L"/c " + cmd).c_str(), NULL,
                    showFlag);
      return true;
    }
  }
  return false;
}

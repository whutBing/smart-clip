#pragma once

#include <string>
#include <vector>
#include <windows.h>

struct SmartAction {
  std::wstring name;
  std::wstring pattern;
  std::wstring action;    // "browser", "explorer", "cmd_template"
  std::wstring customCmd; // {0} = matched text
  bool enabled;
  bool isDefault;
};

// 预设动作模板
struct ActionTemplate {
  const wchar_t *category;
  const wchar_t *name;
  const wchar_t *action;    // "browser", "explorer", "cmd_template"
  const wchar_t *cmdTemplate;
};

extern std::vector<SmartAction> g_smartActions;
extern const ActionTemplate g_actionTemplates[];
extern const int g_actionTemplateCount;

void InitDefaultActions();
void LoadSmartActions();
void SaveSmartActions();
bool MatchAndExecute(const std::wstring &text);
bool HasEnabledMatch(const std::wstring &text);
std::wstring UrlEncode(const std::wstring &text);

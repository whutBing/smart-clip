#pragma once

#include <string>
#include <vector>
#include <windows.h>

struct SmartAction {
  std::wstring name;
  std::wstring pattern;
  std::wstring action;    // "browser", "explorer", "custom"
  std::wstring customCmd; // {0} = matched text
  bool enabled;
  bool isDefault;
};

extern std::vector<SmartAction> g_smartActions;

void InitDefaultActions();
void LoadSmartActions();
void SaveSmartActions();
bool MatchAndExecute(const std::wstring &text);

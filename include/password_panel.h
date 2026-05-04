#pragma once

#include <set>
#include <windows.h>

extern std::set<int> g_pwVisibleSet;

void UpdatePasswordListBox();
void ShowSetMasterPasswordDialog(HWND hwndParent);
void ShowVerifyMasterPasswordDialog(HWND hwndParent);
bool AuthenticateVaultAccess(HWND hwndParent);
void ShowPasswordEntryDialog(HWND hwndParent, int editId = -1);
void ShowResetMasterPasswordDialog(HWND hwndParent);
void ShowPasswordContextMenu(HWND hwnd, int index, POINT pt);
void ShowRandomPasswordGeneratorDialog(HWND hwndParent);
bool QuickGenerateConfiguredPassword(HWND hwndNotify);

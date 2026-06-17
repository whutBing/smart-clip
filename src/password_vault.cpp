#include "password_vault.h"

#include <algorithm>

std::vector<PasswordEntry> g_passwords;
bool g_vaultUnlocked = false;
bool g_masterPasswordSet = false;
int g_nextPasswordId = 1;

bool g_vaultProtectionEnabled = false;
int g_vaultAuthMethod = 0;
bool g_trayPasswordGeneratorEnabled = false;
bool g_passwordGeneratorIncludeDigits = true;
bool g_passwordGeneratorIncludeLower = true;
bool g_passwordGeneratorIncludeUpper = true;
bool g_passwordGeneratorIncludeSymbols = false;
std::wstring g_passwordGeneratorSymbols;
int g_passwordGeneratorLength = 12;

void LoadVault() {}

void SaveVault() {}

std::wstring GetVaultFilePath() { return L""; }

std::wstring GetVaultKeyFilePath() { return L""; }

void SaveVaultSettings() {}

void LoadVaultSettings() {}

bool SetMasterPassword(const std::wstring & /*password*/) { return false; }

bool VerifyMasterPassword(const std::wstring & /*password*/) { return false; }

bool IsMasterPasswordSet() { return false; }

bool ResetMasterPassword(const std::wstring & /*oldPassword*/,
                         const std::wstring & /*newPassword*/) {
  return false;
}

bool TryWindowsHelloAuth(HWND /*hwndParent*/) { return false; }

bool DpapiEncrypt(const std::vector<BYTE> & /*plainData*/,
                  std::vector<BYTE> & /*encryptedData*/) {
  return false;
}

bool DpapiDecrypt(const std::vector<BYTE> & /*encryptedData*/,
                  std::vector<BYTE> & /*plainData*/) {
  return false;
}

int AddPasswordEntry(const std::wstring & /*name*/, const std::wstring & /*title*/,
                     const std::wstring & /*account*/,
                     const std::wstring & /*password*/) {
  return -1;
}

bool UpdatePasswordEntry(int /*id*/, const std::wstring & /*name*/,
                         const std::wstring & /*title*/,
                         const std::wstring & /*account*/,
                         const std::wstring & /*password*/) {
  return false;
}

bool DeletePasswordEntry(int id) {
  auto it = std::remove_if(g_passwords.begin(), g_passwords.end(),
                           [id](const PasswordEntry &entry) {
                             return entry.id == id;
                           });
  bool removed = it != g_passwords.end();
  g_passwords.erase(it, g_passwords.end());
  return removed;
}

bool IsUrlTitle(const std::wstring &title) {
  return title.find(L"://") != std::wstring::npos;
}
#include "password_manager.h"
#include "history.h"
#include "settings.h"
#include <algorithm>
#include <windows.h>
#include <wincred.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "credui.lib")

std::vector<PasswordEntry> g_passwords;
bool g_isPasswordUnlocked = false;
bool g_hasPasswordSetup = false;

static std::wstring GetPasswordsPath() {
  std::wstring filePath = GetDataFilePath();
  size_t lastSlash = filePath.find_last_of(L"\\");
  if (lastSlash == std::wstring::npos)
    return L"";
  return filePath.substr(0, lastSlash) + L"\\passwords.dat";
}

static std::wstring GenerateUUID() {
  GUID guid;
  CoCreateGuid(&guid);
  wchar_t buf[40];
  swprintf_s(buf, L"{%08x-%04x-%04x-%04x-%012x}", guid.Data1, guid.Data2,
             guid.Data3, (guid.Data4[0] << 8) | guid.Data4[1],
             *((unsigned long long *)&guid.Data4[2]));
  return std::wstring(buf);
}

static const wchar_t ENCRYPTION_KEY[] = L"SmartClipPasswordEncryptionKey2024";

std::wstring EncryptString(const std::wstring &plainText) {
  if (plainText.empty())
    return L"";
  std::wstring encrypted;
  size_t keyLen = wcslen(ENCRYPTION_KEY);
  for (size_t i = 0; i < plainText.length(); i++) {
    wchar_t c = plainText[i] ^ ENCRYPTION_KEY[i % keyLen];
    encrypted += c;
  }
  return encrypted;
}

std::wstring DecryptString(const std::wstring &encryptedText) {
  if (encryptedText.empty())
    return L"";
  std::wstring decrypted;
  size_t keyLen = wcslen(ENCRYPTION_KEY);
  for (size_t i = 0; i < encryptedText.length(); i++) {
    wchar_t c = encryptedText[i] ^ ENCRYPTION_KEY[i % keyLen];
    decrypted += c;
  }
  return decrypted;
}

bool SavePasswords() {
  std::wstring path = GetPasswordsPath();
  if (path.empty())
    return false;

  HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE)
    return false;

  DWORD version = 1;
  WriteFile(hFile, &version, sizeof(version), NULL, NULL);

  DWORD count = (DWORD)g_passwords.size();
  WriteFile(hFile, &count, sizeof(count), NULL, NULL);

  for (const auto &entry : g_passwords) {
    DWORD nameLen = (DWORD)(entry.name.length() + 1);
    WriteFile(hFile, &nameLen, sizeof(nameLen), NULL, NULL);
    WriteFile(hFile, entry.name.c_str(), nameLen * sizeof(wchar_t), NULL, NULL);

    DWORD userLen = (DWORD)(entry.username.length() + 1);
    WriteFile(hFile, &userLen, sizeof(userLen), NULL, NULL);
    WriteFile(hFile, entry.username.c_str(), userLen * sizeof(wchar_t), NULL,
              NULL);

    std::wstring encryptedPass = EncryptString(entry.password);
    DWORD passLen = (DWORD)(encryptedPass.length() + 1);
    WriteFile(hFile, &passLen, sizeof(passLen), NULL, NULL);
    WriteFile(hFile, encryptedPass.c_str(), passLen * sizeof(wchar_t), NULL,
              NULL);

    DWORD urlLen = (DWORD)(entry.url.length() + 1);
    WriteFile(hFile, &urlLen, sizeof(urlLen), NULL, NULL);
    WriteFile(hFile, entry.url.c_str(), urlLen * sizeof(wchar_t), NULL, NULL);

    WriteFile(hFile, &entry.isUrl, sizeof(entry.isUrl), NULL, NULL);
    WriteFile(hFile, &entry.isFavorite, sizeof(entry.isFavorite), NULL, NULL);

    DWORD idLen = (DWORD)(entry.id.length() + 1);
    WriteFile(hFile, &idLen, sizeof(idLen), NULL, NULL);
    WriteFile(hFile, entry.id.c_str(), idLen * sizeof(wchar_t), NULL, NULL);
  }

  CloseHandle(hFile);
  return true;
}

bool LoadPasswords() {
  std::wstring path = GetPasswordsPath();
  if (path.empty())
    return false;

  HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE)
    return false;

  DWORD version;
  DWORD bytesRead;
  if (!ReadFile(hFile, &version, sizeof(version), &bytesRead, NULL) ||
      bytesRead != sizeof(version)) {
    CloseHandle(hFile);
    return false;
  }
  if (version != 1) {
    CloseHandle(hFile);
    return false;
  }

  DWORD count;
  if (!ReadFile(hFile, &count, sizeof(count), &bytesRead, NULL) ||
      bytesRead != sizeof(count)) {
    CloseHandle(hFile);
    return false;
  }

  g_passwords.clear();
  for (DWORD i = 0; i < count; i++) {
    PasswordEntry entry;

    DWORD nameLen;
    ReadFile(hFile, &nameLen, sizeof(nameLen), &bytesRead, NULL);
    entry.name.resize(nameLen - 1);
    ReadFile(hFile, (void *)entry.name.data(), nameLen * sizeof(wchar_t),
             &bytesRead, NULL);

    DWORD userLen;
    ReadFile(hFile, &userLen, sizeof(userLen), &bytesRead, NULL);
    entry.username.resize(userLen - 1);
    ReadFile(hFile, (void *)entry.username.data(), userLen * sizeof(wchar_t),
             &bytesRead, NULL);

    DWORD passLen;
    ReadFile(hFile, &passLen, sizeof(passLen), &bytesRead, NULL);
    std::wstring encryptedPass;
    encryptedPass.resize(passLen - 1);
    ReadFile(hFile, (void *)encryptedPass.data(), passLen * sizeof(wchar_t),
             &bytesRead, NULL);
    entry.password = DecryptString(encryptedPass);

    DWORD urlLen;
    ReadFile(hFile, &urlLen, sizeof(urlLen), &bytesRead, NULL);
    entry.url.resize(urlLen - 1);
    ReadFile(hFile, (void *)entry.url.data(), urlLen * sizeof(wchar_t),
             &bytesRead, NULL);

    ReadFile(hFile, &entry.isUrl, sizeof(entry.isUrl), &bytesRead, NULL);
    ReadFile(hFile, &entry.isFavorite, sizeof(entry.isFavorite), &bytesRead,
             NULL);

    DWORD idLen;
    ReadFile(hFile, &idLen, sizeof(idLen), &bytesRead, NULL);
    entry.id.resize(idLen - 1);
    ReadFile(hFile, (void *)entry.id.data(), idLen * sizeof(wchar_t),
             &bytesRead, NULL);

    g_passwords.push_back(entry);
  }

  CloseHandle(hFile);
  return true;
}

bool HasPasswordSetup() { return g_hasPasswordSetup; }

bool SetupMasterPassword(const std::wstring &password) {
  HKEY hKey;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\SmartClip", 0,
                    KEY_WRITE | KEY_READ, &hKey) != ERROR_SUCCESS) {
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\SmartClip", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_READ, NULL,
                        &hKey, NULL) != ERROR_SUCCESS) {
      return false;
    }
  }

  std::wstring hash = EncryptString(password + L"SmartClipSalt");
  RegSetValueExW(hKey, L"MasterPasswordHash", 0, REG_SZ,
                 (const BYTE *)hash.c_str(),
                 (DWORD)((hash.length() + 1) * sizeof(wchar_t)));

  RegCloseKey(hKey);
  g_hasPasswordSetup = true;
  return true;
}

bool VerifyMasterPassword(const std::wstring &password) {
  HKEY hKey;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\SmartClip", 0, KEY_READ,
                    &hKey) != ERROR_SUCCESS) {
    return false;
  }

  wchar_t buffer[512];
  DWORD bufferSize = sizeof(buffer);
  DWORD type;
  if (RegQueryValueExW(hKey, L"MasterPasswordHash", NULL, &type, (BYTE *)buffer,
                       &bufferSize) != ERROR_SUCCESS) {
    RegCloseKey(hKey);
    return false;
  }
  RegCloseKey(hKey);

  std::wstring storedHash = buffer;
  std::wstring computedHash = EncryptString(password + L"SmartClipSalt");

  return storedHash == computedHash;
}

bool CheckWindowsHelloAvailable() {
  BOOL supported = FALSE;
  CREDUI_INFOW uiInfo = {};
  uiInfo.cbSize = sizeof(uiInfo);
  uiInfo.pszCaptionText = L"SmartClip";
  uiInfo.pszMessageText = L"验证身份";

  ULONG authPackage = 0;
  PVOID outAuthBuffer = NULL;
  ULONG outAuthBufferSize = 0;
  BOOL save = FALSE;

  DWORD result = CredUIPromptForWindowsCredentialsW(
      &uiInfo, 0, &authPackage, NULL, 0, &outAuthBuffer, &outAuthBufferSize,
      &save, CREDUIWIN_ENUMERATE_CURRENT_USER);

  if (outAuthBuffer)
    CoTaskMemFree(outAuthBuffer);

  return (result == ERROR_SUCCESS || result == ERROR_CANCELLED);
}

bool AuthenticateWithWindowsHello(HWND hwnd) {
  CREDUI_INFOW uiInfo = {};
  uiInfo.cbSize = sizeof(uiInfo);
  uiInfo.hwndParent = hwnd;
  uiInfo.pszCaptionText = L"SmartClip 密码管理器";
  uiInfo.pszMessageText = L"请验证您的身份以访问密码";

  ULONG authPackage = 0;
  PVOID outAuthBuffer = NULL;
  ULONG outAuthBufferSize = 0;
  BOOL save = FALSE;

  DWORD result = CredUIPromptForWindowsCredentialsW(
      &uiInfo, 0, &authPackage, NULL, 0, &outAuthBuffer, &outAuthBufferSize,
      &save, CREDUIWIN_IN_CRED_ONLY);

  if (outAuthBuffer)
    CoTaskMemFree(outAuthBuffer);

  return (result == ERROR_SUCCESS);
}

bool AddPasswordEntry(const PasswordEntry &entry) {
  PasswordEntry newEntry = entry;
  if (newEntry.id.empty()) {
    newEntry.id = GenerateUUID();
  }
  g_passwords.push_back(newEntry);
  return SavePasswords();
}

bool DeletePasswordEntry(const std::wstring &id) {
  auto it =
      std::remove_if(g_passwords.begin(), g_passwords.end(),
                     [&id](const PasswordEntry &e) { return e.id == id; });
  if (it != g_passwords.end()) {
    g_passwords.erase(it, g_passwords.end());
    return SavePasswords();
  }
  return false;
}

bool UpdatePasswordEntry(const PasswordEntry &entry) {
  for (auto &e : g_passwords) {
    if (e.id == entry.id) {
      e = entry;
      return SavePasswords();
    }
  }
  return false;
}

PasswordEntry *FindPasswordEntry(const std::wstring &id) {
  for (auto &e : g_passwords) {
    if (e.id == id) {
      return &e;
    }
  }
  return nullptr;
}

void CheckMasterPasswordExists() {
  HKEY hKey;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\SmartClip", 0, KEY_READ,
                    &hKey) == ERROR_SUCCESS) {
    wchar_t buffer[512];
    DWORD bufferSize = sizeof(buffer);
    DWORD type;
    if (RegQueryValueExW(hKey, L"MasterPasswordHash", NULL, &type,
                         (BYTE *)buffer, &bufferSize) == ERROR_SUCCESS) {
      g_hasPasswordSetup = true;
    }
    RegCloseKey(hKey);
  }
}

void InitPasswordManager() {
  CheckMasterPasswordExists();
  if (g_hasPasswordSetup) {
    LoadPasswords();
  }
}
#pragma once

#include <string>
#include <vector>
#include <windows.h>

struct PasswordEntry {
  std::wstring id;       // 唯一标识符
  std::wstring name;     // 名称（网址/应用名）
  std::wstring username; // 账号
  std::wstring password; // 密码（加密存储）
  bool isUrl;            // 是否为网址
  std::wstring url;      // 网址URL
  bool isFavorite;       // 是否收藏
};

struct PasswordCategory {
  std::wstring name;            // 分类名称
  std::vector<int> itemIndices; // 属于该分类的密码项索引
};

extern std::vector<PasswordEntry> g_passwords;
extern bool g_isPasswordUnlocked;
extern bool g_hasPasswordSetup;

bool SavePasswords();
bool LoadPasswords();
bool HasPasswordSetup();
bool SetupMasterPassword(const std::wstring &password);
bool VerifyMasterPassword(const std::wstring &password);
bool CheckWindowsHelloAvailable();
bool AuthenticateWithWindowsHello(HWND hwnd);
std::wstring EncryptString(const std::wstring &plainText);
std::wstring DecryptString(const std::wstring &encryptedText);
bool AddPasswordEntry(const PasswordEntry &entry);
bool DeletePasswordEntry(const std::wstring &id);
bool UpdatePasswordEntry(const PasswordEntry &entry);
PasswordEntry *FindPasswordEntry(const std::wstring &id);
void InitPasswordManager();
void CheckMasterPasswordExists();
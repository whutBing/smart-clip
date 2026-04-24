#pragma once

#include <string>
#include <vector>
#include <windows.h>

struct PasswordEntry {
    int id;
    std::wstring name;     // 名称（用于搜索和显示）
    std::wstring title;    // 网址/应用名
    std::wstring account;  // 账号
    std::wstring password; // 密码
    bool isUrl;            // 是否为网址
};

extern std::vector<PasswordEntry> g_passwords;
extern bool g_vaultUnlocked;
extern bool g_masterPasswordSet;
extern int g_nextPasswordId;

// 密码保护设置
extern bool g_vaultProtectionEnabled; // 是否开启密码保护
extern int g_vaultAuthMethod;         // 0=主密码, 1=Windows Hello

// 密码库文件操作
void LoadVault();
void SaveVault();
std::wstring GetVaultFilePath();
std::wstring GetVaultKeyFilePath();

// 密码保护设置持久化
void SaveVaultSettings();
void LoadVaultSettings();

// 主密码管理
bool SetMasterPassword(const std::wstring &password);
bool VerifyMasterPassword(const std::wstring &password);
bool IsMasterPasswordSet();
bool ResetMasterPassword(const std::wstring &oldPassword,
                         const std::wstring &newPassword);

// Windows Hello 认证（动态加载）
bool TryWindowsHelloAuth(HWND hwndParent);

// DPAPI 加密/解密
bool DpapiEncrypt(const std::vector<BYTE> &plainData,
                  std::vector<BYTE> &encryptedData);
bool DpapiDecrypt(const std::vector<BYTE> &encryptedData,
                  std::vector<BYTE> &plainData);

// 密码条目操作
int AddPasswordEntry(const std::wstring &name, const std::wstring &title,
                     const std::wstring &account,
                     const std::wstring &password);
bool UpdatePasswordEntry(int id, const std::wstring &name,
                         const std::wstring &title,
                         const std::wstring &account,
                         const std::wstring &password);
bool DeletePasswordEntry(int id);
bool IsUrlTitle(const std::wstring &title);

// 连续复制（账号→密码）
void StartPasswordBatchCopy(int entryIndex, HWND hwnd);

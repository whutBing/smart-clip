#include "password_vault.h"
#include "history.h"
#include "settings.h"
#include "tray.h"
#include <wincrypt.h>
#include <shlwapi.h>
#include <algorithm>
#include <hstring.h>
#include <roapi.h>
#include <winstring.h>

std::vector<PasswordEntry> g_passwords;
bool g_vaultUnlocked = false;
bool g_masterPasswordSet = false;
int g_nextPasswordId = 1;
bool g_vaultProtectionEnabled = false;
int g_vaultAuthMethod = 0; // 0=主密码, 1=Windows Hello
bool g_trayPasswordGeneratorEnabled = false;
bool g_passwordGeneratorIncludeDigits = true;
bool g_passwordGeneratorIncludeLower = true;
bool g_passwordGeneratorIncludeUpper = true;
bool g_passwordGeneratorIncludeSymbols = false;
int g_passwordGeneratorLength = 12;

// 密码连续复制状态
static bool g_pwBatchMode = false;
static std::wstring g_pwBatchAccount;
static std::wstring g_pwBatchPassword;
static int g_pwBatchStep = 0; // 0=账号已放入, 1=密码已放入
static HHOOK g_hPwBatchHook = NULL;
static HWND g_pwBatchHwnd = NULL;

// ==================== 文件路径 ====================

std::wstring GetVaultFilePath() {
    return GetSmartClipDataDir() + L"\\vault.dat";
}

std::wstring GetVaultKeyFilePath() {
    return GetSmartClipDataDir() + L"\\vault_key.dat";
}

static std::wstring GetVaultSettingsPath() {
    return GetSmartClipDataDir() + L"\\vault_settings.txt";
}

void SaveVaultSettings() {
    std::wstring path = GetVaultSettingsPath();
    std::wstring content = std::to_wstring(g_vaultProtectionEnabled ? 1 : 0) + L"\n" +
                           std::to_wstring(g_vaultAuthMethod) + L"\n" +
                           std::to_wstring(g_trayPasswordGeneratorEnabled ? 1 : 0) + L"\n" +
                           std::to_wstring(g_passwordGeneratorIncludeDigits ? 1 : 0) + L"\n" +
                           std::to_wstring(g_passwordGeneratorIncludeLower ? 1 : 0) + L"\n" +
                           std::to_wstring(g_passwordGeneratorIncludeUpper ? 1 : 0) + L"\n" +
                           std::to_wstring(g_passwordGeneratorIncludeSymbols ? 1 : 0) + L"\n" +
                           std::to_wstring(g_passwordGeneratorLength) + L"\n";
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, NULL, 0, NULL, NULL);
    if (utf8Len > 0) {
        std::vector<char> utf8(utf8Len);
        WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, &utf8[0], utf8Len, NULL, NULL);
        DWORD written = 0;
        WriteFile(hFile, &utf8[0], utf8Len - 1, &written, NULL);
    }
    CloseHandle(hFile);
}

void LoadVaultSettings() {
    std::wstring path = GetVaultSettingsPath();
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    DWORD sz = GetFileSize(hFile, NULL);
    if (sz == 0 || sz == INVALID_FILE_SIZE) { CloseHandle(hFile); return; }
    std::vector<BYTE> buf(sz);
    DWORD read = 0;
    ReadFile(hFile, &buf[0], sz, &read, NULL);
    CloseHandle(hFile);

    int wLen = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)&buf[0], sz, NULL, 0);
    if (wLen <= 0) return;
    std::vector<wchar_t> wBuf(wLen + 1);
    MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)&buf[0], sz, &wBuf[0], wLen);
    wBuf[wLen] = L'\0';

    wchar_t *pLine = &wBuf[0];
    wchar_t *pNext = wcsstr(pLine, L"\n");
    if (pNext) { *pNext = L'\0'; pNext++; }
    g_vaultProtectionEnabled = (_wtoi(pLine) != 0);

    if (pNext) {
        pLine = pNext;
        pNext = wcsstr(pLine, L"\n");
        if (pNext) { *pNext = L'\0'; pNext++; }
        g_vaultAuthMethod = _wtoi(pLine);
        if (g_vaultAuthMethod < 0 || g_vaultAuthMethod > 1) g_vaultAuthMethod = 0;
    }

    if (pNext) {
        pLine = pNext;
        pNext = wcsstr(pLine, L"\n");
        if (pNext) *pNext = L'\0';
        g_trayPasswordGeneratorEnabled = (_wtoi(pLine) != 0);
    } else {
        g_trayPasswordGeneratorEnabled = false;
    }

    if (pNext) {
        pLine = pNext + 1;
        pNext = wcsstr(pLine, L"\n");
        if (pNext) { *pNext = L'\0'; }
        g_passwordGeneratorIncludeDigits = (_wtoi(pLine) != 0);
    }
    if (pNext) {
        pLine = pNext + 1;
        pNext = wcsstr(pLine, L"\n");
        if (pNext) { *pNext = L'\0'; }
        g_passwordGeneratorIncludeLower = (_wtoi(pLine) != 0);
    }
    if (pNext) {
        pLine = pNext + 1;
        pNext = wcsstr(pLine, L"\n");
        if (pNext) { *pNext = L'\0'; }
        g_passwordGeneratorIncludeUpper = (_wtoi(pLine) != 0);
    }
    if (pNext) {
        pLine = pNext + 1;
        pNext = wcsstr(pLine, L"\n");
        if (pNext) { *pNext = L'\0'; }
        g_passwordGeneratorIncludeSymbols = (_wtoi(pLine) != 0);
    }
    if (pNext) {
        pLine = pNext + 1;
        pNext = wcsstr(pLine, L"\n");
        if (pNext) { *pNext = L'\0'; }
        int length = _wtoi(pLine);
        if (length >= 6 && length <= 64) {
            g_passwordGeneratorLength = length;
        }
    }

    if (!g_passwordGeneratorIncludeDigits && !g_passwordGeneratorIncludeLower &&
        !g_passwordGeneratorIncludeUpper && !g_passwordGeneratorIncludeSymbols) {
        g_passwordGeneratorIncludeDigits = true;
        g_passwordGeneratorIncludeLower = true;
        g_passwordGeneratorIncludeUpper = true;
    }
    if (g_passwordGeneratorLength < 6 || g_passwordGeneratorLength > 64) {
        g_passwordGeneratorLength = 12;
    }
}

bool ResetMasterPassword(const std::wstring &oldPassword,
                         const std::wstring &newPassword) {
    if (!VerifyMasterPassword(oldPassword)) return false;
    return SetMasterPassword(newPassword);
}

// ==================== DPAPI 加密/解密 ====================

bool DpapiEncrypt(const std::vector<BYTE> &plainData,
                  std::vector<BYTE> &encryptedData) {
    DATA_BLOB dataIn, dataOut;
    dataIn.pbData = (BYTE *)plainData.data();
    dataIn.cbData = (DWORD)plainData.size();
    dataOut.pbData = NULL;
    dataOut.cbData = 0;

    if (!CryptProtectData(&dataIn, L"SmartClipVault", NULL, NULL, NULL,
                          CRYPTPROTECT_UI_FORBIDDEN, &dataOut)) {
        return false;
    }

    encryptedData.assign(dataOut.pbData, dataOut.pbData + dataOut.cbData);
    LocalFree(dataOut.pbData);
    return true;
}

bool DpapiDecrypt(const std::vector<BYTE> &encryptedData,
                  std::vector<BYTE> &plainData) {
    DATA_BLOB dataIn, dataOut;
    dataIn.pbData = (BYTE *)encryptedData.data();
    dataIn.cbData = (DWORD)encryptedData.size();
    dataOut.pbData = NULL;
    dataOut.cbData = 0;

    if (!CryptUnprotectData(&dataIn, NULL, NULL, NULL, NULL,
                            CRYPTPROTECT_UI_FORBIDDEN, &dataOut)) {
        return false;
    }

    plainData.assign(dataOut.pbData, dataOut.pbData + dataOut.cbData);
    LocalFree(dataOut.pbData);
    return true;
}

// ==================== 主密码管理 ====================

bool IsMasterPasswordSet() {
    std::wstring keyPath = GetVaultKeyFilePath();
    DWORD attrs = GetFileAttributesW(keyPath.c_str());
    g_masterPasswordSet = (attrs != INVALID_FILE_ATTRIBUTES);
    return g_masterPasswordSet;
}

bool SetMasterPassword(const std::wstring &password) {
    // 简单哈希：将密码转为 UTF-8 后用 DPAPI 加密存储
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, password.c_str(), -1,
                                       NULL, 0, NULL, NULL);
    if (utf8Len <= 0) return false;

    std::vector<BYTE> pwBytes(utf8Len);
    WideCharToMultiByte(CP_UTF8, 0, password.c_str(), -1,
                        (LPSTR)&pwBytes[0], utf8Len, NULL, NULL);

    std::vector<BYTE> encrypted;
    if (!DpapiEncrypt(pwBytes, encrypted)) return false;

    std::wstring keyPath = GetVaultKeyFilePath();
    HANDLE hFile = CreateFileW(keyPath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    WriteFile(hFile, encrypted.data(), (DWORD)encrypted.size(), &written, NULL);
    CloseHandle(hFile);

    g_masterPasswordSet = true;
    return true;
}

bool VerifyMasterPassword(const std::wstring &password) {
    std::wstring keyPath = GetVaultKeyFilePath();
    HANDLE hFile = CreateFileW(keyPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return false;
    }

    std::vector<BYTE> encrypted(fileSize);
    DWORD bytesRead = 0;
    ReadFile(hFile, &encrypted[0], fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    std::vector<BYTE> decrypted;
    if (!DpapiDecrypt(encrypted, decrypted)) return false;

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, password.c_str(), -1,
                                       NULL, 0, NULL, NULL);
    if (utf8Len <= 0) return false;

    std::vector<BYTE> pwBytes(utf8Len);
    WideCharToMultiByte(CP_UTF8, 0, password.c_str(), -1,
                        (LPSTR)&pwBytes[0], utf8Len, NULL, NULL);

    if (decrypted.size() != pwBytes.size()) return false;
    return memcmp(decrypted.data(), pwBytes.data(), decrypted.size()) == 0;
}

// ==================== Windows Hello 认证 ====================

// IUserConsentVerifierInterop COM 接口
// {39E050C3-4E74-441A-8DC0-B81104DF949C}
static const GUID IID_IUserConsentVerifierInterop =
    {0x39E050C3, 0x4E74, 0x441A, {0x8D, 0xC0, 0xB8, 0x11, 0x04, 0xDF, 0x94, 0x9C}};

static const GUID IID_IAsyncInfo =
    {0x00000036, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

struct IAsyncOperationVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void*, REFIID, void**);
    ULONG (STDMETHODCALLTYPE *AddRef)(void*);
    ULONG (STDMETHODCALLTYPE *Release)(void*);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void*, ULONG*, GUID**);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void*, HSTRING*);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void*, int*);
    HRESULT (STDMETHODCALLTYPE *put_Completed)(void*, void*);
    HRESULT (STDMETHODCALLTYPE *get_Completed)(void*, void**);
    HRESULT (STDMETHODCALLTYPE *GetResults)(void*, int*);
};

struct IAsyncInfoVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void*, REFIID, void**);
    ULONG (STDMETHODCALLTYPE *AddRef)(void*);
    ULONG (STDMETHODCALLTYPE *Release)(void*);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void*, ULONG*, GUID**);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void*, HSTRING*);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void*, int*);
    HRESULT (STDMETHODCALLTYPE *get_Id)(void*, UINT32*);
    HRESULT (STDMETHODCALLTYPE *get_Status)(void*, int*);
    HRESULT (STDMETHODCALLTYPE *get_ErrorCode)(void*, HRESULT*);
    HRESULT (STDMETHODCALLTYPE *Cancel)(void*);
    HRESULT (STDMETHODCALLTYPE *Close)(void*);
};

enum AsyncStatusValue {
    AsyncStatus_Started = 0,
    AsyncStatus_Completed = 1,
    AsyncStatus_Canceled = 2,
    AsyncStatus_Error = 3
};

struct IUserConsentVerifierInteropVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void*, REFIID, void**);
    ULONG (STDMETHODCALLTYPE *AddRef)(void*);
    ULONG (STDMETHODCALLTYPE *Release)(void*);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void*, ULONG*, GUID**);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void*, HSTRING*);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void*, int*);
    HRESULT (STDMETHODCALLTYPE *RequestVerificationForWindowAsync)(
        void*, HWND, HSTRING, REFIID, void**);
};

bool TryWindowsHelloAuth(HWND hwndParent) {
    HSTRING hClassName = NULL;
    const wchar_t *className = L"Windows.Security.Credentials.UI.UserConsentVerifier";
    HRESULT hr = WindowsCreateString(className, (UINT32)wcslen(className), &hClassName);
    if (FAILED(hr)) return false;

    void *pFactory = NULL;
    hr = RoGetActivationFactory(hClassName, IID_IUserConsentVerifierInterop, &pFactory);
    WindowsDeleteString(hClassName);
    if (FAILED(hr) || !pFactory) return false;

    IUserConsentVerifierInteropVtbl **ppVtbl = (IUserConsentVerifierInteropVtbl**)pFactory;

    HSTRING hMessage = NULL;
    const wchar_t *message = L"请验证身份以访问密码库";
    WindowsCreateString(message, (UINT32)wcslen(message), &hMessage);

    void *pAsyncOp = NULL;
    static const GUID IID_IUnknown_local =
        {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
    hr = (*ppVtbl)->RequestVerificationForWindowAsync(
        pFactory, hwndParent, hMessage, IID_IUnknown_local, &pAsyncOp);

    WindowsDeleteString(hMessage);

    if (FAILED(hr) || !pAsyncOp) {
        (*ppVtbl)->Release(pFactory);
        return false;
    }

    IAsyncOperationVtbl **ppAsyncOp = (IAsyncOperationVtbl**)pAsyncOp;
    void *pAsyncInfo = NULL;
    hr = (*ppAsyncOp)->QueryInterface(pAsyncOp, IID_IAsyncInfo, &pAsyncInfo);

    if (SUCCEEDED(hr) && pAsyncInfo) {
        IAsyncInfoVtbl **ppInfo = (IAsyncInfoVtbl**)pAsyncInfo;
        int status = AsyncStatus_Started;
        for (int i = 0; i < 1200; i++) {
            (*ppInfo)->get_Status(pAsyncInfo, &status);
            if (status == AsyncStatus_Completed ||
                status == AsyncStatus_Canceled ||
                status == AsyncStatus_Error) {
                break;
            }

            // 只处理绘制和系统消息，不分发按钮点击等用户输入
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_PAINT || msg.message == WM_TIMER ||
                    msg.message == WM_NCPAINT || msg.message == WM_ERASEBKGND ||
                    msg.message >= 0xC000) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                } else if (msg.message == WM_QUIT) {
                    PostQuitMessage((int)msg.wParam);
                    (*ppInfo)->Release(pAsyncInfo);
                    (*ppAsyncOp)->Release(pAsyncOp);
                    (*ppVtbl)->Release(pFactory);
                    return false;
                }
            }
            Sleep(50);
        }

        if (status != AsyncStatus_Completed) {
            (*ppInfo)->Release(pAsyncInfo);
            (*ppAsyncOp)->Release(pAsyncOp);
            (*ppVtbl)->Release(pFactory);
            return false;
        }
        (*ppInfo)->Release(pAsyncInfo);
    } else {
        // 如果无法获取 IAsyncInfo，就轮询 GetResults，避免过早返回失败。
        bool completed = false;
        for (int i = 0; i < 1200; i++) {
            int probeResult = -1;
            hr = (*ppAsyncOp)->GetResults(pAsyncOp, &probeResult);
            if (SUCCEEDED(hr)) {
                completed = true;
                break;
            }

            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_PAINT || msg.message == WM_TIMER ||
                    msg.message == WM_NCPAINT || msg.message == WM_ERASEBKGND ||
                    msg.message >= 0xC000) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                } else if (msg.message == WM_QUIT) {
                    PostQuitMessage((int)msg.wParam);
                    (*ppAsyncOp)->Release(pAsyncOp);
                    (*ppVtbl)->Release(pFactory);
                    return false;
                }
            }
            Sleep(50);
        }
        if (!completed) {
            (*ppAsyncOp)->Release(pAsyncOp);
            (*ppVtbl)->Release(pFactory);
            return false;
        }
    }

    int result = -1;
    hr = (*ppAsyncOp)->GetResults(pAsyncOp, &result);

    (*ppAsyncOp)->Release(pAsyncOp);
    (*ppVtbl)->Release(pFactory);

    // 0 = Verified, 其余包括 DeviceNotPresent / NotConfiguredForUser /
    // DisabledByPolicy / DeviceBusy / RetriesExhausted / Canceled 都视为失败。
    return SUCCEEDED(hr) && result == 0;
}

// ==================== 序列化/反序列化 ====================

static std::wstring SerializePasswords() {
    std::wstring data;
    data += std::to_wstring(g_nextPasswordId) + L"\n";
    for (const auto &e : g_passwords) {
        data += std::to_wstring(e.id) + L"\t";
        data += e.name + L"\t";
        data += e.title + L"\t";
        data += e.account + L"\t";
        data += e.password + L"\t";
        data += (e.isUrl ? L"1" : L"0") + std::wstring(L"\n");
    }
    return data;
}

static bool DeserializePasswords(const std::wstring &data) {
    g_passwords.clear();
    g_nextPasswordId = 1;

    size_t pos = 0;
    size_t eol = data.find(L'\n', pos);
    if (eol == std::wstring::npos) return false;
    std::wstring line = data.substr(pos, eol - pos);
    if (!line.empty() && line.back() == L'\r') line.pop_back();
    g_nextPasswordId = _wtoi(line.c_str());
    pos = eol + 1;

    while (pos < data.size()) {
        eol = data.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = data.size();
        line = data.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.empty()) continue;
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        // 新格式: id\tname\ttitle\taccount\tpassword\tisUrl (6字段)
        // 旧格式: id\ttitle\taccount\tpassword\tisUrl (5字段)
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
        if (p < line.size()) parts.push_back(line.substr(p));

        if (parts.size() >= 6) {
            // 新格式
            PasswordEntry e;
            e.id = _wtoi(parts[0].c_str());
            e.name = parts[1];
            e.title = parts[2];
            e.account = parts[3];
            e.password = parts[4];
            e.isUrl = (parts[5] == L"1");
            g_passwords.push_back(e);
        } else if (parts.size() >= 5) {
            // 旧格式兼容
            PasswordEntry e;
            e.id = _wtoi(parts[0].c_str());
            e.name = parts[1]; // 旧格式的 title 作为 name
            e.title = parts[1];
            e.account = parts[2];
            e.password = parts[3];
            e.isUrl = (parts[4] == L"1");
            g_passwords.push_back(e);
        }
    }
    return true;
}

// ==================== 文件读写 ====================

void SaveVault() {
    std::wstring serialized = SerializePasswords();

    // 转为 UTF-8
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, serialized.c_str(), -1,
                                       NULL, 0, NULL, NULL);
    if (utf8Len <= 0) return;
    std::vector<BYTE> utf8Data(utf8Len);
    WideCharToMultiByte(CP_UTF8, 0, serialized.c_str(), -1,
                        (LPSTR)&utf8Data[0], utf8Len, NULL, NULL);

    // DPAPI 加密
    std::vector<BYTE> encrypted;
    if (!DpapiEncrypt(utf8Data, encrypted)) return;

    // 写入文件
    std::wstring path = GetVaultFilePath();
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    WriteFile(hFile, encrypted.data(), (DWORD)encrypted.size(), &written, NULL);
    CloseHandle(hFile);
}

void LoadVault() {
    g_passwords.clear();
    g_nextPasswordId = 1;

    std::wstring path = GetVaultFilePath();
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return;
    }

    std::vector<BYTE> encrypted(fileSize);
    DWORD bytesRead = 0;
    ReadFile(hFile, &encrypted[0], fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    // DPAPI 解密
    std::vector<BYTE> decrypted;
    if (!DpapiDecrypt(encrypted, decrypted)) return;

    // UTF-8 转 wstring
    int wLen = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)decrypted.data(),
                                    (int)decrypted.size(), NULL, 0);
    if (wLen <= 0) return;
    std::vector<wchar_t> wBuf(wLen + 1);
    MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)decrypted.data(),
                        (int)decrypted.size(), &wBuf[0], wLen);
    wBuf[wLen] = L'\0';

    DeserializePasswords(&wBuf[0]);
}

// ==================== 密码条目操作 ====================

bool IsUrlTitle(const std::wstring &title) {
    if (title.find(L"http://") == 0 || title.find(L"https://") == 0 ||
        title.find(L"www.") == 0 || title.find(L"ftp://") == 0) {
        return true;
    }
    // 简单域名检测: 包含 . 且不含空格
    if (title.find(L'.') != std::wstring::npos &&
        title.find(L' ') == std::wstring::npos &&
        title.find(L'\\') == std::wstring::npos) {
        return true;
    }
    return false;
}

int AddPasswordEntry(const std::wstring &name, const std::wstring &title,
                     const std::wstring &account,
                     const std::wstring &password) {
    PasswordEntry e;
    e.id = g_nextPasswordId++;
    e.name = name;
    e.title = title;
    e.account = account;
    e.password = password;
    e.isUrl = IsUrlTitle(title);
    g_passwords.push_back(e);
    SaveVault();
    return e.id;
}

bool UpdatePasswordEntry(int id, const std::wstring &name,
                         const std::wstring &title,
                         const std::wstring &account,
                         const std::wstring &password) {
    for (auto &e : g_passwords) {
        if (e.id == id) {
            e.name = name;
            e.title = title;
            e.account = account;
            e.password = password;
            e.isUrl = IsUrlTitle(title);
            SaveVault();
            return true;
        }
    }
    return false;
}

bool DeletePasswordEntry(int id) {
    auto it = std::remove_if(g_passwords.begin(), g_passwords.end(),
                              [id](const PasswordEntry &e) { return e.id == id; });
    if (it == g_passwords.end()) return false;
    g_passwords.erase(it, g_passwords.end());
    SaveVault();
    return true;
}

// ==================== 连续复制（账号→密码） ====================

static void PwBatchLoadPassword() {
    if (!g_pwBatchMode || g_pwBatchStep != 0) return;
    g_pwBatchStep = 1;

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hGlobal = GlobalAlloc(
            GMEM_MOVEABLE, (g_pwBatchPassword.length() + 1) * sizeof(wchar_t));
        if (hGlobal) {
            wchar_t *pData = (wchar_t *)GlobalLock(hGlobal);
            if (pData) {
                wcscpy_s(pData, g_pwBatchPassword.length() + 1,
                          g_pwBatchPassword.c_str());
                GlobalUnlock(hGlobal);
                SetClipboardData(CF_UNICODETEXT, hGlobal);
            }
        }
        CloseClipboard();
    }
}

static LRESULT CALLBACK PwBatchKeyboardProc(int nCode, WPARAM wParam,
                                             LPARAM lParam) {
    if (nCode == HC_ACTION && g_pwBatchMode) {
        KBDLLHOOKSTRUCT *pKb = (KBDLLHOOKSTRUCT *)lParam;
        if (wParam == WM_KEYUP && pKb->vkCode == 'V' &&
            (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            if (g_pwBatchStep == 0) {
                PwBatchLoadPassword();
                if (g_pwBatchHwnd && g_isNotificationEnabled) {
                    ShowTrayBalloon(g_pwBatchHwnd, L"密码库",
                                    L"密码已就绪，再次 Ctrl+V 粘贴密码");
                }
            } else {
                // 密码已粘贴，退出
                g_pwBatchMode = false;
                g_pwBatchAccount.clear();
                g_pwBatchPassword.clear();
                g_pwBatchStep = 0;
                if (g_hPwBatchHook) {
                    UnhookWindowsHookEx(g_hPwBatchHook);
                    g_hPwBatchHook = NULL;
                }
                if (g_pwBatchHwnd && g_isNotificationEnabled) {
                    ShowTrayBalloon(g_pwBatchHwnd, L"密码库",
                                    L"账号密码已粘贴完成");
                }
            }
        }
        if (wParam == WM_KEYDOWN && pKb->vkCode == VK_ESCAPE) {
            g_pwBatchMode = false;
            g_pwBatchAccount.clear();
            g_pwBatchPassword.clear();
            g_pwBatchStep = 0;
            if (g_hPwBatchHook) {
                UnhookWindowsHookEx(g_hPwBatchHook);
                g_hPwBatchHook = NULL;
            }
            if (g_pwBatchHwnd && g_isNotificationEnabled) {
                ShowTrayBalloon(g_pwBatchHwnd, L"密码库",
                                L"已退出连续复制模式");
            }
        }
    }
    return CallNextHookEx(g_hPwBatchHook, nCode, wParam, lParam);
}

void StartPasswordBatchCopy(int entryIndex, HWND hwnd) {
    if (entryIndex < 0 || entryIndex >= (int)g_passwords.size()) return;

    const PasswordEntry &entry = g_passwords[entryIndex];
    g_pwBatchAccount = entry.account;
    g_pwBatchPassword = entry.password;
    g_pwBatchStep = 0;
    g_pwBatchMode = true;
    g_pwBatchHwnd = hwnd;

    // 将账号放入剪贴板
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hGlobal = GlobalAlloc(
            GMEM_MOVEABLE, (entry.account.length() + 1) * sizeof(wchar_t));
        if (hGlobal) {
            wchar_t *pData = (wchar_t *)GlobalLock(hGlobal);
            if (pData) {
                wcscpy_s(pData, entry.account.length() + 1,
                          entry.account.c_str());
                GlobalUnlock(hGlobal);
                SetClipboardData(CF_UNICODETEXT, hGlobal);
            }
        }
        CloseClipboard();
    }

    // 如果是 URL，打开浏览器
    if (entry.isUrl) {
        std::wstring url = entry.title;
        if (url.find(L"://") == std::wstring::npos &&
            _wcsnicmp(url.c_str(), L"www.", 4) != 0) {
            url = L"https://" + url;
        } else if (_wcsnicmp(url.c_str(), L"www.", 4) == 0) {
            url = L"https://" + url;
        }
        ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }

    // 安装键盘钩子
    if (!g_hPwBatchHook) {
        g_hPwBatchHook = SetWindowsHookExW(WH_KEYBOARD_LL, PwBatchKeyboardProc,
                                            GetModuleHandleW(NULL), 0);
    }

    if (g_isNotificationEnabled) {
        ShowTrayBalloon(hwnd, L"密码库",
                        L"账号已复制，按 Ctrl+V 粘贴账号");
    }
}

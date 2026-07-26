#include "text_utils.h"

#include <cwchar>

// 检测文本是否为 IP 地址(简单格式:数字.数字.数字.数字,可带端口)
bool IsIPAddress(const std::wstring& text) {
    if (text.empty() || text.length() < 7) return false;
    int dotCount = 0;
    int digitCount = 0;
    for (size_t i = 0; i < text.length(); i++) {
        wchar_t ch = text[i];
        if (ch >= L'0' && ch <= L'9') {
            digitCount++;
        } else if (ch == L'.') {
            if (digitCount == 0) return false;
            dotCount++;
            digitCount = 0;
        } else if (ch == L':' && dotCount == 3 && digitCount > 0) {
            for (size_t j = i + 1; j < text.length(); j++) {
                if (text[j] < L'0' || text[j] > L'9') return false;
            }
            return true;
        } else if (ch == L' ' || ch == L'/' || ch == L'\\') {
            break;
        } else {
            return false;
        }
    }
    return (dotCount == 3 && digitCount > 0);
}

LinkType GetLinkType(const std::wstring& text) {
    if (text.empty()) return LINK_NONE;
    if (text.length() > 4 && (
        _wcsnicmp(text.c_str(), L"http://", 7) == 0 ||
        _wcsnicmp(text.c_str(), L"https://", 8) == 0 ||
        _wcsnicmp(text.c_str(), L"ftp://", 6) == 0 ||
        _wcsnicmp(text.c_str(), L"www.", 4) == 0)) {
        return LINK_URL;
    }
    if (text.length() >= 3 &&
        ((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z')) &&
        text[1] == L':' &&
        (text[2] == L'\\' || text[2] == L'/')) {
        return LINK_FILE_PATH;
    }
    if (IsIPAddress(text)) {
        return LINK_IP;
    }
    if (IsEmailAddress(text)) {
        return LINK_EMAIL;
    }
    return LINK_NONE;
}

// 检测文本是否为邮箱地址：local@domain.tld
bool IsEmailAddress(const std::wstring& text) {
    if (text.empty() || text.length() < 5) return false;
    // 必须包含且仅包含一个 @
    size_t atPos = text.find(L'@');
    if (atPos == std::wstring::npos || atPos == 0) return false;
    if (text.find(L'@', atPos + 1) != std::wstring::npos) return false;
    // @ 后必须有一个点
    size_t dotPos = text.find(L'.', atPos + 1);
    if (dotPos == std::wstring::npos || dotPos == atPos + 1 ||
        dotPos == text.length() - 1) return false;
    // local 部分：字母/数字/._%+-
    for (size_t i = 0; i < atPos; i++) {
        wchar_t ch = text[i];
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
              (ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L'_' ||
              ch == L'%' || ch == L'+' || ch == L'-')) {
            return false;
        }
    }
    // domain 部分：字母/数字/.-
    for (size_t i = atPos + 1; i < text.length(); i++) {
        wchar_t ch = text[i];
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
              (ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L'-')) {
            return false;
        }
    }
    return true;
}

bool IsLinkText(const std::wstring& text) {
    return GetLinkType(text) != LINK_NONE;
}

// 统计文本的实际字数(中文 / 英文字母 / 数字,其他字符忽略)
int CountWords(const std::wstring& text) {
    int count = 0;
    for (size_t i = 0; i < text.length(); i++) {
        wchar_t ch = text[i];
        if ((ch >= 0x4E00 && ch <= 0x9FA5) ||
            (ch >= L'a' && ch <= L'z') ||
            (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'0' && ch <= L'9')) {
            count++;
        }
    }
    return count;
}

std::wstring TruncateToWordCount(const std::wstring& text, int maxWords) {
    int count = 0;
    size_t pos = 0;

    for (size_t i = 0; i < text.length(); i++) {
        wchar_t ch = text[i];
        if ((ch >= 0x4E00 && ch <= 0x9FA5) ||
            (ch >= L'a' && ch <= L'z') ||
            (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'0' && ch <= L'9')) {
            count++;
            if (count > maxWords) {
                pos = i;
                break;
            }
        }
    }

    if (count > maxWords && pos > 0) {
        return text.substr(0, pos) + L"...";
    }
    return text;
}

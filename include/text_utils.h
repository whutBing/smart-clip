#pragma once

#include <string>

enum LinkType {
    LINK_NONE = 0,
    LINK_URL,       // 浏览器网址
    LINK_FILE_PATH, // 本地盘符路径
    LINK_IP,        // IP 地址
    LINK_EMAIL      // 邮箱地址
};

bool IsIPAddress(const std::wstring& text);
bool IsEmailAddress(const std::wstring& text);
LinkType GetLinkType(const std::wstring& text);
bool IsLinkText(const std::wstring& text);

int CountWords(const std::wstring& text);
std::wstring TruncateToWordCount(const std::wstring& text, int maxWords);

#include "history.h"
#include <psapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <algorithm>
#include <ctime>
#include "tray.h"
#include "search.h"  // 添加这个头文件以访问 g_hwndTabControl
#include "settings.h"  // 添加这个头文件以访问 g_isNotificationEnabled

// 全局变量定义
std::vector<ClipboardItem> g_history;
HWND g_hwndListBox;
std::wstring g_searchKeyword;
int g_currentTab = 0;  // 当前选中的标签页索引（0=全部，1=文本，2=图像，3=文件，4=收藏）
std::vector<int> g_displayIndexMap;  // 显示索引到实际历史记录索引的映射
std::map<int, bool> g_expandedItems;  // 记录每个历史项的展开状态（key为g_history索引）

// 标签系统全局变量
std::vector<Tag> g_tags;              // 全局标签列表
int g_currentFilterTagId = 0;         // 当前筛选的标签ID（-1=全部收藏，0=未筛选）
static int g_nextTagId = 1;           // 下一个标签ID

// 获取数据文件路径
std::wstring GetDataFilePath() {
    WCHAR szPath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szPath) != S_OK) {
        if (GetCurrentDirectoryW(MAX_PATH, szPath) == 0) {
            wcscpy_s(szPath, L".");
        }
    }
    
    std::wstring dataPath = szPath;
    dataPath += L"\\SmartClip";
    CreateDirectoryW(dataPath.c_str(), NULL);
    
    return dataPath + L"\\history.txt";
}

// 保存历史记录到文件（修复编码问题）
void SaveHistory() {
    std::wstring filePath = GetDataFilePath();

    // 使用Windows API的文件操作函数，确保Unicode编码正确
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD dwBytesWritten = 0;

        // 写入UTF-8 BOM
        const BYTE bom[3] = {0xEF, 0xBB, 0xBF};
        WriteFile(hFile, bom, sizeof(bom), &dwBytesWritten, NULL);

        for (size_t i = 0; i < g_history.size(); i++) {
            const auto& item = g_history[i];
            std::wstring content = L"===TYPE===\r\n";
            content += std::to_wstring(item.type) + L"\r\n";
            content += L"===CONTENT===\r\n";
            content += item.content + L"\r\n";
            content += L"===TIMESTAMP===\r\n";
            content += item.timestamp + L"\r\n";
            content += L"===SOURCEAPP===\r\n";
            content += item.sourceApp + L"\r\n";
            content += L"===SOURCEAPPPATH===\r\n";
            content += item.sourceAppPath + L"\r\n";
            content += L"===FAVORITE===\r\n";
            content += (item.isFavorite ? L"1" : L"0") + std::wstring(L"\r\n");

            // 保存标签ID列表
            content += L"===TAGS===\r\n";
            std::wstring tagIdsStr;
            for (int tagId : item.tagIds) {
                if (!tagIdsStr.empty()) tagIdsStr += L",";
                tagIdsStr += std::to_wstring(tagId);
            }
            content += tagIdsStr + L"\r\n";

            // 如果是图像类型，保存图像信息
            if (item.type == TYPE_IMAGE) {
                content += L"===IMAGEWIDTH===\r\n";
                content += std::to_wstring(item.imageWidth) + L"\r\n";
                content += L"===IMAGEHEIGHT===\r\n";
                content += std::to_wstring(item.imageHeight) + L"\r\n";
                content += L"===THUMBWIDTH===\r\n";
                content += std::to_wstring(item.thumbWidth) + L"\r\n";
                content += L"===THUMBHEIGHT===\r\n";
                content += std::to_wstring(item.thumbHeight) + L"\r\n";
                content += L"===IMAGEFILENAME===\r\n";
                content += item.imageFileName + L"\r\n";
                content += L"===IMAGEFILEPATH===\r\n";
                content += item.imageFilePath + L"\r\n";

                // 保存缩略图数据到单独的文件
                std::wstring thumbFileName = L"thumb_" + std::to_wstring(i) + L".dat";
                content += L"===THUMBFILE===\r\n";
                content += thumbFileName + L"\r\n";

                // 保存缩略图数据到 thumbs 目录
                std::wstring thumbFilePath = GetThumbsPath() + L"\\" + thumbFileName;

                // 保存缩略图数据
                if (!item.imageData.empty()) {
                    HANDLE hThumbFile = CreateFileW(thumbFilePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hThumbFile != INVALID_HANDLE_VALUE) {
                        DWORD dwThumbBytesWritten = 0;
                        WriteFile(hThumbFile, &item.imageData[0], item.imageData.size(), &dwThumbBytesWritten, NULL);
                        CloseHandle(hThumbFile);
                    }
                }
            }

            content += L"===END===\r\n";

            // 转换为UTF-8
            int utf8Length = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, NULL, 0, NULL, NULL);
            if (utf8Length > 0) {
                std::vector<char> utf8Content(utf8Length);
                WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, &utf8Content[0], utf8Length, NULL, NULL);
                WriteFile(hFile, &utf8Content[0], utf8Length - 1, &dwBytesWritten, NULL);
            }
        }

        CloseHandle(hFile);
    }

    UpdateListBox();
}

// 加载历史记录（修复编码问题）
void LoadHistory() {
    std::wstring filePath = GetDataFilePath();

    // 使用Windows API的文件操作函数，确保Unicode编码正确
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD dwFileSize = GetFileSize(hFile, NULL);
        if (dwFileSize > 0) {
            std::vector<BYTE> fileContent(dwFileSize);
            DWORD dwBytesRead = 0;
            if (ReadFile(hFile, &fileContent[0], dwFileSize, &dwBytesRead, NULL)) {
                // 检查并跳过UTF-8 BOM
                DWORD offset = 0;
                if (dwFileSize >= 3 && fileContent[0] == 0xEF && fileContent[1] == 0xBB && fileContent[2] == 0xBF) {
                    offset = 3;
                }

                // 转换为Unicode
                int unicodeLength = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)(&fileContent[offset]), dwFileSize - offset, NULL, 0);
                if (unicodeLength > 0) {
                    std::vector<wchar_t> unicodeContent(unicodeLength + 1);
                    MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)(&fileContent[offset]), dwFileSize - offset, &unicodeContent[0], unicodeLength);
                    unicodeContent[unicodeLength] = L'\0';
                    // 解析内容
                    ClipboardItem item;
                    item.type = TYPE_TEXT;  // 默认类型
                    item.isFavorite = false;
                    item.isInTransferStation = false;  // 初始化为false
                    item.imageWidth = 0;
                    item.imageHeight = 0;
                    item.thumbWidth = 0;
                    item.thumbHeight = 0;
                    std::wstring thumbFileName;
                    enum { NONE, TYPE, CONTENT, TIMESTAMP, SOURCEAPP, SOURCEAPPPATH, FAVORITE, TAGS,
                           IMAGEWIDTH, IMAGEHEIGHT, THUMBWIDTH, THUMBHEIGHT, IMAGEFILENAME, IMAGEFILEPATH, THUMBFILE,
                           IMAGEFILE } state = NONE;  // IMAGEFILE 用于兼容旧格式
                    wchar_t* pLine = &unicodeContent[0];

                    while (pLine != NULL && *pLine != L'\0') {
                        wchar_t* pNextLine = wcsstr(pLine, L"\r\n");
                        if (pNextLine != NULL) {
                            *pNextLine = L'\0';
                            pNextLine += 2;
                        }

                        if (wcscmp(pLine, L"===TYPE===") == 0) {
                            state = TYPE;
                        } else if (wcscmp(pLine, L"===CONTENT===") == 0) {
                            state = CONTENT;
                            item.content.clear();
                        } else if (wcscmp(pLine, L"===TIMESTAMP===") == 0) {
                            state = TIMESTAMP;
                        } else if (wcscmp(pLine, L"===SOURCEAPP===") == 0) {
                            state = SOURCEAPP;
                        } else if (wcscmp(pLine, L"===SOURCEAPPPATH===") == 0) {
                            state = SOURCEAPPPATH;
                        } else if (wcscmp(pLine, L"===FAVORITE===") == 0) {
                            state = FAVORITE;
                        } else if (wcscmp(pLine, L"===TAGS===") == 0) {
                            state = TAGS;
                        } else if (wcscmp(pLine, L"===IMAGEWIDTH===") == 0) {
                            state = IMAGEWIDTH;
                        } else if (wcscmp(pLine, L"===IMAGEHEIGHT===") == 0) {
                            state = IMAGEHEIGHT;
                        } else if (wcscmp(pLine, L"===THUMBWIDTH===") == 0) {
                            state = THUMBWIDTH;
                        } else if (wcscmp(pLine, L"===THUMBHEIGHT===") == 0) {
                            state = THUMBHEIGHT;
                        } else if (wcscmp(pLine, L"===IMAGEFILENAME===") == 0) {
                            state = IMAGEFILENAME;
                        } else if (wcscmp(pLine, L"===IMAGEFILEPATH===") == 0) {
                            state = IMAGEFILEPATH;
                        } else if (wcscmp(pLine, L"===THUMBFILE===") == 0) {
                            state = THUMBFILE;
                        } else if (wcscmp(pLine, L"===IMAGEFILE===") == 0) {
                            state = IMAGEFILE;  // 兼容旧格式
                        } else if (wcscmp(pLine, L"===END===") == 0) {
                            // 如果是图像类型，加载缩略图数据
                            if (item.type == TYPE_IMAGE && !thumbFileName.empty()) {
                                // 先尝试从 thumbs 目录加载
                                std::wstring thumbFilePath = GetThumbsPath() + L"\\" + thumbFileName;

                                // 加载缩略图数据
                                HANDLE hThumbFile = CreateFileW(thumbFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

                                // 如果 thumbs 目录找不到，尝试从旧的 SmartClip 目录加载（兼容旧数据）
                                if (hThumbFile == INVALID_HANDLE_VALUE) {
                                    WCHAR szPath[MAX_PATH];
                                    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szPath) == S_OK) {
                                        std::wstring oldPath = szPath;
                                        oldPath += L"\\SmartClip\\" + thumbFileName;
                                        hThumbFile = CreateFileW(oldPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                                    }
                                }

                                if (hThumbFile != INVALID_HANDLE_VALUE) {
                                    DWORD dwThumbFileSize = GetFileSize(hThumbFile, NULL);
                                    if (dwThumbFileSize > 0) {
                                        item.imageData.resize(dwThumbFileSize);
                                        DWORD dwThumbBytesRead = 0;
                                        ReadFile(hThumbFile, &item.imageData[0], dwThumbFileSize, &dwThumbBytesRead, NULL);
                                    }
                                    CloseHandle(hThumbFile);
                                }
                            }

                            // 去除内容末尾的空白字符（包括换行符）
                            while (!item.content.empty() &&
                                   (item.content.back() == L'\r' || item.content.back() == L'\n' ||
                                    item.content.back() == L' ' || item.content.back() == L'\t')) {
                                item.content.pop_back();
                            }

                            if (!item.content.empty() || item.type == TYPE_IMAGE) {
                                // 兼容旧格式：如果没有缩略图尺寸，使用原图尺寸
                                if (item.thumbWidth == 0) item.thumbWidth = item.imageWidth;
                                if (item.thumbHeight == 0) item.thumbHeight = item.imageHeight;
                                g_history.push_back(item);
                                item = ClipboardItem();
                                item.type = TYPE_TEXT;
                                item.isFavorite = false;
                                item.imageWidth = 0;
                                item.imageHeight = 0;
                                item.thumbWidth = 0;
                                item.thumbHeight = 0;
                                thumbFileName.clear();
                            }
                            state = NONE;
                        } else {
                            switch (state) {
                                case TYPE:
                                    item.type = (ClipboardItemType)_wtoi(pLine);
                                    break;
                                case CONTENT:
                                    item.content += pLine;
                                    if (pNextLine != NULL) {  // 如果不是最后一行，添加换行符
                                        item.content += L"\r\n";
                                    }
                                    break;
                                case TIMESTAMP:
                                    item.timestamp = pLine;
                                    break;
                                case SOURCEAPP:
                                    item.sourceApp = pLine;
                                    break;
                                case SOURCEAPPPATH:
                                    item.sourceAppPath = pLine;
                                    break;
                                case FAVORITE:
                                    item.isFavorite = (_wtoi(pLine) == 1);
                                    break;
                                case TAGS:
                                    // 解析标签ID列表（逗号分隔）
                                    item.tagIds.clear();
                                    if (wcslen(pLine) > 0) {
                                        std::wstring tagsStr = pLine;
                                        size_t pos = 0;
                                        while ((pos = tagsStr.find(L',')) != std::wstring::npos) {
                                            int tagId = _wtoi(tagsStr.substr(0, pos).c_str());
                                            if (tagId > 0) item.tagIds.insert(tagId);
                                            tagsStr = tagsStr.substr(pos + 1);
                                        }
                                        if (!tagsStr.empty()) {
                                            int tagId = _wtoi(tagsStr.c_str());
                                            if (tagId > 0) item.tagIds.insert(tagId);
                                        }
                                        // 同步 isFavorite 状态
                                        item.isFavorite = !item.tagIds.empty();
                                    }
                                    break;
                                case IMAGEWIDTH:
                                    item.imageWidth = _wtoi(pLine);
                                    break;
                                case IMAGEHEIGHT:
                                    item.imageHeight = _wtoi(pLine);
                                    break;
                                case THUMBWIDTH:
                                    item.thumbWidth = _wtoi(pLine);
                                    break;
                                case THUMBHEIGHT:
                                    item.thumbHeight = _wtoi(pLine);
                                    break;
                                case IMAGEFILENAME:
                                    item.imageFileName = pLine;
                                    break;
                                case IMAGEFILEPATH:
                                    item.imageFilePath = pLine;
                                    break;
                                case THUMBFILE:
                                    thumbFileName = pLine;
                                    break;
                                case IMAGEFILE:
                                    // 兼容旧格式：旧的 IMAGEFILE 当作 THUMBFILE 使用
                                    thumbFileName = pLine;
                                    break;
                                default:
                                    break;
                            }
                        }

                        pLine = pNextLine;
                    }

                    // 处理最后一个item
                    if (item.type == TYPE_IMAGE && !thumbFileName.empty()) {
                        // 先尝试从 thumbs 目录加载
                        std::wstring thumbFilePath = GetThumbsPath() + L"\\" + thumbFileName;

                        HANDLE hThumbFile = CreateFileW(thumbFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

                        // 如果 thumbs 目录找不到，尝试从旧的 SmartClip 目录加载（兼容旧数据）
                        if (hThumbFile == INVALID_HANDLE_VALUE) {
                            WCHAR szPath[MAX_PATH];
                            if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szPath) == S_OK) {
                                std::wstring oldPath = szPath;
                                oldPath += L"\\SmartClip\\" + thumbFileName;
                                hThumbFile = CreateFileW(oldPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                            }
                        }

                        if (hThumbFile != INVALID_HANDLE_VALUE) {
                            DWORD dwThumbFileSize = GetFileSize(hThumbFile, NULL);
                            if (dwThumbFileSize > 0) {
                                item.imageData.resize(dwThumbFileSize);
                                DWORD dwThumbBytesRead = 0;
                                ReadFile(hThumbFile, &item.imageData[0], dwThumbFileSize, &dwThumbBytesRead, NULL);
                            }
                            CloseHandle(hThumbFile);
                        }
                    }

                    // 去除内容末尾的空白字符（包括换行符）
                    while (!item.content.empty() &&
                           (item.content.back() == L'\r' || item.content.back() == L'\n' ||
                            item.content.back() == L' ' || item.content.back() == L'\t')) {
                        item.content.pop_back();
                    }

                    if (!item.content.empty() || item.type == TYPE_IMAGE) {
                        // 兼容旧格式
                        if (item.thumbWidth == 0) item.thumbWidth = item.imageWidth;
                        if (item.thumbHeight == 0) item.thumbHeight = item.imageHeight;
                        g_history.push_back(item);
                    }
                }
            }
        }

        CloseHandle(hFile);
    }

    UpdateListBox();
}

// 获取当前活动窗口的进程名
std::wstring GetActiveWindowProcessName() {
    HWND hwndForeground = GetForegroundWindow();
    if (hwndForeground == NULL) {
        return L"Unknown";
    }

    DWORD dwProcessId;
    GetWindowThreadProcessId(hwndForeground, &dwProcessId);

    // 使用 PROCESS_QUERY_LIMITED_INFORMATION 权限，更安全
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwProcessId);
    if (hProcess == NULL) {
        return L"Unknown";
    }

    WCHAR szProcessName[MAX_PATH];
    DWORD size = MAX_PATH;
    if (!QueryFullProcessImageNameW(hProcess, 0, szProcessName, &size)) {
        CloseHandle(hProcess);
        return L"Unknown";
    }

    CloseHandle(hProcess);

    WCHAR* pFileName = wcsrchr(szProcessName, L'\\');
    if (pFileName != NULL) {
        return pFileName + 1;
    }

    return szProcessName;
}

// 获取当前活动窗口的进程完整路径
std::wstring GetActiveWindowProcessPath() {
    HWND hwndForeground = GetForegroundWindow();
    if (hwndForeground == NULL) {
        return L"";
    }

    DWORD dwProcessId;
    GetWindowThreadProcessId(hwndForeground, &dwProcessId);

    // 使用 PROCESS_QUERY_LIMITED_INFORMATION 权限，更安全
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwProcessId);
    if (hProcess == NULL) {
        return L"";
    }

    WCHAR szProcessPath[MAX_PATH];
    DWORD size = MAX_PATH;
    if (!QueryFullProcessImageNameW(hProcess, 0, szProcessPath, &size)) {
        CloseHandle(hProcess);
        return L"";
    }

    CloseHandle(hProcess);
    return szProcessPath;
}

// 图标缓存
static std::map<std::wstring, HICON> g_iconCache;

// 获取应用图标
HICON GetAppIcon(const std::wstring& exePath) {
    if (exePath.empty()) {
        return NULL;
    }

    // 检查缓存
    auto it = g_iconCache.find(exePath);
    if (it != g_iconCache.end()) {
        return it->second;
    }

    // 从文件获取图标
    SHFILEINFOW sfi = {0};
    if (SHGetFileInfoW(exePath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON)) {
        g_iconCache[exePath] = sfi.hIcon;
        return sfi.hIcon;
    }

    return NULL;
}

// 清理图标缓存
void ClearIconCache() {
    for (auto& pair : g_iconCache) {
        if (pair.second != NULL) {
            DestroyIcon(pair.second);
        }
    }
    g_iconCache.clear();
}

// 获取当前时间字符串
std::wstring GetCurrentTimeString() {
    time_t now = time(NULL);
    struct tm* tm_ptr = localtime(&now);
    if (tm_ptr == NULL) {
        return L"";
    }
    struct tm tm = *tm_ptr;

    WCHAR szTime[32];
    swprintf_s(szTime, L"%04d-%02d-%02d %02d:%02d:%02d",
              tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
              tm.tm_hour, tm.tm_min, tm.tm_sec);

    return szTime;
}

// 将时间字符串转换为相对时间显示
std::wstring GetRelativeTimeString(const std::wstring& timeStr) {
    // 解析时间字符串 "YYYY-MM-DD HH:MM:SS"
    if (timeStr.length() < 19) {
        return timeStr; // 格式不正确，返回原字符串
    }

    struct tm tm = {};
    swscanf_s(timeStr.c_str(), L"%d-%d-%d %d:%d:%d",
              &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
              &tm.tm_hour, &tm.tm_min, &tm.tm_sec);

    tm.tm_year -= 1900;
    tm.tm_mon -= 1;

    time_t itemTime = mktime(&tm);
    time_t now = time(NULL);

    if (itemTime == -1) {
        return timeStr; // 转换失败，返回原字符串
    }

    // 计算时间差（秒）
    double diff = difftime(now, itemTime);

    if (diff < 0) {
        return timeStr; // 未来时间，返回原字符串
    }

    // 1分钟以内
    if (diff < 60) {
        return L"刚刚";
    }

    // 1小时以内
    if (diff < 3600) {
        int minutes = (int)(diff / 60);
        return std::to_wstring(minutes) + L"分钟前";
    }

    // 24小时以内
    if (diff < 86400) {
        int hours = (int)(diff / 3600);
        return std::to_wstring(hours) + L"小时前";
    }

    // 30天以内
    if (diff < 2592000) {
        int days = (int)(diff / 86400);
        return std::to_wstring(days) + L"天前";
    }

    // 超过30天，显示原始日期（只显示日期部分）
    return timeStr.substr(0, 10); // 返回 "YYYY-MM-DD"
}


// 更新列表框
void UpdateListBox() {
    // 禁用重绘，避免闪烁
    SendMessageW(g_hwndListBox, WM_SETREDRAW, FALSE, 0);

    SendMessageW(g_hwndListBox, LB_RESETCONTENT, 0, 0);
    g_displayIndexMap.clear();  // 清空索引映射
    // g_expandedItems 不需要清空，因为它是用 actualIndex 作为 key 的 map

    // 统计收藏数量
    int favoriteCount = 0;
    for (const auto& item : g_history) {
        if (item.isFavorite) {
            favoriteCount++;
        }
    }

    // 更新收藏按钮的标题
    if (g_hwndFilterFavorite != NULL) {
        std::wstring favoriteTabText = L"收藏(" + std::to_wstring(favoriteCount) + L")";
        SetWindowTextW(g_hwndFilterFavorite, favoriteTabText.c_str());
    }

    for (int i = 0; i < (int)g_history.size(); i++) {
        const auto& item = g_history[i];

        // 应用标签页过滤
        if (g_currentTab == 1 && item.type != TYPE_TEXT) continue;  // 文本标签页
        if (g_currentTab == 2 && item.type != TYPE_IMAGE) continue; // 图像标签页
        if (g_currentTab == 3 && item.type != TYPE_FILE) continue;  // 文件标签页
        if (g_currentTab == 4) {  // 收藏标签页
            if (g_currentFilterTagId > 0) {
                // 按特定标签筛选
                if (item.tagIds.count(g_currentFilterTagId) == 0) continue;
            } else {
                // 显示所有收藏（有任意标签的项目）
                if (!item.isFavorite) continue;
            }
        }

        // 应用搜索过滤
        if (!g_searchKeyword.empty()) {
            // 检查内容是否包含搜索关键词（不区分大小写）
            std::wstring lowerContent = item.content;
            std::wstring lowerKeyword = g_searchKeyword;

            // 转换为小写进行比较
            std::transform(lowerContent.begin(), lowerContent.end(), lowerContent.begin(), ::towlower);
            std::transform(lowerKeyword.begin(), lowerKeyword.end(), lowerKeyword.begin(), ::towlower);

            if (lowerContent.find(lowerKeyword) == std::wstring::npos) {
                continue; // 不匹配，跳过此项
            }
        }

        // 记录显示索引到实际索引的映射
        g_displayIndexMap.push_back(i);
        // g_expandedItems[i] 会在需要时自动创建，默认为 false

        std::wstring displayText = GetRelativeTimeString(item.timestamp) + L" - " + item.sourceApp + L"\r\n";

        switch (item.type) {
            case TYPE_TEXT:
                // 不截断文本，显示完整内容
                displayText += item.content;
                break;
            case TYPE_IMAGE:
                displayText += L"[图像] " + std::to_wstring(item.imageWidth) + L"x" + std::to_wstring(item.imageHeight);
                break;
            case TYPE_FILE:
                displayText += item.content;
                break;
        }

        SendMessageW(g_hwndListBox, LB_ADDSTRING, 0, (LPARAM)displayText.c_str());
    }

    // 重新启用重绘并强制刷新
    SendMessageW(g_hwndListBox, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(g_hwndListBox, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);

    // 重置列表滚动位置到顶部
    SendMessageW(g_hwndListBox, LB_SETTOPINDEX, 0, 0);
    g_listBoxTopIndex = 0;
    g_currentPage = 0;

    // 更新翻页状态
    int totalItems = (int)g_displayIndexMap.size();
    g_totalPages = (totalItems + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    if (g_totalPages < 1) g_totalPages = 1;

    // 更新翻页按钮状态（强制立即重绘）
    if (g_hwndPageUpBtn) {
        InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
        UpdateWindow(g_hwndPageUpBtn);
    }
    if (g_hwndPageDownBtn) {
        InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
        UpdateWindow(g_hwndPageDownBtn);
    }
}

// 添加内容到历史记录
void AddToHistory(const std::wstring& content) {
    // 检查是否已存在相同内容
    auto it = std::find_if(g_history.begin(), g_history.end(), [&content](const ClipboardItem& item) {
        return item.type == TYPE_TEXT && item.content == content;
    });

    if (it != g_history.end()) {
        // 如果已存在，移除旧的记录
        g_history.erase(it);
    }

    ClipboardItem item;
    item.type = TYPE_TEXT;
    item.content = content;
    item.timestamp = GetCurrentTimeString();
    item.sourceApp = GetActiveWindowProcessName();
    item.sourceAppPath = GetActiveWindowProcessPath();
    item.imageWidth = 0;
    item.imageHeight = 0;
    item.isFavorite = false;
    item.isInTransferStation = false;

    // 限制历史记录数量
    if (g_history.size() >= 20) {
        g_history.pop_back();
    }

    // 将新记录添加到最前面
    g_history.insert(g_history.begin(), item);

    UpdateListBox();

    // 先显示气泡提示，再保存文件
    if (g_hwndMain != NULL && g_isNotificationEnabled) {
        std::wstring preview = item.content.substr(0, 10);
        if (item.content.length() > 10) {
            preview += L"...";
        }
        ShowTrayBalloon(g_hwndMain, L"新内容", preview.c_str());
    }

    SaveHistory();
}

// 添加图像到历史记录
void AddImageToHistory(const std::vector<BYTE>& imageData, int width, int height) {
    ClipboardItem item;
    item.type = TYPE_IMAGE;
    item.content = L"[图像]";
    item.timestamp = GetCurrentTimeString();
    item.sourceApp = GetActiveWindowProcessName();
    item.sourceAppPath = GetActiveWindowProcessPath();
    item.imageWidth = width;
    item.imageHeight = height;
    item.isFavorite = false;
    item.isInTransferStation = false;

    // 生成唯一文件名
    item.imageFileName = GenerateImageFileName();

    // 保存原图到文件
    SaveOriginalImage(imageData, width, height, item.imageFileName);

    // 生成缩略图并存储到内存
    std::vector<BYTE> thumbData;
    int thumbWidth, thumbHeight;

    // 根据图片预览质量设置确定缩略图大小
    int thumbMaxSize = 128;  // 默认标清
    switch (g_imagePreviewQuality) {
        case PREVIEW_OFF:
        case PREVIEW_BLUR:
            thumbMaxSize = 64;
            break;
        case PREVIEW_SD:
            thumbMaxSize = 128;
            break;
        case PREVIEW_HD:
            thumbMaxSize = 256;
            break;
    }

    if (GenerateThumbnail(imageData, width, height, thumbData, thumbWidth, thumbHeight, thumbMaxSize)) {
        item.imageData = thumbData;  // 内存中只保存缩略图
        item.thumbWidth = thumbWidth;
        item.thumbHeight = thumbHeight;
    } else {
        // 如果生成缩略图失败，使用原图（兼容处理）
        item.imageData = imageData;
        item.thumbWidth = width;
        item.thumbHeight = height;
    }

    // 限制历史记录数量
    if (g_history.size() >= 20) {
        g_history.pop_back();
    }

    // 将新记录添加到最前面
    g_history.insert(g_history.begin(), item);

    UpdateListBox();

    if (g_hwndMain != NULL && g_isNotificationEnabled) {
        ShowTrayBalloon(g_hwndMain, L"新内容", L"图像已复制");
    }

    SaveHistory();
}

// 添加文件到历史记录
void AddFileToHistory(const std::wstring& filePath) {
    ClipboardItem item;
    item.type = TYPE_FILE;
    item.content = filePath;
    item.timestamp = GetCurrentTimeString();
    item.sourceApp = GetActiveWindowProcessName();
    item.sourceAppPath = GetActiveWindowProcessPath();
    item.imageWidth = 0;
    item.imageHeight = 0;
    item.isFavorite = false;
    item.isInTransferStation = false;

    // 限制历史记录数量
    if (g_history.size() >= 20) {
        g_history.pop_back();
    }

    // 将新记录添加到最前面
    g_history.insert(g_history.begin(), item);

    UpdateListBox();

    if (g_hwndMain != NULL && g_isNotificationEnabled) {
        ShowTrayBalloon(g_hwndMain, L"新内容", L"文件路径已复制");
    }

    SaveHistory();
}

// 添加图片文件到历史记录（只保存缩略图和原始路径，不复制原图）
void AddImageFileToHistory(const std::wstring& filePath, const std::vector<BYTE>& imageData, int width, int height) {
    ClipboardItem item;
    item.type = TYPE_IMAGE;
    item.content = filePath;  // 保存原始文件路径
    item.timestamp = GetCurrentTimeString();
    item.sourceApp = GetActiveWindowProcessName();
    item.sourceAppPath = GetActiveWindowProcessPath();
    item.imageWidth = width;
    item.imageHeight = height;
    item.isFavorite = false;
    item.isInTransferStation = false;
    item.imageFileName = L"";  // 不保存原图到程序目录
    item.imageFilePath = filePath;  // 保存原始图片文件路径

    // 生成缩略图并存储到内存
    std::vector<BYTE> thumbData;
    int thumbWidth, thumbHeight;

    // 根据图片预览质量设置确定缩略图大小
    int thumbMaxSize = 128;  // 默认标清
    switch (g_imagePreviewQuality) {
        case PREVIEW_OFF:
        case PREVIEW_BLUR:
            thumbMaxSize = 64;
            break;
        case PREVIEW_SD:
            thumbMaxSize = 128;
            break;
        case PREVIEW_HD:
            thumbMaxSize = 256;
            break;
    }

    if (GenerateThumbnail(imageData, width, height, thumbData, thumbWidth, thumbHeight, thumbMaxSize)) {
        item.imageData = thumbData;  // 内存中只保存缩略图
        item.thumbWidth = thumbWidth;
        item.thumbHeight = thumbHeight;
    } else {
        // 缩略图生成失败，使用原始数据
        item.imageData = imageData;
        item.thumbWidth = width;
        item.thumbHeight = height;
    }

    // 限制历史记录数量
    if (g_history.size() >= 20) {
        g_history.pop_back();
    }

    // 将新记录添加到最前面
    g_history.insert(g_history.begin(), item);

    UpdateListBox();

    if (g_hwndMain != NULL && g_isNotificationEnabled) {
        ShowTrayBalloon(g_hwndMain, L"新内容", L"图片文件已添加");
    }

    SaveHistory();
}

// 获取图片存储目录（原图）
std::wstring GetImagesPath() {
    WCHAR szPath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szPath) != S_OK) {
        if (GetCurrentDirectoryW(MAX_PATH, szPath) == 0) {
            wcscpy_s(szPath, L".");
        }
    }

    std::wstring imagesPath = szPath;
    imagesPath += L"\\SmartClip\\images\\originals";

    // 创建目录（包括父目录）
    std::wstring parentPath = szPath;
    parentPath += L"\\SmartClip\\images";
    CreateDirectoryW((std::wstring(szPath) + L"\\SmartClip").c_str(), NULL);
    CreateDirectoryW(parentPath.c_str(), NULL);
    CreateDirectoryW(imagesPath.c_str(), NULL);

    return imagesPath;
}

// 获取缩略图存储目录
std::wstring GetThumbsPath() {
    WCHAR szPath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szPath) != S_OK) {
        if (GetCurrentDirectoryW(MAX_PATH, szPath) == 0) {
            wcscpy_s(szPath, L".");
        }
    }

    std::wstring thumbsPath = szPath;
    thumbsPath += L"\\SmartClip\\images\\thumbs";

    // 创建目录（包括父目录）
    std::wstring parentPath = szPath;
    parentPath += L"\\SmartClip\\images";
    CreateDirectoryW((std::wstring(szPath) + L"\\SmartClip").c_str(), NULL);
    CreateDirectoryW(parentPath.c_str(), NULL);
    CreateDirectoryW(thumbsPath.c_str(), NULL);

    return thumbsPath;
}

// 生成唯一图片文件名
std::wstring GenerateImageFileName() {
    // 使用时间戳 + 随机数生成唯一文件名
    SYSTEMTIME st;
    GetLocalTime(&st);

    WCHAR fileName[64];
    swprintf_s(fileName, L"%04d%02d%02d_%02d%02d%02d_%03d_%04d.png",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond,
        st.wMilliseconds, rand() % 10000);

    return fileName;
}

// 保存原图到文件
bool SaveOriginalImage(const std::vector<BYTE>& imageData, int width, int height, const std::wstring& fileName) {
    std::wstring filePath = GetImagesPath() + L"\\" + fileName;

    // 创建位图文件
    BITMAPFILEHEADER bfh = {};
    bfh.bfType = 0x4D42;  // "BM"
    bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + imageData.size();
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = width;
    bih.biHeight = -height;  // 负值表示从上到下
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = imageData.size();

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD dwBytesWritten;
    WriteFile(hFile, &bfh, sizeof(bfh), &dwBytesWritten, NULL);
    WriteFile(hFile, &bih, sizeof(bih), &dwBytesWritten, NULL);
    WriteFile(hFile, &imageData[0], imageData.size(), &dwBytesWritten, NULL);

    CloseHandle(hFile);
    return true;
}

// 生成缩略图
bool GenerateThumbnail(const std::vector<BYTE>& imageData, int width, int height,
                       std::vector<BYTE>& thumbData, int& thumbWidth, int& thumbHeight, int maxSize) {
    // 计算缩略图尺寸，保持宽高比
    float scale = 1.0f;
    if (width > height) {
        if (width > maxSize) {
            scale = (float)maxSize / width;
        }
    } else {
        if (height > maxSize) {
            scale = (float)maxSize / height;
        }
    }

    thumbWidth = (int)(width * scale);
    thumbHeight = (int)(height * scale);

    if (thumbWidth < 1) thumbWidth = 1;
    if (thumbHeight < 1) thumbHeight = 1;

    // 创建源位图
    HDC hdcScreen = GetDC(NULL);
    HDC hdcSrc = CreateCompatibleDC(hdcScreen);
    HDC hdcDst = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmiSrc = {};
    bmiSrc.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmiSrc.bmiHeader.biWidth = width;
    bmiSrc.bmiHeader.biHeight = -height;
    bmiSrc.bmiHeader.biPlanes = 1;
    bmiSrc.bmiHeader.biBitCount = 24;
    bmiSrc.bmiHeader.biCompression = BI_RGB;

    void* pSrcBits = NULL;
    HBITMAP hbmSrc = CreateDIBSection(hdcScreen, &bmiSrc, DIB_RGB_COLORS, &pSrcBits, NULL, 0);
    if (!hbmSrc || !pSrcBits) {
        DeleteDC(hdcSrc);
        DeleteDC(hdcDst);
        ReleaseDC(NULL, hdcScreen);
        return false;
    }
    memcpy(pSrcBits, &imageData[0], imageData.size());

    // 创建目标位图
    BITMAPINFO bmiDst = {};
    bmiDst.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmiDst.bmiHeader.biWidth = thumbWidth;
    bmiDst.bmiHeader.biHeight = -thumbHeight;
    bmiDst.bmiHeader.biPlanes = 1;
    bmiDst.bmiHeader.biBitCount = 24;
    bmiDst.bmiHeader.biCompression = BI_RGB;

    void* pDstBits = NULL;
    HBITMAP hbmDst = CreateDIBSection(hdcScreen, &bmiDst, DIB_RGB_COLORS, &pDstBits, NULL, 0);
    if (!hbmDst || !pDstBits) {
        DeleteObject(hbmSrc);
        DeleteDC(hdcSrc);
        DeleteDC(hdcDst);
        ReleaseDC(NULL, hdcScreen);
        return false;
    }

    // 缩放绘制
    HBITMAP hOldSrc = (HBITMAP)SelectObject(hdcSrc, hbmSrc);
    HBITMAP hOldDst = (HBITMAP)SelectObject(hdcDst, hbmDst);

    SetStretchBltMode(hdcDst, HALFTONE);
    SetBrushOrgEx(hdcDst, 0, 0, NULL);
    StretchBlt(hdcDst, 0, 0, thumbWidth, thumbHeight, hdcSrc, 0, 0, width, height, SRCCOPY);

    // 复制缩略图数据
    int rowBytes = ((thumbWidth * 3 + 3) / 4) * 4;  // 4字节对齐
    thumbData.resize(rowBytes * thumbHeight);
    memcpy(&thumbData[0], pDstBits, thumbData.size());

    // 清理
    SelectObject(hdcSrc, hOldSrc);
    SelectObject(hdcDst, hOldDst);
    DeleteObject(hbmSrc);
    DeleteObject(hbmDst);
    DeleteDC(hdcSrc);
    DeleteDC(hdcDst);
    ReleaseDC(NULL, hdcScreen);

    return true;
}

// 加载原图
bool LoadOriginalImage(const std::wstring& fileName, std::vector<BYTE>& imageData, int& width, int& height) {
    std::wstring filePath = GetImagesPath() + L"\\" + fileName;

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;
    DWORD dwBytesRead;

    ReadFile(hFile, &bfh, sizeof(bfh), &dwBytesRead, NULL);
    ReadFile(hFile, &bih, sizeof(bih), &dwBytesRead, NULL);

    width = bih.biWidth;
    height = abs(bih.biHeight);

    DWORD imageSize = GetFileSize(hFile, NULL) - sizeof(bfh) - sizeof(bih);
    imageData.resize(imageSize);
    ReadFile(hFile, &imageData[0], imageSize, &dwBytesRead, NULL);

    CloseHandle(hFile);
    return true;
}

// ==================== 标签管理函数 ====================

// 获取标签文件路径
std::wstring GetTagsFilePath() {
    WCHAR szPath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szPath) != S_OK) {
        if (GetCurrentDirectoryW(MAX_PATH, szPath) == 0) {
            wcscpy_s(szPath, L".");
        }
    }

    std::wstring dataPath = szPath;
    dataPath += L"\\SmartClip";
    CreateDirectoryW(dataPath.c_str(), NULL);

    return dataPath + L"\\tags.txt";
}

// 加载标签列表
void LoadTags() {
    g_tags.clear();
    g_nextTagId = 1;

    std::wstring filePath = GetTagsFilePath();
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        // 文件不存在，创建默认标签
        AddTag(L"重要", RGB(244, 67, 54));    // 红色
        AddTag(L"工作", RGB(33, 150, 243));   // 蓝色
        AddTag(L"个人", RGB(76, 175, 80));    // 绿色
        SaveTags();
        return;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return;
    }

    std::vector<char> buffer(fileSize + 1);
    DWORD dwBytesRead;
    ReadFile(hFile, &buffer[0], fileSize, &dwBytesRead, NULL);
    buffer[dwBytesRead] = '\0';
    CloseHandle(hFile);

    // 跳过UTF-8 BOM
    char* pData = &buffer[0];
    if (dwBytesRead >= 3 && (BYTE)pData[0] == 0xEF && (BYTE)pData[1] == 0xBB && (BYTE)pData[2] == 0xBF) {
        pData += 3;
    }

    // 转换为宽字符
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, pData, -1, NULL, 0);
    std::vector<wchar_t> wideBuffer(wideLen);
    MultiByteToWideChar(CP_UTF8, 0, pData, -1, &wideBuffer[0], wideLen);

    // 解析标签数据
    wchar_t* context = NULL;
    wchar_t* line = wcstok_s(&wideBuffer[0], L"\r\n", &context);
    while (line) {
        // 格式: ID|名称|颜色
        std::wstring lineStr = line;
        size_t pos1 = lineStr.find(L'|');
        size_t pos2 = lineStr.find(L'|', pos1 + 1);

        if (pos1 != std::wstring::npos && pos2 != std::wstring::npos) {
            Tag tag;
            tag.id = _wtoi(lineStr.substr(0, pos1).c_str());
            tag.name = lineStr.substr(pos1 + 1, pos2 - pos1 - 1);
            tag.color = (COLORREF)_wtoi(lineStr.substr(pos2 + 1).c_str());

            g_tags.push_back(tag);
            if (tag.id >= g_nextTagId) {
                g_nextTagId = tag.id + 1;
            }
        }

        line = wcstok_s(NULL, L"\r\n", &context);
    }
}

// 保存标签列表
void SaveTags() {
    std::wstring filePath = GetTagsFilePath();
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD dwBytesWritten;

    // 写入UTF-8 BOM
    const BYTE bom[3] = {0xEF, 0xBB, 0xBF};
    WriteFile(hFile, bom, sizeof(bom), &dwBytesWritten, NULL);

    for (const auto& tag : g_tags) {
        // 格式: ID|名称|颜色
        std::wstring line = std::to_wstring(tag.id) + L"|" + tag.name + L"|" + std::to_wstring(tag.color) + L"\r\n";

        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, NULL, 0, NULL, NULL);
        std::vector<char> utf8Buffer(utf8Len);
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, &utf8Buffer[0], utf8Len, NULL, NULL);
        WriteFile(hFile, &utf8Buffer[0], utf8Len - 1, &dwBytesWritten, NULL);
    }

    CloseHandle(hFile);
}

// 添加标签
int AddTag(const std::wstring& name, COLORREF color) {
    Tag tag;
    tag.id = g_nextTagId++;
    tag.name = name;
    tag.color = color;
    g_tags.push_back(tag);
    return tag.id;
}

// 删除标签
bool RemoveTag(int tagId) {
    for (auto it = g_tags.begin(); it != g_tags.end(); ++it) {
        if (it->id == tagId) {
            g_tags.erase(it);

            // 从所有历史项中移除该标签
            for (auto& item : g_history) {
                item.tagIds.erase(tagId);
                // 更新 isFavorite 状态
                item.isFavorite = !item.tagIds.empty();
            }

            return true;
        }
    }
    return false;
}

// 重命名标签
bool RenameTag(int tagId, const std::wstring& newName) {
    for (auto& tag : g_tags) {
        if (tag.id == tagId) {
            tag.name = newName;
            return true;
        }
    }
    return false;
}

// 设置标签颜色
bool SetTagColor(int tagId, COLORREF color) {
    for (auto& tag : g_tags) {
        if (tag.id == tagId) {
            tag.color = color;
            return true;
        }
    }
    return false;
}

// 根据ID获取标签
Tag* GetTagById(int tagId) {
    for (auto& tag : g_tags) {
        if (tag.id == tagId) {
            return &tag;
        }
    }
    return nullptr;
}

// 给项目添加标签
void AddTagToItem(int historyIndex, int tagId) {
    if (historyIndex >= 0 && historyIndex < (int)g_history.size()) {
        g_history[historyIndex].tagIds.insert(tagId);
        g_history[historyIndex].isFavorite = true;
    }
}

// 从项目移除标签
void RemoveTagFromItem(int historyIndex, int tagId) {
    if (historyIndex >= 0 && historyIndex < (int)g_history.size()) {
        g_history[historyIndex].tagIds.erase(tagId);
        g_history[historyIndex].isFavorite = !g_history[historyIndex].tagIds.empty();
    }
}

// 检查项目是否有某标签
bool ItemHasTag(int historyIndex, int tagId) {
    if (historyIndex >= 0 && historyIndex < (int)g_history.size()) {
        return g_history[historyIndex].tagIds.count(tagId) > 0;
    }
    return false;
}

// 获取收藏总数（有任意标签的项目数）
int GetFavoriteCount() {
    int count = 0;
    for (const auto& item : g_history) {
        if (!item.tagIds.empty()) {
            count++;
        }
    }
    return count;
}

// 获取某标签下的项目数
int GetTagItemCount(int tagId) {
    int count = 0;
    for (const auto& item : g_history) {
        if (item.tagIds.count(tagId) > 0) {
            count++;
        }
    }
    return count;
}

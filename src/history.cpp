// MinGW 下 objidl.h 必须在 gdiplus.h 之前，提供 PROPID
#include <windows.h>
#include <objidl.h>
#include "history.h"
#include <psapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <algorithm>
#include <ctime>
#include <set>
#include <map>
#include "sqlite3.h"
#include "tray.h"
#include "search.h"  // 添加这个头文件以访问 g_hwndTabControl
#include "settings.h"  // 添加这个头文件以访问 g_isNotificationEnabled
#include "smartclip.h"
#include "i18n.h"     // 收藏按钮文本需要根据语言切换
#include "image_handler.h" // LoadImageFile（从文件加载缩略图）

// ==================== SQLite 辅助函数 ====================

// 宽字符串转 UTF-8
static std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, NULL, NULL);
    return s;
}

// UTF-8 转宽字符串
static std::wstring Utf8ToW(const char* s) {
    if (!s || !*s) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (len <= 0) return L"";
    std::wstring w(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], len);
    return w;
}

// 打开历史数据库并初始化表结构
static sqlite3* OpenHistoryDatabase() {
    std::wstring dbPathW = GetDataFilePath();
    std::string dbPath = WToUtf8(dbPathW);
    sqlite3* db = NULL;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return NULL;
    }
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);

    const char* schema =
        "CREATE TABLE IF NOT EXISTS history ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  type INTEGER NOT NULL,"
        "  content TEXT NOT NULL,"
        "  timestamp TEXT NOT NULL,"
        "  source_app TEXT DEFAULT '',"
        "  source_app_path TEXT DEFAULT '',"
        "  is_favorite INTEGER DEFAULT 0,"
        "  image_width INTEGER DEFAULT 0,"
        "  image_height INTEGER DEFAULT 0,"
        "  thumb_width INTEGER DEFAULT 0,"
        "  thumb_height INTEGER DEFAULT 0,"
        "  image_file_name TEXT DEFAULT '',"
        "  image_file_path TEXT DEFAULT '',"
        "  thumb_data BLOB,"
        "  tags TEXT DEFAULT ''"
        ");"
        "CREATE TABLE IF NOT EXISTS tags ("
        "  id INTEGER PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  color INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS settings ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_history_ts ON history(timestamp);";
    sqlite3_exec(db, schema, NULL, NULL, NULL);
    return db;
}

// 从旧版 history.txt 迁移数据到 SQLite
static void MigrateFromLegacyFormat() {
    std::wstring legacyPath = GetSmartClipDataDir() + L"\\history.txt";
    DWORD attrs = GetFileAttributesW(legacyPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return; // 无旧文件

    // 读取旧文件
    HANDLE hFile = CreateFileW(legacyPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    DWORD dwFileSize = GetFileSize(hFile, NULL);
    if (dwFileSize == 0 || dwFileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return;
    }

    std::vector<BYTE> fileContent(dwFileSize);
    DWORD dwBytesRead = 0;
    if (!ReadFile(hFile, &fileContent[0], dwFileSize, &dwBytesRead, NULL)) {
        CloseHandle(hFile);
        return;
    }
    CloseHandle(hFile);

    // 跳过 UTF-8 BOM
    DWORD offset = 0;
    if (dwFileSize >= 3 && fileContent[0] == 0xEF && fileContent[1] == 0xBB && fileContent[2] == 0xBF) {
        offset = 3;
    }

    int unicodeLength = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)(&fileContent[offset]), dwFileSize - offset, NULL, 0);
    if (unicodeLength <= 0) return;

    std::vector<wchar_t> unicodeContent(unicodeLength + 1);
    MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)(&fileContent[offset]), dwFileSize - offset, &unicodeContent[0], unicodeLength);
    unicodeContent[unicodeLength] = L'\0';

    // 解析旧格式并写入数据库
    sqlite3* db = OpenHistoryDatabase();
    if (!db) return;

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    const char* insertSql =
        "INSERT INTO history (type,content,timestamp,source_app,source_app_path,"
        "is_favorite,image_width,image_height,thumb_width,thumb_height,"
        "image_file_name,image_file_path,thumb_data,tags) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt* stmt = NULL;
    sqlite3_prepare_v2(db, insertSql, -1, &stmt, NULL);

    ClipboardItem item;
    item.type = TYPE_TEXT;
    item.isFavorite = false;
    std::wstring thumbFileName;
    enum ParseState { NONE, TYPE_S, CONTENT_S, TIMESTAMP_S, SOURCEAPP_S, SOURCEAPPPATH_S,
                      FAVORITE_S, TAGS_S, IMAGEWIDTH_S, IMAGEHEIGHT_S, THUMBWIDTH_S,
                      THUMBHEIGHT_S, IMAGEFILENAME_S, IMAGEFILEPATH_S, THUMBFILE_S } state = NONE;

    wchar_t* pLine = &unicodeContent[0];
    while (pLine != NULL && *pLine != L'\0') {
        wchar_t* pNextLine = wcsstr(pLine, L"\r\n");
        if (pNextLine != NULL) {
            *pNextLine = L'\0';
            pNextLine += 2;
        }

        if (wcscmp(pLine, L"===TYPE===") == 0) { state = TYPE_S; }
        else if (wcscmp(pLine, L"===CONTENT===") == 0) { state = CONTENT_S; item.content.clear(); }
        else if (wcscmp(pLine, L"===TIMESTAMP===") == 0) { state = TIMESTAMP_S; }
        else if (wcscmp(pLine, L"===SOURCEAPP===") == 0) { state = SOURCEAPP_S; }
        else if (wcscmp(pLine, L"===SOURCEAPPPATH===") == 0) { state = SOURCEAPPPATH_S; }
        else if (wcscmp(pLine, L"===FAVORITE===") == 0) { state = FAVORITE_S; }
        else if (wcscmp(pLine, L"===TAGS===") == 0) { state = TAGS_S; }
        else if (wcscmp(pLine, L"===IMAGEWIDTH===") == 0) { state = IMAGEWIDTH_S; }
        else if (wcscmp(pLine, L"===IMAGEHEIGHT===") == 0) { state = IMAGEHEIGHT_S; }
        else if (wcscmp(pLine, L"===THUMBWIDTH===") == 0) { state = THUMBWIDTH_S; }
        else if (wcscmp(pLine, L"===THUMBHEIGHT===") == 0) { state = THUMBHEIGHT_S; }
        else if (wcscmp(pLine, L"===IMAGEFILENAME===") == 0) { state = IMAGEFILENAME_S; }
        else if (wcscmp(pLine, L"===IMAGEFILEPATH===") == 0) { state = IMAGEFILEPATH_S; }
        else if (wcscmp(pLine, L"===THUMBFILE===") == 0) { state = THUMBFILE_S; }
        else if (wcscmp(pLine, L"===END===") == 0) {
            // 加载缩略图文件
            if (item.type == TYPE_IMAGE && !thumbFileName.empty()) {
                std::wstring thumbFilePath = GetThumbsPath() + L"\\" + thumbFileName;
                HANDLE hThumb = CreateFileW(thumbFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hThumb != INVALID_HANDLE_VALUE) {
                    DWORD thumbSize = GetFileSize(hThumb, NULL);
                    if (thumbSize > 0) {
                        item.imageData.resize(thumbSize);
                        DWORD thumbRead = 0;
                        ReadFile(hThumb, &item.imageData[0], thumbSize, &thumbRead, NULL);
                    }
                    CloseHandle(hThumb);
                }
            }

            // 去除尾部空白
            while (!item.content.empty() && (item.content.back() == L'\r' || item.content.back() == L'\n' || item.content.back() == L' ' || item.content.back() == L'\t'))
                item.content.pop_back();

            if (!item.content.empty() || item.type == TYPE_IMAGE) {
                if (item.thumbWidth == 0) item.thumbWidth = item.imageWidth;
                if (item.thumbHeight == 0) item.thumbHeight = item.imageHeight;

                // 写入数据库
                sqlite3_bind_int(stmt, 1, (int)item.type);
                sqlite3_bind_text(stmt, 2, WToUtf8(item.content).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, WToUtf8(item.timestamp).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, WToUtf8(item.sourceApp).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 5, WToUtf8(item.sourceAppPath).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 6, item.isFavorite ? 1 : 0);
                sqlite3_bind_int(stmt, 7, item.imageWidth);
                sqlite3_bind_int(stmt, 8, item.imageHeight);
                sqlite3_bind_int(stmt, 9, item.thumbWidth);
                sqlite3_bind_int(stmt, 10, item.thumbHeight);
                sqlite3_bind_text(stmt, 11, WToUtf8(item.imageFileName).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 12, WToUtf8(item.imageFilePath).c_str(), -1, SQLITE_TRANSIENT);
                if (!item.imageData.empty()) {
                    sqlite3_bind_blob(stmt, 13, &item.imageData[0], (int)item.imageData.size(), SQLITE_TRANSIENT);
                } else {
                    sqlite3_bind_null(stmt, 13);
                }
                // 标签ID列表
                std::wstring tagIdsStr;
                for (int tagId : item.tagIds) {
                    if (!tagIdsStr.empty()) tagIdsStr += L",";
                    tagIdsStr += std::to_wstring(tagId);
                }
                sqlite3_bind_text(stmt, 14, WToUtf8(tagIdsStr).c_str(), -1, SQLITE_TRANSIENT);

                sqlite3_step(stmt);
                sqlite3_reset(stmt);

                item = ClipboardItem();
                item.type = TYPE_TEXT;
                item.isFavorite = false;
                thumbFileName.clear();
            }
            state = NONE;
        } else {
            switch (state) {
                case TYPE_S: item.type = (ClipboardItemType)_wtoi(pLine); break;
                case CONTENT_S:
                    item.content += pLine;
                    if (pNextLine != NULL) item.content += L"\r\n";
                    break;
                case TIMESTAMP_S: item.timestamp = pLine; break;
                case SOURCEAPP_S: item.sourceApp = pLine; break;
                case SOURCEAPPPATH_S: item.sourceAppPath = pLine; break;
                case FAVORITE_S: item.isFavorite = (_wtoi(pLine) == 1); break;
                case TAGS_S: {
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
                        item.isFavorite = !item.tagIds.empty();
                    }
                    break;
                }
                case IMAGEWIDTH_S: item.imageWidth = _wtoi(pLine); break;
                case IMAGEHEIGHT_S: item.imageHeight = _wtoi(pLine); break;
                case THUMBWIDTH_S: item.thumbWidth = _wtoi(pLine); break;
                case THUMBHEIGHT_S: item.thumbHeight = _wtoi(pLine); break;
                case IMAGEFILENAME_S: item.imageFileName = pLine; break;
                case IMAGEFILEPATH_S: item.imageFilePath = pLine; break;
                case THUMBFILE_S: thumbFileName = pLine; break;
                default: break;
            }
        }
        pLine = pNextLine;
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    sqlite3_close(db);

    // 迁移完成后重命名旧文件（保留备份）
    std::wstring backupPath = legacyPath + L".bak";
    MoveFileW(legacyPath.c_str(), backupPath.c_str());
}

// 全局变量定义
std::vector<ClipboardItem> g_history;
HWND g_hwndListBox;
std::wstring g_searchKeyword;
int g_currentTab = 0;  // 当前选中的标签页索引（0=全部，1=文本，2=图像，3=文件，4=收藏）
std::vector<int> g_displayIndexMap;  // 显示索引到实际历史记录索引的映射
// 展开态虚拟子项：每个显示索引对应的子行号
// -1 = 普通项（非多文件或未展开），0 = 头行，1+ = 文件子行
std::vector<int> g_displaySubIndexMap;

// 标签系统全局变量
std::vector<Tag> g_tags;              // 全局标签列表
int g_currentFilterTagId = 0;         // 当前筛选的标签ID（-1=全部收藏，0=未筛选）
static int g_nextTagId = 1;           // 下一个标签ID

// 多文件记录展开状态（key 为 g_history 索引）
std::map<int, bool> g_expandedItems;

bool IsMultiFileExpanded(int historyIndex) {
    auto it = g_expandedItems.find(historyIndex);
    return it != g_expandedItems.end() && it->second;
}

void ToggleMultiFileExpanded(int historyIndex) {
    auto it = g_expandedItems.find(historyIndex);
    if (it == g_expandedItems.end())
        g_expandedItems[historyIndex] = true;
    else
        it->second = !it->second;
}

// 拆分多文件路径（content 用 L'\n' 分隔）
void SplitMultiFilePaths(const std::wstring& content,
                         std::vector<std::wstring>& out) {
    out.clear();
    size_t start = 0;
    while (start <= content.size()) {
        size_t end = content.find(L'\n', start);
        if (end == std::wstring::npos)
            end = content.size();
        if (end > start)
            out.push_back(content.substr(start, end - start));
        if (end == content.size())
            break;
        start = end + 1;
    }
}

int GetMultiFilePathCount(const std::wstring& content) {
    if (content.empty())
        return 0;
    int count = 1;
    for (size_t i = 0; i < content.size(); i++) {
        if (content[i] == L'\n')
            count++;
    }
    return count;
}

// 快速筛选全局变量
std::wstring g_quickFilterApp;    // 来源应用名（空=不筛选）
std::wstring g_quickFilterDate;   // 日期（空=不筛选，格式 YYYY-MM-DD）

// 自定义数据目录（空=使用默认 %APPDATA%\SmartClip）
static std::wstring g_customDataDir;

// 获取默认数据根目录
static std::wstring GetDefaultDataDir() {
    WCHAR szPath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szPath) != S_OK) {
        if (GetCurrentDirectoryW(MAX_PATH, szPath) == 0) {
            wcscpy_s(szPath, L".");
        }
    }
    return std::wstring(szPath) + L"\\SmartClip";
}

// 获取配置文件路径（始终在默认位置，用于存储自定义目录路径）
static std::wstring GetConfigFilePath() {
    return GetDefaultDataDir() + L"\\datadir.cfg";
}

void LoadCustomDataDir() {
    CreateDirectoryW(GetDefaultDataDir().c_str(), NULL);
    std::wstring cfgPath = GetConfigFilePath();
    HANDLE hFile = CreateFileW(cfgPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { g_customDataDir.clear(); return; }
    DWORD sz = GetFileSize(hFile, NULL);
    if (sz > 0 && sz < MAX_PATH * 2) {
        std::vector<BYTE> buf(sz + 2);
        DWORD read = 0;
        ReadFile(hFile, &buf[0], sz, &read, NULL);
        buf[sz] = 0; buf[sz + 1] = 0;
        int uLen = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)&buf[0], sz, NULL, 0);
        if (uLen > 0) {
            std::vector<wchar_t> wbuf(uLen + 1);
            MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)&buf[0], sz, &wbuf[0], uLen);
            wbuf[uLen] = 0;
            g_customDataDir = &wbuf[0];
            // 去除换行
            while (!g_customDataDir.empty() && (g_customDataDir.back() == L'\n' || g_customDataDir.back() == L'\r'))
                g_customDataDir.pop_back();
        }
    }
    CloseHandle(hFile);
}

void SaveCustomDataDir() {
    CreateDirectoryW(GetDefaultDataDir().c_str(), NULL);
    std::wstring cfgPath = GetConfigFilePath();
    HANDLE hFile = CreateFileW(cfgPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, g_customDataDir.c_str(), -1, NULL, 0, NULL, NULL);
        if (utf8Len > 0) {
            std::vector<char> utf8(utf8Len);
            WideCharToMultiByte(CP_UTF8, 0, g_customDataDir.c_str(), -1, &utf8[0], utf8Len, NULL, NULL);
            DWORD written = 0;
            WriteFile(hFile, &utf8[0], utf8Len - 1, &written, NULL);
        }
        CloseHandle(hFile);
    }
}

std::wstring GetSmartClipDataDir() {
    if (!g_customDataDir.empty()) {
        CreateDirectoryW(g_customDataDir.c_str(), NULL);
        return g_customDataDir;
    }
    std::wstring dir = GetDefaultDataDir();
    CreateDirectoryW(dir.c_str(), NULL);
    return dir;
}

// 获取数据文件路径（SQLite 数据库）
std::wstring GetDataFilePath() {
    return GetSmartClipDataDir() + L"\\history.db";
}

void SaveHistory() {
    sqlite3* db = OpenHistoryDatabase();
    if (!db) { UpdateListBox(); return; }

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    // 清空并重写（保持与原逻辑一致：全量保存）
    sqlite3_exec(db, "DELETE FROM history;", NULL, NULL, NULL);

    const char* insertSql =
        "INSERT INTO history (type,content,timestamp,source_app,source_app_path,"
        "is_favorite,image_width,image_height,thumb_width,thumb_height,"
        "image_file_name,image_file_path,thumb_data,tags) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt* stmt = NULL;
    sqlite3_prepare_v2(db, insertSql, -1, &stmt, NULL);

    for (size_t i = 0; i < g_history.size(); i++) {
        const auto& item = g_history[i];
        sqlite3_bind_int(stmt, 1, (int)item.type);
        sqlite3_bind_text(stmt, 2, WToUtf8(item.content).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, WToUtf8(item.timestamp).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, WToUtf8(item.sourceApp).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, WToUtf8(item.sourceAppPath).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, item.isFavorite ? 1 : 0);
        sqlite3_bind_int(stmt, 7, item.imageWidth);
        sqlite3_bind_int(stmt, 8, item.imageHeight);
        sqlite3_bind_int(stmt, 9, item.thumbWidth);
        sqlite3_bind_int(stmt, 10, item.thumbHeight);
        sqlite3_bind_text(stmt, 11, WToUtf8(item.imageFileName).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 12, WToUtf8(item.imageFilePath).c_str(), -1, SQLITE_TRANSIENT);
        // 缩略图不再写入数据库（已改为 PNG 文件存储，由 SaveThumbnailImage 负责）
        // 此处始终绑定 NULL，避免数据库膨胀
        sqlite3_bind_null(stmt, 13);
        std::wstring tagIdsStr;
        for (int tagId : item.tagIds) {
            if (!tagIdsStr.empty()) tagIdsStr += L",";
            tagIdsStr += std::to_wstring(tagId);
        }
        sqlite3_bind_text(stmt, 14, WToUtf8(tagIdsStr).c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    sqlite3_close(db);

    UpdateListBox();
}

// 加载历史记录
void LoadHistory() {
    // 先检查是否需要从旧格式迁移
    std::wstring dbPath = GetDataFilePath();
    std::wstring legacyPath = GetSmartClipDataDir() + L"\\history.txt";

    bool dbExists = (GetFileAttributesW(dbPath.c_str()) != INVALID_FILE_ATTRIBUTES);
    bool legacyExists = (GetFileAttributesW(legacyPath.c_str()) != INVALID_FILE_ATTRIBUTES);

    if (!dbExists && legacyExists) {
        MigrateFromLegacyFormat();
    }

    sqlite3* db = OpenHistoryDatabase();
    if (!db) { UpdateListBox(); return; }

    const char* query =
        "SELECT type,content,timestamp,source_app,source_app_path,"
        "is_favorite,image_width,image_height,thumb_width,thumb_height,"
        "image_file_name,image_file_path,thumb_data,tags FROM history "
        "ORDER BY id DESC"; // 最新的在前（旧版新记录插在头部，id 自增）

    sqlite3_stmt* stmt = NULL;
    sqlite3_prepare_v2(db, query, -1, &stmt, NULL);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ClipboardItem item;
        item.type = (ClipboardItemType)sqlite3_column_int(stmt, 0);
        item.content = Utf8ToW((const char*)sqlite3_column_text(stmt, 1));
        item.timestamp = Utf8ToW((const char*)sqlite3_column_text(stmt, 2));
        item.sourceApp = Utf8ToW((const char*)sqlite3_column_text(stmt, 3));
        item.sourceAppPath = Utf8ToW((const char*)sqlite3_column_text(stmt, 4));
        item.isFavorite = sqlite3_column_int(stmt, 5) != 0;
        item.imageWidth = sqlite3_column_int(stmt, 6);
        item.imageHeight = sqlite3_column_int(stmt, 7);
        item.thumbWidth = sqlite3_column_int(stmt, 8);
        item.thumbHeight = sqlite3_column_int(stmt, 9);
        item.imageFileName = Utf8ToW((const char*)sqlite3_column_text(stmt, 10));
        item.imageFilePath = Utf8ToW((const char*)sqlite3_column_text(stmt, 11));

        // 加载缩略图：优先从数据库读取（兼容旧数据），数据库为空时从文件加载
        // 缩略图不再持久化到数据库，改为存储为 PNG 文件（images\thumbs\），
        // 以减小数据库体积、提升加载速度。
        // 内存优化：启动时不预加载缩略图数据，改为按需懒加载（EnsureItemImageLoaded）
        // 仅保留文件名与尺寸元数据，实际位图数据在绘制/复制时才加载
        const void* blobData = sqlite3_column_blob(stmt, 12);
        int blobSize = sqlite3_column_bytes(stmt, 12);
        if (blobData && blobSize > 0) {
            // 迁移：数据库中仍有缩略图数据时，若文件不存在则保存到文件，
            // 后续保存将不再写入数据库，确保数据不丢失。
            // 迁移完成后不保留内存数据，改为按需懒加载。
            if (!item.imageFileName.empty() && item.thumbWidth > 0 &&
                item.thumbHeight > 0) {
                std::wstring thumbFilePath =
                    GetThumbsPath() + L"\\" + item.imageFileName;
                if (GetFileAttributesW(thumbFilePath.c_str()) ==
                    INVALID_FILE_ATTRIBUTES) {
                    std::vector<BYTE> migrateData(blobSize);
                    memcpy(&migrateData[0], blobData, blobSize);
                    SaveThumbnailImage(migrateData, item.thumbWidth,
                                       item.thumbHeight, item.imageFileName);
                }
            }
        }
        // else: 数据库无缩略图数据，文件存在时由 EnsureItemImageLoaded 按需加载

        // 解析标签
        const char* tagsStr = (const char*)sqlite3_column_text(stmt, 13);
        if (tagsStr && *tagsStr) {
            std::wstring tagsW = Utf8ToW(tagsStr);
            size_t pos = 0;
            while ((pos = tagsW.find(L',')) != std::wstring::npos) {
                int tagId = _wtoi(tagsW.substr(0, pos).c_str());
                if (tagId > 0) item.tagIds.insert(tagId);
                tagsW = tagsW.substr(pos + 1);
            }
            if (!tagsW.empty()) {
                int tagId = _wtoi(tagsW.c_str());
                if (tagId > 0) item.tagIds.insert(tagId);
            }
        }

        g_history.push_back(item);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    UpdateListBox();
}

// 懒加载缩略图：仅在 imageData 为空时按需加载
// 优先从 imageFileName（截图缩略图文件）加载，其次从 imageFilePath（图片文件原始路径）加载
// 用于绘制、复制到剪贴板、拖拽等场景，避免启动时全量加载占用内存
// item 为 const 引用：imageData/thumbWidth/thumbHeight 声明为 mutable，允许缓存填充
bool EnsureItemImageLoaded(const ClipboardItem& item) {
    if (!item.imageData.empty())
        return true;

    // 1. 优先从缩略图文件加载（截图类型）
    if (!item.imageFileName.empty()) {
        std::wstring thumbFilePath =
            GetThumbsPath() + L"\\" + item.imageFileName;
        std::vector<BYTE> fileData;
        int fileW = 0, fileH = 0;
        if (LoadImageFile(thumbFilePath.c_str(), fileData, fileW, fileH)) {
            item.imageData = std::move(fileData);
            if (fileW > 0) item.thumbWidth = fileW;
            if (fileH > 0) item.thumbHeight = fileH;
            return true;
        }
    }

    // 2. 从原始图片文件路径加载（复制图片文件类型）
    if (!item.imageFilePath.empty()) {
        std::vector<BYTE> fileData;
        int fileW = 0, fileH = 0;
        if (LoadImageFile(item.imageFilePath.c_str(), fileData, fileW, fileH)) {
            // 生成缩略图以节省内存
            int thumbMaxSize = 256;
            switch (g_imagePreviewQuality) {
                case PREVIEW_OFF:
                case PREVIEW_BLUR:
                    thumbMaxSize = 64; break;
                case PREVIEW_SD:
                    thumbMaxSize = 128; break;
                case PREVIEW_HD:
                    thumbMaxSize = 256; break;
            }
            std::vector<BYTE> thumbData;
            int thumbW = 0, thumbH = 0;
            if (GenerateThumbnail(fileData, fileW, fileH, thumbData,
                                  thumbW, thumbH, thumbMaxSize)) {
                item.imageData = std::move(thumbData);
                item.thumbWidth = thumbW;
                item.thumbHeight = thumbH;
            } else {
                item.imageData = std::move(fileData);
                item.thumbWidth = fileW;
                item.thumbHeight = fileH;
            }
            if (item.imageWidth <= 0) item.imageWidth = fileW;
            if (item.imageHeight <= 0) item.imageHeight = fileH;
            return true;
        }
    }

    return false;
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
    SHFILEINFOW sfi = {};
    if (SHGetFileInfoW(exePath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
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

// 清理所有非收藏历史记录
void ClearNonFavoriteHistory() {
    std::wstring imagesPath = GetImagesPath();

    for (int i = (int)g_history.size() - 1; i >= 0; i--) {
        if (!g_history[i].isFavorite) {
            const ClipboardItem& item = g_history[i];
            // 删除关联的原图文件（缩略图在数据库中，随记录自动删除）
            if (item.type == TYPE_IMAGE && !item.imageFileName.empty()) {
                std::wstring imgFile = imagesPath + L"\\" + item.imageFileName;
                DeleteFileW(imgFile.c_str());
            }
            g_history.erase(g_history.begin() + i);
        }
    }
    SaveHistory();
    UpdateListBox();
}

void CleanInvalidImageRecords() {
    std::wstring imagesPath = GetImagesPath();
    int cleaned = 0;

    for (int i = (int)g_history.size() - 1; i >= 0; i--) {
        if (g_history[i].type == TYPE_IMAGE) {
            const ClipboardItem& item = g_history[i];
            bool isInvalid = false;

            if (!item.imageFilePath.empty()) {
                // 图片文件类型：检查原始路径
                DWORD attrs = GetFileAttributesW(item.imageFilePath.c_str());
                if (attrs == INVALID_FILE_ATTRIBUTES) isInvalid = true;
            } else if (!item.imageFileName.empty()) {
                // 截图类型：检查 images 目录下的文件
                std::wstring imgFile = imagesPath + L"\\" + item.imageFileName;
                DWORD attrs = GetFileAttributesW(imgFile.c_str());
                if (attrs == INVALID_FILE_ATTRIBUTES) isInvalid = true;
            } else {
                // 没有任何图片路径，视为失效
                isInvalid = true;
            }

            if (isInvalid) {
                if (!item.imageFileName.empty()) {
                    std::wstring imgFile = imagesPath + L"\\" + item.imageFileName;
                    DeleteFileW(imgFile.c_str());
                }
                g_history.erase(g_history.begin() + i);
                cleaned++;
            }
        }
    }
    if (cleaned > 0) {
        SaveHistory();
        UpdateListBox();
    }
}

// ==================== 粘贴次数统计 ====================

int g_pasteCount = 0;

void LoadPasteCount() {
    // 迁移旧版 paste_count.txt
    std::wstring legacyPath = GetSmartClipDataDir() + L"\\paste_count.txt";
    if (GetFileAttributesW(legacyPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        HANDLE hFile = CreateFileW(legacyPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            char buf[32] = {};
            DWORD read = 0;
            ReadFile(hFile, buf, 31, &read, NULL);
            CloseHandle(hFile);
            g_pasteCount = atoi(buf);
            if (g_pasteCount < 0) g_pasteCount = 0;
            SavePasteCount(); // 写入数据库
            DeleteFileW(legacyPath.c_str()); // 删除旧文件
            return;
        }
    }

    sqlite3* db = OpenHistoryDatabase();
    if (!db) return;
    sqlite3_stmt* stmt = NULL;
    sqlite3_prepare_v2(db, "SELECT value FROM settings WHERE key='paste_count'", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* val = (const char*)sqlite3_column_text(stmt, 0);
        if (val) g_pasteCount = atoi(val);
        if (g_pasteCount < 0) g_pasteCount = 0;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    // 数据已在 SQLite，清理可能残留的旧版 paste_count.txt（避免用户误以为仍在生成）
    if (GetFileAttributesW(legacyPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        DeleteFileW(legacyPath.c_str());
}

void SavePasteCount() {
    sqlite3* db = OpenHistoryDatabase();
    if (!db) return;
    char buf[32];
    sprintf_s(buf, "%d", g_pasteCount);
    const char* sql = "INSERT OR REPLACE INTO settings (key,value) VALUES ('paste_count',?)";
    sqlite3_stmt* stmt = NULL;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, buf, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void IncrementPasteCount() {
    g_pasteCount++;
    SavePasteCount();
}

// 用户协议接受状态查询：检查数据库 settings 表是否含有 agreement_accepted=1
bool IsAgreementAcceptedInDb() {
    sqlite3* db = OpenHistoryDatabase();
    if (!db) return false;
    sqlite3_stmt* stmt = NULL;
    bool accepted = false;
    sqlite3_prepare_v2(db,
        "SELECT value FROM settings WHERE key='agreement_accepted'",
        -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* val = (const char*)sqlite3_column_text(stmt, 0);
        if (val && std::string(val) == "1") accepted = true;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return accepted;
}

// 记录一次协议接受/拒绝事件到数据库 settings 表，并写入时间戳
// action: "accepted" 或 "declined"
void RecordAgreementAction(const std::wstring& action) {
    sqlite3* db = OpenHistoryDatabase();
    if (!db) return;
    // 当前时间字符串（ISO 格式）
    std::wstring nowW = GetCurrentTimeString();
    std::string now = WToUtf8(nowW);
    std::string act = WToUtf8(action);

    // 1) 在 settings 表保存接受状态
    sqlite3_stmt* stmt = NULL;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO settings (key,value) VALUES ('agreement_accepted',?)",
        -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, (act == "accepted") ? "1" : "0", -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // 2) 在 settings 表附加一条事件记录，键名带时间戳以保证多条记录共存
    std::string eventKey = "agreement_event_" + now;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO settings (key,value) VALUES (?,?)",
        -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, eventKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, act.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    sqlite3_close(db);
}

// 清除数据库中的协议接受状态（重置为首次安装）
void ClearAgreementAcceptedInDb() {
    sqlite3* db = OpenHistoryDatabase();
    if (!db) return;
    sqlite3_exec(db,
        "DELETE FROM settings WHERE key='agreement_accepted'",
        NULL, NULL, NULL);
    sqlite3_close(db);
}

// ==================== settings 表通用读写辅助 ====================

void DbSetSetting(const char *key, const wchar_t *value) {
    sqlite3 *db = OpenHistoryDatabase();
    if (!db) return;
    std::string utf8Value = WToUtf8(value);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO settings(key, value) VALUES(?, ?);",
            -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, utf8Value.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

bool DbGetSetting(const char *key, std::wstring &outValue) {
    sqlite3 *db = OpenHistoryDatabase();
    if (!db) return false;
    bool found = false;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT value FROM settings WHERE key=?;",
            -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *val = (const char *)sqlite3_column_text(stmt, 0);
            if (val)
                outValue = Utf8ToW(val);
            found = true;
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return found;
}

void DbSetSettingInt(const char *key, int value) {
    wchar_t buf[32];
    _snwprintf_s(buf, _countof(buf), L"%d", value);
    DbSetSetting(key, buf);
}

int DbGetSettingInt(const char *key, int defaultValue) {
    std::wstring val;
    if (!DbGetSetting(key, val) || val.empty())
        return defaultValue;
    return _wtoi(val.c_str());
}

void DbDeleteSetting(const char *key) {
    sqlite3 *db = OpenHistoryDatabase();
    if (!db) return;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "DELETE FROM settings WHERE key=?;",
            -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

// 递归计算目录大小
static ULONGLONG CalcDirSize(const std::wstring& dir) {
    ULONGLONG total = 0;
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring path = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            total += CalcDirSize(path);
        } else {
            LARGE_INTEGER sz;
            sz.LowPart = fd.nFileSizeLow;
            sz.HighPart = fd.nFileSizeHigh;
            total += sz.QuadPart;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return total;
}

ULONGLONG GetDataDirSize() {
    std::wstring filePath = GetDataFilePath();
    size_t lastSlash = filePath.find_last_of(L"\\");
    if (lastSlash == std::wstring::npos) return 0;
    return CalcDirSize(filePath.substr(0, lastSlash));
}

std::wstring FormatFileSize(ULONGLONG bytes) {
    wchar_t buf[64];
    if (bytes < 1024ULL)
        swprintf(buf, 64, L"%llu B", bytes);
    else if (bytes < 1024ULL * 1024)
        swprintf(buf, 64, L"%.1f KB", bytes / 1024.0);
    else if (bytes < 1024ULL * 1024 * 1024)
        swprintf(buf, 64, L"%.1f MB", bytes / (1024.0 * 1024));
    else
        swprintf(buf, 64, L"%.2f GB", bytes / (1024.0 * 1024 * 1024));
    return buf;
}

// ==================== 数据迁移（带进度，非阻塞） ====================

struct MigrateContext {
    std::wstring srcDir;
    std::wstring dstDir;
    HWND hwndProgress;
    HWND hwndProgressBar;
    HWND hwndProgressText;
    HFONT hFont;
    int totalFiles;
    int copiedFiles;
    bool success;
};

static int CountFilesRecursive(const std::wstring& dir) {
    int count = 0;
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            count += CountFilesRecursive(dir + L"\\" + fd.cFileName);
        else
            count++;
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return count;
}

static bool CopyDirWithProgress(const std::wstring& src, const std::wstring& dst, MigrateContext* ctx) {
    CreateDirectoryW(dst.c_str(), NULL);
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((src + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return true;
    bool ok = true;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring srcPath = src + L"\\" + fd.cFileName;
        std::wstring dstPath = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!CopyDirWithProgress(srcPath, dstPath, ctx)) ok = false;
        } else {
            if (!CopyFileW(srcPath.c_str(), dstPath.c_str(), FALSE)) ok = false;
            ctx->copiedFiles++;
            if (ctx->hwndProgress && IsWindow(ctx->hwndProgress)) {
                PostMessageW(ctx->hwndProgressBar, PBM_SETPOS, ctx->copiedFiles, 0);
                PostMessageW(ctx->hwndProgress, WM_APP + 1, 0, (LPARAM)_wcsdup(fd.cFileName));
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return ok;
}

static DWORD WINAPI MigrateThreadProc(LPVOID lpParam) {
    MigrateContext* ctx = (MigrateContext*)lpParam;
    ctx->success = CopyDirWithProgress(ctx->srcDir, ctx->dstDir, ctx);
    if (ctx->hwndProgress && IsWindow(ctx->hwndProgress))
        PostMessageW(ctx->hwndProgress, WM_APP + 2, 0, 0);
    return 0;
}

static LRESULT CALLBACK ProgressDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MigrateContext* ctx = (MigrateContext*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_APP + 1: {
        wchar_t* fileName = (wchar_t*)lParam;
        if (fileName && ctx) {
            std::wstring text = L"正在复制: " + std::wstring(fileName)
                + L"  (" + std::to_wstring(ctx->copiedFiles) + L"/" + std::to_wstring(ctx->totalFiles) + L")";
            SetWindowTextW(ctx->hwndProgressText, text.c_str());
            free(fileName);
        }
        return 0;
    }
    case WM_APP + 2: {
        // 迁移完成，应用结果
        if (ctx) {
            if (ctx->success) {
                g_customDataDir = ctx->dstDir;
                SaveCustomDataDir();
                g_history.clear();
                LoadHistory();
                UpdateListBox();
                MessageBoxW(hwnd, L"数据迁移成功！", L"提示", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(hwnd, L"数据迁移失败，请检查目标目录权限。", L"错误", MB_OK | MB_ICONERROR);
            }
            if (ctx->hFont) DeleteObject(ctx->hFont);
            delete ctx;
        }
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_CLOSE:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool MigrateDataDir(const std::wstring& newDir) {
    std::wstring oldDir = GetSmartClipDataDir();
    std::wstring newSmartClipDir = newDir + L"\\SmartClip";
    if (oldDir == newSmartClipDir) return true;

    CreateDirectoryW(newSmartClipDir.c_str(), NULL);
    int totalFiles = CountFilesRecursive(oldDir);

    static bool progressClassReg = false;
    if (!progressClassReg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = ProgressDlgProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(245, 245, 245));
        wc.lpszClassName = L"SmartClipProgress";
        RegisterClassExW(&wc);
        progressClassReg = true;
    }

    int pw = 420, ph = 110;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - pw) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - ph) / 2;
    HWND hwndProg = CreateWindowExW(WS_EX_TOPMOST,
        L"SmartClipProgress", L"数据迁移中...",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        sx, sy, pw, ph, NULL, NULL, GetModuleHandleW(NULL), NULL);

    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icex);

    HWND hwndBar = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        10, 10, pw - 40, 20, hwndProg, NULL, GetModuleHandleW(NULL), NULL);
    SendMessageW(hwndBar, PBM_SETRANGE, 0, MAKELPARAM(0, totalFiles > 0 ? totalFiles : 1));

    HWND hwndText = CreateWindowExW(0, L"STATIC", L"准备中...",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS,
        10, 38, pw - 40, 20, hwndProg, NULL, GetModuleHandleW(NULL), NULL);
    HFONT hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    SendMessageW(hwndText, WM_SETFONT, (WPARAM)hFont, TRUE);

    MigrateContext* ctx = new MigrateContext();
    ctx->srcDir = oldDir;
    ctx->dstDir = newSmartClipDir;
    ctx->hwndProgress = hwndProg;
    ctx->hwndProgressBar = hwndBar;
    ctx->hwndProgressText = hwndText;
    ctx->hFont = hFont;
    ctx->totalFiles = totalFiles;
    ctx->copiedFiles = 0;
    ctx->success = false;

    SetWindowLongPtrW(hwndProg, GWLP_USERDATA, (LONG_PTR)ctx);
    ShowWindow(hwndProg, SW_SHOW);
    UpdateWindow(hwndProg);

    // 创建迁移线程并立即关闭线程句柄（线程继续运行，避免句柄泄漏）
    HANDLE hThread = CreateThread(NULL, 0, MigrateThreadProc, ctx, 0, NULL);
    if (hThread)
        CloseHandle(hThread);
    return true;
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
    // 清理已删除记录的展开状态（g_history 索引越界的记录已不存在）
    for (auto it = g_expandedItems.begin(); it != g_expandedItems.end();) {
        if (it->first < 0 || it->first >= (int)g_history.size())
            it = g_expandedItems.erase(it);
        else
            ++it;
    }

    // 禁用重绘，避免闪烁
    SendMessageW(g_hwndListBox, WM_SETREDRAW, FALSE, 0);

    SendMessageW(g_hwndListBox, LB_RESETCONTENT, 0, 0);
    g_displayIndexMap.clear();  // 清空索引映射
    g_displaySubIndexMap.clear();

    // 列表内容变化后，文本选中状态的显示索引已失效，清除避免误复制
    ClearTextSelectionAfterRefresh();

    // 收藏按钮显示分类数（即标签数量），而非记录数；超过 99 显示 99+
    int categoryCount = (int)g_tags.size();
    if (g_hwndFilterFavorite != NULL) {
        std::wstring countStr;
        if (categoryCount > 99) {
            countStr = L"99+";
        } else {
            countStr = std::to_wstring(categoryCount);
        }
        std::wstring favoriteTabText = std::wstring(T(STR_FILTER_FAVORITE)) + L"(" + countStr + L")";
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
            // 匹配文本与实际显示内容保持一致：
            // - 文本记录：匹配完整文本
            // - 文件记录：匹配文件名（显示即文件名），避免路径片段误匹配无关文件
            std::wstring matchText = item.content;
            if (item.type == TYPE_FILE) {
                // 与显示逻辑一致：多文件用 L'\n' 分隔，各自取文件名再拼接
                matchText.clear();
                size_t start = 0;
                while (start <= item.content.size()) {
                    size_t end = item.content.find(L'\n', start);
                    if (end == std::wstring::npos)
                        end = item.content.size();
                    if (end > start) {
                        std::wstring path = item.content.substr(start, end - start);
                        size_t lastSep = path.find_last_of(L"\\/");
                        std::wstring name = (lastSep != std::wstring::npos)
                            ? path.substr(lastSep + 1)
                            : path;
                        if (!matchText.empty())
                            matchText += L'\n';
                        matchText += name;
                    }
                    if (end == item.content.size())
                        break;
                    start = end + 1;
                }
            }

            // 转换为小写进行比较
            std::wstring lowerContent = matchText;
            std::wstring lowerKeyword = g_searchKeyword;
            std::transform(lowerContent.begin(), lowerContent.end(), lowerContent.begin(), ::towlower);
            std::transform(lowerKeyword.begin(), lowerKeyword.end(), lowerKeyword.begin(), ::towlower);

            if (lowerContent.find(lowerKeyword) == std::wstring::npos)
                continue; // 不匹配，跳过此项
        }

        // 应用快速筛选：来源应用
        if (!g_quickFilterApp.empty() && item.sourceApp != g_quickFilterApp) continue;
        // 应用快速筛选：日期
        if (!g_quickFilterDate.empty() && item.timestamp.length() >= 10) {
            if (item.timestamp.substr(0, 10) != g_quickFilterDate) continue;
        }

        // 记录显示索引到实际索引的映射
        // 展开的多文件记录：拆成 n 个虚拟子项（头行 + 各文件行），
        // 支持逐行滚动、右键删除等操作
        bool isMultiFile = (item.type == TYPE_FILE &&
                            item.content.find(L'\n') != std::wstring::npos);
        if (isMultiFile && IsMultiFileExpanded(i)) {
            int fileCount = GetMultiFilePathCount(item.content);
            for (int sub = 0; sub < fileCount; ++sub) {
                g_displayIndexMap.push_back(i);
                g_displaySubIndexMap.push_back(sub);
                SendMessageW(g_hwndListBox, LB_ADDSTRING, 0,
                             (LPARAM)L"");  // 占位字符串，实际由 WM_DRAWITEM 绘制
            }
        } else {
            g_displayIndexMap.push_back(i);
            g_displaySubIndexMap.push_back(-1);
            // displayText 在下方构建
        }

        std::wstring displayText = GetRelativeTimeString(item.timestamp) + L" - " + item.sourceApp + L"\r\n";

        switch (item.type) {
            case TYPE_TEXT:
                // 不截断文本，显示完整内容
                displayText += item.content;
                break;
            case TYPE_IMAGE:
                displayText += L"[图像] " + std::to_wstring(item.imageWidth) + L"x" + std::to_wstring(item.imageHeight);
                break;
            case TYPE_FILE: {
                // 多文件记录（content 含 L'\n'）：显示"文件名1、文件名2、..."
                // 一行显示不全时由列表绘制端 DT_END_ELLIPSIS 自动省略
                size_t nlPos = item.content.find(L'\n');
                if (nlPos != std::wstring::npos) {
                    // 拆分所有路径，提取文件名用"、"连接
                    std::wstring displayNames;
                    size_t start = 0;
                    while (start <= item.content.size()) {
                        size_t end = item.content.find(L'\n', start);
                        if (end == std::wstring::npos)
                            end = item.content.size();
                        if (end > start) {
                            std::wstring path = item.content.substr(start, end - start);
                            size_t lastSep = path.find_last_of(L"\\/");
                            std::wstring name = (lastSep != std::wstring::npos)
                                ? path.substr(lastSep + 1)
                                : path;
                            if (!displayNames.empty())
                                displayNames += L"、";
                            displayNames += name;
                        }
                        if (end == item.content.size())
                            break;
                        start = end + 1;
                    }
                    displayText += displayNames;
                } else {
                    // 单路径：显示文件名（取最后分隔符后部分），
                    // 与多文件记录显示风格一致，避免完整路径过长难读
                    std::wstring name = item.content;
                    size_t lastSep = name.find_last_of(L"\\/");
                    if (lastSep != std::wstring::npos)
                        name = name.substr(lastSep + 1);
                    displayText += name;
                }
                break;
            }
        }

        // 展开的多文件记录已在上方逐行 LB_ADDSTRING，跳过此处的统一添加
        if (!(isMultiFile && IsMultiFileExpanded(i)))
            SendMessageW(g_hwndListBox, LB_ADDSTRING, 0, (LPARAM)displayText.c_str());
    }

    // 重置列表滚动位置到顶部
    SendMessageW(g_hwndListBox, LB_SETTOPINDEX, 0, 0);
    g_listBoxTopIndex = 0;
    g_currentPage = 0;

    // 重新启用重绘并强制刷新
    SendMessageW(g_hwndListBox, WM_SETREDRAW, TRUE, 0);

    // 列表内容变化后，原快捷键缓存（displayIndex 映射）已失效，
    // 必须在 RedrawWindow 触发 WM_PAINT 之前标记为脏，
    // 否则 WM_PAINT 会沿用旧缓存导致快捷键编号错位（新复制文本不更新）。
    ResetShortcutAssignment();

    RedrawWindow(g_hwndListBox, NULL, NULL,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE |
                     RDW_ALLCHILDREN);

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

void ApplyImagePreviewQualityChange() {
    if (g_hwndListBox == NULL) {
        return;
    }

    int oldTopDisplayIndex =
        (int)SendMessageW(g_hwndListBox, LB_GETTOPINDEX, 0, 0);
    int oldSelDisplayIndex =
        (int)SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0);

    int oldTopActualIndex = -1;
    int oldSelActualIndex = -1;
    if (oldTopDisplayIndex >= 0 &&
        oldTopDisplayIndex < (int)g_displayIndexMap.size()) {
        oldTopActualIndex = g_displayIndexMap[oldTopDisplayIndex];
    }
    if (oldSelDisplayIndex >= 0 &&
        oldSelDisplayIndex < (int)g_displayIndexMap.size()) {
        oldSelActualIndex = g_displayIndexMap[oldSelDisplayIndex];
    }

    UpdateListBox();

    int newTopDisplayIndex = 0;
    int newSelDisplayIndex = LB_ERR;
    for (int i = 0; i < (int)g_displayIndexMap.size(); ++i) {
        if (g_displayIndexMap[i] == oldTopActualIndex && oldTopActualIndex >= 0) {
            newTopDisplayIndex = i;
        }
        if (g_displayIndexMap[i] == oldSelActualIndex && oldSelActualIndex >= 0) {
            newSelDisplayIndex = i;
        }
    }

    ApplyListBoxTopIndex(g_hwndListBox, newTopDisplayIndex);
    if (newSelDisplayIndex != LB_ERR) {
        SendMessageW(g_hwndListBox, LB_SETCURSEL, newSelDisplayIndex, 0);
    }

    int itemCount = (int)SendMessageW(g_hwndListBox, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < itemCount; ++i) {
        SendMessageW(g_hwndListBox, LB_SETITEMHEIGHT, i, 0);
    }

    RedrawWindow(g_hwndListBox, NULL, NULL,
                 RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW |
                     RDW_ALLCHILDREN);
    if (g_hwndMain != NULL) {
        RedrawWindow(g_hwndMain, NULL, NULL,
                     RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
}

// 添加内容到历史记录
void AddToHistory(const std::wstring& content) {
    // 文本大小上限检查：超过设定值则不记录
    if (g_maxTextSizeKB > 0 && (int)(content.size() * sizeof(wchar_t)) > g_maxTextSizeKB * 1024) {
        return;
    }

    // 检查是否已存在相同内容
    bool wasFavorite = false;
    std::set<int> oldTagIds;
    auto it = std::find_if(g_history.begin(), g_history.end(), [&content](const ClipboardItem& item) {
        return item.type == TYPE_TEXT && item.content == content;
    });

    if (it != g_history.end()) {
        wasFavorite = it->isFavorite;
        oldTagIds = it->tagIds;
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
    item.isFavorite = wasFavorite;
    item.tagIds = oldTagIds;

    // 限制历史记录数量（收藏项不占名额）
    int nonFavCount = 0;
    for (const auto& h : g_history) {
        if (!h.isFavorite) nonFavCount++;
    }
    while (nonFavCount >= g_maxHistoryCount) {
        // 从末尾找第一个非收藏项删除
        bool removed = false;
        for (int i = (int)g_history.size() - 1; i >= 0; i--) {
            if (!g_history[i].isFavorite) {
                g_history.erase(g_history.begin() + i);
                nonFavCount--;
                removed = true;
                break;
            }
        }
        if (!removed) break;
    }

    // 将新记录添加到最前面
    g_history.insert(g_history.begin(), item);

    UpdateListBox();

    // 先显示气泡提示，再保存文件
    if (g_hwndMain != NULL && g_isNotificationEnabled) {
        // 显示更多复制内容（最多200字符，约1.1倍于托盘气泡宽度）
        std::wstring preview = item.content.substr(0, 200);
        if (item.content.length() > 200) {
            preview += L"...";
        }
        // 将换行符替换为空格，避免通知中显示多行
        for (auto& c : preview) {
            if (c == L'\r' || c == L'\n') c = L' ';
        }
        ShowTrayBalloon(g_hwndMain, T(STR_TRAY_COPY_TITLE), preview.c_str());
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
        // 同时将缩略图保存为 PNG 文件到 images\thumbs，便于用户在文件系统中查看
        SaveThumbnailImage(thumbData, thumbWidth, thumbHeight, item.imageFileName);
    } else {
        // 如果生成缩略图失败，使用原图（兼容处理）
        item.imageData = imageData;
        item.thumbWidth = width;
        item.thumbHeight = height;
    }

    // 去重：查找相同图像（同尺寸 + 同缩略图字节）的旧项，删除以更新复制时间
    {
        bool wasFavorite = false;
        std::set<int> oldTagIds;
        auto it = std::find_if(g_history.begin(), g_history.end(),
            [&](const ClipboardItem& old) {
                return old.type == TYPE_IMAGE && old.content == L"[图像]" &&
                       old.imageWidth == width && old.imageHeight == height &&
                       old.thumbWidth == item.thumbWidth &&
                       old.thumbHeight == item.thumbHeight &&
                       old.imageData == item.imageData;
            });
        if (it != g_history.end()) {
            wasFavorite = it->isFavorite;
            oldTagIds = it->tagIds;
            g_history.erase(it);
            item.isFavorite = wasFavorite;
            item.tagIds = oldTagIds;
        }
    }

    // 限制历史记录数量（收藏项不占名额）
    int nonFavCount = 0;
    for (const auto& h : g_history) {
        if (!h.isFavorite) nonFavCount++;
    }
    while (nonFavCount >= g_maxHistoryCount) {
        bool removed = false;
        for (int i = (int)g_history.size() - 1; i >= 0; i--) {
            if (!g_history[i].isFavorite) {
                g_history.erase(g_history.begin() + i);
                nonFavCount--;
                removed = true;
                break;
            }
        }
        if (!removed) break;
    }

    // 将新记录添加到最前面
    g_history.insert(g_history.begin(), item);

    UpdateListBox();

    if (g_hwndMain != NULL && g_isNotificationEnabled) {
        ShowTrayBalloon(g_hwndMain, T(STR_TRAY_COPY_TITLE), T(STR_TRAY_IMAGE_COPIED));
    }

    SaveHistory();
}

// 添加文件到历史记录
void AddFileToHistory(const std::wstring& filePath) {
    // 检查是否已存在相同文件路径
    bool wasFavorite = false;
    std::set<int> oldTagIds;
    auto it = std::find_if(g_history.begin(), g_history.end(), [&filePath](const ClipboardItem& item) {
        return item.type == TYPE_FILE && item.content == filePath;
    });

    if (it != g_history.end()) {
        wasFavorite = it->isFavorite;
        oldTagIds = it->tagIds;
        g_history.erase(it);
    }

    ClipboardItem item;
    item.type = TYPE_FILE;
    item.content = filePath;
    item.timestamp = GetCurrentTimeString();
    item.sourceApp = GetActiveWindowProcessName();
    item.sourceAppPath = GetActiveWindowProcessPath();
    item.imageWidth = 0;
    item.imageHeight = 0;
    item.isFavorite = wasFavorite;
    item.tagIds = oldTagIds;

    // 限制历史记录数量（收藏项不占名额）
    int nonFavCount = 0;
    for (const auto& h : g_history) {
        if (!h.isFavorite) nonFavCount++;
    }
    while (nonFavCount >= g_maxHistoryCount) {
        bool removed = false;
        for (int i = (int)g_history.size() - 1; i >= 0; i--) {
            if (!g_history[i].isFavorite) {
                g_history.erase(g_history.begin() + i);
                nonFavCount--;
                removed = true;
                break;
            }
        }
        if (!removed) break;
    }

    // 将新记录添加到最前面
    g_history.insert(g_history.begin(), item);

    UpdateListBox();

    if (g_hwndMain != NULL && g_isNotificationEnabled) {
        ShowTrayBalloon(g_hwndMain, T(STR_TRAY_COPY_TITLE), T(STR_TRAY_FILE_PATH_COPIED));
    }

    SaveHistory();
}

// 添加多文件记录：joinedFilePaths 用 L'\n' 连接的多个文件路径。
// 去重按整个连接字符串匹配；存储为一条 TYPE_FILE 记录。
void AddFilesToHistory(const std::wstring& joinedFilePaths,
                       std::vector<int>* outNewIndices) {
    if (joinedFilePaths.empty())
        return;

    // 按 L'\n' 拆分为单路径列表（过滤空串）
    std::vector<std::wstring> paths;
    {
        size_t start = 0;
        while (start <= joinedFilePaths.size()) {
            size_t end = joinedFilePaths.find(L'\n', start);
            if (end == std::wstring::npos)
                end = joinedFilePaths.size();
            if (end > start) {
                std::wstring p = joinedFilePaths.substr(start, end - start);
                if (!p.empty())
                    paths.push_back(p);
            }
            if (end == joinedFilePaths.size())
                break;
            start = end + 1;
        }
    }
    if (paths.empty())
        return;

    // 单文件：保持原行为——存为一条记录（content=单路径），按完整字符串去重
    if (paths.size() == 1) {
        const std::wstring &single = paths[0];
        bool wasFavorite = false;
        std::set<int> oldTagIds;
        auto it = std::find_if(g_history.begin(), g_history.end(),
                               [&single](const ClipboardItem& item) {
                                   return item.type == TYPE_FILE &&
                                          item.content == single;
                               });
        if (it != g_history.end()) {
            wasFavorite = it->isFavorite;
            oldTagIds = it->tagIds;
            g_history.erase(it);
        }

        ClipboardItem item;
        item.type = TYPE_FILE;
        item.content = single;
        item.timestamp = GetCurrentTimeString();
        item.sourceApp = GetActiveWindowProcessName();
        item.sourceAppPath = GetActiveWindowProcessPath();
        item.imageWidth = 0;
        item.imageHeight = 0;
        item.isFavorite = wasFavorite;
        item.tagIds = oldTagIds;

        int nonFavCount = 0;
        for (const auto& h : g_history) {
            if (!h.isFavorite) nonFavCount++;
        }
        while (nonFavCount >= g_maxHistoryCount) {
            bool removed = false;
            for (int i = (int)g_history.size() - 1; i >= 0; i--) {
                if (!g_history[i].isFavorite) {
                    g_history.erase(g_history.begin() + i);
                    nonFavCount--;
                    removed = true;
                    break;
                }
            }
            if (!removed) break;
        }

        g_history.insert(g_history.begin(), item);
        if (outNewIndices)
            outNewIndices->push_back(0);

        UpdateListBox();
        if (g_hwndMain != NULL && g_isNotificationEnabled) {
            ShowTrayBalloon(g_hwndMain, T(STR_TRAY_COPY_TITLE), T(STR_TRAY_FILE_PATH_COPIED));
        }
        SaveHistory();
        return;
    }

    // 多文件（>=2）：存为一条记录（content 用 L'\n' 连接所有路径），
    // 列表显示时用顿号分隔 + 下拉三角形展开/收起。
    std::wstring ts = GetCurrentTimeString();
    std::wstring srcApp = GetActiveWindowProcessName();
    std::wstring srcAppPath = GetActiveWindowProcessPath();

    // 收集旧记录状态：先检查是否有完全相同的多文件记录，再检查各单文件记录
    bool wasFavorite = false;
    std::set<int> oldTagIds;

    // 1) 检查是否存在完全相同的多文件记录（content == joinedFilePaths）
    auto itMulti = std::find_if(g_history.begin(), g_history.end(),
        [&joinedFilePaths](const ClipboardItem& item) {
            return item.type == TYPE_FILE && item.content == joinedFilePaths;
        });
    if (itMulti != g_history.end()) {
        wasFavorite = itMulti->isFavorite;
        oldTagIds = itMulti->tagIds;
        g_history.erase(itMulti);
    } else {
        // 2) 检查是否有任一单文件记录匹配，迁移收藏/标签状态
        for (const auto& p : paths) {
            auto it = std::find_if(g_history.begin(), g_history.end(),
                [&p](const ClipboardItem& item) {
                    return item.type == TYPE_FILE && item.content == p;
                });
            if (it != g_history.end()) {
                if (it->isFavorite) wasFavorite = true;
                for (int t : it->tagIds) oldTagIds.insert(t);
            }
        }
        // 删除所有匹配的单文件旧记录
        g_history.erase(std::remove_if(g_history.begin(), g_history.end(),
            [&paths](const ClipboardItem& item) {
                if (item.type != TYPE_FILE)
                    return false;
                // 不删除多文件记录（content 含 L'\n'）
                if (item.content.find(L'\n') != std::wstring::npos)
                    return false;
                return std::find(paths.begin(), paths.end(), item.content) != paths.end();
            }), g_history.end());
    }

    // 构造一条多文件记录
    ClipboardItem item;
    item.type = TYPE_FILE;
    item.content = joinedFilePaths;
    item.timestamp = ts;
    item.sourceApp = srcApp;
    item.sourceAppPath = srcAppPath;
    item.imageWidth = 0;
    item.imageHeight = 0;
    item.isFavorite = wasFavorite;
    item.tagIds = oldTagIds;

    // 容量限制：确保插入后不超 maxHistoryCount（裁剪尾部非收藏项）
    int nonFavCount = 0;
    for (const auto& h : g_history) {
        if (!h.isFavorite) nonFavCount++;
    }
    while (nonFavCount >= g_maxHistoryCount) {
        bool removed = false;
        for (int i = (int)g_history.size() - 1; i >= 0; i--) {
            if (!g_history[i].isFavorite) {
                g_history.erase(g_history.begin() + i);
                nonFavCount--;
                removed = true;
                break;
            }
        }
        if (!removed) break;
    }

    g_history.insert(g_history.begin(), item);
    if (outNewIndices)
        outNewIndices->push_back(0);

    UpdateListBox();

    if (g_hwndMain != NULL && g_isNotificationEnabled) {
        ShowTrayBalloon(g_hwndMain, T(STR_TRAY_COPY_TITLE), T(STR_TRAY_FILE_PATH_COPIED));
    }

    SaveHistory();
}
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

    // 去重：查找相同文件路径的旧项，删除以更新复制时间
    {
        bool wasFavorite = false;
        std::set<int> oldTagIds;
        auto it = std::find_if(g_history.begin(), g_history.end(),
            [&](const ClipboardItem& old) {
                return old.type == TYPE_IMAGE && old.content == filePath;
            });
        if (it != g_history.end()) {
            wasFavorite = it->isFavorite;
            oldTagIds = it->tagIds;
            g_history.erase(it);
            item.isFavorite = wasFavorite;
            item.tagIds = oldTagIds;
        }
    }

    // 限制历史记录数量（收藏项不占名额）
    int nonFavCount = 0;
    for (const auto& h : g_history) {
        if (!h.isFavorite) nonFavCount++;
    }
    while (nonFavCount >= g_maxHistoryCount) {
        bool removed = false;
        for (int i = (int)g_history.size() - 1; i >= 0; i--) {
            if (!g_history[i].isFavorite) {
                g_history.erase(g_history.begin() + i);
                nonFavCount--;
                removed = true;
                break;
            }
        }
        if (!removed) break;
    }

    // 将新记录添加到最前面
    g_history.insert(g_history.begin(), item);

    UpdateListBox();

    if (g_hwndMain != NULL && g_isNotificationEnabled) {
        ShowTrayBalloon(g_hwndMain, T(STR_TRAY_COPY_TITLE), T(STR_TRAY_IMAGE_ADDED));
    }

    SaveHistory();
}

// 获取图片存储目录（原图）
std::wstring GetImagesPath() {
    std::wstring base = GetSmartClipDataDir();
    std::wstring imagesPath = base + L"\\images\\originals";
    CreateDirectoryW((base + L"\\images").c_str(), NULL);
    CreateDirectoryW(imagesPath.c_str(), NULL);
    return imagesPath;
}

// 获取缩略图存储目录
std::wstring GetThumbsPath() {
    std::wstring base = GetSmartClipDataDir();
    std::wstring thumbsPath = base + L"\\images\\thumbs";
    CreateDirectoryW((base + L"\\images").c_str(), NULL);
    CreateDirectoryW(thumbsPath.c_str(), NULL);
    return thumbsPath;

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

// 保存原图到文件（PNG 格式）
bool SaveOriginalImage(const std::vector<BYTE>& imageData, int width, int height, const std::wstring& fileName) {
    std::wstring filePath = GetImagesPath() + L"\\" + fileName;

    // 构建 BITMAPINFO
    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = width;
    bih.biHeight = -height;  // 负值表示从上到下
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = imageData.size();

    BITMAPINFO bi = {};
    bi.bmiHeader = bih;

    Gdiplus::Bitmap* pBitmap = Gdiplus::Bitmap::FromBITMAPINFO(&bi, (void*)&imageData[0]);
    if (!pBitmap) return false;

    // 查找 PNG 编码器
    CLSID pngClsid = {};
    bool found = false;
    UINT numEncoders = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&numEncoders, &size);
    if (size > 0) {
        std::vector<BYTE> encoderBuffer(size);
        Gdiplus::ImageCodecInfo* pEncoders = (Gdiplus::ImageCodecInfo*)&encoderBuffer[0];
        Gdiplus::GetImageEncoders(numEncoders, size, pEncoders);
        for (UINT i = 0; i < numEncoders; i++) {
            if (wcscmp(pEncoders[i].MimeType, L"image/png") == 0) {
                pngClsid = pEncoders[i].Clsid;
                found = true;
                break;
            }
        }
    }

    if (!found) {
        delete pBitmap;
        return false;
    }

    Gdiplus::Status status = pBitmap->Save(filePath.c_str(), &pngClsid, NULL);
    delete pBitmap;

    return (status == Gdiplus::Ok);
}

// 保存缩略图到 images\thumbs 文件夹（PNG 格式）
// 与原图同名，便于配对管理。缩略图同时仍以 BLOB 存入 SQLite 以便快速加载。
bool SaveThumbnailImage(const std::vector<BYTE>& thumbData, int width, int height, const std::wstring& fileName) {
    if (thumbData.empty() || width <= 0 || height <= 0)
        return false;
    std::wstring filePath = GetThumbsPath() + L"\\" + fileName;

    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = width;
    bih.biHeight = -height;
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = (DWORD)thumbData.size();

    BITMAPINFO bi = {};
    bi.bmiHeader = bih;

    Gdiplus::Bitmap* pBitmap = Gdiplus::Bitmap::FromBITMAPINFO(&bi, (void*)&thumbData[0]);
    if (!pBitmap) return false;

    CLSID pngClsid = {};
    bool found = false;
    UINT numEncoders = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&numEncoders, &size);
    if (size > 0) {
        std::vector<BYTE> encoderBuffer(size);
        Gdiplus::ImageCodecInfo* pEncoders = (Gdiplus::ImageCodecInfo*)&encoderBuffer[0];
        Gdiplus::GetImageEncoders(numEncoders, size, pEncoders);
        for (UINT i = 0; i < numEncoders; i++) {
            if (wcscmp(pEncoders[i].MimeType, L"image/png") == 0) {
                pngClsid = pEncoders[i].Clsid;
                found = true;
                break;
            }
        }
    }

    if (!found) {
        delete pBitmap;
        return false;
    }

    Gdiplus::Status status = pBitmap->Save(filePath.c_str(), &pngClsid, NULL);
    delete pBitmap;
    return (status == Gdiplus::Ok);
}

// 保存图片到临时文件（用于拖拽时原图缺失的回退情况）
// extension 指定目标格式扩展名（如 L"png", L"jpg", L"bmp"），默认 png
// originalFileName 若提供，则使用该文件名（保留原图文件名），否则生成随机名
std::wstring SaveImageToTempFile(const std::vector<BYTE>& imageData, int width, int height, const std::wstring& extension, const std::wstring& originalFileName) {
    wchar_t tempPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tempPath) == 0) return L"";

    // 确定扩展名（去掉可能的点前缀）
    std::wstring ext = extension;
    if (ext.empty()) ext = L"png";
    if (ext[0] == L'.') ext = ext.substr(1);

    std::wstring outPath;

    if (!originalFileName.empty()) {
        // 使用原始文件名：从 originalFileName 中取出纯文件名（去掉路径）
        std::wstring fileName = originalFileName;
        size_t slashPos = fileName.find_last_of(L"\\/");
        if (slashPos != std::wstring::npos) fileName = fileName.substr(slashPos + 1);

        // 若 fileName 没有扩展名或扩展名与目标 ext 不一致，统一改成目标扩展名
        size_t dotPos = fileName.rfind(L'.');
        if (dotPos == std::wstring::npos) {
            fileName += L"." + ext;
        } else {
            fileName = fileName.substr(0, dotPos) + L"." + ext;
        }

        outPath = std::wstring(tempPath) + L"\\" + fileName;

        // 若目标文件已存在，加数字后缀避免覆盖
        if (GetFileAttributesW(outPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            for (int i = 1; i < 1000; ++i) {
                std::wstring altName = fileName.substr(0, fileName.rfind(L'.')) +
                                       L"_" + std::to_wstring(i) + L"." + ext;
                std::wstring altPath = std::wstring(tempPath) + L"\\" + altName;
                if (GetFileAttributesW(altPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    outPath = altPath;
                    break;
                }
            }
        }
    } else {
        // 生成唯一临时文件名
        wchar_t tempFile[MAX_PATH];
        if (GetTempFileNameW(tempPath, L"img", 0, tempFile) == 0) return L"";

        // GetTempFileNameW 创建 .tmp 文件，改用目标扩展名
        std::wstring tmpPath(tempFile);
        outPath = tmpPath.substr(0, tmpPath.rfind(L'.')) + L"." + ext;
        DeleteFileW(tmpPath.c_str());  // 删除空的 .tmp 文件
    }

    // BMP 格式：直接写入文件头+像素数据（最快，无需 GDI+）
    if (ext == L"bmp") {
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

        HANDLE hFile = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return L"";

        DWORD dwBytesWritten;
        WriteFile(hFile, &bfh, sizeof(bfh), &dwBytesWritten, NULL);
        WriteFile(hFile, &bih, sizeof(bih), &dwBytesWritten, NULL);
        WriteFile(hFile, &imageData[0], imageData.size(), &dwBytesWritten, NULL);
        CloseHandle(hFile);
        return outPath;
    }

    // 非 BMP 格式：使用 GDI+ 编码器
    // 构建 BITMAPINFO 供 Bitmap::FromBITMAPINFO 使用
    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = width;
    bih.biHeight = -height;
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = imageData.size();

    BITMAPINFO bi = {};
    bi.bmiHeader = bih;

    Gdiplus::Bitmap* pBitmap = Gdiplus::Bitmap::FromBITMAPINFO(&bi, (void*)&imageData[0]);
    if (!pBitmap) return L"";

    // 获取目标格式的编码器 CLSID
    CLSID encoderClsid = {};
    bool found = false;

    // 映射扩展名到 MIME 类型
    std::wstring mimeType;
    if (ext == L"png") mimeType = L"image/png";
    else if (ext == L"jpg" || ext == L"jpeg") mimeType = L"image/jpeg";
    else if (ext == L"gif") mimeType = L"image/gif";
    else if (ext == L"tiff" || ext == L"tif") mimeType = L"image/tiff";
    else {
        // 未知格式，回退到 PNG
        mimeType = L"image/png";
        ext = L"png";
        // 重新生成 outPath 为 .png 扩展名
        size_t lastSlash = outPath.find_last_of(L"\\/");
        std::wstring dirPart = (lastSlash != std::wstring::npos) ? outPath.substr(0, lastSlash + 1) : L"";
        size_t lastDot = outPath.rfind(L'.');
        std::wstring baseName = (lastDot != std::wstring::npos && (lastSlash == std::wstring::npos || lastDot > lastSlash))
                                ? outPath.substr(lastSlash + 1, lastDot - lastSlash - 1)
                                : (lastSlash != std::wstring::npos ? outPath.substr(lastSlash + 1) : outPath);
        outPath = dirPart + baseName + L".png";
    }

    // 查找编码器
    UINT numEncoders = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&numEncoders, &size);
    if (size > 0) {
        std::vector<BYTE> encoderBuffer(size);
        Gdiplus::ImageCodecInfo* pEncoders = (Gdiplus::ImageCodecInfo*)&encoderBuffer[0];
        Gdiplus::GetImageEncoders(numEncoders, size, pEncoders);
        for (UINT i = 0; i < numEncoders; i++) {
            if (wcscmp(pEncoders[i].MimeType, mimeType.c_str()) == 0) {
                encoderClsid = pEncoders[i].Clsid;
                found = true;
                break;
            }
        }
    }

    if (!found) {
        delete pBitmap;
        return L"";
    }

    // 保存图片
    Gdiplus::Status status = pBitmap->Save(outPath.c_str(), &encoderClsid, NULL);
    delete pBitmap;

    if (status != Gdiplus::Ok) return L"";
    return outPath;
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

    // 使用 GDI+ 加载任意格式（PNG/JPG/BMP 等）
    Gdiplus::Bitmap* pBitmap = new Gdiplus::Bitmap(filePath.c_str());
    if (pBitmap == NULL || pBitmap->GetLastStatus() != Gdiplus::Ok) {
        if (pBitmap) delete pBitmap;
        return false;
    }

    width = pBitmap->GetWidth();
    height = pBitmap->GetHeight();

    if (width <= 0 || height <= 0) {
        delete pBitmap;
        return false;
    }

    // 创建 24 位 DIB
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // 自顶向下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(NULL);
    void* pBits = NULL;
    HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (hBitmap == NULL || pBits == NULL) {
        ReleaseDC(NULL, hdc);
        delete pBitmap;
        return false;
    }

    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    Gdiplus::Graphics graphics(hdcMem);
    graphics.DrawImage(pBitmap, 0, 0, width, height);

    SelectObject(hdcMem, hOldBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdc);

    // 复制 DIB 数据
    DWORD imageSize = ((width * 24 + 31) / 32) * 4 * height;
    imageData.resize(imageSize);
    memcpy(&imageData[0], pBits, imageSize);

    DeleteObject(hBitmap);
    delete pBitmap;

    return true;
}

// ==================== 标签管理函数 ====================

// 加载标签列表
void LoadTags() {
    g_tags.clear();
    g_nextTagId = 1;

    // 迁移旧版 tags.txt
    std::wstring legacyPath = GetSmartClipDataDir() + L"\\tags.txt";
    bool needMigrate = (GetFileAttributesW(legacyPath.c_str()) != INVALID_FILE_ATTRIBUTES);

    sqlite3* db = OpenHistoryDatabase();
    if (!db) return;

    if (needMigrate) {
        HANDLE hFile = CreateFileW(legacyPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD fileSize = GetFileSize(hFile, NULL);
            if (fileSize > 0 && fileSize != INVALID_FILE_SIZE) {
                std::vector<char> buffer(fileSize + 1);
                DWORD dwBytesRead;
                ReadFile(hFile, &buffer[0], fileSize, &dwBytesRead, NULL);
                buffer[dwBytesRead] = '\0';

                char* pData = &buffer[0];
                if (dwBytesRead >= 3 && (BYTE)pData[0] == 0xEF && (BYTE)pData[1] == 0xBB && (BYTE)pData[2] == 0xBF) {
                    pData += 3;
                }

                int wideLen = MultiByteToWideChar(CP_UTF8, 0, pData, -1, NULL, 0);
                std::vector<wchar_t> wideBuffer(wideLen);
                MultiByteToWideChar(CP_UTF8, 0, pData, -1, &wideBuffer[0], wideLen);

                sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
                sqlite3_stmt* insStmt = NULL;
                sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO tags (id,name,color) VALUES (?,?,?)", -1, &insStmt, NULL);

                wchar_t* context = NULL;
                wchar_t* line = wcstok_s(&wideBuffer[0], L"\r\n", &context);
                while (line) {
                    std::wstring lineStr = line;
                    size_t pos1 = lineStr.find(L'|');
                    size_t pos2 = lineStr.find(L'|', pos1 + 1);
                    if (pos1 != std::wstring::npos && pos2 != std::wstring::npos) {
                        int id = _wtoi(lineStr.substr(0, pos1).c_str());
                        std::wstring name = lineStr.substr(pos1 + 1, pos2 - pos1 - 1);
                        COLORREF color = (COLORREF)_wtoi(lineStr.substr(pos2 + 1).c_str());
                        sqlite3_bind_int(insStmt, 1, id);
                        sqlite3_bind_text(insStmt, 2, WToUtf8(name).c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int(insStmt, 3, (int)color);
                        sqlite3_step(insStmt);
                        sqlite3_reset(insStmt);
                    }
                    line = wcstok_s(NULL, L"\r\n", &context);
                }
                sqlite3_finalize(insStmt);
                sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
            }
            // 确保所有路径都关闭句柄（修复 fileSize==0 时的句柄泄漏）
            CloseHandle(hFile);
            DeleteFileW(legacyPath.c_str());
        }
    }

    // 从数据库加载标签
    sqlite3_stmt* stmt = NULL;
    sqlite3_prepare_v2(db, "SELECT id,name,color FROM tags ORDER BY id", -1, &stmt, NULL);
    bool hasTags = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Tag tag;
        tag.id = sqlite3_column_int(stmt, 0);
        tag.name = Utf8ToW((const char*)sqlite3_column_text(stmt, 1));
        tag.color = (COLORREF)sqlite3_column_int(stmt, 2);
        g_tags.push_back(tag);
        if (tag.id >= g_nextTagId) g_nextTagId = tag.id + 1;
        hasTags = true;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    // 无标签时创建默认标签
    if (!hasTags && g_tags.empty()) {
        AddTag(L"重要", RGB(244, 67, 54));
        AddTag(L"工作", RGB(33, 150, 243));
        AddTag(L"个人", RGB(76, 175, 80));
        SaveTags();
    }
}

// 保存标签列表
void SaveTags() {
    sqlite3* db = OpenHistoryDatabase();
    if (!db) return;
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM tags;", NULL, NULL, NULL);

    sqlite3_stmt* stmt = NULL;
    sqlite3_prepare_v2(db, "INSERT INTO tags (id,name,color) VALUES (?,?,?)", -1, &stmt, NULL);
    for (const auto& tag : g_tags) {
        sqlite3_bind_int(stmt, 1, tag.id);
        sqlite3_bind_text(stmt, 2, WToUtf8(tag.name).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, (int)tag.color);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    sqlite3_close(db);
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

// ==================== 数据导出 ====================

// 导出 SmartClip 工作目录为 ZIP 压缩包
bool ExportData(const std::wstring& outputPath) {
    std::wstring dataDir = GetSmartClipDataDir();

    // 先保存数据，确保数据库文件是最新的
    SaveHistory();
    SaveTags();
    SavePasteCount();

    // 使用 tar（bsdtar）创建 ZIP 文件
    // 命令: tar -a -cf "output.zip" -C "data_dir" .
    std::wstring cmd = L"tar -a -cf \"";
    cmd += outputPath;
    cmd += L"\" -C \"";
    cmd += dataDir;
    cmd += L"\" .";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    // 用 cmd.exe /C 执行命令
    std::wstring fullCmd = L"cmd.exe /C " + cmd;
    BOOL ok = CreateProcessW(NULL, &fullCmd[0], NULL, NULL, FALSE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (!ok) return false;

    WaitForSingleObject(pi.hProcess, 30000); // 最多等待30秒
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exitCode == 0;
}

// ==================== 数据导入 ====================

static bool RemoveDirectoryRecursively(const std::wstring& dirPath) {
    std::wstring searchPath = dirPath + L"\\*.*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return RemoveDirectoryW(dirPath.c_str()) != FALSE;
    }

    do {
        std::wstring fileName = findData.cFileName;
        if (fileName == L"." || fileName == L"..") continue;

        std::wstring fullPath = dirPath + L"\\" + fileName;
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            RemoveDirectoryRecursively(fullPath);
        } else {
            DeleteFileW(fullPath.c_str());
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
    return RemoveDirectoryW(dirPath.c_str()) != FALSE;
}

static void CopyDirectoryFiles(const std::wstring& srcDir, const std::wstring& dstDir, bool skipExisting) {
    CreateDirectoryW(dstDir.c_str(), NULL);

    std::wstring searchPath = srcDir + L"\\*.*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        std::wstring fileName = findData.cFileName;
        if (fileName == L"." || fileName == L"..") continue;

        std::wstring srcPath = srcDir + L"\\" + fileName;
        std::wstring dstPath = dstDir + L"\\" + fileName;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CopyDirectoryFiles(srcPath, dstPath, skipExisting);
        } else {
            if (skipExisting && GetFileAttributesW(dstPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                continue;
            }
            CopyFileW(srcPath.c_str(), dstPath.c_str(), FALSE);
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
}

static sqlite3* OpenDatabaseAtPath(const std::wstring& dbPath) {
    std::string dbPathUtf8 = WToUtf8(dbPath);
    sqlite3* db = NULL;
    if (sqlite3_open(dbPathUtf8.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return NULL;
    }
    return db;
}

bool ImportData(const std::wstring& zipPath, bool overwrite) {
    std::wstring dataDir = GetSmartClipDataDir();

    std::wstring tempDir = dataDir + L"\\import_temp_";
    wchar_t tempSuffix[32];
    swprintf_s(tempSuffix, L"%d", GetTickCount());
    tempDir += tempSuffix;

    if (GetFileAttributesW(zipPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    if (!CreateDirectoryW(tempDir.c_str(), NULL)) {
        return false;
    }

    std::wstring cmd = L"tar -xf \"";
    cmd += zipPath;
    cmd += L"\" -C \"";
    cmd += tempDir;
    cmd += L"\"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    std::wstring fullCmd = L"cmd.exe /C " + cmd;
    BOOL ok = CreateProcessW(NULL, &fullCmd[0], NULL, NULL, FALSE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (!ok) {
        RemoveDirectoryRecursively(tempDir);
        return false;
    }

    WaitForSingleObject(pi.hProcess, 30000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0) {
        RemoveDirectoryRecursively(tempDir);
        return false;
    }

    std::wstring tempDbPath = tempDir + L"\\history.db";
    if (GetFileAttributesW(tempDbPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        RemoveDirectoryRecursively(tempDir);
        return false;
    }

    bool success = false;

    if (overwrite) {
        SaveHistory();
        SaveTags();
        SavePasteCount();

        std::wstring currentDbPath = GetDataFilePath();
        std::wstring backupDbPath = currentDbPath + L".bak";

        CopyFileW(currentDbPath.c_str(), backupDbPath.c_str(), FALSE);

        CopyFileW(tempDbPath.c_str(), currentDbPath.c_str(), FALSE);

        std::wstring tempImagesDir = tempDir + L"\\images";
        std::wstring currentImagesDir = dataDir + L"\\images";
        if (GetFileAttributesW(tempImagesDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
            CopyDirectoryFiles(tempImagesDir, currentImagesDir, false);
        }

        LoadHistory();
        LoadTags();
        LoadPasteCount();
        success = true;
    } else {
        sqlite3* srcDb = OpenDatabaseAtPath(tempDbPath);
        sqlite3* dstDb = OpenHistoryDatabase();

        if (srcDb && dstDb) {
            sqlite3_exec(dstDb, "BEGIN TRANSACTION;", NULL, NULL, NULL);

            std::map<std::wstring, int> tagNameMap;
            sqlite3_stmt* tagStmt = NULL;
            const char* tagQuery = "SELECT id, name, color FROM tags";
            sqlite3_prepare_v2(dstDb, tagQuery, -1, &tagStmt, NULL);
            while (sqlite3_step(tagStmt) == SQLITE_ROW) {
                int id = sqlite3_column_int(tagStmt, 0);
                std::wstring name = Utf8ToW((const char*)sqlite3_column_text(tagStmt, 1));
                tagNameMap[name] = id;
            }
            sqlite3_finalize(tagStmt);

            sqlite3_stmt* srcTagStmt = NULL;
            sqlite3_prepare_v2(srcDb, tagQuery, -1, &srcTagStmt, NULL);
            std::map<int, int> tagIdMap;
            int nextTagId = 1;
            for (auto& pair : tagNameMap) {
                int id = pair.second;
                if (id >= nextTagId) nextTagId = id + 1;
            }

            while (sqlite3_step(srcTagStmt) == SQLITE_ROW) {
                int srcId = sqlite3_column_int(srcTagStmt, 0);
                std::wstring name = Utf8ToW((const char*)sqlite3_column_text(srcTagStmt, 1));
                int color = sqlite3_column_int(srcTagStmt, 2);

                auto it = tagNameMap.find(name);
                if (it == tagNameMap.end()) {
                    int newId = nextTagId++;
                    tagNameMap[name] = newId;
                    tagIdMap[srcId] = newId;

                    sqlite3_stmt* insertTagStmt = NULL;
                    const char* insertTagSql = "INSERT INTO tags (id, name, color) VALUES (?, ?, ?)";
                    sqlite3_prepare_v2(dstDb, insertTagSql, -1, &insertTagStmt, NULL);
                    sqlite3_bind_int(insertTagStmt, 1, newId);
                    sqlite3_bind_text(insertTagStmt, 2, WToUtf8(name).c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(insertTagStmt, 3, color);
                    sqlite3_step(insertTagStmt);
                    sqlite3_finalize(insertTagStmt);
                } else {
                    tagIdMap[srcId] = it->second;
                }
            }
            sqlite3_finalize(srcTagStmt);

            sqlite3_stmt* srcHistStmt = NULL;
            const char* histQuery =
                "SELECT type,content,timestamp,source_app,source_app_path,"
                "is_favorite,image_width,image_height,thumb_width,thumb_height,"
                "image_file_name,image_file_path,thumb_data,tags FROM history "
                "ORDER BY id ASC";
            sqlite3_prepare_v2(srcDb, histQuery, -1, &srcHistStmt, NULL);

            const char* insertHistSql =
                "INSERT INTO history (type,content,timestamp,source_app,source_app_path,"
                "is_favorite,image_width,image_height,thumb_width,thumb_height,"
                "image_file_name,image_file_path,thumb_data,tags) "
                "SELECT ?,?,?,?,?,?,?,?,?,?,?,?,?,? "
                "WHERE NOT EXISTS (SELECT 1 FROM history WHERE content = ? AND timestamp = ?)";
            sqlite3_stmt* insertHistStmt = NULL;
            sqlite3_prepare_v2(dstDb, insertHistSql, -1, &insertHistStmt, NULL);

            while (sqlite3_step(srcHistStmt) == SQLITE_ROW) {
                int type = sqlite3_column_int(srcHistStmt, 0);
                std::wstring content = Utf8ToW((const char*)sqlite3_column_text(srcHistStmt, 1));
                std::wstring timestamp = Utf8ToW((const char*)sqlite3_column_text(srcHistStmt, 2));
                std::wstring sourceApp = Utf8ToW((const char*)sqlite3_column_text(srcHistStmt, 3));
                std::wstring sourceAppPath = Utf8ToW((const char*)sqlite3_column_text(srcHistStmt, 4));
                int isFavorite = sqlite3_column_int(srcHistStmt, 5);
                int imageWidth = sqlite3_column_int(srcHistStmt, 6);
                int imageHeight = sqlite3_column_int(srcHistStmt, 7);
                int thumbWidth = sqlite3_column_int(srcHistStmt, 8);
                int thumbHeight = sqlite3_column_int(srcHistStmt, 9);
                std::wstring imageFileName = Utf8ToW((const char*)sqlite3_column_text(srcHistStmt, 10));
                std::wstring imageFilePath = Utf8ToW((const char*)sqlite3_column_text(srcHistStmt, 11));
                const void* thumbData = sqlite3_column_blob(srcHistStmt, 12);
                int thumbSize = sqlite3_column_bytes(srcHistStmt, 12);
                std::wstring tagsStr = Utf8ToW((const char*)sqlite3_column_text(srcHistStmt, 13));

                std::wstring newTagsStr;
                if (!tagsStr.empty()) {
                    size_t pos = 0;
                    while (pos < tagsStr.size()) {
                        size_t comma = tagsStr.find(L',', pos);
                        std::wstring tagIdStr = (comma == std::wstring::npos)
                            ? tagsStr.substr(pos)
                            : tagsStr.substr(pos, comma - pos);
                        int srcTagId = _wtoi(tagIdStr.c_str());
                        auto it = tagIdMap.find(srcTagId);
                        int newTagId = (it != tagIdMap.end()) ? it->second : srcTagId;
                        if (!newTagsStr.empty()) newTagsStr += L',';
                        newTagsStr += std::to_wstring(newTagId);
                        if (comma == std::wstring::npos) break;
                        pos = comma + 1;
                    }
                }

                sqlite3_bind_int(insertHistStmt, 1, type);
                sqlite3_bind_text(insertHistStmt, 2, WToUtf8(content).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(insertHistStmt, 3, WToUtf8(timestamp).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(insertHistStmt, 4, WToUtf8(sourceApp).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(insertHistStmt, 5, WToUtf8(sourceAppPath).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(insertHistStmt, 6, isFavorite);
                sqlite3_bind_int(insertHistStmt, 7, imageWidth);
                sqlite3_bind_int(insertHistStmt, 8, imageHeight);
                sqlite3_bind_int(insertHistStmt, 9, thumbWidth);
                sqlite3_bind_int(insertHistStmt, 10, thumbHeight);
                sqlite3_bind_text(insertHistStmt, 11, WToUtf8(imageFileName).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(insertHistStmt, 12, WToUtf8(imageFilePath).c_str(), -1, SQLITE_TRANSIENT);
                if (thumbData && thumbSize > 0) {
                    sqlite3_bind_blob(insertHistStmt, 13, thumbData, thumbSize, SQLITE_TRANSIENT);
                } else {
                    sqlite3_bind_null(insertHistStmt, 13);
                }
                sqlite3_bind_text(insertHistStmt, 14, WToUtf8(newTagsStr).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(insertHistStmt, 15, WToUtf8(content).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(insertHistStmt, 16, WToUtf8(timestamp).c_str(), -1, SQLITE_TRANSIENT);

                sqlite3_step(insertHistStmt);
                sqlite3_reset(insertHistStmt);
            }
            sqlite3_finalize(srcHistStmt);
            sqlite3_finalize(insertHistStmt);

            sqlite3_exec(dstDb, "COMMIT;", NULL, NULL, NULL);

            std::wstring tempOriginalsDir = tempDir + L"\\images\\originals";
            std::wstring currentOriginalsDir = GetImagesPath();
            if (GetFileAttributesW(tempOriginalsDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
                CopyDirectoryFiles(tempOriginalsDir, currentOriginalsDir, true);
            }

            std::wstring tempThumbsDir = tempDir + L"\\images\\thumbs";
            std::wstring currentThumbsDir = GetThumbsPath();
            if (GetFileAttributesW(tempThumbsDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
                CopyDirectoryFiles(tempThumbsDir, currentThumbsDir, true);
            }

            success = true;
        }

        if (srcDb) sqlite3_close(srcDb);
        if (dstDb) sqlite3_close(dstDb);

        if (success) {
            LoadHistory();
            LoadTags();
        }
    }

    RemoveDirectoryRecursively(tempDir);
    return success;
}

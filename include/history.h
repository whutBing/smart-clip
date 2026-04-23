#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include <map>
#include <set>

enum ClipboardItemType {
    TYPE_TEXT,
    TYPE_IMAGE,
    TYPE_FILE
};

// 标签结构
struct Tag {
    int id;                     // 标签ID
    std::wstring name;          // 标签名称
    COLORREF color;             // 标签颜色
};

struct ClipboardItem {
    ClipboardItemType type;
    std::wstring content;      // 文本内容或文件路径
    std::wstring timestamp;
    std::wstring sourceApp;    // 来源应用名称（如 chrome.exe）
    std::wstring sourceAppPath; // 来源应用完整路径（用于获取图标）
    std::vector<BYTE> imageData;  // 缩略图数据（内存中只保留缩略图）
    int imageWidth;            // 原图宽度
    int imageHeight;           // 原图高度
    int thumbWidth;            // 缩略图宽度
    int thumbHeight;           // 缩略图高度
    std::wstring imageFileName; // 图片文件名（不含路径，如 "a1b2c3.png"）- 截图用
    std::wstring imageFilePath; // 图片文件原始路径（图片文件类型用，非截图）
    bool isFavorite;  // 是否收藏（保留兼容性，有标签即为收藏）
    bool isInTransferStation;  // 是否在中转站中
    std::set<int> tagIds;      // 所属标签ID集合
};

// 中转站项目结构
struct TransferStationItem {
    int historyIndex;           // 对应 g_history 中的索引
    HWND hwndCard;              // 对应的卡片窗口句柄
    POINT position;             // 卡片位置
    SIZE size;                  // 卡片大小
    bool isHidden;              // 是否已隐藏（收起后）
    bool isCollapsed;           // 是否处于收起状态（窄条）
};

// 主窗口句柄
extern HWND g_hwndMain;

// 剪贴板历史管理类
extern std::vector<ClipboardItem> g_history;
extern HWND g_hwndListBox;
extern std::wstring g_searchKeyword;
extern int g_currentTab;  // 当前选中的标签页索引
extern std::vector<int> g_displayIndexMap;  // 显示索引到实际历史记录索引的映射
extern std::map<int, bool> g_expandedItems;  // 记录每个历史项的展开状态（key为g_history索引）

// 标签系统
extern std::vector<Tag> g_tags;             // 全局标签列表
extern int g_currentFilterTagId;            // 当前筛选的标签ID（-1=全部收藏，0=未筛选）

// 翻页相关
extern int g_currentPage;           // 当前页码（从0开始）
extern int g_totalPages;            // 总页数
extern int g_listBoxTopIndex;       // 列表框顶部索引
extern HWND g_hwndPageUpBtn;        // 上一页按钮句柄
extern HWND g_hwndPageDownBtn;      // 下一页按钮句柄
#define ITEMS_PER_PAGE 9            // 每页显示的项目数

// 中转站相关全局变量
extern std::vector<TransferStationItem> g_transferStation;
extern HWND g_hwndTransferStationContainer;
extern bool g_isTransferStationVisible;

std::wstring GetDataFilePath();
void SaveHistory();
void LoadHistory();
std::wstring GetActiveWindowProcessName();
std::wstring GetActiveWindowProcessPath();  // 获取完整路径
HICON GetAppIcon(const std::wstring& exePath);  // 获取应用图标
std::wstring GetCurrentTimeString();
std::wstring GetRelativeTimeString(const std::wstring& timeStr);
void AddToHistory(const std::wstring& content);
void AddImageToHistory(const std::vector<BYTE>& imageData, int width, int height);
void AddImageFileToHistory(const std::wstring& filePath, const std::vector<BYTE>& imageData, int width, int height);  // 添加图片文件（只保存缩略图和路径）
void AddFileToHistory(const std::wstring& filePath);
void UpdateListBox();
void ClearIconCache();  // 清理图标缓存
std::wstring GetImagesPath();  // 获取图片存储目录
std::wstring GetThumbsPath();  // 获取缩略图存储目录
std::wstring GenerateImageFileName();  // 生成唯一图片文件名
bool SaveOriginalImage(const std::vector<BYTE>& imageData, int width, int height, const std::wstring& fileName);  // 保存原图
bool GenerateThumbnail(const std::vector<BYTE>& imageData, int width, int height, std::vector<BYTE>& thumbData, int& thumbWidth, int& thumbHeight, int maxSize = 128);  // 生成缩略图
bool LoadOriginalImage(const std::wstring& fileName, std::vector<BYTE>& imageData, int& width, int& height);  // 加载原图

// 数据管理函数
void ClearNonFavoriteHistory();                     // 清理所有非收藏历史记录
void CleanInvalidImageRecords();                    // 清理失效图片记录
ULONGLONG GetDataDirSize();                         // 获取数据目录总大小（字节）
std::wstring FormatFileSize(ULONGLONG bytes);       // 格式化文件大小

// 粘贴次数统计
extern int g_pasteCount;
void LoadPasteCount();
void SavePasteCount();
void IncrementPasteCount();
std::wstring GetSmartClipDataDir();                 // 获取 SmartClip 数据根目录
bool MigrateDataDir(const std::wstring& newDir);    // 迁移数据目录到新位置
void LoadCustomDataDir();                           // 加载自定义数据目录配置
void SaveCustomDataDir();                           // 保存自定义数据目录配置

// 标签管理函数
void LoadTags();                                    // 加载标签列表
void SaveTags();                                    // 保存标签列表
int AddTag(const std::wstring& name, COLORREF color = RGB(66, 133, 244));  // 添加标签，返回标签ID
bool RemoveTag(int tagId);                          // 删除标签
bool RenameTag(int tagId, const std::wstring& newName);  // 重命名标签
bool SetTagColor(int tagId, COLORREF color);        // 设置标签颜色
Tag* GetTagById(int tagId);                         // 根据ID获取标签
void AddTagToItem(int historyIndex, int tagId);     // 给项目添加标签
void RemoveTagFromItem(int historyIndex, int tagId); // 从项目移除标签
bool ItemHasTag(int historyIndex, int tagId);       // 检查项目是否有某标签
int GetFavoriteCount();                             // 获取收藏总数（有任意标签的项目数）
int GetTagItemCount(int tagId);                     // 获取某标签下的项目数

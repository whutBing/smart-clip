#include <windows.h>
#include <windowsx.h>  // 用于 GET_X_LPARAM, GET_Y_LPARAM
#include <commctrl.h>
#include <objidl.h>   // MinGW 下必须在 gdiplus.h 之前,提供 PROPID
#include <gdiplus.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <shlobj.h>   // 用于拖放
#include <shellapi.h> // 用于 ShellExecuteW
#include <ole2.h>     // 用于OLE拖放
#include <cmath>  // 用于sin函数
#include <algorithm>  // 用于std::remove_if
#include "resource.h"  // 添加资源头文件
#include "history.h"
#include "tray.h"
#include "hotkey.h"
#include "settings.h"
#include "search.h"
#include "card_renderer.h"
#include "image_handler.h"
#include "transfer_station.h"
#include "text_utils.h"
#include "graphics_utils.h"

#ifdef _MSC_VER
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#endif

// 定义 TTTOOLINFOW_V1_SIZE（如果未定义）
#ifndef TTTOOLINFOW_V1_SIZE
#define TTTOOLINFOW_V1_SIZE 44
#endif

using namespace Gdiplus;

// 函数声明
bool InputBox(HWND hwnd, const wchar_t* title, const wchar_t* prompt, wchar_t* result, int maxLen);

// 菜单和控件ID定义
#define ID_LISTBOX 1001
#define ID_BATCH_EDIT_BUTTON 1002
#define ID_REFRESH_BUTTON 1003
#define IDM_SETTINGS 2001
#define IDM_STARTUP 2002
#define IDM_SHOW 2003
#define IDM_EXIT 2004
#define IDM_HOTKEY_SETTINGS 2005
#define IDM_NOTIFICATION 2006
#define IDM_THEME_LIGHT 2007
#define IDM_THEME_DARK 2008
#define ID_TOPMOST_BUTTON 1006
#define ID_DARKMODE_BUTTON 1007
#define IDM_COPY 3001
#define IDM_PASTE 3002
#define IDM_FAVORITE 3003
#define IDM_DELETE 3004
#define IDM_ADD_TO_STATION 3005
#define IDM_REMOVE_FROM_STATION 3006
#define IDM_OPEN_LOCATION 3007  // 打开所在位置
#define IDM_BATCH_ADD_TAG 3008  // 批量加入标签

// 标签菜单ID（动态分配，从3100开始）
#define IDM_TAG_BASE 3100
#define IDM_TAG_FILTER_ALL 3200  // 全部收藏筛选
#define IDM_TAG_FILTER_BASE 3201  // 标签筛选基础ID
#define IDM_TAG_ADD_NEW 3300      // 新增标签

// 增加新的控件ID定义
#define ID_TAB_CONTROL 103
#define ID_SEARCH_BOX 104
#define ID_SEARCH_BUTTON 105
#define ID_TRANSFER_STATION_BUTTON 1008

// 筛选按钮ID
#define ID_FILTER_ALL 1101
#define ID_FILTER_TEXT 1102
#define ID_FILTER_IMAGE 1103
#define ID_FILTER_FILE 1104
#define ID_FILTER_FAVORITE 1105

// 翻页按钮ID
#define ID_PAGE_UP_BTN 1201
#define ID_PAGE_DOWN_BTN 1202

// 标题栏按钮ID
#define ID_TITLEBAR_TOPMOST 1301
#define ID_TITLEBAR_MINIMIZE 1302
#define ID_TITLEBAR_MAXIMIZE 1303
#define ID_TITLEBAR_CLOSE 1304

// 标题栏高度
#define TITLEBAR_HEIGHT 30

// 筛选按钮句柄
HWND g_hwndFilterAll = NULL;
HWND g_hwndFilterText = NULL;
HWND g_hwndFilterImage = NULL;
HWND g_hwndFilterFile = NULL;
HWND g_hwndFilterFavorite = NULL;

// 剪贴板恢复标志
bool g_isRestoringClipboard = false;
// 中转站粘贴标志（定义在transfer_station.cpp）
extern bool g_isTransferStationPasting;
// 主窗口句柄
extern HWND g_hwndMain;
HWND g_hwndMain;
// 主菜单句柄
extern HMENU g_hMenu;
HMENU g_hMenu;
// 窗口置顶状态
bool g_isTopmost = false;
// 置顶按钮悬浮状态
bool g_isTopmostBtnHover = false;
// 暗黑模式状态
bool g_isDarkMode = false;
// 主题模式设置
ThemeMode g_themeMode = THEME_LIGHT;

// 标题栏按钮句柄
HWND g_hwndTitleTopmost = NULL;
HWND g_hwndTitleMinimize = NULL;
HWND g_hwndTitleMaximize = NULL;
HWND g_hwndTitleClose = NULL;
// 标题栏按钮悬浮状态
bool g_isTitleTopmostHover = false;
bool g_isTitleMinimizeHover = false;
bool g_isTitleMaximizeHover = false;
bool g_isTitleCloseHover = false;
// 标题栏按钮原始窗口过程
WNDPROC g_oldTitleTopmostProc = NULL;
WNDPROC g_oldTitleMinimizeProc = NULL;
WNDPROC g_oldTitleMaximizeProc = NULL;
WNDPROC g_oldTitleCloseProc = NULL;

// 暗黑模式颜色定义
#define COLOR_LIGHT_BG RGB(245, 245, 245)       // 明亮模式背景色
#define COLOR_LIGHT_WHITE RGB(255, 255, 255)    // 明亮模式白色
#define COLOR_LIGHT_TEXT RGB(60, 60, 60)        // 明亮模式文字色
#define COLOR_DARK_BG RGB(23, 23, 26)           // 暗黑模式背景色
#define COLOR_DARK_WHITE RGB(46, 46, 48)        // 暗黑模式白色（输入框等）
#define COLOR_DARK_TEXT RGB(226, 222, 226)      // 暗黑模式文字色

// 获取当前模式的颜色
inline COLORREF GetBgColor() { return g_isDarkMode ? COLOR_DARK_BG : COLOR_LIGHT_BG; }
inline COLORREF GetWhiteColor() { return g_isDarkMode ? COLOR_DARK_WHITE : COLOR_LIGHT_WHITE; }
inline COLORREF GetTextColor() { return g_isDarkMode ? COLOR_DARK_TEXT : COLOR_LIGHT_TEXT; }

// 检测系统是否为暗黑模式
bool IsSystemDarkMode() {
    HKEY hKey;
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size);
        RegCloseKey(hKey);
    }
    return value == 0;  // 0 表示暗黑模式，1 表示明亮模式
}

// 应用主题
void ApplyTheme() {
    bool newDarkMode = false;
    switch (g_themeMode) {
        case THEME_LIGHT:
            newDarkMode = false;
            break;
        case THEME_DARK:
            newDarkMode = true;
            break;
        case THEME_SYSTEM:
            newDarkMode = IsSystemDarkMode();
            break;
    }

    if (newDarkMode != g_isDarkMode) {
        g_isDarkMode = newDarkMode;

        // 更新窗口背景色
        if (g_hwndMain) {
            SetClassLongPtrW(g_hwndMain, GCLP_HBRBACKGROUND, (LONG_PTR)CreateSolidBrush(GetBgColor()));

            // 更新列表框背景色
            if (g_hwndListBox) {
                InvalidateRect(g_hwndListBox, NULL, TRUE);
            }

            // 更新搜索框背景色
            if (g_hwndSearchBox) {
                InvalidateRect(g_hwndSearchBox, NULL, TRUE);
            }

            // 强制重绘窗口
            InvalidateRect(g_hwndMain, NULL, TRUE);
            UpdateWindow(g_hwndMain);
        }

        // 刷新设置对话框
        if (g_hwndSettingsDlg && IsWindow(g_hwndSettingsDlg)) {
            InvalidateRect(g_hwndSettingsDlg, NULL, TRUE);
        }
    }
}

// 当前右键选中的索引
int g_contextMenuIndex = -1;
// 记录呼出剪贴板前的活动窗口
HWND g_previousActiveWindow = NULL;

// 中转站相关全局变量
std::vector<TransferStationItem> g_transferStation;
HWND g_hwndTransferStationContainer = NULL;
bool g_isTransferStationVisible = false;

// 列表框子类化
WNDPROC g_oldListBoxProc = NULL;
HWND g_hwndListBoxTooltip = NULL;  // 列表框 Tooltip
int g_lastTooltipIndex = -1;       // 上次显示 Tooltip 的项目索引
int g_hoverIconIndex = -1;         // 鼠标悬浮的图标所在项目索引
bool g_isHoveringIcon = false;     // 鼠标是否悬浮在图标上
int g_hoverFolderIndex = -1;       // 鼠标悬浮的文件夹所在项目索引
bool g_isHoveringFolder = false;   // 鼠标是否悬浮在文件夹名称上
int g_hoverImageIndex = -1;        // 鼠标悬浮的图像所在项目索引
bool g_isHoveringImage = false;    // 鼠标是否悬浮在图像上

// 文件夹下划线动画（展开）
#define ID_FOLDER_UNDERLINE_TIMER 203
float g_folderUnderlineProgress = 0.0f;  // 下划线动画进度 0.0-1.0
bool g_folderUnderlineAnimating = false; // 是否正在动画
int g_folderUnderlineAnimIndex = -1;     // 正在动画的项目索引

// 文件夹下划线动画（收起）
#define ID_FOLDER_COLLAPSE_TIMER 204
float g_folderCollapseProgress = 0.0f;   // 收起动画进度 0.0-1.0
bool g_folderCollapseAnimating = false;  // 是否正在收起动画
int g_folderCollapseAnimIndex = -1;      // 正在收起动画的项目索引

// 链接文本（URL/路径）颜色动画
#define ID_LINK_COLOR_EXPAND_TIMER 205   // 链接文本颜色展开动画
#define ID_LINK_COLOR_COLLAPSE_TIMER 206 // 链接文本颜色收起动画
int g_hoverLinkIndex = -1;              // 鼠标悬浮的链接所在项目索引
bool g_isHoveringLink = false;          // 鼠标是否悬浮在链接文本上
float g_linkColorExpandProgress = 0.0f; // 颜色展开动画进度 0.0-1.0
bool g_linkColorExpandAnimating = false; // 是否正在展开动画
int g_linkColorExpandAnimIndex = -1;     // 正在展开动画的项目索引
float g_linkColorCollapseProgress = 0.0f; // 颜色收起动画进度 0.0-1.0
bool g_linkColorCollapseAnimating = false; // 是否正在收起动画
int g_linkColorCollapseAnimIndex = -1;   // 正在收起动画的项目索引

// 文件拖放相关
bool g_isDragging = false;               // 是否正在拖拽
POINT g_dragStartPoint = {0, 0};         // 拖拽起始点
int g_dragItemIndex = -1;                // 正在拖拽的项目索引
#define DRAG_THRESHOLD 5                 // 拖拽阈值（像素）

// IDropSource 实现
class CDropSource : public IDropSource {
private:
    LONG m_refCount;
public:
    CDropSource() : m_refCount(1) {}
    virtual ~CDropSource() = default;

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) delete this;
        return count;
    }

    // IDropSource
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override {
        if (fEscapePressed) return DRAGDROP_S_CANCEL;
        if (!(grfKeyState & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD /*dwEffect*/) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
};

// 简单的 IDropTarget 实现（用于显示拖拽图像）
class CDropTarget : public IDropTarget {
private:
    LONG m_refCount;
    IDropTargetHelper* m_pDropTargetHelper;

public:
    CDropTarget() : m_refCount(1), m_pDropTargetHelper(NULL) {
        CoCreateInstance(CLSID_DragDropHelper, NULL, CLSCTX_INPROC_SERVER,
                        IID_IDropTargetHelper, (void**)&m_pDropTargetHelper);
    }
    virtual ~CDropTarget() {
        if (m_pDropTargetHelper) m_pDropTargetHelper->Release();
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) delete this;
        return count;
    }

    // IDropTarget
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pDataObj, DWORD /*grfKeyState*/, POINTL pt, DWORD* pdwEffect) override {
        // 先设置效果为 COPY，再通知 helper
        *pdwEffect = DROPEFFECT_COPY;
        if (m_pDropTargetHelper) {
            POINT point = {pt.x, pt.y};
            m_pDropTargetHelper->DragEnter(g_hwndMain, pDataObj, &point, *pdwEffect);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD /*grfKeyState*/, POINTL pt, DWORD* pdwEffect) override {
        // 先设置效果为 COPY，再通知 helper
        *pdwEffect = DROPEFFECT_COPY;
        if (m_pDropTargetHelper) {
            POINT point = {pt.x, pt.y};
            m_pDropTargetHelper->DragOver(&point, *pdwEffect);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        if (m_pDropTargetHelper) {
            m_pDropTargetHelper->DragLeave();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* pDataObj, DWORD /*grfKeyState*/, POINTL pt, DWORD* pdwEffect) override {
        if (m_pDropTargetHelper) {
            POINT point = {pt.x, pt.y};
            m_pDropTargetHelper->Drop(pDataObj, &point, *pdwEffect);
        }
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }
};

static CDropTarget* g_pDropTarget = NULL;

// 创建文件拖放数据对象
IDataObject* CreateFileDataObject(const std::wstring& filePath) {
    IDataObject* pDataObject = NULL;

    // 使用 Shell 创建数据对象
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(filePath.c_str());
    if (pidl) {
        IShellFolder* pDesktop = NULL;
        if (SUCCEEDED(SHGetDesktopFolder(&pDesktop))) {
            PIDLIST_RELATIVE pidlChild = ILFindLastID(pidl);
            PIDLIST_ABSOLUTE pidlParent = ILClone(pidl);
            ILRemoveLastID(pidlParent);

            IShellFolder* pFolder = NULL;
            if (SUCCEEDED(pDesktop->BindToObject(pidlParent, NULL, IID_IShellFolder, (void**)&pFolder))) {
                pFolder->GetUIObjectOf(NULL, 1, (PCUITEMID_CHILD*)&pidlChild, IID_IDataObject, NULL, (void**)&pDataObject);
                pFolder->Release();
            }
            ILFree(pidlParent);
            pDesktop->Release();
        }
        ILFree(pidl);
    }

    return pDataObject;
}

// 为拖放设置图像（显示文件图标和文件名）
void SetDragImage(IDataObject* pDataObject, const std::wstring& filePath, POINT /*ptStart*/) {
    IDragSourceHelper* pDragSourceHelper = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, NULL, CLSCTX_INPROC_SERVER,
                                   IID_IDragSourceHelper, (void**)&pDragSourceHelper))) {
        IDragSourceHelper2* pDragSourceHelper2 = NULL;
        if (SUCCEEDED(pDragSourceHelper->QueryInterface(IID_PPV_ARGS(&pDragSourceHelper2)))) {
            pDragSourceHelper2->SetFlags(0x0001);
            pDragSourceHelper2->Release();
        }

        SHFILEINFOW sfi = {};
        SHGetFileInfoW(filePath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON);

        if (sfi.hIcon) {
            std::wstring fileName = filePath;
            size_t pos = fileName.find_last_of(L"\\/");
            if (pos != std::wstring::npos) fileName = fileName.substr(pos + 1);

            int iconSize = 32;
            int textHeight = 20;
            int hPadding = 16;

            // 测量文件名宽度，动态计算位图宽度
            HDC hdcScreen = GetDC(NULL);
            HDC hdcMeasure = CreateCompatibleDC(hdcScreen);
            Gdiplus::Graphics gMeasure(hdcMeasure);
            Gdiplus::Font font(L"Microsoft YaHei", 9.0f);
            Gdiplus::RectF bounds;
            gMeasure.MeasureString(fileName.c_str(), -1, &font, Gdiplus::PointF(0, 0), &bounds);
            DeleteDC(hdcMeasure);

            int textWidth = (int)(bounds.Width + 0.5f) + hPadding * 2;
            int minWidth = iconSize + hPadding * 2;
            int maxWidth = 320;
            int bmpWidth = textWidth;
            if (bmpWidth < minWidth) bmpWidth = minWidth;
            if (bmpWidth > maxWidth) bmpWidth = maxWidth;
            int bmpHeight = iconSize + textHeight + 4;

            // 使用 32 位 ARGB 位图实现真正的 alpha 透明
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = bmpWidth;
            bmi.bmiHeader.biHeight = -bmpHeight;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void* pBits = NULL;
            HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
            HDC hdcMem = CreateCompatibleDC(hdcScreen);
            HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

            // 清零（全透明）
            memset(pBits, 0, bmpWidth * bmpHeight * 4);

            // 用 GDI+ 绘制带 alpha 的内容
            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            // 白色圆角背景
            Gdiplus::GraphicsPath bgPath;
            CreateRoundRectPath(&bgPath, 0, 0, bmpWidth, bmpHeight, 8);
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(240, 255, 255, 255));
            g.FillPath(&bgBrush, &bgPath);

            // 边框
            Gdiplus::Pen borderPen(Gdiplus::Color(60, 0, 0, 0), 1.0f);
            g.DrawPath(&borderPen, &bgPath);

            // 图标
            int iconX = (bmpWidth - iconSize) / 2;
            DrawIconEx(hdcMem, iconX, 2, sfi.hIcon, iconSize, iconSize, 0, NULL, DI_NORMAL);

            // 文件名
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 50, 50, 50));
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
            Gdiplus::RectF textRect(2.0f, (float)(iconSize + 2), (float)(bmpWidth - 4), (float)textHeight);
            g.DrawString(fileName.c_str(), -1, &font, textRect, &sf, &textBrush);

            g.Flush();

            // 修正预乘 alpha（GDI 绘制的像素需要预乘）
            BYTE* pixels = (BYTE*)pBits;
            for (int i = 0; i < bmpWidth * bmpHeight; i++) {
                BYTE b = pixels[0], gr = pixels[1], r = pixels[2], a = pixels[3];
                if (a > 0 && a < 255) {
                    pixels[0] = (BYTE)((b * a) / 255);
                    pixels[1] = (BYTE)((gr * a) / 255);
                    pixels[2] = (BYTE)((r * a) / 255);
                }
                pixels += 4;
            }

            SelectObject(hdcMem, hOldBitmap);
            DeleteDC(hdcMem);
            ReleaseDC(NULL, hdcScreen);

            SHDRAGIMAGE shdi = {};
            shdi.sizeDragImage.cx = bmpWidth;
            shdi.sizeDragImage.cy = bmpHeight;
            shdi.ptOffset.x = bmpWidth / 2;
            shdi.ptOffset.y = iconSize / 2;
            shdi.hbmpDragImage = hBitmap;
            shdi.crColorKey = CLR_NONE;

            pDragSourceHelper->InitializeFromBitmap(&shdi, pDataObject);
            DestroyIcon(sfi.hIcon);
        }

        pDragSourceHelper->Release();
    }
}

// 搜索框子类化（渐变光标）
WNDPROC g_oldSearchBoxProc = NULL;
#define ID_CARET_TIMER 101

// 置顶按钮子类化（悬浮效果）
WNDPROC g_oldTopmostBtnProc = NULL;
HWND g_hwndTopmostBtn = NULL;

// 批量编辑按钮子类化（悬浮效果）
WNDPROC g_oldBatchEditBtnProc = NULL;
HWND g_hwndBatchEditBtn = NULL;
bool g_isBatchEditBtnHover = false;
bool g_isBatchEditMode = false;  // 批量编辑模式状态
std::vector<int> g_selectedItems;  // 批量编辑模式下选中的记录索引

// 按钮图片句柄
Gdiplus::Image* g_imgTopmostSelected = NULL;
Gdiplus::Image* g_imgTopmostUnselected = NULL;
Gdiplus::Image* g_imgBatchEditSelected = NULL;
Gdiplus::Image* g_imgBatchEditUnselected = NULL;
Gdiplus::Image* g_imgFolderIcon = NULL;  // 文件夹图标
Gdiplus::Image* g_imgNoExistIcon = NULL;  // 文件不存在图标

// 置顶按钮波浪动画
#define ID_TOPMOST_ANIM_TIMER 201
#define ID_BATCH_EDIT_ANIM_TIMER 202
float g_topmostAnimProgress = 0.0f;  // 动画进度 0.0-1.0
bool g_topmostAnimating = false;     // 是否正在动画
bool g_topmostAnimDirection = true;  // true=选中动画, false=取消选中动画
float g_batchEditAnimProgress = 0.0f;
bool g_batchEditAnimating = false;
bool g_batchEditAnimDirection = true;

bool g_caretVisible = false;
float g_caretGradientPos = 0.0f;
int g_caretBlinkCounter = 0;  // 闪烁计数器
bool g_caretShowState = true; // 光标显示状态

// 滚动条自动隐藏
#define ID_SCROLLBAR_HIDE_TIMER 100
bool g_scrollbarVisible = false;

// 列表框顶部索引缓存（用于快捷键提示，避免频繁调用LB_GETTOPINDEX）
int g_listBoxTopIndex = 0;

// 翻页相关
#define ITEMS_PER_PAGE 9         // 每页显示的项目数
int g_currentPage = 0;           // 当前页码（从0开始）
int g_totalPages = 1;            // 总页数
HWND g_hwndPageUpBtn = NULL;     // 上一页按钮句柄
HWND g_hwndPageDownBtn = NULL;   // 下一页按钮句柄
bool g_isPageUpBtnHover = false; // 上一页按钮悬浮状态
bool g_isPageDownBtnHover = false; // 下一页按钮悬浮状态
WNDPROC g_oldPageUpBtnProc = NULL;   // 上一页按钮原始窗口过程
WNDPROC g_oldPageDownBtnProc = NULL; // 下一页按钮原始窗口过程

// 快捷键提示显示状态（与滚动条同步，滚动时显示，停止800ms后隐藏）
bool g_quickPasteHintVisible = false;

// 平滑滚动相关
#define ID_SMOOTH_SCROLL_TIMER 101
static float g_smoothScrollTarget = 0.0f;    // 目标滚动位置
static float g_smoothScrollCurrent = 0.0f;   // 当前滚动位置
static bool g_smoothScrollActive = false;    // 是否正在平滑滚动
static HWND g_smoothScrollListBox = NULL;    // 正在滚动的列表框

// 计算单个项目的高度（基于显示索引）
int GetItemDisplayHeight(int displayIndex) {
    if (displayIndex < 0 || displayIndex >= (int)g_displayIndexMap.size()) {
        return 57;  // 默认文本高度
    }

    int actualIndex = g_displayIndexMap[displayIndex];
    if (actualIndex < 0 || actualIndex >= (int)g_history.size()) {
        return 57;
    }

    const ClipboardItem& item = g_history[actualIndex];

    if (item.type == TYPE_IMAGE) {
        // 检查图像尺寸是否有效
        if (item.imageWidth <= 0 || item.imageHeight <= 0) {
            return 87;  // 默认图像高度
        }

        // 获取列表框宽度
        RECT rcListBox;
        GetClientRect(g_hwndListBox, &rcListBox);
        int listBoxWidth = rcListBox.right - rcListBox.left - 20;
        if (listBoxWidth < 100) listBoxWidth = 560;

        int availableWidth = listBoxWidth - 20;
        float scale = (float)availableWidth / item.imageWidth;

        // 限制最大显示高度为150像素
        if (scale * item.imageHeight > 150) {
            scale = 150.0f / item.imageHeight;
        }

        int displayHeight = (int)(item.imageHeight * scale);
        // 标题(25) + 图片高度 + 尺寸信息(20) + 底部边距(10)
        return 25 + displayHeight + 20 + 10;
    } else {
        // 文本或文件类型：固定高度
        return 57;
    }
}

// 计算从指定索引开始，在可视区域内能完整显示的项目数
int CalculateVisibleItemCount(int startIndex) {
    if (g_hwndListBox == NULL) return ITEMS_PER_PAGE;

    RECT rcListBox;
    GetClientRect(g_hwndListBox, &rcListBox);
    int visibleHeight = rcListBox.bottom - rcListBox.top;

    int count = 0;
    int totalItems = (int)g_displayIndexMap.size();

    // 使用 LB_GETITEMRECT 获取每个项目的实际矩形
    for (int i = startIndex; i < totalItems && i < startIndex + 9; i++) {
        RECT rcItem;
        if (SendMessageW(g_hwndListBox, LB_GETITEMRECT, i, (LPARAM)&rcItem) != LB_ERR) {
            // 检查项目底部是否超出可视区域
            if (rcItem.bottom > visibleHeight) {
                break;  // 这个项目无法完整显示，停止计数
            }
            count++;
        } else {
            break;
        }
    }

    return count > 0 ? count : 1;  // 至少返回1
}

// 计算下一页的起始索引（确保当前页最后一个不完整显示的项目成为下一页第一个）
int CalculateNextPageIndex(int currentTopIndex) {
    if (g_hwndListBox == NULL) return currentTopIndex + ITEMS_PER_PAGE;

    RECT rcListBox;
    GetClientRect(g_hwndListBox, &rcListBox);
    int visibleHeight = rcListBox.bottom - rcListBox.top;

    int totalItems = (int)g_displayIndexMap.size();

    // 使用 LB_GETITEMRECT 获取每个项目的实际矩形
    for (int i = currentTopIndex; i < totalItems; i++) {
        RECT rcItem;
        if (SendMessageW(g_hwndListBox, LB_GETITEMRECT, i, (LPARAM)&rcItem) != LB_ERR) {
            // 检查项目底部是否超出可视区域
            if (rcItem.bottom > visibleHeight) {
                // 这个项目无法完整显示，它应该成为下一页的第一个
                return i;
            }
        }
    }

    // 所有项目都能显示，返回最大索引
    return totalItems;
}

// 计算上一页的起始索引（向上翻一页可视高度的内容）
int CalculatePrevPageIndex(int currentTopIndex) {
    if (g_hwndListBox == NULL || currentTopIndex <= 0) return 0;

    RECT rcListBox;
    GetClientRect(g_hwndListBox, &rcListBox);
    int visibleHeight = rcListBox.bottom - rcListBox.top;

    int totalHeight = 0;

    // 从当前顶部向上计算，找到能填满一页的起始位置
    for (int i = currentTopIndex - 1; i >= 0; i--) {
        int itemHeight = GetItemDisplayHeight(i);
        if (totalHeight + itemHeight > visibleHeight) {
            // 这个项目加上去会超出可视区域，返回下一个索引
            return i + 1;
        }
        totalHeight += itemHeight;
    }

    // 到达列表开头
    return 0;
}

// ==================== 中转站核心功能函数 ====================

// 列表框子类化窗口过程 - 处理展开/收起按钮点击和自绘滚动条
LRESULT CALLBACK ListBoxProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static int s_lastScrollPos = -1;  // 记录上次滚动位置

    // 处理背景擦除 - 用正确的颜色填充空白区域
    if (message == WM_ERASEBKGND) {
        HDC hdc = (HDC)wParam;
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        HBRUSH hBrush = CreateSolidBrush(GetWhiteColor());
        FillRect(hdc, &rcClient, hBrush);
        DeleteObject(hBrush);
        return 1;  // 表示已处理
    }

    // 处理鼠标滚轮 - 在边界时阻止消息传递以避免闪烁
    if (message == WM_MOUSEWHEEL) {
        SCROLLINFO si = {};
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);

        // 如果内容不足以滚动，直接阻止消息传递
        if (si.nMax == 0 || si.nPage > (UINT)si.nMax) {
            return 0;  // 阻止消息传递，避免闪烁
        }

        // 检查是否需要滚动
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int maxPos = si.nMax - si.nPage + 1;

        // 如果在顶部向上滚动，或在底部向下滚动，阻止消息传递
        if ((si.nPos <= 0 && delta > 0) || (si.nPos >= maxPos && delta < 0)) {
            return 0;  // 阻止消息传递，避免闪烁
        }

        // 正常滚动时更新滚动条和快捷键提示状态
        g_scrollbarVisible = true;
        g_quickPasteHintVisible = true;
        KillTimer(hwnd, ID_SCROLLBAR_HIDE_TIMER);
        SetTimer(hwnd, ID_SCROLLBAR_HIDE_TIMER, 800, NULL);

        // 自定义滚轮滚动：每次只滚动1个项目（对图像更友好）
        int currentTop = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
        int newTop = currentTop;
        if (delta > 0) {
            // 向上滚动
            newTop = currentTop - 1;
            if (newTop < 0) newTop = 0;
        } else {
            // 向下滚动
            newTop = currentTop + 1;
            int itemCount = (int)SendMessageW(hwnd, LB_GETCOUNT, 0, 0);
            if (newTop >= itemCount) newTop = itemCount - 1;
        }

        // 设置新的顶部索引
        if (newTop != currentTop) {
            SendMessageW(hwnd, LB_SETTOPINDEX, newTop, 0);
            g_listBoxTopIndex = newTop;
        }

        // 同步更新页码
        int newPage = g_listBoxTopIndex / ITEMS_PER_PAGE;
        if (newPage != g_currentPage) {
            g_currentPage = newPage;
        }
        // 始终更新翻页按钮状态（因为禁用状态依赖于 g_listBoxTopIndex）
        InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
        InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
        InvalidateRect(hwnd, NULL, FALSE);  // 刷新列表以更新快捷键提示
        return 0;  // 已处理，不再传递给默认处理
    }

    // 处理平滑滚动定时器
    if (message == WM_TIMER && wParam == ID_SMOOTH_SCROLL_TIMER) {
        if (g_smoothScrollActive) {
            // 使用缓动函数实现平滑效果
            float diff = g_smoothScrollTarget - g_smoothScrollCurrent;
            float step = diff * 0.25f;  // 缓动系数

            // 如果差值很小，直接到达目标
            if (fabs(diff) < 0.5f) {
                g_smoothScrollCurrent = g_smoothScrollTarget;
                g_smoothScrollActive = false;
                KillTimer(hwnd, ID_SMOOTH_SCROLL_TIMER);
                // 最终位置设置
                int finalPos = (int)(g_smoothScrollTarget + 0.5f);
                SendMessageW(hwnd, LB_SETTOPINDEX, finalPos, 0);
                g_listBoxTopIndex = finalPos;
            } else {
                g_smoothScrollCurrent += step;
                // 设置滚动位置
                int newPos = (int)(g_smoothScrollCurrent + 0.5f);
                SendMessageW(hwnd, LB_SETTOPINDEX, newPos, 0);
                g_listBoxTopIndex = newPos;
            }

            // 更新翻页按钮状态
            InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
            InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    // 处理垂直滚动
    if (message == WM_VSCROLL) {
        SCROLLINFO si = {};
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);

        if (si.nMax > 0 && si.nPage <= (UINT)si.nMax) {
            if (s_lastScrollPos != si.nPos) {
                s_lastScrollPos = si.nPos;
                g_scrollbarVisible = true;
                g_quickPasteHintVisible = true;
                KillTimer(hwnd, ID_SCROLLBAR_HIDE_TIMER);
                SetTimer(hwnd, ID_SCROLLBAR_HIDE_TIMER, 800, NULL);
                RedrawWindow(hwnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
            }
        }

        // 垂直滚动后更新顶部索引缓存
        LRESULT result = CallWindowProcW(g_oldListBoxProc, hwnd, message, wParam, lParam);
        g_listBoxTopIndex = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
        if (g_listBoxTopIndex < 0) g_listBoxTopIndex = 0;
        // 同步更新页码
        int newPage = g_listBoxTopIndex / ITEMS_PER_PAGE;
        if (newPage != g_currentPage) {
            g_currentPage = newPage;
        }
        // 始终更新翻页按钮状态（因为禁用状态依赖于 g_listBoxTopIndex）
        InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
        InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
        return result;
    }

    // 定时器触发时隐藏滚动条和快捷键提示
    if (message == WM_TIMER && wParam == ID_SCROLLBAR_HIDE_TIMER) {
        KillTimer(hwnd, ID_SCROLLBAR_HIDE_TIMER);
        g_scrollbarVisible = false;
        g_quickPasteHintVisible = false;
        RedrawWindow(hwnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
        return 0;
    }

    // 自绘滚动条 - 用白色覆盖原生滚动条，只画滑块
    if (message == WM_NCPAINT) {
        // 先检查是否需要滚动
        SCROLLINFO si = {};
        si.cbSize = sizeof(SCROLLINFO);        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);

        // 如果不需要滚动（内容不足以滚动），直接返回，避免闪烁
        if (si.nMax == 0 || si.nPage > (UINT)si.nMax) {
            return CallWindowProcW(g_oldListBoxProc, hwnd, message, wParam, lParam);
        }

        // 先让系统绘制
        CallWindowProcW(g_oldListBoxProc, hwnd, message, wParam, lParam);

        HDC hdc = GetWindowDC(hwnd);
        if (hdc) {
            RECT rcWnd;
            GetWindowRect(hwnd, &rcWnd);
            int width = rcWnd.right - rcWnd.left;
            int height = rcWnd.bottom - rcWnd.top;
            int scrollbarWidth = GetSystemMetrics(SM_CXVSCROLL);

            // 用背景色覆盖整个滚动条区域（支持暗黑模式）
            RECT rcScrollbar = {width - scrollbarWidth, 0, width, height};
            HBRUSH hBrush = CreateSolidBrush(GetWhiteColor());
            FillRect(hdc, &rcScrollbar, hBrush);
            DeleteObject(hBrush);

            // 绘制滑块
            if (g_scrollbarVisible && si.nMax > 0) {
                int trackHeight = height;
                int thumbHeight = (si.nPage * trackHeight) / (si.nMax + 1);
                if (thumbHeight < 30) thumbHeight = 30;

                int thumbY = 0;
                if (si.nMax - si.nPage + 1 > 0) {
                    thumbY = (si.nPos * (trackHeight - thumbHeight)) / (si.nMax - si.nPage + 1);
                }

                // 绘制灰色滑块
                RECT rcThumb = {width - 6, thumbY + 2, width - 2, thumbY + thumbHeight - 2};
                HBRUSH hThumbBrush = CreateSolidBrush(RGB(180, 180, 180));
                FillRect(hdc, &rcThumb, hThumbBrush);
                DeleteObject(hThumbBrush);
            }

            ReleaseDC(hwnd, hdc);
        }
        return 0;
    }

    // 处理光标设置，防止手指光标闪烁
    if (message == WM_SETCURSOR) {
        if (g_isHoveringLink || g_isHoveringFolder || g_isHoveringIcon) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
    }

    // 鼠标移动时检测是否悬浮在图标上，并更新 Tooltip
    if (message == WM_MOUSEMOVE) {
        POINT pt;
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);

        // 启用鼠标追踪以接收 WM_MOUSELEAVE
        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(TRACKMOUSEEVENT);        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);

        // 获取鼠标所在的列表项索引
        int index = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
        bool wasHoveringIcon = g_isHoveringIcon;
        int oldHoverIndex = g_hoverIconIndex;
        bool wasHoveringFolder = g_isHoveringFolder;
        int oldFolderHoverIndex = g_hoverFolderIndex;
        bool wasHoveringLink = g_isHoveringLink;
        int oldLinkHoverIndex = g_hoverLinkIndex;

        // 重置文件夹悬浮状态
        g_isHoveringFolder = false;
        g_hoverFolderIndex = -1;
        // 重置图像悬浮状态
        g_isHoveringImage = false;
        g_hoverImageIndex = -1;
        // 重置链接悬浮状态
        g_isHoveringLink = false;
        g_hoverLinkIndex = -1;

        if (HIWORD(index) == 0) {
            index = LOWORD(index);

            bool iconFound = false;
            if (index >= 0 && index < (int)g_displayIndexMap.size()) {
                int actualIndex = g_displayIndexMap[index];

                if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
                    const ClipboardItem& item = g_history[actualIndex];

                    // 获取列表项的矩形
                    RECT rcItem;
                    SendMessageW(hwnd, LB_GETITEMRECT, index, (LPARAM)&rcItem);

                    // 计算图标区域（需要与绘制代码保持一致）
                    // 时间文本宽度需要计算
                    HDC hdc = GetDC(hwnd);
                    HFONT hHeaderFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                    HFONT hOldFont = (HFONT)SelectObject(hdc, hHeaderFont);

                    std::wstring headerText = GetRelativeTimeString(item.timestamp) + L" -";
                    SIZE textSize;
                    GetTextExtentPoint32W(hdc, headerText.c_str(), (int)headerText.length(), &textSize);

                    SelectObject(hdc, hOldFont);
                    DeleteObject(hHeaderFont);
                    ReleaseDC(hwnd, hdc);

                    // 图标区域
                    int iconX = rcItem.left + 10 + textSize.cx + 4;
                    int iconY = rcItem.top + 2 + 2;
                    int iconSize = 12;
                    RECT rcIcon = {iconX, iconY, iconX + iconSize, iconY + iconSize};

                    // 检查鼠标是否在图标区域内
                    if (PtInRect(&rcIcon, pt) && !item.sourceAppPath.empty()) {
                        iconFound = true;
                        g_isHoveringIcon = true;
                        g_hoverIconIndex = index;

                        // 显示 Tooltip（显示应用名）
                        if (g_hwndListBoxTooltip != NULL && !item.sourceApp.empty()) {
                            // 先更新文本以获取 tooltip 大小
                            TOOLINFOW ti = {};
                            ti.cbSize = TTTOOLINFOW_V1_SIZE;
                            ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
                            ti.hwnd = g_hwndListBox;
                            ti.uId = 0;
                            ti.lpszText = (LPWSTR)item.sourceApp.c_str();
                            SendMessageW(g_hwndListBoxTooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);

                            // 获取鼠标当前屏幕位置
                            POINT ptMouse;
                            GetCursorPos(&ptMouse);

                            // 设置 tooltip 位置（在鼠标上方）
                            POINT ptScreen = {ptMouse.x - 20, ptMouse.y - 25};
                            SendMessageW(g_hwndListBoxTooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(ptScreen.x, ptScreen.y));

                            // 激活 tooltip
                            SendMessageW(g_hwndListBoxTooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
                        }
                    }

                    // 检查是否悬浮在文件夹名称区域
                    if (!iconFound && item.type == TYPE_FILE) {
                        DWORD attrs = GetFileAttributesW(item.content.c_str());
                        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                            // 这是一个文件夹，检查鼠标是否在文件夹名称文字区域（不包括图标）
                            // 文件夹名称区域：标题下方，左边距10 + 图标22，高度22
                            RECT rcFolderName;
                            rcFolderName.left = rcItem.left + 10 + 22;  // 左边距 + 文件夹图标宽度
                            rcFolderName.top = rcItem.top + 2 + 20;     // 顶部边距 + 标题高度
                            rcFolderName.right = rcItem.right - 10;     // 右边距
                            rcFolderName.bottom = rcFolderName.top + 22;

                            // 计算文字实际宽度
                            HDC hdc = GetDC(hwnd);
                            HFONT hFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
                            SIZE textSize;
                            GetTextExtentPoint32W(hdc, item.content.c_str(), (int)item.content.length(), &textSize);
                            SelectObject(hdc, hOldFont);
                            DeleteObject(hFont);
                            ReleaseDC(hwnd, hdc);

                            // 限制文字区域宽度为实际文字宽度
                            int maxTextWidth = rcFolderName.right - rcFolderName.left;
                            int actualTextWidth = std::min((int)textSize.cx, maxTextWidth);
                            rcFolderName.right = rcFolderName.left + actualTextWidth;

                            if (PtInRect(&rcFolderName, pt)) {
                                g_isHoveringFolder = true;
                                g_hoverFolderIndex = index;
                                // 设置手型光标
                                SetCursor(LoadCursor(NULL, IDC_HAND));
                            }
                        }
                    }

                    // 检查是否悬浮在链接文本上（URL 或本地盘符路径）（批量编辑模式下禁用）
                    if (!g_isBatchEditMode && !iconFound && !g_isHoveringFolder && (item.type == TYPE_TEXT || item.type == TYPE_FILE)) {
                        // 获取文本内容
                        std::wstring contentText = item.content;
                        // 替换换行符为空格
                        for (size_t ci = 0; ci < contentText.length(); ci++) {
                            if (contentText[ci] == L'\r' || contentText[ci] == L'\n') {
                                contentText[ci] = L' ';
                            }
                        }

                        if (IsLinkText(contentText)) {
                            // 文本区域：标题下方
                            RECT rcLinkText;
                            rcLinkText.left = rcItem.left + 10;
                            rcLinkText.top = rcItem.top + 2 + 20;     // 顶部边距 + 标题高度
                            rcLinkText.right = rcItem.right - 10;
                            rcLinkText.bottom = rcLinkText.top + 22;

                            // 计算文字实际宽度
                            HDC hdc = GetDC(hwnd);
                            HFONT hFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
                            SIZE textSize;
                            GetTextExtentPoint32W(hdc, contentText.c_str(), (int)contentText.length(), &textSize);
                            SelectObject(hdc, hOldFont);
                            DeleteObject(hFont);
                            ReleaseDC(hwnd, hdc);

                            // 限制文字区域宽度为实际文字宽度
                            int maxTextWidth = rcLinkText.right - rcLinkText.left;
                            int actualTextWidth = std::min((int)textSize.cx, maxTextWidth);
                            rcLinkText.right = rcLinkText.left + actualTextWidth;

                            if (PtInRect(&rcLinkText, pt)) {
                                g_isHoveringLink = true;
                                g_hoverLinkIndex = index;
                                SetCursor(LoadCursor(NULL, IDC_HAND));
                            }
                        }
                    }

                    // 检查是否悬浮在图像区域（图片文件或截图）
                    if (!iconFound && item.type == TYPE_IMAGE && !item.imageData.empty()) {
                        // 构建图片路径
                        std::wstring imagePath;
                        bool hasValidPath = false;

                        if (!item.imageFilePath.empty()) {
                            // 图片文件类型
                            imagePath = item.imageFilePath;
                            DWORD attrs = GetFileAttributesW(imagePath.c_str());
                            hasValidPath = (attrs != INVALID_FILE_ATTRIBUTES);
                        } else if (!item.imageFileName.empty()) {
                            // 截图类型：构建完整路径
                            imagePath = GetImagesPath() + L"\\" + item.imageFileName;
                            DWORD attrs = GetFileAttributesW(imagePath.c_str());
                            hasValidPath = (attrs != INVALID_FILE_ATTRIBUTES);
                        }

                        if (hasValidPath) {
                            // 计算图像实际显示区域（与绘制代码保持一致）
                            RECT rcContent = rcItem;
                            rcContent.left += 10;
                            rcContent.right -= 10;
                            rcContent.top += 2 + 20;  // 顶部边距 + 标题高度

                            int availableWidth = rcContent.right - rcContent.left;
                            int availableHeight = rcItem.bottom - rcContent.top - 10;

                            int srcWidth = item.thumbWidth > 0 ? item.thumbWidth : item.imageWidth;
                            int srcHeight = item.thumbHeight > 0 ? item.thumbHeight : item.imageHeight;

                            if (srcWidth > 0 && srcHeight > 0) {
                                float scaleX = (float)availableWidth / srcWidth;
                                float scaleY = (float)availableHeight / srcHeight;
                                float scale = (scaleX < scaleY ? scaleX : scaleY);

                                if (scale * srcHeight > 150) {
                                    scale = 150.0f / srcHeight;
                                }

                                int displayWidth = (int)(srcWidth * scale);
                                int displayHeight = (int)(srcHeight * scale);

                                // 居中显示的图像区域
                                int imgX = rcContent.left + (availableWidth - displayWidth) / 2;
                                int imgY = rcContent.top;

                                RECT rcImage;
                                rcImage.left = imgX;
                                rcImage.top = imgY;
                                rcImage.right = imgX + displayWidth;
                                rcImage.bottom = imgY + displayHeight;

                                if (PtInRect(&rcImage, pt)) {
                                    g_isHoveringImage = true;
                                    g_hoverImageIndex = index;

                                    // 显示 Tooltip（显示图片文件路径）
                                    if (g_hwndListBoxTooltip != NULL) {
                                        // 使用静态变量保存路径，避免临时变量被销毁
                                        static std::wstring s_tooltipImagePath;
                                        s_tooltipImagePath = imagePath;

                                        TOOLINFOW ti = {};
                                        ti.cbSize = TTTOOLINFOW_V1_SIZE;
                                        ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
                                        ti.hwnd = g_hwndListBox;
                                        ti.uId = 0;
                                        ti.lpszText = (LPWSTR)s_tooltipImagePath.c_str();
                                        SendMessageW(g_hwndListBoxTooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);

                                        // 获取鼠标当前屏幕位置
                                        POINT ptMouse;
                                        GetCursorPos(&ptMouse);

                                        // 设置 tooltip 位置（在鼠标上方）
                                        POINT ptScreen = {ptMouse.x - 20, ptMouse.y - 25};
                                        SendMessageW(g_hwndListBoxTooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(ptScreen.x, ptScreen.y));

                                        // 激活 tooltip
                                        SendMessageW(g_hwndListBoxTooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 如果没有找到图标悬浮且没有图像悬浮，隐藏 tooltip
            if (!iconFound && !g_isHoveringImage) {
                g_isHoveringIcon = false;
                g_hoverIconIndex = -1;

                if (g_hwndListBoxTooltip != NULL) {
                    TOOLINFOW ti = {};
                    ti.cbSize = TTTOOLINFOW_V1_SIZE;
                    ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
                    ti.hwnd = g_hwndListBox;
                    ti.uId = 0;
                    SendMessageW(g_hwndListBoxTooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
                }
            }
        } else {
            g_isHoveringIcon = false;
            g_hoverIconIndex = -1;

            // 隐藏 tooltip
            if (g_hwndListBoxTooltip != NULL) {
                TOOLINFOW ti = {};
                ti.cbSize = TTTOOLINFOW_V1_SIZE;
                ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
                ti.hwnd = g_hwndListBox;
                ti.uId = 0;
                SendMessageW(g_hwndListBoxTooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
            }
        }

        // 如果悬浮状态变化，重绘相关项目
        if (wasHoveringIcon != g_isHoveringIcon || oldHoverIndex != g_hoverIconIndex) {
            if (oldHoverIndex >= 0) {
                RECT rcOld;
                SendMessageW(hwnd, LB_GETITEMRECT, oldHoverIndex, (LPARAM)&rcOld);
                InvalidateRect(hwnd, &rcOld, FALSE);
            }
            if (g_hoverIconIndex >= 0 && g_hoverIconIndex != oldHoverIndex) {
                RECT rcNew;
                SendMessageW(hwnd, LB_GETITEMRECT, g_hoverIconIndex, (LPARAM)&rcNew);
                InvalidateRect(hwnd, &rcNew, FALSE);
            }
        }

        // 如果文件夹悬浮状态变化，重绘相关项目并启动动画
        if (wasHoveringFolder != g_isHoveringFolder || oldFolderHoverIndex != g_hoverFolderIndex) {
            // 启动收起动画（鼠标离开文件夹）- 先处理收起
            if (wasHoveringFolder && oldFolderHoverIndex >= 0 && oldFolderHoverIndex != g_hoverFolderIndex) {
                g_folderCollapseAnimating = true;
                g_folderCollapseAnimIndex = oldFolderHoverIndex;
                g_folderCollapseProgress = 1.0f;
                SetTimer(hwnd, ID_FOLDER_COLLAPSE_TIMER, 16, NULL);
            }
            // 启动展开动画（鼠标进入新文件夹）
            if (g_isHoveringFolder && g_hoverFolderIndex >= 0) {
                g_folderUnderlineAnimating = true;
                g_folderUnderlineAnimIndex = g_hoverFolderIndex;
                g_folderUnderlineProgress = 0.0f;
                SetTimer(hwnd, ID_FOLDER_UNDERLINE_TIMER, 16, NULL);
            }

            if (oldFolderHoverIndex >= 0) {
                RECT rcOld;
                SendMessageW(hwnd, LB_GETITEMRECT, oldFolderHoverIndex, (LPARAM)&rcOld);
                InvalidateRect(hwnd, &rcOld, FALSE);
            }
            if (g_hoverFolderIndex >= 0 && g_hoverFolderIndex != oldFolderHoverIndex) {
                RECT rcNew;
                SendMessageW(hwnd, LB_GETITEMRECT, g_hoverFolderIndex, (LPARAM)&rcNew);
                InvalidateRect(hwnd, &rcNew, FALSE);
            }
        }

        // 如果链接悬浮状态变化，启动颜色动画
        if (wasHoveringLink != g_isHoveringLink || oldLinkHoverIndex != g_hoverLinkIndex) {
            // 启动收起动画（鼠标离开链接文本）
            if (wasHoveringLink && oldLinkHoverIndex >= 0 && oldLinkHoverIndex != g_hoverLinkIndex) {
                // 如果之前有另一个收起动画正在进行，先强制完成它
                if (g_linkColorCollapseAnimating && g_linkColorCollapseAnimIndex >= 0 &&
                    g_linkColorCollapseAnimIndex != oldLinkHoverIndex) {
                    int prevIndex = g_linkColorCollapseAnimIndex;
                    g_linkColorCollapseAnimating = false;
                    g_linkColorCollapseAnimIndex = -1;
                    g_linkColorCollapseProgress = 0.0f;
                    // 强制重绘之前收起动画的项目，确保恢复原色
                    RECT rcPrev;
                    SendMessageW(hwnd, LB_GETITEMRECT, prevIndex, (LPARAM)&rcPrev);
                    InvalidateRect(hwnd, &rcPrev, FALSE);
                }
                g_linkColorCollapseAnimating = true;
                g_linkColorCollapseAnimIndex = oldLinkHoverIndex;
                g_linkColorCollapseProgress = 1.0f;
                SetTimer(hwnd, ID_LINK_COLOR_COLLAPSE_TIMER, 16, NULL);
            }
            // 启动展开动画（鼠标进入链接文本）
            if (g_isHoveringLink && g_hoverLinkIndex >= 0) {
                // 如果之前有另一个展开动画正在进行，先强制完成它
                if (g_linkColorExpandAnimating && g_linkColorExpandAnimIndex >= 0 &&
                    g_linkColorExpandAnimIndex != g_hoverLinkIndex) {
                    int prevIndex = g_linkColorExpandAnimIndex;
                    g_linkColorExpandAnimating = false;
                    g_linkColorExpandAnimIndex = -1;
                    g_linkColorExpandProgress = 0.0f;
                    RECT rcPrev;
                    SendMessageW(hwnd, LB_GETITEMRECT, prevIndex, (LPARAM)&rcPrev);
                    InvalidateRect(hwnd, &rcPrev, FALSE);
                }
                g_linkColorExpandAnimating = true;
                g_linkColorExpandAnimIndex = g_hoverLinkIndex;
                g_linkColorExpandProgress = 0.0f;
                SetTimer(hwnd, ID_LINK_COLOR_EXPAND_TIMER, 16, NULL);
            }

            if (oldLinkHoverIndex >= 0) {
                RECT rcOld;
                SendMessageW(hwnd, LB_GETITEMRECT, oldLinkHoverIndex, (LPARAM)&rcOld);
                InvalidateRect(hwnd, &rcOld, FALSE);
            }
            if (g_hoverLinkIndex >= 0 && g_hoverLinkIndex != oldLinkHoverIndex) {
                RECT rcNew;
                SendMessageW(hwnd, LB_GETITEMRECT, g_hoverLinkIndex, (LPARAM)&rcNew);
                InvalidateRect(hwnd, &rcNew, FALSE);
            }
        }
    }

    // 鼠标离开时隐藏 Tooltip 并重置悬浮状态
    if (message == WM_MOUSELEAVE) {
        int oldHoverIndex = g_hoverIconIndex;
        int oldFolderHoverIndex = g_hoverFolderIndex;
        int oldLinkHoverIndex = g_hoverLinkIndex;
        g_lastTooltipIndex = -1;
        g_isHoveringIcon = false;
        g_hoverIconIndex = -1;
        g_isHoveringFolder = false;
        g_hoverFolderIndex = -1;
        g_isHoveringLink = false;
        g_hoverLinkIndex = -1;

        // 启动收起动画（鼠标离开列表框）
        if (oldFolderHoverIndex >= 0) {
            g_folderCollapseAnimating = true;
            g_folderCollapseAnimIndex = oldFolderHoverIndex;
            g_folderCollapseProgress = 1.0f;
            SetTimer(hwnd, ID_FOLDER_COLLAPSE_TIMER, 16, NULL);
        }

        // 启动链接颜色收起动画（鼠标离开列表框）
        if (oldLinkHoverIndex >= 0) {
            // 如果之前有另一个收起动画正在进行，先强制完成它
            if (g_linkColorCollapseAnimating && g_linkColorCollapseAnimIndex >= 0 &&
                g_linkColorCollapseAnimIndex != oldLinkHoverIndex) {
                int prevIndex = g_linkColorCollapseAnimIndex;
                g_linkColorCollapseAnimating = false;
                g_linkColorCollapseAnimIndex = -1;
                g_linkColorCollapseProgress = 0.0f;
                RECT rcPrev;
                SendMessageW(hwnd, LB_GETITEMRECT, prevIndex, (LPARAM)&rcPrev);
                InvalidateRect(hwnd, &rcPrev, FALSE);
            }
            g_linkColorCollapseAnimating = true;
            g_linkColorCollapseAnimIndex = oldLinkHoverIndex;
            g_linkColorCollapseProgress = 1.0f;
            SetTimer(hwnd, ID_LINK_COLOR_COLLAPSE_TIMER, 16, NULL);
        }

        if (g_hwndListBoxTooltip != NULL) {
            TOOLINFOW ti = {};
            ti.cbSize = TTTOOLINFOW_V1_SIZE;
            ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            ti.hwnd = g_hwndListBox;
            ti.uId = 0;
            SendMessageW(g_hwndListBoxTooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
        }
        // 重绘之前悬浮的项目
        if (oldHoverIndex >= 0) {
            RECT rcOld;
            SendMessageW(hwnd, LB_GETITEMRECT, oldHoverIndex, (LPARAM)&rcOld);
            InvalidateRect(hwnd, &rcOld, FALSE);
        }
        // 重绘之前悬浮的文件夹项目
        if (oldFolderHoverIndex >= 0 && oldFolderHoverIndex != oldHoverIndex) {
            RECT rcOld;
            SendMessageW(hwnd, LB_GETITEMRECT, oldFolderHoverIndex, (LPARAM)&rcOld);
            InvalidateRect(hwnd, &rcOld, FALSE);
        }
        // 重绘之前悬浮的链接项目
        if (oldLinkHoverIndex >= 0 && oldLinkHoverIndex != oldHoverIndex && oldLinkHoverIndex != oldFolderHoverIndex) {
            RECT rcOld;
            SendMessageW(hwnd, LB_GETITEMRECT, oldLinkHoverIndex, (LPARAM)&rcOld);
            InvalidateRect(hwnd, &rcOld, FALSE);
        }
    }

    // 处理鼠标按下 - 记录拖拽起始点
    if (message == WM_LBUTTONDOWN) {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        int index = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
        if (HIWORD(index) == 0) {
            index = LOWORD(index);
            if (index >= 0 && index < (int)g_displayIndexMap.size()) {
                int actualIndex = g_displayIndexMap[index];
                if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
                    const ClipboardItem& item = g_history[actualIndex];
                    // 文件类型（非文件夹）支持拖拽
                    if (item.type == TYPE_FILE) {
                        DWORD attrs = GetFileAttributesW(item.content.c_str());
                        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                            // 普通文件，支持拖拽
                            g_dragStartPoint = pt;
                            g_dragItemIndex = index;
                        }
                    }
                    // 图像类型支持拖拽
                    else if (item.type == TYPE_IMAGE) {
                        // 检查是否有有效的图片路径
                        std::wstring imagePath;
                        if (!item.imageFilePath.empty()) {
                            imagePath = item.imageFilePath;
                        } else if (!item.imageFileName.empty()) {
                            imagePath = GetImagesPath() + L"\\" + item.imageFileName;
                        }
                        if (!imagePath.empty()) {
                            DWORD attrs = GetFileAttributesW(imagePath.c_str());
                            if (attrs != INVALID_FILE_ATTRIBUTES) {
                                g_dragStartPoint = pt;
                                g_dragItemIndex = index;
                            }
                        }
                    }
                }
            }
        }
    }

    // 处理鼠标移动 - 检测拖拽
    if (message == WM_MOUSEMOVE && (wParam & MK_LBUTTON) && g_dragItemIndex >= 0) {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        // 检查是否超过拖拽阈值
        int dx = pt.x - g_dragStartPoint.x;
        int dy = pt.y - g_dragStartPoint.y;
        if (dx * dx + dy * dy > DRAG_THRESHOLD * DRAG_THRESHOLD) {
            // 开始拖拽
            if (g_dragItemIndex < (int)g_displayIndexMap.size()) {
                int actualIndex = g_displayIndexMap[g_dragItemIndex];
                if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
                    const ClipboardItem& item = g_history[actualIndex];
                    std::wstring dragFilePath;

                    // 获取拖拽文件路径
                    if (item.type == TYPE_FILE) {
                        DWORD attrs = GetFileAttributesW(item.content.c_str());
                        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                            dragFilePath = item.content;
                        }
                    } else if (item.type == TYPE_IMAGE) {
                        if (!item.imageFilePath.empty()) {
                            dragFilePath = item.imageFilePath;
                        } else if (!item.imageFileName.empty()) {
                            dragFilePath = GetImagesPath() + L"\\" + item.imageFileName;
                        }
                    }

                    if (!dragFilePath.empty()) {
                        // 创建数据对象
                        IDataObject* pDataObject = CreateFileDataObject(dragFilePath);
                        if (pDataObject) {
                            // 设置拖放图像（显示文件图标和文件名）
                            SetDragImage(pDataObject, dragFilePath, pt);

                            // 创建拖放源
                            CDropSource* pDropSource = new CDropSource();

                            // 执行拖放
                            DWORD dwEffect = 0;
                            DoDragDrop(pDataObject, pDropSource, DROPEFFECT_COPY | DROPEFFECT_MOVE, &dwEffect);

                            pDropSource->Release();
                            pDataObject->Release();
                        }

                        // 重置拖拽状态
                        g_dragItemIndex = -1;
                        return 0;
                    }
                }
            }
        }
    }

    // 处理鼠标释放 - 重置拖拽状态
    if (message == WM_LBUTTONUP) {
        g_dragItemIndex = -1;

        if (g_isHoveringFolder && g_hoverFolderIndex >= 0) {
            // 获取文件夹路径
            if (g_hoverFolderIndex < (int)g_displayIndexMap.size()) {
                int actualIndex = g_displayIndexMap[g_hoverFolderIndex];
                if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
                    const ClipboardItem& item = g_history[actualIndex];
                    if (item.type == TYPE_FILE) {
                        // 打开资源管理器并定位到该文件夹
                        ShellExecuteW(NULL, L"explore", item.content.c_str(), NULL, NULL, SW_SHOWNORMAL);
                        return 0;
                    }
                }
            }
        }

        // 链接文本点击处理（批量编辑模式下禁用）
        if (!g_isBatchEditMode && g_isHoveringLink && g_hoverLinkIndex >= 0) {
            if (g_hoverLinkIndex < (int)g_displayIndexMap.size()) {
                int actualIndex = g_displayIndexMap[g_hoverLinkIndex];
                if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
                    const ClipboardItem& item = g_history[actualIndex];
                    std::wstring contentText = item.content;
                    // 替换换行符为空格
                    for (size_t ci = 0; ci < contentText.length(); ci++) {
                        if (contentText[ci] == L'\r' || contentText[ci] == L'\n') {
                            contentText[ci] = L' ';
                        }
                    }
                    // 截取第一个空格前的部分作为链接
                    size_t spacePos = contentText.find(L' ');
                    if (spacePos != std::wstring::npos) {
                        contentText = contentText.substr(0, spacePos);
                    }

                    LinkType linkType = GetLinkType(contentText);
                    if (linkType == LINK_FILE_PATH) {
                        // 本地路径：用资源管理器打开
                        DWORD attrs = GetFileAttributesW(contentText.c_str());
                        if (attrs != INVALID_FILE_ATTRIBUTES) {
                            if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
                                // 文件夹：直接打开
                                ShellExecuteW(NULL, L"explore", contentText.c_str(), NULL, NULL, SW_SHOWNORMAL);
                            } else {
                                // 文件：在资源管理器中选中
                                std::wstring param = L"/select,\"" + contentText + L"\"";
                                ShellExecuteW(NULL, NULL, L"explorer.exe", param.c_str(), NULL, SW_SHOWNORMAL);
                            }
                        }
                        return 0;
                    } else if (linkType == LINK_URL) {
                        // 网址：用默认浏览器打开
                        std::wstring url = contentText;
                        // 如果以 www. 开头但没有协议，补上 https://
                        if (_wcsnicmp(url.c_str(), L"www.", 4) == 0) {
                            url = L"https://" + url;
                        }
                        ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
                        return 0;
                    } else if (linkType == LINK_IP) {
                        // IP 地址：调用 cmd 进行 ping
                        std::wstring cmd = L"/c ping " + contentText + L" & pause";
                        ShellExecuteW(NULL, L"open", L"cmd.exe", cmd.c_str(), NULL, SW_SHOWNORMAL);
                        return 0;
                    }
                }
            }
        }
    }

    // 处理下划线动画定时器
    // 处理展开动画定时器
    if (message == WM_TIMER && wParam == ID_FOLDER_UNDERLINE_TIMER) {
        if (g_folderUnderlineAnimating) {
            float step = 0.1f; // 动画步进
            g_folderUnderlineProgress += step;
            if (g_folderUnderlineProgress >= 1.0f) {
                g_folderUnderlineProgress = 1.0f;
                g_folderUnderlineAnimating = false;
                KillTimer(hwnd, ID_FOLDER_UNDERLINE_TIMER);
            }
            // 只重绘下划线区域（项目底部的一小条）
            if (g_folderUnderlineAnimIndex >= 0) {
                RECT rcAnim;
                SendMessageW(hwnd, LB_GETITEMRECT, g_folderUnderlineAnimIndex, (LPARAM)&rcAnim);
                // 只重绘下划线所在区域（标题下方的文本行）
                rcAnim.top = rcAnim.top + 22; // 标题高度
                rcAnim.bottom = rcAnim.top + 25; // 只重绘文本行区域
                InvalidateRect(hwnd, &rcAnim, FALSE);
            }
        }
        return 0;
    }

    // 处理收起动画定时器
    if (message == WM_TIMER && wParam == ID_FOLDER_COLLAPSE_TIMER) {
        if (g_folderCollapseAnimating) {
            float step = 0.1f; // 动画步进
            g_folderCollapseProgress -= step;
            int animIndex = g_folderCollapseAnimIndex; // 保存索引用于重绘
            if (g_folderCollapseProgress <= 0.0f) {
                g_folderCollapseProgress = 0.0f;
                g_folderCollapseAnimating = false;
                g_folderCollapseAnimIndex = -1;
                KillTimer(hwnd, ID_FOLDER_COLLAPSE_TIMER);
            }
            // 只重绘下划线区域
            if (animIndex >= 0) {
                RECT rcAnim;
                SendMessageW(hwnd, LB_GETITEMRECT, animIndex, (LPARAM)&rcAnim);
                // 只重绘下划线所在区域
                rcAnim.top = rcAnim.top + 22;
                rcAnim.bottom = rcAnim.top + 25;
                InvalidateRect(hwnd, &rcAnim, FALSE);
            }
        }
        return 0;
    }

    // 处理链接颜色展开动画定时器
    if (message == WM_TIMER && wParam == ID_LINK_COLOR_EXPAND_TIMER) {
        if (g_linkColorExpandAnimating) {
            float step = 0.08f; // 动画步进（稍慢，更柔和）
            g_linkColorExpandProgress += step;
            if (g_linkColorExpandProgress >= 1.0f) {
                g_linkColorExpandProgress = 1.0f;
                g_linkColorExpandAnimating = false;
                KillTimer(hwnd, ID_LINK_COLOR_EXPAND_TIMER);
            }
            if (g_linkColorExpandAnimIndex >= 0) {
                RECT rcAnim;
                SendMessageW(hwnd, LB_GETITEMRECT, g_linkColorExpandAnimIndex, (LPARAM)&rcAnim);
                rcAnim.top = rcAnim.top + 22;
                rcAnim.bottom = rcAnim.top + 25;
                InvalidateRect(hwnd, &rcAnim, FALSE);
            }
        }
        return 0;
    }

    // 处理链接颜色收起动画定时器
    if (message == WM_TIMER && wParam == ID_LINK_COLOR_COLLAPSE_TIMER) {
        if (g_linkColorCollapseAnimating) {
            float step = 0.08f;
            g_linkColorCollapseProgress -= step;
            int animIndex = g_linkColorCollapseAnimIndex;
            if (g_linkColorCollapseProgress <= 0.0f) {
                g_linkColorCollapseProgress = 0.0f;
                g_linkColorCollapseAnimating = false;
                g_linkColorCollapseAnimIndex = -1;
                KillTimer(hwnd, ID_LINK_COLOR_COLLAPSE_TIMER);
            }
            if (animIndex >= 0) {
                RECT rcAnim;
                SendMessageW(hwnd, LB_GETITEMRECT, animIndex, (LPARAM)&rcAnim);
                rcAnim.top = rcAnim.top + 22;
                rcAnim.bottom = rcAnim.top + 25;
                InvalidateRect(hwnd, &rcAnim, FALSE);
            }
        }
        return 0;
    }

    return CallWindowProcW(g_oldListBoxProc, hwnd, message, wParam, lParam);
}

// 置顶按钮子类化窗口过程 - 处理悬浮效果
LRESULT CALLBACK TopmostBtnProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_MOUSEMOVE: {
            // 设置鼠标追踪以接收 WM_MOUSELEAVE
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);

            if (!g_isTopmostBtnHover) {
                g_isTopmostBtnHover = true;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (g_isTopmostBtnHover) {
                g_isTopmostBtnHover = false;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
    }
    return CallWindowProcW(g_oldTopmostBtnProc, hwnd, message, wParam, lParam);
}

// 批量编辑按钮子类化窗口过程 - 处理悬浮效果
LRESULT CALLBACK BatchEditBtnProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);

            if (!g_isBatchEditBtnHover) {
                g_isBatchEditBtnHover = true;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (g_isBatchEditBtnHover) {
                g_isBatchEditBtnHover = false;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
    }
    return CallWindowProcW(g_oldBatchEditBtnProc, hwnd, message, wParam, lParam);
}

// 上一页按钮子类化窗口过程 - 处理悬浮效果
LRESULT CALLBACK PageUpBtnProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);

            if (!g_isPageUpBtnHover) {
                g_isPageUpBtnHover = true;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (g_isPageUpBtnHover) {
                g_isPageUpBtnHover = false;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
    }
    return CallWindowProcW(g_oldPageUpBtnProc, hwnd, message, wParam, lParam);
}

// 下一页按钮子类化窗口过程 - 处理悬浮效果
LRESULT CALLBACK PageDownBtnProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);

            if (!g_isPageDownBtnHover) {
                g_isPageDownBtnHover = true;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (g_isPageDownBtnHover) {
                g_isPageDownBtnHover = false;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
    }
    return CallWindowProcW(g_oldPageDownBtnProc, hwnd, message, wParam, lParam);
}

// 标题栏置顶按钮子类化窗口过程
LRESULT CALLBACK TitleTopmostBtnProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);

            if (!g_isTitleTopmostHover) {
                g_isTitleTopmostHover = true;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (g_isTitleTopmostHover) {
                g_isTitleTopmostHover = false;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
    }
    return CallWindowProcW(g_oldTitleTopmostProc, hwnd, message, wParam, lParam);
}

// 标题栏最小化按钮子类化窗口过程
LRESULT CALLBACK TitleMinimizeBtnProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);

            if (!g_isTitleMinimizeHover) {
                g_isTitleMinimizeHover = true;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (g_isTitleMinimizeHover) {
                g_isTitleMinimizeHover = false;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
    }
    return CallWindowProcW(g_oldTitleMinimizeProc, hwnd, message, wParam, lParam);
}

// 标题栏最大化按钮子类化窗口过程
LRESULT CALLBACK TitleMaximizeBtnProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);

            if (!g_isTitleMaximizeHover) {
                g_isTitleMaximizeHover = true;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (g_isTitleMaximizeHover) {
                g_isTitleMaximizeHover = false;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
    }
    return CallWindowProcW(g_oldTitleMaximizeProc, hwnd, message, wParam, lParam);
}

// 标题栏关闭按钮子类化窗口过程
LRESULT CALLBACK TitleCloseBtnProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);

            if (!g_isTitleCloseHover) {
                g_isTitleCloseHover = true;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (g_isTitleCloseHover) {
                g_isTitleCloseHover = false;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
    }
    return CallWindowProcW(g_oldTitleCloseProc, hwnd, message, wParam, lParam);
}

// 从资源加载PNG图片
Gdiplus::Image* LoadImageFromResource(int resourceId) {
    HMODULE hModule = GetModuleHandle(NULL);
    HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(resourceId), RT_RCDATA);
    if (!hResource) return NULL;

    DWORD imageSize = SizeofResource(hModule, hResource);
    if (imageSize == 0) return NULL;

    HGLOBAL hGlobal = LoadResource(hModule, hResource);
    if (!hGlobal) return NULL;

    void* pResourceData = LockResource(hGlobal);
    if (!pResourceData) return NULL;

    // 创建内存流
    HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, imageSize);
    if (!hBuffer) return NULL;

    void* pBuffer = GlobalLock(hBuffer);
    if (!pBuffer) {
        GlobalFree(hBuffer);
        return NULL;
    }

    memcpy(pBuffer, pResourceData, imageSize);
    GlobalUnlock(hBuffer);

    IStream* pStream = NULL;
    if (CreateStreamOnHGlobal(hBuffer, TRUE, &pStream) != S_OK) {
        GlobalFree(hBuffer);
        return NULL;
    }

    Gdiplus::Image* image = Gdiplus::Image::FromStream(pStream);
    pStream->Release();

    return image;
}

// 加载按钮图片资源（从exe资源加载）
void LoadButtonImages() {
    g_imgTopmostSelected = LoadImageFromResource(IDB_TOPMOST_SELECTED);
    g_imgTopmostUnselected = LoadImageFromResource(IDB_TOPMOST_UNSELECTED);
    g_imgBatchEditSelected = LoadImageFromResource(IDB_BATCH_EDIT_SELECTED);
    g_imgBatchEditUnselected = LoadImageFromResource(IDB_BATCH_EDIT_UNSELECTED);
    g_imgFolderIcon = LoadImageFromResource(IDB_FOLDER_ICON);
    g_imgNoExistIcon = LoadImageFromResource(IDB_NOEXIST_ICON);
}

// 释放按钮图片资源
void FreeButtonImages() {
    if (g_imgTopmostSelected) { delete g_imgTopmostSelected; g_imgTopmostSelected = NULL; }
    if (g_imgTopmostUnselected) { delete g_imgTopmostUnselected; g_imgTopmostUnselected = NULL; }
    if (g_imgBatchEditSelected) { delete g_imgBatchEditSelected; g_imgBatchEditSelected = NULL; }
    if (g_imgBatchEditUnselected) { delete g_imgBatchEditUnselected; g_imgBatchEditUnselected = NULL; }
    if (g_imgFolderIcon) { delete g_imgFolderIcon; g_imgFolderIcon = NULL; }
    if (g_imgNoExistIcon) { delete g_imgNoExistIcon; g_imgNoExistIcon = NULL; }
}

// 搜索框子类化窗口过程 - 处理渐变光标
LRESULT CALLBACK SearchBoxProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_LBUTTONDBLCLK: {
            // 双击全选文本
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
        case WM_CHAR: {
            // Ctrl+A 全选
            if (wParam == 1) {  // Ctrl+A
                SendMessageW(hwnd, EM_SETSEL, 0, -1);
                return 0;
            }
            break;
        }
        case WM_SETFOCUS: {
            // 隐藏默认光标，启动自定义光标定时器
            LRESULT result = CallWindowProcW(g_oldSearchBoxProc, hwnd, message, wParam, lParam);
            HideCaret(hwnd);
            DestroyCaret();  // 销毁默认光标
            g_caretVisible = true;
            g_caretGradientPos = 0.0f;
            g_caretBlinkCounter = 0;
            g_caretShowState = true;
            SetTimer(hwnd, ID_CARET_TIMER, 50, NULL);
            InvalidateRect(hwnd, NULL, FALSE);
            return result;
        }
        case WM_KILLFOCUS: {
            KillTimer(hwnd, ID_CARET_TIMER);
            g_caretVisible = false;
            InvalidateRect(hwnd, NULL, FALSE);
            return CallWindowProcW(g_oldSearchBoxProc, hwnd, message, wParam, lParam);
        }
        case WM_TIMER: {
            if (wParam == ID_CARET_TIMER) {
                g_caretGradientPos += 0.15f;
                if (g_caretGradientPos > 2.0f) g_caretGradientPos = 0.0f;

                // 闪烁控制（每10次切换一次，约500ms）
                g_caretBlinkCounter++;
                if (g_caretBlinkCounter >= 10) {
                    g_caretBlinkCounter = 0;
                    g_caretShowState = !g_caretShowState;
                }

                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            break;
        }
        case WM_PAINT: {
            LRESULT result = CallWindowProcW(g_oldSearchBoxProc, hwnd, message, wParam, lParam);

            HDC hdc = GetDC(hwnd);
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);

            // 获取文本长度
            int textLen = GetWindowTextLengthW(hwnd);

            // 失焦且无文本时显示占位符
            if (GetFocus() != hwnd && textLen == 0) {
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(160, 160, 160));  // 灰色
                HFONT hFont = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
                HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

                RECT textRect = rcClient;
                textRect.left += 4;
                DrawTextW(hdc, L"键入搜索", -1, &textRect, DT_SINGLELINE | DT_VCENTER);

                SelectObject(hdc, hOldFont);
            }
            // 获取焦点时显示渐变光标
            else if (g_caretVisible && g_caretShowState && GetFocus() == hwnd) {
                // 获取光标位置
                DWORD sel = SendMessageW(hwnd, EM_GETSEL, 0, 0);
                int charIndex = HIWORD(sel);  // 光标位置（选择结束位置）

                // 获取文本区域
                RECT rcText;
                SendMessageW(hwnd, EM_GETRECT, 0, (LPARAM)&rcText);
                int textLeft = rcText.left;

                // 计算光标X位置
                int caretX = textLeft;  // 默认起始位置（textLeft已经是4）
                int textLen = GetWindowTextLengthW(hwnd);

                if (textLen > 0 && charIndex > 0) {
                    // 计算光标前文本的宽度
                    HDC hdcTemp = GetDC(hwnd);
                    HFONT hFont = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
                    HFONT hOldFont = (HFONT)SelectObject(hdcTemp, hFont);

                    wchar_t text[256] = {0};
                    GetWindowTextW(hwnd, text, 256);

                    SIZE textSize;
                    int len = (charIndex > textLen) ? textLen : charIndex;
                    GetTextExtentPoint32W(hdcTemp, text, len, &textSize);
                    caretX = textLeft + textSize.cx;  // 文本宽度（无额外偏移）

                    SelectObject(hdcTemp, hOldFont);
                    ReleaseDC(hwnd, hdcTemp);
                }

                // 绘制渐变光标
                Graphics graphics(hdc);
                graphics.SetSmoothingMode(SmoothingModeAntiAlias);

                // 光标垂直居中
                int caretHeight = rcClient.bottom - 8;
                int caretY = 5;

                // 创建渐变画刷
                LinearGradientBrush brush(
                    Point(caretX, caretY),
                    Point(caretX, caretY + caretHeight),
                    Color(255, 0x65, 0x47, 0xFF),
                    Color(255, 0x00, 0x90, 0xFE)
                );

                Pen pen(&brush, 1.0f);
                graphics.DrawLine(&pen, caretX, caretY, caretX, caretY + caretHeight);
            }

            ReleaseDC(hwnd, hdc);
            return result;
        }
    }
    return CallWindowProcW(g_oldSearchBoxProc, hwnd, message, wParam, lParam);
}

// ==================== 标签管理弹出窗口 ====================
#define IDC_TAG_POPUP_LIST 4010
#define IDC_TAG_POPUP_ADD 4011
#define IDC_TAG_POPUP_EDIT 4012
#define IDC_TAG_POPUP_NAME 4013
#define IDC_TAG_POPUP_COLORS 4014

static HWND g_hwndTagPopup = NULL;
static HWND g_hwndTagPopupTooltip = NULL;
static int g_tagPopupHoverIndex = -1;
static int g_tagPopupEditIndex = -1;  // 正在编辑的标签索引
static HWND g_hwndTagPopupEdit = NULL;  // 编辑框句柄
static COLORREF g_tagPopupEditColor = RGB(66, 133, 244);  // 编辑中的颜色
static COLORREF g_tagPopupOriginalColor = RGB(66, 133, 244);  // 原始颜色（用于取消时恢复）
static int g_tagPopupColorPickerIndex = -1;  // 正在选择颜色的标签索引
static int g_tagPopupParentBtnWidth = 0;  // 父按钮宽度（用于边框绘制）
static int g_tagPopupArrowHeight = 10;   // 气泡箭头高度
static int g_tagPopupArrowWidth = 16;    // 气泡箭头宽度
static bool g_tagPopupFilterMode = false; // true=筛选模式（左击，仅选择分类），false=编辑模式（右击，可编辑颜色和名称）

// 常用颜色列表
static const COLORREF g_commonColors[] = {
    RGB(244, 67, 54),   // 红色
    RGB(233, 30, 99),   // 粉色
    RGB(156, 39, 176),  // 紫色
    RGB(103, 58, 183),  // 深紫
    RGB(63, 81, 181),   // 靛蓝
    RGB(33, 150, 243),  // 蓝色
    RGB(0, 188, 212),   // 青色
    RGB(0, 150, 136),   // 蓝绿
    RGB(76, 175, 80),   // 绿色
    RGB(139, 195, 74),  // 浅绿
    RGB(255, 193, 7),   // 琥珀
    RGB(255, 152, 0),   // 橙色
};
static const int g_commonColorsCount = sizeof(g_commonColors) / sizeof(g_commonColors[0]);

// 标签弹出窗口过程
LRESULT CALLBACK TagPopupProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static int itemHeight = 32;
    static int colorBoxSize = 16;
    static int padding = 8;
    static int cornerRadius = 8;  // 圆角半径
    int arrowHeight = g_tagPopupArrowHeight;
    int arrowWidth = g_tagPopupArrowWidth;

    switch (message) {
        case WM_CREATE: {
            // 创建tooltip
            g_hwndTagPopupTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                hwnd, NULL, GetModuleHandle(NULL), NULL);

            SendMessageW(g_hwndTagPopupTooltip, TTM_SETTIPBKCOLOR, (WPARAM)RGB(100, 100, 100), 0);
            SendMessageW(g_hwndTagPopupTooltip, TTM_SETTIPTEXTCOLOR, (WPARAM)RGB(255, 255, 255), 0);

            // 设置气泡形状区域（带箭头）
            RECT rc;
            GetWindowRect(hwnd, &rc);
            int width = rc.right - rc.left;
            int height = rc.bottom - rc.top;

            // 创建气泡形状：圆角矩形 + 顶部箭头
            POINT arrowPoints[3] = {
                {width / 2 - arrowWidth / 2, arrowHeight},
                {width / 2, 0},
                {width / 2 + arrowWidth / 2, arrowHeight}
            };
            HRGN hArrowRgn = CreatePolygonRgn(arrowPoints, 3, WINDING);
            HRGN hBodyRgn = CreateRoundRectRgn(0, arrowHeight, width + 1, height + 1, cornerRadius, cornerRadius);
            CombineRgn(hBodyRgn, hBodyRgn, hArrowRgn, RGN_OR);
            SetWindowRgn(hwnd, hBodyRgn, TRUE);
            DeleteObject(hArrowRgn);

            return 0;
        }

        case WM_SIZE: {
            // 窗口大小变化时更新气泡区域
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);

            POINT arrowPoints[3] = {
                {width / 2 - arrowWidth / 2, arrowHeight},
                {width / 2, 0},
                {width / 2 + arrowWidth / 2, arrowHeight}
            };
            HRGN hArrowRgn = CreatePolygonRgn(arrowPoints, 3, WINDING);
            HRGN hBodyRgn = CreateRoundRectRgn(0, arrowHeight, width + 1, height + 1, cornerRadius, cornerRadius);
            CombineRgn(hBodyRgn, hBodyRgn, hArrowRgn, RGN_OR);
            SetWindowRgn(hwnd, hBodyRgn, TRUE);
            DeleteObject(hArrowRgn);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rcClient;
            GetClientRect(hwnd, &rcClient);

            // 背景
            HBRUSH hBgBrush = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(hdc, &rcClient, hBgBrush);
            DeleteObject(hBgBrush);

            // 绘制气泡边框（实线箭头）
            HPEN hBorderPen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
            HPEN hOldPen = (HPEN)SelectObject(hdc, hBorderPen);

            // 先填充箭头（白色实心）
            POINT arrowPoints[3] = {
                {rcClient.right / 2 - arrowWidth / 2, arrowHeight},
                {rcClient.right / 2, 0},
                {rcClient.right / 2 + arrowWidth / 2, arrowHeight}
            };
            HBRUSH hWhiteBrush = CreateSolidBrush(RGB(255, 255, 255));
            HBRUSH hOldBrush2 = (HBRUSH)SelectObject(hdc, hWhiteBrush);
            Polygon(hdc, arrowPoints, 3);
            SelectObject(hdc, hOldBrush2);
            DeleteObject(hWhiteBrush);

            // 绘制圆角矩形边框
            HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hNullBrush);
            RoundRect(hdc, 0, arrowHeight, rcClient.right, rcClient.bottom, cornerRadius, cornerRadius);
            SelectObject(hdc, hOldBrush);

            SelectObject(hdc, hOldPen);
            DeleteObject(hBorderPen);

            // 字体（增大4px）
            HFONT hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

            int y = arrowHeight + padding;  // 从箭头下方开始

            // 绘制"全部分类"文字（蓝色，20px）
            HFONT hTitleFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
            SelectObject(hdc, hTitleFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(33, 150, 243));  // 蓝色
            RECT rcTitle = {padding, y, rcClient.right - padding - 30, y + itemHeight};
            DrawTextW(hdc, L"全部分类", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            DeleteObject(hTitleFont);

            // 绘制右上角的"+"按钮（筛选模式下隐藏，新增模式下禁用）
            int addBtnSize = 24;
            int addBtnX = rcClient.right - padding - addBtnSize;
            int addBtnY = y + (itemHeight - addBtnSize) / 2;
            RECT rcAddBtn = {addBtnX, addBtnY, addBtnX + addBtnSize, addBtnY + addBtnSize};

            bool isAddingNew = (g_tagPopupEditIndex >= (int)g_tags.size() && g_hwndTagPopupEdit != NULL);

            if (!g_tagPopupFilterMode) {
                // 编辑模式下才显示"+"按钮
                // 悬浮高亮（新增模式下不高亮）
                if (g_tagPopupHoverIndex == -2 && !isAddingNew) {
                    HBRUSH hHoverBrush = CreateSolidBrush(RGB(220, 240, 255));
                    SelectObject(hdc, GetStockObject(NULL_PEN));
                    RoundRect(hdc, rcAddBtn.left - 2, rcAddBtn.top - 2, rcAddBtn.right + 2, rcAddBtn.bottom + 2, 6, 6);
                    DeleteObject(hHoverBrush);
                }

                // 绘制"+"按钮（新增模式下显示灰色）
                HFONT hAddFont = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                SelectObject(hdc, hAddFont);
                SetBkMode(hdc, TRANSPARENT);
                if (isAddingNew) {
                    SetTextColor(hdc, RGB(180, 180, 180));  // 灰色（禁用状态）
                } else {
                    SetTextColor(hdc, RGB(33, 150, 243));  // 蓝色
                }
                DrawTextW(hdc, L"+", -1, &rcAddBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, hFont);
                DeleteObject(hAddFont);
            }

            y += itemHeight;

            // 绘制各个标签
            for (int i = 0; i < (int)g_tags.size(); i++) {
                const Tag& tag = g_tags[i];
                RECT rcTag = {padding, y, rcClient.right - padding, y + itemHeight};

                // 如果正在编辑这个标签，绘制编辑区域的颜色方块和确认/取消按钮
                if (g_tagPopupEditIndex == i) {
                    // 绘制当前选中的颜色方块
                    RECT rcEditColor = {padding + padding, y + (itemHeight - colorBoxSize) / 2,
                                       padding + padding + colorBoxSize, y + (itemHeight + colorBoxSize) / 2};
                    HBRUSH hEditColorBrush = CreateSolidBrush(g_tagPopupEditColor);
                    FillRect(hdc, &rcEditColor, hEditColorBrush);
                    DeleteObject(hEditColorBrush);

                    // 绘制确认按钮（绿色对勾）、取消按钮（红色❌）和删除按钮
                    int btnSize = 20;
                    int btnY = y + (itemHeight - btnSize) / 2;
                    int deleteX = rcClient.right - padding - btnSize * 3 - 8;
                    int confirmX = rcClient.right - padding - btnSize * 2 - 8;
                    int cancelX = rcClient.right - padding - btnSize;

                    HFONT hBtnFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
                    SelectObject(hdc, hBtnFont);

                    // 删除按钮（灰色垃圾桶图标）
                    SetTextColor(hdc, RGB(158, 158, 158));
                    RECT rcDelete = {deleteX, btnY, deleteX + btnSize, btnY + btnSize};
                    DrawTextW(hdc, L"\uE74D", -1, &rcDelete, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                    // 绿色对勾
                    HFONT hSymFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
                    SelectObject(hdc, hSymFont);
                    SetTextColor(hdc, RGB(76, 175, 80));
                    RECT rcConfirm = {confirmX, btnY, confirmX + btnSize, btnY + btnSize};
                    DrawTextW(hdc, L"✓", -1, &rcConfirm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                    // 红色❌
                    SetTextColor(hdc, RGB(244, 67, 54));
                    RECT rcCancel = {cancelX, btnY, cancelX + btnSize, btnY + btnSize};
                    DrawTextW(hdc, L"✕", -1, &rcCancel, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                    SelectObject(hdc, hFont);
                    DeleteObject(hSymFont);
                    DeleteObject(hBtnFont);

                    y += itemHeight;
                    continue;
                }

                // 悬浮高亮
                if (g_tagPopupHoverIndex == i && g_tagPopupColorPickerIndex < 0) {
                    HBRUSH hHoverBrush = CreateSolidBrush(RGB(240, 240, 240));
                    FillRect(hdc, &rcTag, hHoverBrush);
                    DeleteObject(hHoverBrush);
                }

                // 颜色方块（如果正在选择颜色，显示临时颜色）
                COLORREF displayColor = tag.color;
                if (g_tagPopupColorPickerIndex == i) {
                    displayColor = g_tagPopupEditColor;
                }
                RECT rcColor = {rcTag.left + padding, y + (itemHeight - colorBoxSize) / 2,
                               rcTag.left + padding + colorBoxSize, y + (itemHeight + colorBoxSize) / 2};
                HBRUSH hColorBrush = CreateSolidBrush(displayColor);
                FillRect(hdc, &rcColor, hColorBrush);
                DeleteObject(hColorBrush);

                // 标签名称
                SetTextColor(hdc, RGB(60, 60, 60));
                RECT rcName = rcTag;
                rcName.left = rcColor.right + padding;
                rcName.right = rcClient.right - padding;  // 不再预留按钮空间
                DrawTextW(hdc, tag.name.c_str(), -1, &rcName, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                y += itemHeight;
            }

            // 如果正在选择颜色，绘制高斯模糊遮罩和颜色选择器
            if (g_tagPopupColorPickerIndex >= 0) {
                int currentRowTop;
                if (g_tagPopupColorPickerIndex >= (int)g_tags.size()) {
                    // 新增模式
                    currentRowTop = arrowHeight + padding + itemHeight + g_tags.size() * itemHeight;
                } else {
                    // 编辑模式
                    currentRowTop = arrowHeight + padding + itemHeight + g_tagPopupColorPickerIndex * itemHeight;
                }
                int currentRowBottom = currentRowTop + itemHeight;

                // 计算颜色方块区域（只有这个区域不被遮罩）
                int colorBoxLeft = padding + padding;
                int colorBoxRight = colorBoxLeft + colorBoxSize;
                int colorBoxTop = currentRowTop + (itemHeight - colorBoxSize) / 2;
                int colorBoxBottom = colorBoxTop + colorBoxSize;

                // 计算颜色选择器区域
                int colorY = currentRowBottom + 4;
                int colorBtnSize = 18;
                int colorSpacing = 4;
                int colorsPerRow = 6;
                int colorRows = (g_commonColorsCount + colorsPerRow - 1) / colorsPerRow;
                int colorAreaHeight = 20 + colorRows * (colorBtnSize + colorSpacing);

                // 绘制全屏半透明遮罩
                RECT rcOverlay = {0, 0, rcClient.right, rcClient.bottom};
                HDC hdcMem = CreateCompatibleDC(hdc);
                HBITMAP hBmpMem = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
                SelectObject(hdcMem, hBmpMem);
                HBRUSH hOverlayBrush = CreateSolidBrush(RGB(255, 255, 255));
                FillRect(hdcMem, &rcOverlay, hOverlayBrush);
                BLENDFUNCTION bf = {AC_SRC_OVER, 0, 200, 0};
                AlphaBlend(hdc, 0, 0, rcClient.right, rcClient.bottom,
                          hdcMem, 0, 0, rcClient.right, rcClient.bottom, bf);
                DeleteDC(hdcMem);
                DeleteObject(hBmpMem);
                DeleteObject(hOverlayBrush);

                // 重新绘制颜色方块（清晰显示）
                RECT rcColorBox = {colorBoxLeft, colorBoxTop, colorBoxRight, colorBoxBottom};
                HBRUSH hColorBoxBrush = CreateSolidBrush(g_tagPopupEditColor);
                FillRect(hdc, &rcColorBox, hColorBoxBrush);
                DeleteObject(hColorBoxBrush);

                // 绘制颜色选择区域背景
                RECT rcColorArea = {padding - 4, colorY - 4, rcClient.right - padding + 4, colorY + colorAreaHeight + 4};
                HBRUSH hColorAreaBg = CreateSolidBrush(RGB(255, 255, 255));
                FillRect(hdc, &rcColorArea, hColorAreaBg);
                DeleteObject(hColorAreaBg);

                // 绘制"选择颜色:"标签
                SetTextColor(hdc, RGB(80, 80, 80));
                RECT rcColorLabel = {padding, colorY, rcClient.right - padding, colorY + 16};
                DrawTextW(hdc, L"选择颜色:", -1, &rcColorLabel, DT_LEFT | DT_TOP | DT_SINGLELINE);
                colorY += 20;

                // 绘制颜色按钮
                for (int c = 0; c < g_commonColorsCount; c++) {
                    int col = c % colorsPerRow;
                    int row = c / colorsPerRow;
                    int cx = padding + col * (colorBtnSize + colorSpacing);
                    int cy = colorY + row * (colorBtnSize + colorSpacing);

                    RECT rcColorBtn = {cx, cy, cx + colorBtnSize, cy + colorBtnSize};
                    HBRUSH hColorBtnBrush = CreateSolidBrush(g_commonColors[c]);
                    FillRect(hdc, &rcColorBtn, hColorBtnBrush);
                    DeleteObject(hColorBtnBrush);

                    // 如果是当前选中的颜色，绘制边框
                    if (g_commonColors[c] == g_tagPopupEditColor) {
                        HPEN hSelPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
                        HPEN hOldSelPen = (HPEN)SelectObject(hdc, hSelPen);
                        SelectObject(hdc, GetStockObject(NULL_BRUSH));
                        Rectangle(hdc, cx - 1, cy - 1, cx + colorBtnSize + 1, cy + colorBtnSize + 1);
                        SelectObject(hdc, hOldSelPen);
                        DeleteObject(hSelPen);
                    }
                }
            }

            // 如果正在新增标签，绘制新增行（红色方块 + 输入框 + 对勾/红叉）
            if (g_tagPopupEditIndex >= (int)g_tags.size() && g_hwndTagPopupEdit) {
                int newRowY = arrowHeight + padding + itemHeight + g_tags.size() * itemHeight;

                // 绘制颜色方块
                RECT rcNewColor = {padding + padding, newRowY + (itemHeight - colorBoxSize) / 2,
                                  padding + padding + colorBoxSize, newRowY + (itemHeight + colorBoxSize) / 2};
                HBRUSH hNewColorBrush = CreateSolidBrush(g_tagPopupEditColor);
                FillRect(hdc, &rcNewColor, hNewColorBrush);
                DeleteObject(hNewColorBrush);

                // 绘制确认按钮（绿色对勾）和取消按钮（红色❌）
                int btnSize = 20;
                int btnY = newRowY + (itemHeight - btnSize) / 2;
                int confirmX = rcClient.right - padding - btnSize * 2 - 8;
                int cancelX = rcClient.right - padding - btnSize;

                HFONT hBtnFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
                SelectObject(hdc, hBtnFont);
                SetTextColor(hdc, RGB(76, 175, 80));  // 绿色
                RECT rcConfirm = {confirmX, btnY, confirmX + btnSize, btnY + btnSize};
                DrawTextW(hdc, L"✓", -1, &rcConfirm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SetTextColor(hdc, RGB(244, 67, 54));  // 红色
                RECT rcCancel = {cancelX, btnY, cancelX + btnSize, btnY + btnSize};
                DrawTextW(hdc, L"✕", -1, &rcCancel, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SelectObject(hdc, hFont);
                DeleteObject(hBtnFont);
            }

            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);

            RECT rcClient;
            GetClientRect(hwnd, &rcClient);

            // 计算悬浮的项
            int newHoverIndex = -100;  // 无效值
            int itemY = arrowHeight + padding;  // 从箭头下方开始

            // 检查是否在"+"按钮区域
            int addBtnSize = 24;
            int addBtnX = rcClient.right - padding - addBtnSize;
            int addBtnY = itemY + (itemHeight - addBtnSize) / 2;
            if (x >= addBtnX - 2 && x < addBtnX + addBtnSize + 2 &&
                y >= addBtnY - 2 && y < addBtnY + addBtnSize + 2) {
                newHoverIndex = -2;  // "+"按钮
            }
            // "全部收藏"区域（排除"+"按钮区域）
            else if (y >= itemY && y < itemY + itemHeight && x < addBtnX - 2) {
                newHoverIndex = -1;
            }
            itemY += itemHeight;  // 跳过"+"按钮行

            // 各个标签
            for (int i = 0; i < (int)g_tags.size(); i++) {
                if (y >= itemY && y < itemY + itemHeight) {
                    newHoverIndex = i;
                    break;
                }
                itemY += itemHeight;
            }

            if (newHoverIndex != g_tagPopupHoverIndex) {
                g_tagPopupHoverIndex = newHoverIndex;
                InvalidateRect(hwnd, NULL, FALSE);

                // 更新tooltip
                TOOLINFOW ti = {};
                ti.cbSize = TTTOOLINFOW_V1_SIZE;
                ti.uFlags = TTF_SUBCLASS;
                ti.hwnd = hwnd;
                ti.uId = 1;
                RECT rcTip;
                GetClientRect(hwnd, &rcTip);
                ti.rect = rcTip;

                SendMessageW(g_hwndTagPopupTooltip, TTM_DELTOOLW, 0, (LPARAM)&ti);

                if (newHoverIndex == -2) {
                    // "+"按钮显示"点击新增标签"
                    ti.lpszText = (LPWSTR)L"点击新增标签";
                    SendMessageW(g_hwndTagPopupTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
                } else if (newHoverIndex >= 0) {
                    // 标签项显示"单击进行修改"
                    ti.lpszText = (LPWSTR)L"单击进行修改";
                    SendMessageW(g_hwndTagPopupTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
                }
            }

            // 跟踪鼠标离开
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE: {
            g_tagPopupHoverIndex = -100;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            int itemY = arrowHeight + padding;  // 从箭头下方开始

            RECT rcClient;
            GetClientRect(hwnd, &rcClient);

            // 检查是否点击了"+"按钮（筛选模式下禁用，新增模式下禁用）
            int addBtnSize = 24;
            int addBtnX = rcClient.right - padding - addBtnSize;
            int addBtnY = itemY + (itemHeight - addBtnSize) / 2;
            bool isAddingNew = (g_tagPopupEditIndex >= (int)g_tags.size() && g_hwndTagPopupEdit != NULL);
            if (!g_tagPopupFilterMode && !isAddingNew && x >= addBtnX - 2 && x < addBtnX + addBtnSize + 2 &&
                y >= addBtnY - 2 && y < addBtnY + addBtnSize + 2) {
                // 进入新增模式
                g_tagPopupEditIndex = (int)g_tags.size();  // 新增位置
                g_tagPopupEditColor = RGB(244, 67, 54);  // 默认红色
                g_tagPopupColorPickerIndex = -1;  // 不显示颜色选择器

                // 计算编辑框位置
                int editY = arrowHeight + padding + itemHeight + g_tags.size() * itemHeight;

                // 扩展窗口高度（只包含新增行）
                int extraHeight = itemHeight + padding;
                RECT rcWindow;
                GetWindowRect(hwnd, &rcWindow);
                SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left,
                            rcWindow.bottom - rcWindow.top + extraHeight,
                            SWP_NOMOVE | SWP_NOZORDER);

                // 创建编辑框
                if (g_hwndTagPopupEdit) {
                    DestroyWindow(g_hwndTagPopupEdit);
                }
                int editHeight = 22;
                int editYOffset = (itemHeight - editHeight) / 2;
                g_hwndTagPopupEdit = CreateWindowExW(0, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
                    padding + padding + colorBoxSize + padding, editY + editYOffset,
                    80, editHeight, hwnd, (HMENU)IDC_TAG_POPUP_NAME, GetModuleHandle(NULL), NULL);

                HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                SendMessageW(g_hwndTagPopupEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
                SetFocus(g_hwndTagPopupEdit);

                InvalidateRect(hwnd, NULL, TRUE);
                return 0;
            }

            // "全部收藏"（排除"+"按钮区域）
            if (y >= itemY && y < itemY + itemHeight && x < addBtnX - 2) {
                g_currentFilterTagId = 0;
                g_currentTab = 4;
                InvalidateRect(g_hwndFilterAll, NULL, TRUE);
                InvalidateRect(g_hwndFilterText, NULL, TRUE);
                InvalidateRect(g_hwndFilterImage, NULL, TRUE);
                InvalidateRect(g_hwndFilterFile, NULL, TRUE);
                InvalidateRect(g_hwndFilterFavorite, NULL, TRUE);
                UpdateListBox();
                DestroyWindow(hwnd);
                return 0;
            }
            itemY += itemHeight;  // 跳过"+"按钮行

            // 各个标签 - 左键点击
            for (int i = 0; i < (int)g_tags.size(); i++) {
                if (y >= itemY && y < itemY + itemHeight) {
                    // 如果正在编辑某个标签（有编辑框），只允许操作当前编辑的行
                    if (g_tagPopupEditIndex >= 0) {
                        if (g_tagPopupEditIndex == i) {
                            // 当前编辑的行，让后面的编辑模式逻辑处理
                            break;
                        } else {
                            // 点击了其他行，忽略
                            return 0;
                        }
                    }

                    // 如果正在选择颜色，只允许操作当前行的颜色方块
                    if (g_tagPopupColorPickerIndex >= 0) {
                        if (g_tagPopupColorPickerIndex == i) {
                            // 当前选择颜色的行，只允许点击颜色方块
                            int colorBoxLeft = padding + padding;
                            int colorBoxRight = colorBoxLeft + colorBoxSize;
                            if (x >= colorBoxLeft && x < colorBoxRight) {
                                // 点击颜色方块，关闭颜色选择器
                                g_tagPopupColorPickerIndex = -1;
                                int newHeight = arrowHeight + padding + itemHeight + g_tags.size() * itemHeight + padding;
                                RECT rcWindow;
                                GetWindowRect(hwnd, &rcWindow);
                                SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left, newHeight,
                                            SWP_NOMOVE | SWP_NOZORDER);
                                InvalidateRect(hwnd, NULL, TRUE);
                            }
                            // 其他区域忽略
                            return 0;
                        } else {
                            // 点击了其他行，忽略
                            return 0;
                        }
                    }

                    RECT rcClient;
                    GetClientRect(hwnd, &rcClient);

                    // 计算各区域
                    int colorBoxLeft = padding + padding;
                    int colorBoxRight = colorBoxLeft + colorBoxSize;
                    int nameLeft = colorBoxRight + padding;
                    int nameRight = rcClient.right - padding;  // 不再预留按钮空间

                    if (g_tagPopupFilterMode) {
                        // 筛选模式：点击整行（颜色方块或名称）都是筛选
                        g_currentFilterTagId = g_tags[i].id;
                        g_currentTab = 4;
                        InvalidateRect(g_hwndFilterAll, NULL, TRUE);
                        InvalidateRect(g_hwndFilterText, NULL, TRUE);
                        InvalidateRect(g_hwndFilterImage, NULL, TRUE);
                        InvalidateRect(g_hwndFilterFile, NULL, TRUE);
                        InvalidateRect(g_hwndFilterFavorite, NULL, TRUE);
                        UpdateListBox();
                        DestroyWindow(hwnd);
                        return 0;
                    }

                    // 编辑模式：点击颜色方块 - 显示/隐藏颜色选择器
                    if (x >= colorBoxLeft && x < colorBoxRight) {
                        // 显示颜色选择器
                        g_tagPopupColorPickerIndex = i;
                        g_tagPopupEditColor = g_tags[i].color;
                        g_tagPopupOriginalColor = g_tags[i].color;

                        // 扩展窗口高度（包含颜色选择区域）
                        int colorRows = (g_commonColorsCount + 5) / 6;
                        int colorAreaHeight = 20 + colorRows * 22 + 8;
                        int newHeight = padding + itemHeight + (i + 1) * itemHeight + colorAreaHeight +
                                       (g_tags.size() - i - 1) * itemHeight + padding;
                        RECT rcWindow;
                        GetWindowRect(hwnd, &rcWindow);
                        SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left, newHeight,
                                    SWP_NOMOVE | SWP_NOZORDER);
                        InvalidateRect(hwnd, NULL, TRUE);
                        return 0;
                    }

                    // 编辑模式：点击名称区域 - 进入编辑模式
                    if (x >= nameLeft && x < nameRight) {
                        g_tagPopupEditIndex = i;
                        g_tagPopupEditColor = g_tags[i].color;
                        g_tagPopupColorPickerIndex = -1;

                        // 创建编辑框
                        if (g_hwndTagPopupEdit) {
                            DestroyWindow(g_hwndTagPopupEdit);
                        }
                        int editHeight = 22;
                        int editYOffset = (itemHeight - editHeight) / 2;
                        g_hwndTagPopupEdit = CreateWindowExW(0, L"EDIT", g_tags[i].name.c_str(),
                            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
                            padding + padding + colorBoxSize + padding, itemY + editYOffset,
                            80, editHeight, hwnd, (HMENU)IDC_TAG_POPUP_NAME, GetModuleHandle(NULL), NULL);

                        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                        SendMessageW(g_hwndTagPopupEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
                        SendMessageW(g_hwndTagPopupEdit, EM_SETSEL, 0, -1);
                        SetFocus(g_hwndTagPopupEdit);

                        InvalidateRect(hwnd, NULL, TRUE);
                        return 0;
                    }
                }
                itemY += itemHeight;
                // 如果当前标签有颜色选择器打开，需要跳过颜色选择区域
                if (g_tagPopupColorPickerIndex == i) {
                    int colorRows = (g_commonColorsCount + 5) / 6;
                    int colorAreaHeight = 20 + colorRows * 22 + 8;
                    itemY += colorAreaHeight;
                }
            }

            // 检查是否点击了编辑模式下的确认或取消按钮
            if (g_tagPopupEditIndex >= 0 && g_hwndTagPopupEdit) {
                int editY;
                if (g_tagPopupEditIndex >= (int)g_tags.size()) {
                    // 新增模式
                    editY = arrowHeight + padding + itemHeight + g_tags.size() * itemHeight;
                } else {
                    // 编辑模式
                    editY = arrowHeight + padding + itemHeight + g_tagPopupEditIndex * itemHeight;
                }

                RECT rcClient;
                GetClientRect(hwnd, &rcClient);

                int btnSize = 20;
                int btnY = editY + (itemHeight - btnSize) / 2;
                int deleteX = rcClient.right - padding - btnSize * 3 - 8;
                int confirmX = rcClient.right - padding - btnSize * 2 - 8;
                int cancelX = rcClient.right - padding - btnSize;

                // 检查是否点击了颜色方块（新增模式下可以选择颜色）
                int colorBoxLeft = padding + padding;
                int colorBoxRight = colorBoxLeft + colorBoxSize;
                int colorBoxTop = editY + (itemHeight - colorBoxSize) / 2;
                int colorBoxBottom = colorBoxTop + colorBoxSize;
                if (x >= colorBoxLeft && x < colorBoxRight &&
                    y >= colorBoxTop && y < colorBoxBottom) {
                    // 显示/隐藏颜色选择器
                    if (g_tagPopupColorPickerIndex == g_tagPopupEditIndex) {
                        // 再次点击颜色方块，隐藏颜色选择器
                        g_tagPopupColorPickerIndex = -1;
                        // 重新计算窗口大小
                        int newHeight = arrowHeight + padding + itemHeight + g_tags.size() * itemHeight + itemHeight + padding;
                        RECT rcWindow;
                        GetWindowRect(hwnd, &rcWindow);
                        SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left, newHeight,
                                    SWP_NOMOVE | SWP_NOZORDER);
                    } else {
                        // 显示颜色选择器
                        g_tagPopupColorPickerIndex = g_tagPopupEditIndex;
                        g_tagPopupOriginalColor = g_tagPopupEditColor;

                        // 扩展窗口高度（包含颜色选择区域）
                        int colorRows = (g_commonColorsCount + 5) / 6;
                        int colorAreaHeight = 20 + colorRows * 22 + 8;
                        int newHeight = arrowHeight + padding + itemHeight + g_tags.size() * itemHeight + itemHeight + colorAreaHeight + padding;
                        RECT rcWindow;
                        GetWindowRect(hwnd, &rcWindow);
                        SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left, newHeight,
                                    SWP_NOMOVE | SWP_NOZORDER);
                    }
                    InvalidateRect(hwnd, NULL, TRUE);
                    return 0;
                }

                // 检查删除按钮（仅编辑现有标签时有效）
                if (g_tagPopupEditIndex < (int)g_tags.size() &&
                    x >= deleteX && x < deleteX + btnSize &&
                    y >= btnY && y < btnY + btnSize) {
                    int tagId = g_tags[g_tagPopupEditIndex].id;
                    std::wstring tagName = g_tags[g_tagPopupEditIndex].name;

                    int result = MessageBoxW(hwnd,
                        (L"确定要删除分类「" + tagName + L"」吗？\n该分类下的记录不会被删除。").c_str(),
                        L"确认删除", MB_YESNO | MB_ICONQUESTION);
                    if (result == IDYES) {
                        DestroyWindow(g_hwndTagPopupEdit);
                        g_hwndTagPopupEdit = NULL;
                        g_tagPopupEditIndex = -1;
                        g_tagPopupColorPickerIndex = -1;

                        RemoveTag(tagId);
                        SaveTags();
                        SaveHistory();

                        int newHeight = arrowHeight + padding + itemHeight + g_tags.size() * itemHeight + padding;
                        RECT rcWindow;
                        GetWindowRect(hwnd, &rcWindow);
                        SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left, newHeight,
                                    SWP_NOMOVE | SWP_NOZORDER);
                        InvalidateRect(hwnd, NULL, TRUE);

                        if (g_currentFilterTagId == tagId) {
                            g_currentFilterTagId = 0;
                            UpdateListBox();
                        }
                    }
                    return 0;
                }

                // 检查确认按钮
                if (x >= confirmX && x < confirmX + btnSize &&
                    y >= btnY && y < btnY + btnSize) {
                    // 保存修改
                    wchar_t name[256] = {0};
                    GetWindowTextW(g_hwndTagPopupEdit, name, 256);

                    if (wcslen(name) > 0) {
                        // 检查名称是否重复
                        bool isDuplicate = false;
                        for (int i = 0; i < (int)g_tags.size(); i++) {
                            if (i == g_tagPopupEditIndex) continue;
                            if (g_tags[i].name == name) {
                                isDuplicate = true;
                                break;
                            }
                        }

                        if (isDuplicate) {
                            MessageBoxW(hwnd, L"标签名称已存在，请使用其他名称", L"提示", MB_OK | MB_ICONWARNING);
                            SetFocus(g_hwndTagPopupEdit);
                            return 0;
                        }

                        if (g_tagPopupEditIndex >= (int)g_tags.size()) {
                            // 新增标签
                            AddTag(name, g_tagPopupEditColor);
                        } else {
                            // 编辑现有标签
                            g_tags[g_tagPopupEditIndex].name = name;
                            g_tags[g_tagPopupEditIndex].color = g_tagPopupEditColor;
                        }
                        SaveTags();
                    }

                    DestroyWindow(g_hwndTagPopupEdit);
                    g_hwndTagPopupEdit = NULL;
                    g_tagPopupEditIndex = -1;

                    // 重新计算窗口大小
                    int newHeight = arrowHeight + padding + itemHeight + g_tags.size() * itemHeight + padding;
                    RECT rcWindow;
                    GetWindowRect(hwnd, &rcWindow);
                    SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left, newHeight,
                                SWP_NOMOVE | SWP_NOZORDER);
                    InvalidateRect(hwnd, NULL, TRUE);
                    return 0;
                }

                // 检查取消按钮
                if (x >= cancelX && x < cancelX + btnSize &&
                    y >= btnY && y < btnY + btnSize) {
                    // 取消编辑
                    DestroyWindow(g_hwndTagPopupEdit);
                    g_hwndTagPopupEdit = NULL;
                    g_tagPopupEditIndex = -1;

                    // 重新计算窗口大小
                    int newHeight = arrowHeight + padding + itemHeight + g_tags.size() * itemHeight + padding;
                    RECT rcWindow;
                    GetWindowRect(hwnd, &rcWindow);
                    SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left, newHeight,
                                SWP_NOMOVE | SWP_NOZORDER);
                    InvalidateRect(hwnd, NULL, TRUE);
                    return 0;
                }
            }

            // 检查是否点击了颜色选择按钮
            if (g_tagPopupColorPickerIndex >= 0) {
                int colorPickerY;
                if (g_tagPopupColorPickerIndex >= (int)g_tags.size()) {
                    // 新增模式
                    colorPickerY = arrowHeight + padding + itemHeight + g_tags.size() * itemHeight + itemHeight + 4;
                } else {
                    // 编辑模式
                    colorPickerY = arrowHeight + padding + itemHeight + (g_tagPopupColorPickerIndex + 1) * itemHeight + 4;
                }
                int colorY = colorPickerY + 20;  // 跳过"选择颜色:"标签
                int colorBtnSize = 18;
                int colorSpacing = 4;
                int colorsPerRow = 6;

                for (int c = 0; c < g_commonColorsCount; c++) {
                    int col = c % colorsPerRow;
                    int row = c / colorsPerRow;
                    int cx = padding + col * (colorBtnSize + colorSpacing);
                    int cy = colorY + row * (colorBtnSize + colorSpacing);

                    if (x >= cx && x < cx + colorBtnSize &&
                        y >= cy && y < cy + colorBtnSize) {
                        // 选择颜色
                        g_tagPopupEditColor = g_commonColors[c];

                        if (g_tagPopupColorPickerIndex < (int)g_tags.size()) {
                            // 编辑模式 - 立即保存并关闭颜色选择器
                            g_tags[g_tagPopupColorPickerIndex].color = g_commonColors[c];
                            SaveTags();
                            g_tagPopupColorPickerIndex = -1;

                            // 重新计算窗口大小
                            int newHeight = arrowHeight + padding + itemHeight + g_tags.size() * itemHeight + padding;
                            RECT rcWindow;
                            GetWindowRect(hwnd, &rcWindow);
                            SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left, newHeight,
                                        SWP_NOMOVE | SWP_NOZORDER);
                        } else {
                            // 新增模式 - 只更新颜色，关闭颜色选择器
                            g_tagPopupColorPickerIndex = -1;

                            // 重新计算窗口大小
                            int newHeight = arrowHeight + padding + itemHeight + g_tags.size() * itemHeight + itemHeight + padding;
                            RECT rcWindow;
                            GetWindowRect(hwnd, &rcWindow);
                            SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left, newHeight,
                                        SWP_NOMOVE | SWP_NOZORDER);
                        }
                        InvalidateRect(hwnd, NULL, TRUE);
                        return 0;
                    }
                }
            }
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            // 筛选模式下不允许双击编辑
            if (g_tagPopupFilterMode) return 0;

            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            int itemY = arrowHeight + padding + itemHeight;  // 跳过箭头和"+"按钮行

            // 各个标签 - 双击进入编辑模式
            for (int i = 0; i < (int)g_tags.size(); i++) {
                if (y >= itemY && y < itemY + itemHeight) {
                    // 如果正在编辑或选择颜色，忽略
                    if (g_tagPopupEditIndex >= 0 || g_tagPopupColorPickerIndex >= 0) {
                        return 0;
                    }

                    // 计算名称区域
                    int colorBoxLeft = padding + padding;
                    int colorBoxRight = colorBoxLeft + colorBoxSize;
                    int nameLeft = colorBoxRight + padding;
                    RECT rcClient;
                    GetClientRect(hwnd, &rcClient);
                    int nameRight = rcClient.right - padding;

                    // 双击名称区域 - 进入编辑模式
                    if (x >= nameLeft && x < nameRight) {
                        g_tagPopupEditIndex = i;
                        g_tagPopupEditColor = g_tags[i].color;
                        g_tagPopupColorPickerIndex = -1;

                        // 创建编辑框
                        if (g_hwndTagPopupEdit) {
                            DestroyWindow(g_hwndTagPopupEdit);
                        }
                        int editHeight = 22;
                        int editYOffset = (itemHeight - editHeight) / 2;
                        g_hwndTagPopupEdit = CreateWindowExW(0, L"EDIT", g_tags[i].name.c_str(),
                            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
                            padding + padding + colorBoxSize + padding, itemY + editYOffset,
                            80, editHeight, hwnd, (HMENU)IDC_TAG_POPUP_NAME, GetModuleHandle(NULL), NULL);

                        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                        SendMessageW(g_hwndTagPopupEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
                        SendMessageW(g_hwndTagPopupEdit, EM_SETSEL, 0, -1);
                        SetFocus(g_hwndTagPopupEdit);

                        InvalidateRect(hwnd, NULL, TRUE);
                        return 0;
                    }
                }
                itemY += itemHeight;
            }
            return 0;
        }

        case WM_RBUTTONDOWN: {
            // 筛选模式下不允许右键编辑
            if (g_tagPopupFilterMode) return 0;

            int y = HIWORD(lParam);
            int itemY = arrowHeight + padding + itemHeight;  // 跳过箭头和"+"按钮行

            // 各个标签 - 右键点击编辑名称
            for (int i = 0; i < (int)g_tags.size(); i++) {
                if (y >= itemY && y < itemY + itemHeight) {
                    // 进入编辑模式
                    g_tagPopupEditIndex = i;
                    g_tagPopupEditColor = g_tags[i].color;
                    g_tagPopupColorPickerIndex = -1;  // 关闭颜色选择器

                    // 创建编辑框
                    if (g_hwndTagPopupEdit) {
                        DestroyWindow(g_hwndTagPopupEdit);
                    }
                    int editHeight = 22;
                    int editYOffset = (itemHeight - editHeight) / 2;
                    g_hwndTagPopupEdit = CreateWindowExW(0, L"EDIT", g_tags[i].name.c_str(),
                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
                        padding + padding + colorBoxSize + padding, itemY + editYOffset,
                        80, editHeight, hwnd, (HMENU)IDC_TAG_POPUP_NAME, GetModuleHandle(NULL), NULL);

                    HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                    SendMessageW(g_hwndTagPopupEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
                    SendMessageW(g_hwndTagPopupEdit, EM_SETSEL, 0, -1);
                    SetFocus(g_hwndTagPopupEdit);

                    InvalidateRect(hwnd, NULL, TRUE);
                    return 0;
                }
                itemY += itemHeight;
            }
            return 0;
        }

        case WM_COMMAND: {
            WORD wID = LOWORD(wParam);
            WORD wNotifyCode = HIWORD(wParam);

            if (wID == IDC_TAG_POPUP_NAME && wNotifyCode == EN_KILLFOCUS) {
                // 编辑框失去焦点 - 不自动保存，等待用户点击确认或取消按钮
                // 只有在窗口即将关闭时才处理
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (wParam == VK_RETURN && g_hwndTagPopupEdit) {
                // 回车保存
                SetFocus(hwnd);  // 触发EN_KILLFOCUS
                return 0;
            }
            break;
        }

        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE) {
                // 窗口失去焦点时关闭（除非焦点在编辑框）
                HWND hwndFocus = GetFocus();
                if (hwndFocus != g_hwndTagPopupEdit) {
                    DestroyWindow(hwnd);
                }
            }
            return 0;
        }

        case WM_DESTROY: {
            g_hwndTagPopup = NULL;
            g_tagPopupEditIndex = -1;
            if (g_hwndTagPopupEdit) {
                DestroyWindow(g_hwndTagPopupEdit);
                g_hwndTagPopupEdit = NULL;
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// 显示标签弹出窗口
void ShowTagPopup(HWND hwndParent, int x, int y, int btnWidth) {
    if (g_hwndTagPopup) {
        DestroyWindow(g_hwndTagPopup);
    }

    // 注册窗口类
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = TagPopupProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"TagPopupWindow";
        RegisterClassExW(&wc);
        registered = true;
    }

    // 计算窗口大小（包含箭头高度）
    int itemHeight = 32;
    int padding = 8;
    int width = 200;
    int height = g_tagPopupArrowHeight + padding + itemHeight + g_tags.size() * itemHeight + padding;

    // 计算位置：气泡箭头指向按钮中心
    int popupX = x + btnWidth / 2 - width / 2;  // 居中对齐
    int popupY = y;  // 紧贴按钮下方

    g_hwndTagPopup = CreateWindowExW(WS_EX_TOOLWINDOW,
        L"TagPopupWindow", NULL,
        WS_POPUP,
        popupX, popupY, width, height,
        hwndParent, NULL, GetModuleHandle(NULL), NULL);

    ShowWindow(g_hwndTagPopup, SW_SHOW);
    UpdateWindow(g_hwndTagPopup);
}

// 窗口过程
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCACTIVATE: {
            return TRUE;
        }
        case WM_NCPAINT: {
            // 自绘非客户区边框，用窗口背景色替代系统灰色边框
            HDC hdc = GetWindowDC(hwnd);
            if (hdc) {
                RECT rcWin;
                GetWindowRect(hwnd, &rcWin);
                OffsetRect(&rcWin, -rcWin.left, -rcWin.top);

                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                // 将客户区坐标映射到窗口坐标
                POINT ptClient = {0, 0};
                ClientToScreen(hwnd, &ptClient);
                RECT rcWinScreen;
                GetWindowRect(hwnd, &rcWinScreen);
                int borderLeft = ptClient.x - rcWinScreen.left;
                int borderTop = ptClient.y - rcWinScreen.top;
                int borderRight = rcWin.right - rcClient.right - borderLeft;
                int borderBottom = rcWin.bottom - rcClient.bottom - borderTop;

                // 用背景色填充非客户区边框
                HBRUSH hBrush = CreateSolidBrush(GetBgColor());
                // 上
                RECT rcTop = { 0, 0, rcWin.right, borderTop };
                FillRect(hdc, &rcTop, hBrush);
                // 左
                RECT rcLeft = { 0, borderTop, borderLeft, rcWin.bottom };
                FillRect(hdc, &rcLeft, hBrush);
                // 右
                RECT rcRight = { rcWin.right - borderRight, borderTop, rcWin.right, rcWin.bottom };
                FillRect(hdc, &rcRight, hBrush);
                // 下
                RECT rcBottom = { borderLeft, rcWin.bottom - borderBottom, rcWin.right - borderRight, rcWin.bottom };
                FillRect(hdc, &rcBottom, hBrush);

                DeleteObject(hBrush);
                ReleaseDC(hwnd, hdc);
            }
            return 0;
        }
        case WM_NCCALCSIZE: {
            if (wParam == TRUE) {
                NCCALCSIZE_PARAMS* pParams = (NCCALCSIZE_PARAMS*)lParam;
                pParams->rgrc[0].top += 1;
                return 0;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        case WM_NCHITTEST: {
            // 处理标题栏区域的点击测试
            LRESULT hit = DefWindowProcW(hwnd, message, wParam, lParam);
            if (hit == HTCLIENT) {
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd, &pt);

                // 标题栏区域（顶部 TITLEBAR_HEIGHT 像素）
                if (pt.y < TITLEBAR_HEIGHT) {
                    // 检查是否在按钮区域
                    RECT rcBtn;
                    if (g_hwndTitleClose) {
                        GetWindowRect(g_hwndTitleClose, &rcBtn);
                        MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rcBtn, 2);
                        if (PtInRect(&rcBtn, pt)) return HTCLIENT;
                    }
                    if (g_hwndTitleMaximize) {
                        GetWindowRect(g_hwndTitleMaximize, &rcBtn);
                        MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rcBtn, 2);
                        if (PtInRect(&rcBtn, pt)) return HTCLIENT;
                    }
                    if (g_hwndTitleMinimize) {
                        GetWindowRect(g_hwndTitleMinimize, &rcBtn);
                        MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rcBtn, 2);
                        if (PtInRect(&rcBtn, pt)) return HTCLIENT;
                    }
                    if (g_hwndTitleTopmost) {
                        GetWindowRect(g_hwndTitleTopmost, &rcBtn);
                        MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rcBtn, 2);
                        if (PtInRect(&rcBtn, pt)) return HTCLIENT;
                    }
                    // 不在按钮上，返回 HTCAPTION 允许拖动
                    // 如果标签弹出窗口正在显示，禁止拖动主窗口
                    if (g_hwndTagPopup && IsWindowVisible(g_hwndTagPopup)) {
                        return HTCLIENT;
                    }
                    return HTCAPTION;
                }
            }
            return hit;
        }
        case WM_CREATE: {
            // 加载快捷键设置
            LoadHotkeySettings();

            // 不再创建主菜单

            // 注册快捷键，如果默认快捷键冲突则禁用
            if (!RegisterHotkey(hwnd)) {
                g_isHotkeyEnabled = false;
                SaveHotkeySettings();
            }

            // 注册快捷粘贴快捷键
            RegisterQuickPasteHotkeys(hwnd);

            // 创建搜索栏（使用ES_MULTILINE以支持EM_SETRECT垂直居中）
            g_hwndSearchBox = CreateWindowExW(0, L"EDIT", NULL,
                                          WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_MULTILINE,
                                          0, 0, 0, 0, hwnd, (HMENU)ID_SEARCH_BOX, GetModuleHandleW(NULL), NULL);

            // 占位符文本将在SearchBoxProc中自绘

            // 设置搜索框字体（比UI字体大3px）
            HFONT hSearchFont = CreateFontW(g_fontSize + 3, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_fontName.c_str());
            SendMessageW(g_hwndSearchBox, WM_SETFONT, (WPARAM)hSearchFont, TRUE);

            // 设置搜索框左边距
            SendMessageW(g_hwndSearchBox, EM_SETMARGINS, EC_LEFTMARGIN, MAKELPARAM(1, 0));

            // 设置UI控件字体
            HFONT hUIFont = CreateFontW(g_fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_fontName.c_str());

            // 子类化搜索框以处理渐变光标
            g_oldSearchBoxProc = (WNDPROC)SetWindowLongPtrW(g_hwndSearchBox, GWLP_WNDPROC, (LONG_PTR)SearchBoxProc);

            // 创建筛选按钮（自绘样式）
            g_hwndFilterAll = CreateWindowExW(0, L"BUTTON", L"全部",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 0, 0, hwnd, (HMENU)ID_FILTER_ALL, GetModuleHandleW(NULL), NULL);
            g_hwndFilterText = CreateWindowExW(0, L"BUTTON", L"文本",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 0, 0, hwnd, (HMENU)ID_FILTER_TEXT, GetModuleHandleW(NULL), NULL);
            g_hwndFilterImage = CreateWindowExW(0, L"BUTTON", L"图像",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 0, 0, hwnd, (HMENU)ID_FILTER_IMAGE, GetModuleHandleW(NULL), NULL);
            g_hwndFilterFile = CreateWindowExW(0, L"BUTTON", L"文件",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 0, 0, hwnd, (HMENU)ID_FILTER_FILE, GetModuleHandleW(NULL), NULL);
            g_hwndFilterFavorite = CreateWindowExW(0, L"BUTTON", L"收藏",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 0, 0, hwnd, (HMENU)ID_FILTER_FAVORITE, GetModuleHandleW(NULL), NULL);

            // 设置筛选按钮字体（比UI字体大4px）
            HFONT hFilterFont = CreateFontW(g_fontSize + 4, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_fontName.c_str());
            SendMessageW(g_hwndFilterAll, WM_SETFONT, (WPARAM)hFilterFont, TRUE);
            SendMessageW(g_hwndFilterText, WM_SETFONT, (WPARAM)hFilterFont, TRUE);
            SendMessageW(g_hwndFilterImage, WM_SETFONT, (WPARAM)hFilterFont, TRUE);
            SendMessageW(g_hwndFilterFile, WM_SETFONT, (WPARAM)hFilterFont, TRUE);
            SendMessageW(g_hwndFilterFavorite, WM_SETFONT, (WPARAM)hFilterFont, TRUE);

            // 创建剪贴板内容列表（使用 owner-drawn 模式，自绘滚动条）
            g_hwndListBox = CreateWindowExW(0, L"LISTBOX", NULL,
                                          WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY | LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS,
                                          0, 0, 0, 0, hwnd, (HMENU)ID_LISTBOX, GetModuleHandleW(NULL), NULL);

            // 子类化列表框以处理展开/收起按钮点击和自绘滚动条
            g_oldListBoxProc = (WNDPROC)SetWindowLongPtrW(g_hwndListBox, GWLP_WNDPROC, (LONG_PTR)ListBoxProc);

            // 注册主窗口为拖放目标（用于显示拖拽图像）
            g_pDropTarget = new CDropTarget();
            RegisterDragDrop(hwnd, g_pDropTarget);

            // 创建列表框 Tooltip（用于显示来源应用名）
            g_hwndListBoxTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                          WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                          CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                          g_hwndListBox, NULL, GetModuleHandleW(NULL), NULL);

            // 设置 tooltip 最大宽度（允许多行显示）
            SendMessageW(g_hwndListBoxTooltip, TTM_SETMAXTIPWIDTH, 0, 300);

            // 设置 tooltip 颜色：淡灰色底纹，白色字体
            SendMessageW(g_hwndListBoxTooltip, TTM_SETTIPBKCOLOR, (WPARAM)RGB(140, 140, 140), 0);
            SendMessageW(g_hwndListBoxTooltip, TTM_SETTIPTEXTCOLOR, (WPARAM)RGB(255, 255, 255), 0);

            // 为列表框添加 Tooltip 工具（使用 TTF_TRACK 实现手动控制显示）
            TOOLINFOW tiListBox = {};
            tiListBox.cbSize = TTTOOLINFOW_V1_SIZE;
            tiListBox.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            tiListBox.hwnd = g_hwndListBox;
            tiListBox.uId = 0;
            tiListBox.lpszText = (LPWSTR)L"";
            RECT rcListBox;
            GetClientRect(g_hwndListBox, &rcListBox);
            tiListBox.rect = rcListBox;
            SendMessageW(g_hwndListBoxTooltip, TTM_ADDTOOLW, 0, (LPARAM)&tiListBox);
            SendMessageW(g_hwndListBoxTooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 0);  // 立即显示

            // 加载按钮图片资源
            LoadButtonImages();

            // 创建功能按钮（自绘样式，只显示图标）
            // 创建置顶按钮（已移至标题栏，此处隐藏）
            HWND hwndTopmostButton = CreateWindowExW(0, L"BUTTON", L"置顶", WS_CHILD | BS_OWNERDRAW,
                          0, 0, 0, 0, hwnd, (HMENU)ID_TOPMOST_BUTTON, GetModuleHandleW(NULL), NULL);
            // 子类化置顶按钮以处理悬浮效果
            g_hwndTopmostBtn = hwndTopmostButton;
            g_oldTopmostBtnProc = (WNDPROC)SetWindowLongPtrW(hwndTopmostButton, GWLP_WNDPROC, (LONG_PTR)TopmostBtnProc);

            // 创建批量编辑按钮
            HWND hwndBatchEditButton = CreateWindowExW(0, L"BUTTON", L"批量编辑", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                          0, 0, 0, 0, hwnd, (HMENU)ID_BATCH_EDIT_BUTTON, GetModuleHandleW(NULL), NULL);
            // 子类化批量编辑按钮以处理悬浮效果
            g_hwndBatchEditBtn = hwndBatchEditButton;
            g_oldBatchEditBtnProc = (WNDPROC)SetWindowLongPtrW(hwndBatchEditButton, GWLP_WNDPROC, (LONG_PTR)BatchEditBtnProc);

            // 创建暗黑模式按钮（已移至设置对话框，此处隐藏）
            HWND hwndDarkmodeButton = CreateWindowExW(0, L"BUTTON", L"暗黑", WS_CHILD | BS_OWNERDRAW,
                          0, 0, 0, 0, hwnd, (HMENU)ID_DARKMODE_BUTTON, GetModuleHandleW(NULL), NULL);

            // 创建翻页按钮（上一页）
            g_hwndPageUpBtn = CreateWindowExW(0, L"BUTTON", L"",
                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                          0, 0, 0, 0, hwnd, (HMENU)ID_PAGE_UP_BTN, GetModuleHandleW(NULL), NULL);
            // 子类化上一页按钮以处理悬浮效果
            g_oldPageUpBtnProc = (WNDPROC)SetWindowLongPtrW(g_hwndPageUpBtn, GWLP_WNDPROC, (LONG_PTR)PageUpBtnProc);

            // 创建翻页按钮（下一页）
            g_hwndPageDownBtn = CreateWindowExW(0, L"BUTTON", L"",
                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                          0, 0, 0, 0, hwnd, (HMENU)ID_PAGE_DOWN_BTN, GetModuleHandleW(NULL), NULL);
            // 子类化下一页按钮以处理悬浮效果
            g_oldPageDownBtnProc = (WNDPROC)SetWindowLongPtrW(g_hwndPageDownBtn, GWLP_WNDPROC, (LONG_PTR)PageDownBtnProc);

            // 创建Tooltip控件
            HWND hwndTooltip = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL,
                          WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                          CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                          hwnd, NULL, GetModuleHandleW(NULL), NULL);

            // 设置 tooltip 颜色：淡灰色底纹，白色字体（与列表框tooltip样式一致）
            SendMessageW(hwndTooltip, TTM_SETTIPBKCOLOR, (WPARAM)RGB(140, 140, 140), 0);
            SendMessageW(hwndTooltip, TTM_SETTIPTEXTCOLOR, (WPARAM)RGB(255, 255, 255), 0);

            // 为每个按钮添加Tooltip
            TOOLINFOW ti = {};
            ti.cbSize = TTTOOLINFOW_V1_SIZE;
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd = hwnd;

            ti.uId = (UINT_PTR)hwndTopmostButton;
            ti.lpszText = (LPWSTR)L"置顶";
            SendMessageW(hwndTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

            ti.uId = (UINT_PTR)hwndBatchEditButton;
            ti.lpszText = (LPWSTR)L"批量编辑";
            SendMessageW(hwndTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

            ti.uId = (UINT_PTR)hwndDarkmodeButton;
            ti.lpszText = (LPWSTR)L"暗黑模式";
            SendMessageW(hwndTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

            ti.uId = (UINT_PTR)g_hwndPageUpBtn;
            ti.lpszText = (LPWSTR)L"上一页";
            SendMessageW(hwndTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

            ti.uId = (UINT_PTR)g_hwndPageDownBtn;
            ti.lpszText = (LPWSTR)L"下一页";
            SendMessageW(hwndTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

            // 为收藏按钮添加Tooltip
            ti.uId = (UINT_PTR)g_hwndFilterFavorite;
            ti.lpszText = (LPWSTR)L"右击修改";
            SendMessageW(hwndTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

            // 创建标题栏按钮
            g_hwndTitleTopmost = CreateWindowExW(0, L"BUTTON", L"",
                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                          0, 0, 46, TITLEBAR_HEIGHT, hwnd, (HMENU)ID_TITLEBAR_TOPMOST, GetModuleHandleW(NULL), NULL);
            g_oldTitleTopmostProc = (WNDPROC)SetWindowLongPtrW(g_hwndTitleTopmost, GWLP_WNDPROC, (LONG_PTR)TitleTopmostBtnProc);

            g_hwndTitleMinimize = CreateWindowExW(0, L"BUTTON", L"",
                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                          0, 0, 46, TITLEBAR_HEIGHT, hwnd, (HMENU)ID_TITLEBAR_MINIMIZE, GetModuleHandleW(NULL), NULL);
            g_oldTitleMinimizeProc = (WNDPROC)SetWindowLongPtrW(g_hwndTitleMinimize, GWLP_WNDPROC, (LONG_PTR)TitleMinimizeBtnProc);

            g_hwndTitleMaximize = CreateWindowExW(0, L"BUTTON", L"",
                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                          0, 0, 46, TITLEBAR_HEIGHT, hwnd, (HMENU)ID_TITLEBAR_MAXIMIZE, GetModuleHandleW(NULL), NULL);
            g_oldTitleMaximizeProc = (WNDPROC)SetWindowLongPtrW(g_hwndTitleMaximize, GWLP_WNDPROC, (LONG_PTR)TitleMaximizeBtnProc);

            g_hwndTitleClose = CreateWindowExW(0, L"BUTTON", L"",
                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                          0, 0, 46, TITLEBAR_HEIGHT, hwnd, (HMENU)ID_TITLEBAR_CLOSE, GetModuleHandleW(NULL), NULL);
            g_oldTitleCloseProc = (WNDPROC)SetWindowLongPtrW(g_hwndTitleClose, GWLP_WNDPROC, (LONG_PTR)TitleCloseBtnProc);

            // 为标题栏按钮添加Tooltip
            ti.uId = (UINT_PTR)g_hwndTitleTopmost;
            ti.lpszText = (LPWSTR)L"置顶";
            SendMessageW(hwndTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

            ti.uId = (UINT_PTR)g_hwndTitleMinimize;
            ti.lpszText = (LPWSTR)L"最小化";
            SendMessageW(hwndTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

            ti.uId = (UINT_PTR)g_hwndTitleMaximize;
            ti.lpszText = (LPWSTR)L"最大化";
            SendMessageW(hwndTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

            ti.uId = (UINT_PTR)g_hwndTitleClose;
            ti.lpszText = (LPWSTR)L"关闭";
            SendMessageW(hwndTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

            // 为按钮设置字体
            SendMessageW(hwndTopmostButton, WM_SETFONT, (WPARAM)hUIFont, TRUE);
            SendMessageW(hwndBatchEditButton, WM_SETFONT, (WPARAM)hUIFont, TRUE);
            SendMessageW(hwndDarkmodeButton, WM_SETFONT, (WPARAM)hUIFont, TRUE);

            g_isStartupEnabled = CheckStartup();
            LoadTags();    // 加载标签列表
            LoadHistory();
            UpdateListBox();

            // 强制重新计算所有列表项的高度
            if (g_hwndListBox) {
                int itemCount = SendMessageW(g_hwndListBox, LB_GETCOUNT, 0, 0);
                // g_expandedItems 是 map，不需要 resize
                for (int i = 0; i < itemCount; i++) {
                    SendMessageW(g_hwndListBox, LB_SETITEMHEIGHT, i, 0);
                }
                InvalidateRect(g_hwndListBox, NULL, TRUE);
                UpdateWindow(g_hwndListBox);
            }

            AddClipboardFormatListener(hwnd);
            AddTrayIcon(hwnd);

            // 延迟刷新，确保窗口完全创建后再计算高度
            SetTimer(hwnd, 2, 200, NULL);  // 增加到200ms

            break;
        }
        case WM_SHOWWINDOW: {
            // 窗口显示时刷新列表项高度
            if (wParam == TRUE && g_hwndListBox) {
                int itemCount = SendMessageW(g_hwndListBox, LB_GETCOUNT, 0, 0);
                for (int i = 0; i < itemCount; i++) {
                    SendMessageW(g_hwndListBox, LB_SETITEMHEIGHT, i, 0);
                }
                InvalidateRect(g_hwndListBox, NULL, TRUE);
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        case WM_MOUSEWHEEL: {
            // 鼠标滚轮事件 - 转发给ListBox
            if (g_hwndListBox) {
                return SendMessageW(g_hwndListBox, message, wParam, lParam);
            }
            break;
        }
        // 添加WM_SIZE消息处理
        case WM_SIZE: {
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);

            int clientWidth = clientRect.right - clientRect.left;
            int clientHeight = clientRect.bottom - clientRect.top;

            // 标题栏按钮位置（从右到左）
            const int titleBtnWidth = 46;
            MoveWindow(g_hwndTitleClose, clientWidth - titleBtnWidth, 0, titleBtnWidth, TITLEBAR_HEIGHT, TRUE);
            MoveWindow(g_hwndTitleMaximize, clientWidth - titleBtnWidth * 2, 0, titleBtnWidth, TITLEBAR_HEIGHT, TRUE);
            MoveWindow(g_hwndTitleMinimize, clientWidth - titleBtnWidth * 3, 0, titleBtnWidth, TITLEBAR_HEIGHT, TRUE);
            MoveWindow(g_hwndTitleTopmost, clientWidth - titleBtnWidth * 4, 0, titleBtnWidth, TITLEBAR_HEIGHT, TRUE);

            // 边距
            const int margin = 10;
            // 内容区域起始Y（标题栏下方）
            const int contentTop = TITLEBAR_HEIGHT;
            // 搜索栏高度（包含边框）
            const int searchHeight = 33;
            // 标签页高度
            const int tabHeight = 30;

            // 调整搜索栏（留出边框空间）
            const int borderPadding = 5;
            MoveWindow(g_hwndSearchBox, margin + borderPadding, contentTop + margin + borderPadding,
                      clientWidth - margin * 2 - borderPadding * 2,
                      searchHeight - borderPadding * 2, TRUE);

            // 设置搜索框文本区域以实现垂直居中
            {
                RECT rcEdit;
                GetClientRect(g_hwndSearchBox, &rcEdit);
                int editHeight = rcEdit.bottom - rcEdit.top;
                int fontHeight = g_fontSize + 3;
                int topMargin = (editHeight - fontHeight) / 2;
                if (topMargin < 0) topMargin = 0;
                rcEdit.left = 4;  // 与光标初始位置偏移一致
                rcEdit.top = topMargin;
                rcEdit.right -= 1;
                rcEdit.bottom = rcEdit.top + fontHeight + 4;
                SendMessageW(g_hwndSearchBox, EM_SETRECT, 0, (LPARAM)&rcEdit);
            }

            // 调整筛选按钮位置
            const int filterBtnWidth = 75;
            const int filterBtnSpacing = 5;
            int filterY = contentTop + margin + searchHeight + margin;
            MoveWindow(g_hwndFilterAll, margin, filterY, filterBtnWidth, tabHeight, TRUE);
            MoveWindow(g_hwndFilterText, margin + (filterBtnWidth + filterBtnSpacing), filterY, filterBtnWidth, tabHeight, TRUE);
            MoveWindow(g_hwndFilterImage, margin + (filterBtnWidth + filterBtnSpacing) * 2, filterY, filterBtnWidth, tabHeight, TRUE);
            MoveWindow(g_hwndFilterFile, margin + (filterBtnWidth + filterBtnSpacing) * 3, filterY, filterBtnWidth, tabHeight, TRUE);
            MoveWindow(g_hwndFilterFavorite, margin + (filterBtnWidth + filterBtnSpacing) * 4, filterY, filterBtnWidth, tabHeight, TRUE);

            // 调整列表框大小（右侧留出按钮空间）
            const int iconBtnSize = 32;  // 图标按钮大小
            const int iconBtnSpacing = 5; // 按钮间距
            int listBoxTop = contentTop + margin + searchHeight + margin + tabHeight;
            MoveWindow(g_hwndListBox, margin, listBoxTop,
                      clientWidth - margin * 2 - iconBtnSize - margin,
                      clientHeight - listBoxTop - margin,
                      TRUE);

            // 右侧垂直排列图标按钮（批量编辑在最上边）
            int btnX = clientWidth - margin - iconBtnSize;
            int btnY = listBoxTop;

            MoveWindow(GetDlgItem(hwnd, ID_BATCH_EDIT_BUTTON),
                      btnX, btnY, iconBtnSize, iconBtnSize, TRUE);

            MoveWindow(GetDlgItem(hwnd, ID_DARKMODE_BUTTON),
                      btnX, btnY + (iconBtnSize + iconBtnSpacing), iconBtnSize, iconBtnSize, TRUE);

            // 翻页按钮位置（在列表框右侧，垂直居中）
            int listBoxHeight = clientHeight - listBoxTop - margin;
            int listBoxCenterY = listBoxTop + listBoxHeight / 2;
            int pageUpY = listBoxCenterY - iconBtnSize;   // 中心线上方
            int pageDownY = listBoxCenterY;               // 中心线下方

            MoveWindow(g_hwndPageUpBtn, btnX, pageUpY, iconBtnSize, iconBtnSize, TRUE);
            MoveWindow(g_hwndPageDownBtn, btnX, pageDownY, iconBtnSize, iconBtnSize, TRUE);

            // 窗口大小改变后，重新计算所有列表项高度
            if (g_hwndListBox) {
                int itemCount = SendMessageW(g_hwndListBox, LB_GETCOUNT, 0, 0);
                for (int i = 0; i < itemCount; i++) {
                    SendMessageW(g_hwndListBox, LB_SETITEMHEIGHT, i, 0);
                }
                InvalidateRect(g_hwndListBox, NULL, TRUE);
            }

            break;
        }
        case WM_GETMINMAXINFO: {
            // 设置窗口最小尺寸
            LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
            lpMMI->ptMinTrackSize.x = 600;  // 最小宽度
            lpMMI->ptMinTrackSize.y = 700;  // 最小高度
            return 0;
        }
        case WM_MEASUREITEM: {
            LPMEASUREITEMSTRUCT lpMIS = (LPMEASUREITEMSTRUCT)lParam;
            if (lpMIS->CtlID == ID_LISTBOX) {
                // 动态获取列表框宽度
                RECT rcListBox;
                GetClientRect(g_hwndListBox, &rcListBox);
                int listBoxWidth = rcListBox.right - rcListBox.left - 20; // 减去左右边距
                if (listBoxWidth < 100) listBoxWidth = 560; // 初始化时的默认值

                // 获取列表项对应的实际数据
                if (lpMIS->itemID != (UINT)-1 && lpMIS->itemID < g_displayIndexMap.size()) {
                    int actualIndex = g_displayIndexMap[lpMIS->itemID];
                    if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
                        const ClipboardItem& item = g_history[actualIndex];

                        if (item.type == TYPE_IMAGE) {
                            // 检查图片文件是否存在（仅对图片文件类型，非截图）
                            bool imageFileExists = true;
                            if (!item.imageFilePath.empty()) {
                                DWORD attrs = GetFileAttributesW(item.imageFilePath.c_str());
                                imageFileExists = (attrs != INVALID_FILE_ATTRIBUTES);
                            }

                            if (!imageFileExists) {
                                // 图片文件不存在：使用一行高度
                                lpMIS->itemHeight = 57;
                            } else {
                                // 图片类型：计算缩放后的高度
                                int availableWidth = listBoxWidth - 20; // 减去左右边距
                                float scale = (float)availableWidth / item.imageWidth;

                                // 限制最大显示高度为150像素
                                if (scale * item.imageHeight > 150) {
                                    scale = 150.0f / item.imageHeight;
                                }

                                int displayHeight = (int)(item.imageHeight * scale);

                                // 标题(25) + 图片高度 + 尺寸信息(20) + 底部边距(10)
                                lpMIS->itemHeight = 25 + displayHeight + 20 + 10;
                            }
                        } else {
                            // 文本或文件类型：固定高度，一行显示
                            // 顶部边距(2) + 标题(20) + 一行文本(22) + 底部边距(13)
                            lpMIS->itemHeight = 57;
                        }
                    } else {
                        lpMIS->itemHeight = 87;
                    }
                } else {
                    lpMIS->itemHeight = 87;
                }
            }
            return TRUE;
        }
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT lpDIS = (LPDRAWITEMSTRUCT)lParam;

            // 处理筛选按钮绘制
            if (lpDIS->CtlID >= ID_FILTER_ALL && lpDIS->CtlID <= ID_FILTER_FAVORITE) {
                HDC hdc = lpDIS->hDC;
                RECT rc = lpDIS->rcItem;

                // 判断是否选中
                int filterIndex = lpDIS->CtlID - ID_FILTER_ALL;
                bool isSelected = (filterIndex == g_currentTab);

                // 设置背景色：选中为白色，未选中为窗口背景色（支持暗黑模式）
                COLORREF bgColor = isSelected ? GetWhiteColor() : GetBgColor();
                HBRUSH hBrush = CreateSolidBrush(bgColor);
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);

                // 获取按钮文本
                wchar_t text[64];
                GetWindowTextW(lpDIS->hwndItem, text, 64);

                // 设置文本颜色和背景（支持暗黑模式）
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, isSelected ? GetTextColor() : RGB(128, 128, 128));

                // 根据按钮ID选择图标（使用Segoe MDL2 Assets字体）
                const wchar_t* icon = L"";
                switch (lpDIS->CtlID) {
                    case ID_FILTER_ALL:      icon = L"\uE8FD"; break; // List
                    case ID_FILTER_TEXT:     icon = L"\uE8D2"; break; // Font
                    case ID_FILTER_IMAGE:    icon = L"\uEB9F"; break; // Photo
                    case ID_FILTER_FILE:     icon = L"\uE8B7"; break; // Document
                    case ID_FILTER_FAVORITE: icon = L"\uE734"; break; // FavoriteStar
                }

                // 创建图标字体
                HFONT hIconFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");

                // 计算图标和文本的总宽度
                SIZE iconSize, textSize;
                SelectObject(hdc, hIconFont);
                GetTextExtentPoint32W(hdc, icon, 1, &iconSize);

                HFONT hFont = (HFONT)SendMessageW(lpDIS->hwndItem, WM_GETFONT, 0, 0);
                SelectObject(hdc, hFont);
                GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &textSize);

                int totalWidth = iconSize.cx + 4 + textSize.cx; // 4px间距
                int startX = rc.left + (rc.right - rc.left - totalWidth) / 2;
                int centerY = rc.top + (rc.bottom - rc.top) / 2;

                // 绘制图标
                SelectObject(hdc, hIconFont);
                TextOutW(hdc, startX, centerY - iconSize.cy / 2, icon, 1);

                // 绘制文本
                SelectObject(hdc, hFont);
                TextOutW(hdc, startX + iconSize.cx + 4, centerY - textSize.cy / 2, text, (int)wcslen(text));

                DeleteObject(hIconFont);
                return TRUE;
            }

            // 处理功能按钮绘制（置顶、批量编辑、暗黑）
            if (lpDIS->CtlID == ID_BATCH_EDIT_BUTTON ||
                lpDIS->CtlID == ID_TOPMOST_BUTTON || lpDIS->CtlID == ID_DARKMODE_BUTTON) {
                HDC hdc = lpDIS->hDC;
                RECT rc = lpDIS->rcItem;

                // 设置背景色
                COLORREF bgColor = RGB(245, 245, 245);
                HBRUSH hBrush = CreateSolidBrush(bgColor);
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);

                // 置顶按钮：使用图片绘制（带波浪动画）
                if (lpDIS->CtlID == ID_TOPMOST_BUTTON) {
                    Gdiplus::Graphics graphics(hdc);
                    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

                    int btnW = rc.right - rc.left;
                    int btnH = rc.bottom - rc.top;

                    // 动画进行中：绘制两张图片的混合效果
                    if (g_topmostAnimating) {
                        Gdiplus::Image* imgFrom = g_topmostAnimDirection ? g_imgTopmostUnselected : g_imgTopmostSelected;
                        Gdiplus::Image* imgTo = g_topmostAnimDirection ? g_imgTopmostSelected : g_imgTopmostUnselected;

                        if (imgFrom && imgTo && imgFrom->GetLastStatus() == Gdiplus::Ok && imgTo->GetLastStatus() == Gdiplus::Ok) {
                            int imgW = imgFrom->GetWidth();
                            int imgH = imgFrom->GetHeight();
                            float scale = std::min((float)btnW / imgW, (float)btnH / imgH);
                            int drawW = (int)(imgW * scale);
                            int drawH = (int)(imgH * scale);
                            int x = rc.left + (btnW - drawW) / 2;
                            int y = rc.top + (btnH - drawH) / 2;

                            // 先绘制底层图片（原状态）
                            graphics.DrawImage(imgFrom, x, y, drawW, drawH);

                            // 计算波浪半径（从左上角扩散）
                            float maxRadius = sqrtf((float)(drawW * drawW + drawH * drawH));
                            float currentRadius = maxRadius * g_topmostAnimProgress;

                            // 创建圆形裁剪区域（波浪效果）
                            Gdiplus::GraphicsPath clipPath;
                            // 选中时波浪中心在左上角，取消选中时波浪中心在右下角
                            float centerX, centerY;
                            if (g_topmostAnimDirection) {
                                // 选中：从左上角扩散
                                centerX = (float)x;
                                centerY = (float)y;
                            } else {
                                // 取消选中：从右下角扩散
                                centerX = (float)(x + drawW);
                                centerY = (float)(y + drawH);
                            }
                            clipPath.AddEllipse(
                                centerX - currentRadius,
                                centerY - currentRadius,
                                currentRadius * 2,
                                currentRadius * 2
                            );

                            graphics.SetClip(&clipPath);
                            graphics.DrawImage(imgTo, x, y, drawW, drawH);
                            graphics.ResetClip();
                        }
                    } else {
                        // 非动画状态：正常绘制
                        Gdiplus::Image* img = g_isTopmost ? g_imgTopmostSelected : g_imgTopmostUnselected;
                        if (img && img->GetLastStatus() == Gdiplus::Ok) {
                            int imgW = img->GetWidth();
                            int imgH = img->GetHeight();
                            float scale = std::min((float)btnW / imgW, (float)btnH / imgH);
                            int drawW = (int)(imgW * scale);
                            int drawH = (int)(imgH * scale);
                            int x = rc.left + (btnW - drawW) / 2;
                            int y = rc.top + (btnH - drawH) / 2;
                            graphics.DrawImage(img, x, y, drawW, drawH);
                        }
                    }
                    return TRUE;
                }

                // 批量编辑按钮：使用图片绘制（带波浪动画）
                if (lpDIS->CtlID == ID_BATCH_EDIT_BUTTON) {
                    Gdiplus::Graphics graphics(hdc);
                    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

                    int btnW = rc.right - rc.left;
                    int btnH = rc.bottom - rc.top;

                    if (g_batchEditAnimating) {
                        Gdiplus::Image* imgFrom = g_batchEditAnimDirection ? g_imgBatchEditUnselected : g_imgBatchEditSelected;
                        Gdiplus::Image* imgTo = g_batchEditAnimDirection ? g_imgBatchEditSelected : g_imgBatchEditUnselected;

                        if (imgFrom && imgTo && imgFrom->GetLastStatus() == Gdiplus::Ok && imgTo->GetLastStatus() == Gdiplus::Ok) {
                            int imgW = imgFrom->GetWidth();
                            int imgH = imgFrom->GetHeight();
                            float scale = std::min((float)btnW / imgW, (float)btnH / imgH);
                            int drawW = (int)(imgW * scale);
                            int drawH = (int)(imgH * scale);
                            int x = rc.left + (btnW - drawW) / 2;
                            int y = rc.top + (btnH - drawH) / 2;

                            graphics.DrawImage(imgFrom, x, y, drawW, drawH);

                            float maxRadius = sqrtf((float)(drawW * drawW + drawH * drawH));
                            float currentRadius = maxRadius * g_batchEditAnimProgress;

                            Gdiplus::GraphicsPath clipPath;
                            // 选中时波浪中心在左上角，取消选中时波浪中心在右下角
                            float centerX, centerY;
                            if (g_batchEditAnimDirection) {
                                // 选中：从左上角扩散
                                centerX = (float)x;
                                centerY = (float)y;
                            } else {
                                // 取消选中：从右下角扩散
                                centerX = (float)(x + drawW);
                                centerY = (float)(y + drawH);
                            }
                            clipPath.AddEllipse(
                                centerX - currentRadius,
                                centerY - currentRadius,
                                currentRadius * 2,
                                currentRadius * 2
                            );

                            graphics.SetClip(&clipPath);
                            graphics.DrawImage(imgTo, x, y, drawW, drawH);
                            graphics.ResetClip();
                        }
                    } else {
                        Gdiplus::Image* img = g_isBatchEditMode ? g_imgBatchEditSelected : g_imgBatchEditUnselected;
                        if (img && img->GetLastStatus() == Gdiplus::Ok) {
                            int imgW = img->GetWidth();
                            int imgH = img->GetHeight();
                            float scale = std::min((float)btnW / imgW, (float)btnH / imgH);
                            int drawW = (int)(imgW * scale);
                            int drawH = (int)(imgH * scale);
                            int x = rc.left + (btnW - drawW) / 2;
                            int y = rc.top + (btnH - drawH) / 2;
                            graphics.DrawImage(img, x, y, drawW, drawH);
                        }
                    }
                    return TRUE;
                }

                // 暗黑模式按钮：暂时保留原有绘制
                if (lpDIS->CtlID == ID_DARKMODE_BUTTON) {
                    HFONT hIconFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(80, 80, 80));
                    HFONT hOldFont = (HFONT)SelectObject(hdc, hIconFont);
                    DrawTextW(hdc, L"\uE708", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, hOldFont);
                    DeleteObject(hIconFont);
                    return TRUE;
                }

                return TRUE;
            }

            // 处理翻页按钮绘制
            if (lpDIS->CtlID == ID_PAGE_UP_BTN || lpDIS->CtlID == ID_PAGE_DOWN_BTN) {
                HDC hdc = lpDIS->hDC;
                RECT rc = lpDIS->rcItem;

                bool isPageUp = (lpDIS->CtlID == ID_PAGE_UP_BTN);
                // 上一页禁用条件：已在顶部
                // 下一页禁用条件：当前可见区域已能完整显示到最后一项
                bool isDisabled;
                if (isPageUp) {
                    isDisabled = (g_listBoxTopIndex <= 0);
                } else {
                    // 使用新的计算方式：检查是否还有未完整显示的项目
                    int nextPageIndex = CalculateNextPageIndex(g_listBoxTopIndex);
                    isDisabled = (nextPageIndex >= (int)g_displayIndexMap.size() || nextPageIndex <= g_listBoxTopIndex);
                }
                bool isHover = isPageUp ? g_isPageUpBtnHover : g_isPageDownBtnHover;

                // 设置背景色（支持暗黑模式）
                COLORREF bgColor = GetBgColor();
                HBRUSH hBrush = CreateSolidBrush(bgColor);
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);

                // 图标字体
                HFONT hIconFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
                SetBkMode(hdc, TRANSPARENT);

                // 图标颜色：禁用时灰色，悬浮时蓝色，正常时文字色
                COLORREF iconColor;
                if (isDisabled) {
                    iconColor = RGB(180, 180, 180);
                } else if (isHover) {
                    iconColor = RGB(0, 120, 215);
                } else {
                    iconColor = GetTextColor();
                }
                SetTextColor(hdc, iconColor);

                // 绘制图标：上箭头 \uE70E，下箭头 \uE70D
                const wchar_t* icon = isPageUp ? L"\uE70E" : L"\uE70D";
                HFONT hOldFont = (HFONT)SelectObject(hdc, hIconFont);
                DrawTextW(hdc, icon, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, hOldFont);
                DeleteObject(hIconFont);

                return TRUE;
            }

            // 处理标题栏按钮绘制
            if (lpDIS->CtlID == ID_TITLEBAR_TOPMOST || lpDIS->CtlID == ID_TITLEBAR_MINIMIZE ||
                lpDIS->CtlID == ID_TITLEBAR_MAXIMIZE || lpDIS->CtlID == ID_TITLEBAR_CLOSE) {
                HDC hdc = lpDIS->hDC;
                RECT rc = lpDIS->rcItem;

                bool isHover = false;
                const wchar_t* icon = L"";
                COLORREF hoverBgColor = RGB(229, 229, 229);  // 默认悬浮背景色

                switch (lpDIS->CtlID) {
                    case ID_TITLEBAR_TOPMOST:
                        isHover = g_isTitleTopmostHover;
                        icon = g_isTopmost ? L"\uE840" : L"\uE718";  // Pinned / Pin
                        break;
                    case ID_TITLEBAR_MINIMIZE:
                        isHover = g_isTitleMinimizeHover;
                        icon = L"\uE921";  // ChromeMinimize
                        break;
                    case ID_TITLEBAR_MAXIMIZE:
                        isHover = g_isTitleMaximizeHover;
                        icon = IsZoomed(GetParent(lpDIS->hwndItem)) ? L"\uE923" : L"\uE922";  // ChromeRestore / ChromeMaximize
                        break;
                    case ID_TITLEBAR_CLOSE:
                        isHover = g_isTitleCloseHover;
                        icon = L"\uE8BB";  // ChromeClose
                        hoverBgColor = RGB(232, 17, 35);  // 关闭按钮悬浮红色
                        break;
                }

                // 设置背景色
                COLORREF bgColor = GetBgColor();
                if (isHover) {
                    bgColor = hoverBgColor;
                }
                HBRUSH hBrush = CreateSolidBrush(bgColor);
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);

                // 图标字体
                HFONT hIconFont = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
                SetBkMode(hdc, TRANSPARENT);

                // 图标颜色
                COLORREF iconColor = GetTextColor();
                if (lpDIS->CtlID == ID_TITLEBAR_CLOSE && isHover) {
                    iconColor = RGB(255, 255, 255);
                }
                if (lpDIS->CtlID == ID_TITLEBAR_TOPMOST && g_isTopmost) {
                    iconColor = RGB(0, 120, 215);
                }
                if (lpDIS->CtlID == ID_TITLEBAR_MINIMIZE && g_isTopmost) {
                    iconColor = RGB(180, 180, 180);
                    isHover = false;
                }
                SetTextColor(hdc, iconColor);

                // 绘制图标
                HFONT hOldFont = (HFONT)SelectObject(hdc, hIconFont);
                DrawTextW(hdc, icon, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, hOldFont);
                DeleteObject(hIconFont);

                return TRUE;
            }

            if (lpDIS->CtlID == ID_LISTBOX) {
                // 获取设备上下文
                HDC hdc = lpDIS->hDC;
                RECT rcItem = lpDIS->rcItem;

                // 设置背景色和文字色（支持暗黑模式）
                COLORREF bgColor = GetWhiteColor();
                COLORREF textColor = GetTextColor();
                bool isSelected = (lpDIS->itemState & ODS_SELECTED) != 0;

                // 批量编辑模式下，只使用g_selectedItems来确定是否选中
                if (g_isBatchEditMode && lpDIS->itemID < (UINT)g_displayIndexMap.size()) {
                    int actualIndex = g_displayIndexMap[lpDIS->itemID];
                    isSelected = (std::find(g_selectedItems.begin(), g_selectedItems.end(), actualIndex) != g_selectedItems.end());
                }

                // 填充背景
                HBRUSH hBrush = CreateSolidBrush(bgColor);
                FillRect(hdc, &rcItem, hBrush);
                DeleteObject(hBrush);

                // 如果选中，绘制蓝色边框
                if (isSelected) {
                    HPEN hBorderPen = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
                    HPEN hOldPen = (HPEN)SelectObject(hdc, hBorderPen);
                    HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
                    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hNullBrush);

                    Rectangle(hdc, rcItem.left + 1, rcItem.top + 1, rcItem.right, rcItem.bottom - 9);

                    SelectObject(hdc, hOldPen);
                    SelectObject(hdc, hOldBrush);
                    DeleteObject(hBorderPen);
                }

                SetTextColor(hdc, textColor);

                // 绘制内容区域
                RECT rcContent = rcItem;
                rcContent.left += 10;   // 左边距
                rcContent.right -= 10;  // 右边距
                rcContent.top += 2;     // 顶部边距

                // 获取列表项对应的实际数据
                if (lpDIS->itemID != (UINT)-1 && lpDIS->itemID < g_displayIndexMap.size()) {
                    int actualIndex = g_displayIndexMap[lpDIS->itemID];
                    if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
                        const ClipboardItem& item = g_history[actualIndex];

                        // 创建字体
                        HFONT hFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
                        SetBkMode(hdc, TRANSPARENT);

                        // 绘制时间戳和来源应用图标（使用16px字体）
                        HFONT hHeaderFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                        HFONT hPrevFont = (HFONT)SelectObject(hdc, hHeaderFont);

                        // 绘制时间戳
                        std::wstring headerText = GetRelativeTimeString(item.timestamp) + L" -";
                        RECT rcHeader = rcContent;
                        rcHeader.bottom = rcHeader.top + 18;
                        SetTextColor(hdc, RGB(148, 149, 148));  // #949594
                        DrawTextW(hdc, headerText.c_str(), -1, &rcHeader, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

                        // 计算时间文本宽度，在其后绘制图标
                        SIZE textSize;
                        GetTextExtentPoint32W(hdc, headerText.c_str(), (int)headerText.length(), &textSize);

                        // 绘制来源应用图标（在时间后面，灰度化 + 缩小尺寸 12x12）
                        // 鼠标悬浮时显示彩色图标
                        HICON hAppIcon = GetAppIcon(item.sourceAppPath);
                        if (hAppIcon != NULL) {
                            int iconX = rcContent.left + textSize.cx + 4;  // 4px 间距
                            int iconY = rcContent.top + 2;  // 垂直居中微调
                            int iconSize = 12;  // 缩小到 12x12

                            // 检查是否鼠标悬浮在此图标上
                            bool isIconHovered = (g_isHoveringIcon && g_hoverIconIndex == (int)lpDIS->itemID);

                            if (isIconHovered) {
                                // 悬浮时直接绘制彩色图标
                                DrawIconEx(hdc, iconX, iconY, hAppIcon, iconSize, iconSize, 0, NULL, DI_NORMAL);
                            } else {
                                // 非悬浮时绘制灰度图标
                                // 创建内存 DC 用于灰度化处理
                                HDC hdcMem = CreateCompatibleDC(hdc);
                                HBITMAP hBitmap = CreateCompatibleBitmap(hdc, iconSize, iconSize);
                                HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

                                // 填充背景色（支持暗黑模式）
                                RECT rcIcon = {0, 0, iconSize, iconSize};
                                HBRUSH hWhiteBrush = CreateSolidBrush(GetWhiteColor());
                                FillRect(hdcMem, &rcIcon, hWhiteBrush);
                                DeleteObject(hWhiteBrush);

                                // 绘制图标到内存 DC
                                DrawIconEx(hdcMem, 0, 0, hAppIcon, iconSize, iconSize, 0, NULL, DI_NORMAL);

                                // 灰度化处理
                                for (int y = 0; y < iconSize; y++) {
                                    for (int x = 0; x < iconSize; x++) {
                                        COLORREF color = GetPixel(hdcMem, x, y);
                                        int r = GetRValue(color);
                                        int g = GetGValue(color);
                                        int b = GetBValue(color);
                                        // 计算灰度值（加权平均）
                                        int gray = (int)(0.299 * r + 0.587 * g + 0.114 * b);
                                        // 稍微提亮，使其更柔和
                                        gray = gray + 40;
                                        if (gray > 255) gray = 255;
                                        SetPixel(hdcMem, x, y, RGB(gray, gray, gray));
                                    }
                                }

                                // 将灰度化后的图标绘制到目标 DC
                                BitBlt(hdc, iconX, iconY, iconSize, iconSize, hdcMem, 0, 0, SRCCOPY);

                                SelectObject(hdcMem, hOldBitmap);
                                DeleteObject(hBitmap);
                                DeleteDC(hdcMem);
                            }
                        }

                        // 在标题行右侧绘制快捷键提示（一直显示，位置随滚动更新）
                        if (g_isQuickPasteEnabled) {
                            // 使用缓存的顶部索引（在滚动事件中更新，避免频繁调用LB_GETTOPINDEX）
                            int visibleIndex = (int)lpDIS->itemID - g_listBoxTopIndex;  // 相对于可见区域的索引
                            if (visibleIndex >= 0 && visibleIndex < 9) {
                                // 检查当前项目是否能完整显示在可视区域内
                                int visibleItemCount = CalculateVisibleItemCount(g_listBoxTopIndex);
                                if (visibleIndex < visibleItemCount) {
                                    std::wstring shortcutText = GetQuickPasteModifierText() + std::to_wstring(visibleIndex + 1);
                                    RECT rcShortcut = rcHeader;
                                    SetTextColor(hdc, RGB(100, 149, 237));  // 淡蓝色
                                    DrawTextW(hdc, shortcutText.c_str(), -1, &rcShortcut, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
                                }
                            }
                        }

                        // 在快捷键下方绘制分类标签（所有标签页都显示）
                        if (!item.tagIds.empty()) {
                            HFONT hTagFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                            HFONT hPrevTagFont = (HFONT)SelectObject(hdc, hTagFont);

                            // 计算标签区域的中心位置（在快捷键提示和时间文本之间）
                            int timeTextWidth = 100;  // 时间文本大致宽度
                            int shortcutWidth = 80;  // 快捷键提示大致宽度
                            int availableWidth = rcHeader.right - rcHeader.left - timeTextWidth - shortcutWidth;
                            int startX = rcHeader.left + timeTextWidth + availableWidth / 2;
                            
                            // 先计算所有标签的总宽度
                            int totalTagWidth = 0;
                            for (auto it = item.tagIds.begin(); it != item.tagIds.end(); ++it) {
                                Tag* tag = GetTagById(*it);
                                if (!tag) continue;
                                SIZE tagTextSize;
                                GetTextExtentPoint32W(hdc, tag->name.c_str(), (int)tag->name.length(), &tagTextSize);
                                totalTagWidth += tagTextSize.cx + 14;  // 标签宽度 + 内边距 + 间距
                            }
                            
                            // 从中心位置开始排列标签
                            int tagX = startX - totalTagWidth / 2;
                            
                            for (auto it = item.tagIds.begin(); it != item.tagIds.end(); ++it) {
                                Tag* tag = GetTagById(*it);
                                if (!tag) continue;

                                SIZE tagTextSize;
                                GetTextExtentPoint32W(hdc, tag->name.c_str(), (int)tag->name.length(), &tagTextSize);
                                int tagPadH = 5;  // 水平内边距
                                int tagPadV = 1;  // 垂直内边距
                                int tagWidth = tagTextSize.cx + tagPadH * 2;
                                int tagHeight = tagTextSize.cy + tagPadV * 2;
                                int tagY = rcHeader.top + (18 - tagHeight) / 2;  // 垂直居中

                                if (tagX < rcHeader.left + timeTextWidth || tagX + tagWidth > rcHeader.right - shortcutWidth) {
                                    break;  // 防止超出边界
                                }

                                // 绘制圆角背景
                                HBRUSH hTagBrush = CreateSolidBrush(tag->color);
                                HBRUSH hOldTagBrush = (HBRUSH)SelectObject(hdc, hTagBrush);
                                HPEN hTagPen = CreatePen(PS_SOLID, 1, tag->color);
                                HPEN hOldTagPen = (HPEN)SelectObject(hdc, hTagPen);
                                RoundRect(hdc, tagX, tagY, tagX + tagWidth, tagY + tagHeight, 6, 6);
                                SelectObject(hdc, hOldTagBrush);
                                SelectObject(hdc, hOldTagPen);
                                DeleteObject(hTagBrush);
                                DeleteObject(hTagPen);

                                // 绘制白色文字
                                SetTextColor(hdc, RGB(255, 255, 255));
                                RECT rcTagText = {tagX, tagY, tagX + tagWidth, tagY + tagHeight};
                                DrawTextW(hdc, tag->name.c_str(), -1, &rcTagText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                                tagX += tagWidth + 4;  // 标签间距
                            }

                            SelectObject(hdc, hPrevTagFont);
                            DeleteObject(hTagFont);
                        }

                        SelectObject(hdc, hPrevFont);
                        DeleteObject(hHeaderFont);

                        // 恢复主体内容颜色（支持暗黑模式）
                        SetTextColor(hdc, GetTextColor());

                        // 调整内容区域（在标题下方）
                        rcContent.top += 20;

                        if (item.type == TYPE_IMAGE) {
                            // 检查图片文件是否存在（仅对图片文件类型，非截图）
                            bool imageFileExists = true;
                            if (!item.imageFilePath.empty()) {
                                DWORD attrs = GetFileAttributesW(item.imageFilePath.c_str());
                                imageFileExists = (attrs != INVALID_FILE_ATTRIBUTES);

                                // 文件不存在时，清除缩略图数据
                                if (!imageFileExists && !g_history[actualIndex].imageData.empty()) {
                                    g_history[actualIndex].imageData.clear();
                                    g_history[actualIndex].thumbWidth = 0;
                                    g_history[actualIndex].thumbHeight = 0;
                                    SaveHistory();  // 保存更新后的历史记录
                                }
                            }

                            // 图片文件不存在的情况
                            if (!imageFileExists) {
                                // 显示 noexist.png 图标和浅色文件名
                                if (g_imgNoExistIcon) {
                                    Gdiplus::Graphics graphics(hdc);
                                    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                                    graphics.DrawImage(g_imgNoExistIcon, rcContent.left, rcContent.top + 1, 18, 18);
                                }

                                // 获取文件名
                                std::wstring fileName = item.imageFilePath;
                                size_t lastSlash = fileName.find_last_of(L"\\/");
                                if (lastSlash != std::wstring::npos) {
                                    fileName = fileName.substr(lastSlash + 1);
                                }

                                // 使用浅色字体绘制文件名
                                RECT rcText = rcContent;
                                rcText.left += 22;
                                rcText.bottom = rcText.top + 22;
                                SetTextColor(hdc, RGB(180, 180, 180));
                                DrawTextW(hdc, fileName.c_str(), -1, &rcText,
                                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                            }
                            // 检查图片预览设置
                            else if (g_imagePreviewQuality == PREVIEW_OFF) {
                                // 关闭预览模式：只显示文件名和尺寸信息
                                HFONT hTextFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_fontName.c_str());
                                HFONT hPrevTextFont = (HFONT)SelectObject(hdc, hTextFont);

                                // 显示图片图标和文件名
                                std::wstring displayText = L"\U0001F5BC ";  // 图片图标
                                if (!item.imageFileName.empty()) {
                                    displayText += item.imageFileName;
                                } else {
                                    displayText += L"图片";
                                }
                                displayText += L"  [" + std::to_wstring(item.imageWidth) + L"x" + std::to_wstring(item.imageHeight) + L"]";

                                SetTextColor(hdc, GetTextColor());
                                DrawTextW(hdc, displayText.c_str(), -1, &rcContent, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                                SelectObject(hdc, hPrevTextFont);
                                DeleteObject(hTextFont);
                            } else {
                                // 绘制图片（使用缩略图数据）
                                int availableWidth = rcContent.right - rcContent.left;
                                int availableHeight = rcItem.bottom - rcContent.top - 10; // 留出底部边距

                                // 使用缩略图尺寸进行显示计算（如果有缩略图）
                                int srcWidth = item.thumbWidth > 0 ? item.thumbWidth : item.imageWidth;
                                int srcHeight = item.thumbHeight > 0 ? item.thumbHeight : item.imageHeight;

                                // 计算缩放比例以适应显示区域
                                float scaleX = (float)availableWidth / srcWidth;
                                float scaleY = (float)availableHeight / srcHeight;
                                float scale = (scaleX < scaleY ? scaleX : scaleY);

                                // 限制最大显示高度为150像素
                                if (scale * srcHeight > 150) {
                                    scale = 150.0f / srcHeight;
                                }

                                int displayWidth = (int)(srcWidth * scale);
                                int displayHeight = (int)(srcHeight * scale);

                                // 居中显示
                                int x = rcContent.left + (availableWidth - displayWidth) / 2;
                                int y = rcContent.top;

                                // 创建位图并绘制（使用缩略图尺寸）
                                BITMAPINFO bmi = {};
                                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                                bmi.bmiHeader.biWidth = srcWidth;
                                bmi.bmiHeader.biHeight = -srcHeight; // 负值表示从上到下
                                bmi.bmiHeader.biPlanes = 1;
                                bmi.bmiHeader.biBitCount = 24;
                                bmi.bmiHeader.biCompression = BI_RGB;

                                SetStretchBltMode(hdc, HALFTONE);
                                StretchDIBits(hdc, x, y, displayWidth, displayHeight,
                                             0, 0, srcWidth, srcHeight,
                                             &item.imageData[0], &bmi, DIB_RGB_COLORS, SRCCOPY);

                                // 绘制图片尺寸信息（16px字体）
                                HFONT hSizeFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                                HFONT hPrevSizeFont = (HFONT)SelectObject(hdc, hSizeFont);

                                std::wstring sizeText = L"[" + std::to_wstring(item.imageWidth) + L"x" + std::to_wstring(item.imageHeight) + L"]";
                                RECT rcSize = rcContent;
                                rcSize.top = y + displayHeight + 5;
                                SetTextColor(hdc, RGB(128, 128, 128));
                                DrawTextW(hdc, sizeText.c_str(), -1, &rcSize, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

                                SelectObject(hdc, hPrevSizeFont);
                                DeleteObject(hSizeFont);
                            }

                        } else {
                            // 绘制文本或文件路径（单行显示，超出部分显示省略号）
                            int textLen = SendMessageW(lpDIS->hwndItem, LB_GETTEXTLEN, lpDIS->itemID, 0);
                            if (textLen > 0) {
                                std::vector<wchar_t> buffer(textLen + 1);
                                SendMessageW(lpDIS->hwndItem, LB_GETTEXT, lpDIS->itemID, (LPARAM)&buffer[0]);
                                std::wstring text = &buffer[0];

                                // 移除标题部分（时间戳和来源应用）
                                size_t pos = text.find(L"\r\n");
                                if (pos != std::wstring::npos) {
                                    text = text.substr(pos + 2);
                                }

                                // 替换换行符为空格，确保单行显示
                                for (size_t i = 0; i < text.length(); i++) {
                                    if (text[i] == L'\r' || text[i] == L'\n') {
                                        text[i] = L' ';
                                    }
                                }

                                RECT rcText = rcContent;
                                rcText.bottom = rcText.top + 22;

                                // 检查文件是否存在
                                bool fileExists = true;
                                std::wstring filePath;
                                if (item.type == TYPE_FILE) {
                                    filePath = item.content;
                                    DWORD attrs = GetFileAttributesW(filePath.c_str());
                                    fileExists = (attrs != INVALID_FILE_ATTRIBUTES);
                                } else if (item.type == TYPE_IMAGE && !item.imageFilePath.empty()) {
                                    filePath = item.imageFilePath;
                                    DWORD attrs = GetFileAttributesW(filePath.c_str());
                                    fileExists = (attrs != INVALID_FILE_ATTRIBUTES);
                                }

                                // 检查是否为文件夹类型
                                bool isFolder = false;
                                if (item.type == TYPE_FILE && fileExists) {
                                    DWORD attrs = GetFileAttributesW(item.content.c_str());
                                    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                                        isFolder = true;
                                    }
                                }

                                // 文件不存在的情况
                                if (!fileExists && (item.type == TYPE_FILE || (item.type == TYPE_IMAGE && !item.imageFilePath.empty()))) {
                                    // 显示 noexist.png 图标
                                    if (g_imgNoExistIcon) {
                                        Gdiplus::Graphics graphics(hdc);
                                        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                                        graphics.DrawImage(g_imgNoExistIcon, rcText.left, rcText.top + 1, 18, 18);
                                    }

                                    // 调整文本位置（图标后面）
                                    rcText.left += 22;

                                    // 获取文件名
                                    std::wstring fileName = filePath;
                                    size_t lastSlash = fileName.find_last_of(L"\\/");
                                    if (lastSlash != std::wstring::npos) {
                                        fileName = fileName.substr(lastSlash + 1);
                                    }

                                    // 使用浅色字体绘制文件名
                                    SetTextColor(hdc, RGB(180, 180, 180));
                                    DrawTextW(hdc, fileName.c_str(), -1, &rcText,
                                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                                } else if (isFolder) {
                                    // 文件夹类型：显示文件夹图标（使用 PNG 图片）
                                    if (g_imgFolderIcon) {
                                        Gdiplus::Graphics graphics(hdc);
                                        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                                        graphics.DrawImage(g_imgFolderIcon, rcText.left, rcText.top + 1, 18, 18);
                                    }

                                    // 调整文本位置（图标后面）
                                    rcText.left += 22;

                                    // 检查是否悬浮在此文件夹上或正在动画
                                    bool isFolderHovered = (g_isHoveringFolder && g_hoverFolderIndex == (int)lpDIS->itemID);
                                    bool isExpandAnimating = (g_folderUnderlineAnimating && g_folderUnderlineAnimIndex == (int)lpDIS->itemID);
                                    bool isCollapseAnimating = (g_folderCollapseAnimating && g_folderCollapseAnimIndex == (int)lpDIS->itemID);

                                    // 计算动画进度
                                    float animProgress = 0.0f;
                                    if (isExpandAnimating) {
                                        // 正在展开动画中
                                        animProgress = g_folderUnderlineProgress;
                                    } else if (isCollapseAnimating) {
                                        // 正在收起动画中
                                        animProgress = g_folderCollapseProgress;
                                    } else if (isFolderHovered) {
                                        // 悬浮但不在动画中（动画已完成），显示完整下划线
                                        animProgress = 1.0f;
                                    }
                                    // 注意：不悬浮且不在动画中时，animProgress 保持为 0

                                    // 保持正常文本颜色（不变蓝色）
                                    SetTextColor(hdc, GetTextColor());

                                    // 先绘制普通文本（不带下划线）
                                    RECT rcPathText = rcText;
                                    rcPathText.right -= 20; // 预留箭头空间
                                    DrawTextW(hdc, text.c_str(), -1, &rcPathText,
                                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                                    // 如果有动画进度大于0，绘制下划线
                                    if (animProgress > 0.001f) {
                                        // 计算文本实际宽度
                                        SIZE textSize;
                                        GetTextExtentPoint32W(hdc, text.c_str(), (int)text.length(), &textSize);
                                        int textWidth = std::min((int)textSize.cx, (int)(rcPathText.right - rcPathText.left));

                                        // 计算下划线长度（根据动画进度，从第一个字开始到结尾）
                                        int underlineWidth = (int)(textWidth * animProgress);

                                        // 绘制下划线（从文本起始位置开始）
                                        HPEN hUnderlinePen = CreatePen(PS_SOLID, 1, GetTextColor());
                                        HPEN hOldPen = (HPEN)SelectObject(hdc, hUnderlinePen);

                                        int underlineY = rcText.top + 18; // 文字下方
                                        MoveToEx(hdc, rcText.left, underlineY, NULL);
                                        LineTo(hdc, rcText.left + underlineWidth, underlineY);

                                        SelectObject(hdc, hOldPen);
                                        DeleteObject(hUnderlinePen);
                                    }
                                } else {
                                    // 普通文本或文件，检查是否为链接并应用颜色动画
                                    bool isLink = IsLinkText(text);
                                    bool isLinkHovered = isLink && (g_isHoveringLink && g_hoverLinkIndex == (int)lpDIS->itemID);
                                    bool isLinkExpandAnim = isLink && (g_linkColorExpandAnimating && g_linkColorExpandAnimIndex == (int)lpDIS->itemID);
                                    bool isLinkCollapseAnim = isLink && (g_linkColorCollapseAnimating && g_linkColorCollapseAnimIndex == (int)lpDIS->itemID);

                                    float linkAnimProgress = 0.0f;
                                    if (isLinkExpandAnim) {
                                        linkAnimProgress = g_linkColorExpandProgress;
                                    } else if (isLinkCollapseAnim) {
                                        linkAnimProgress = g_linkColorCollapseProgress;
                                    } else if (isLinkHovered) {
                                        linkAnimProgress = 1.0f;
                                    }

                                    if (isLink && linkAnimProgress > 0.001f && linkAnimProgress < 0.999f) {
                                        // 动画进行中：分两段绘制文字
                                        // 先计算文字实际宽度
                                        SIZE fullTextSize;
                                        GetTextExtentPoint32W(hdc, text.c_str(), (int)text.length(), &fullTextSize);
                                        int maxWidth = rcText.right - rcText.left;
                                        int textPixelWidth = std::min((int)fullTextSize.cx, maxWidth);

                                        // 计算分割点（像素位置）
                                        int splitX = rcText.left + (int)(textPixelWidth * linkAnimProgress);

                                        // 找到分割点对应的字符索引
                                        int splitCharIndex = 0;
                                        for (int ci = 1; ci <= (int)text.length(); ci++) {
                                            SIZE partSize;
                                            GetTextExtentPoint32W(hdc, text.c_str(), ci, &partSize);
                                            if (partSize.cx + rcText.left > splitX) {
                                                splitCharIndex = ci;
                                                break;
                                            }
                                            splitCharIndex = ci;
                                        }
                                        if (splitCharIndex <= 0) splitCharIndex = 1;
                                        if (splitCharIndex > (int)text.length()) splitCharIndex = (int)text.length();

                                        COLORREF blueColor = RGB(0, 102, 204);
                                        COLORREF origColor = GetTextColor();

                                        // 使用裁剪区域绘制两段不同颜色的文字
                                        HRGN hOldRgn = CreateRectRgn(0, 0, 0, 0);
                                        int hasOldRgn = GetClipRgn(hdc, hOldRgn);

                                        // 第一段：左侧蓝色部分
                                        HRGN hLeftRgn = CreateRectRgn(rcText.left, rcText.top, splitX, rcText.bottom);
                                        SelectClipRgn(hdc, hLeftRgn);
                                        SetTextColor(hdc, blueColor);
                                        DrawTextW(hdc, text.c_str(), -1, &rcText,
                                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                                        DeleteObject(hLeftRgn);

                                        // 第二段：右侧原色部分
                                        HRGN hRightRgn = CreateRectRgn(splitX, rcText.top, rcText.right, rcText.bottom);
                                        SelectClipRgn(hdc, hRightRgn);
                                        SetTextColor(hdc, origColor);
                                        DrawTextW(hdc, text.c_str(), -1, &rcText,
                                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                                        DeleteObject(hRightRgn);

                                        // 恢复裁剪区域
                                        if (hasOldRgn == 1) {
                                            SelectClipRgn(hdc, hOldRgn);
                                        } else {
                                            SelectClipRgn(hdc, NULL);
                                        }
                                        DeleteObject(hOldRgn);
                                    } else if (isLink && linkAnimProgress >= 0.999f) {
                                        // 动画完成或稳定悬浮：全蓝色
                                        SetTextColor(hdc, RGB(0, 102, 204));
                                        DrawTextW(hdc, text.c_str(), -1, &rcText,
                                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                                    } else {
                                        // 无动画：原色
                                        DrawTextW(hdc, text.c_str(), -1, &rcText,
                                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                                    }
                                }
                            }
                        }

                        SelectObject(hdc, hOldFont);
                        DeleteObject(hFont);
                    }
                }

                // 绘制底部虚线分割线（每条记录下边都有）
                HPEN hPen = CreatePen(PS_DOT, 1, RGB(200, 200, 200));
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

                MoveToEx(hdc, rcItem.left + 10, rcItem.bottom - 5, NULL);
                LineTo(hdc, rcItem.right - 10, rcItem.bottom - 5);

                SelectObject(hdc, hOldPen);
                DeleteObject(hPen);

                return TRUE;
            }
            return TRUE;
        }
        // 移除LBN_SELCHANGE事件中的按钮点击处理逻辑
        case WM_COMMAND: {
            WORD wNotifyCode = HIWORD(wParam);
            WORD wID = LOWORD(wParam);

            // 处理搜索框文本变化 - 实时搜索
            if (wID == ID_SEARCH_BOX && wNotifyCode == EN_CHANGE) {
                PerformSearch(hwnd);
            }

            // 处理列表框选择变化
            if (wID == ID_LISTBOX && wNotifyCode == LBN_SELCHANGE) {
                if (g_isBatchEditMode) {
                    // 检查Ctrl键状态
                    bool isCtrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    
                    int index = SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0);
                    if (index != LB_ERR && index < (int)g_displayIndexMap.size()) {
                        int actualIndex = g_displayIndexMap[index];
                        
                        if (isCtrlPressed) {
                            // Ctrl键按下，取消选择
                            auto it = std::find(g_selectedItems.begin(), g_selectedItems.end(), actualIndex);
                            if (it != g_selectedItems.end()) {
                                // 已选中，取消选择
                                g_selectedItems.erase(it);
                                // 立即重绘列表框，使蓝色边框立即消失
                                RedrawWindow(g_hwndListBox, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                                // 同时重绘主窗口
                                RedrawWindow(g_hwndMain, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                            }
                        } else {
                            // 未按Ctrl键，添加到选择列表
                            auto it = std::find(g_selectedItems.begin(), g_selectedItems.end(), actualIndex);
                            if (it == g_selectedItems.end()) {
                                // 未选中，添加选择
                                g_selectedItems.push_back(actualIndex);
                                // 立即重绘列表框
                                RedrawWindow(g_hwndListBox, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                                // 同时重绘主窗口
                                RedrawWindow(g_hwndMain, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                            }
                        }
                    }
                }
            }

            // 处理筛选按钮点击
            if (wNotifyCode == BN_CLICKED) {
                bool filterChanged = false;
                if (wID == ID_FILTER_ALL) {
                    g_currentTab = 0;
                    filterChanged = true;
                } else if (wID == ID_FILTER_TEXT) {
                    g_currentTab = 1;
                    filterChanged = true;
                } else if (wID == ID_FILTER_IMAGE) {
                    g_currentTab = 2;
                    filterChanged = true;
                } else if (wID == ID_FILTER_FILE) {
                    g_currentTab = 3;
                    filterChanged = true;
                } else if (wID == ID_FILTER_FAVORITE) {
                    if (g_currentTab == 4) {
                        // 已在收藏页，再次单击 → 弹出分类下拉菜单（筛选模式，不可编辑）
                        RECT btnRect;
                        GetWindowRect(g_hwndFilterFavorite, &btnRect);
                        g_tagPopupParentBtnWidth = btnRect.right - btnRect.left;
                        g_tagPopupFilterMode = true;  // 筛选模式
                        ShowTagPopup(hwnd, btnRect.left, btnRect.bottom, g_tagPopupParentBtnWidth);
                    } else {
                        // 首次单击 → 切换到收藏页，显示全部
                        g_currentTab = 4;
                        g_currentFilterTagId = 0;  // 重置为显示全部收藏
                        filterChanged = true;
                    }
                }

                if (filterChanged) {
                    // 重绘所有筛选按钮以更新选中状态
                    InvalidateRect(g_hwndFilterAll, NULL, TRUE);
                    InvalidateRect(g_hwndFilterText, NULL, TRUE);
                    InvalidateRect(g_hwndFilterImage, NULL, TRUE);
                    InvalidateRect(g_hwndFilterFile, NULL, TRUE);
                    InvalidateRect(g_hwndFilterFavorite, NULL, TRUE);
                    UpdateListBox();
                }
            }

            // 处理列表框双击事件
            if (wID == ID_LISTBOX && wNotifyCode == LBN_DBLCLK) {
                int index = SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0);
                if (index != LB_ERR && index < (int)g_displayIndexMap.size()) {
                    // 批量编辑模式下，双击切换选择状态
                    if (g_isBatchEditMode) {
                        int actualIndex = g_displayIndexMap[index];
                        // 检查是否已选中
                        auto it = std::find(g_selectedItems.begin(), g_selectedItems.end(), actualIndex);
                        if (it != g_selectedItems.end()) {
                            // 已选中，取消选择
                            g_selectedItems.erase(it);
                        } else {
                            // 未选中，添加选择
                            g_selectedItems.push_back(actualIndex);
                        }
                        // 重绘列表框
                        InvalidateRect(g_hwndListBox, NULL, TRUE);
                        return 0;
                    }
                    
                    int actualIndex = g_displayIndexMap[index];  // 获取实际索引
                    const ClipboardItem& item = g_history[actualIndex];

                    if (item.type == TYPE_TEXT) {
                        // 文本类型：双击直接粘贴
                        if (OpenClipboard(NULL)) {
                            EmptyClipboard();

                            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, (item.content.length() + 1) * sizeof(wchar_t));
                            if (hGlobal != NULL) {
                                wchar_t* pData = (wchar_t*)GlobalLock(hGlobal);
                                if (pData != NULL) {
                                    wcscpy_s(pData, item.content.length() + 1, item.content.c_str());
                                    GlobalUnlock(hGlobal);
                                    SetClipboardData(CF_UNICODETEXT, hGlobal);
                                }
                            }

                            g_isRestoringClipboard = true;
                            CloseClipboard();
                            SetTimer(hwnd, 1, 100, NULL);
                        }

                        if (!g_isTopmost) {
                            if (g_hwndTagPopup) {
                                DestroyWindow(g_hwndTagPopup);
                                g_hwndTagPopup = NULL;
                            }
                            ShowWindow(hwnd, SW_HIDE);
                        }

                        Sleep(100);

                        if (g_previousActiveWindow != NULL && IsWindow(g_previousActiveWindow)) {
                            SetForegroundWindow(g_previousActiveWindow);
                            Sleep(100);
                        }

                        keybd_event(VK_CONTROL, 0, 0, 0);
                        keybd_event('V', 0, 0, 0);
                        keybd_event('V', 0, KEYEVENTF_KEYUP, 0);
                        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);

                        if (g_isNotificationEnabled) {
                            ShowTrayBalloon(hwnd, L"提示", L"已粘贴");
                        }
                    } else if (item.type == TYPE_FILE) {
                        // 文件类型：复制文件路径到剪贴板
                        if (OpenClipboard(NULL)) {
                            EmptyClipboard();

                            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, (item.content.length() + 1) * sizeof(wchar_t));
                            if (hGlobal != NULL) {
                                wchar_t* pData = (wchar_t*)GlobalLock(hGlobal);
                                if (pData != NULL) {
                                    wcscpy_s(pData, item.content.length() + 1, item.content.c_str());
                                    GlobalUnlock(hGlobal);
                                    SetClipboardData(CF_UNICODETEXT, hGlobal);

                                    g_isRestoringClipboard = true;
                                    if (g_isNotificationEnabled) {
                                        ShowTrayBalloon(hwnd, L"提示", L"文件路径已复制");
                                    }
                                }
                            }

                            CloseClipboard();
                            SetTimer(hwnd, 1, 100, NULL);
                        }
                    } else if (item.type == TYPE_IMAGE) {
                        // 图像类型：显示预览窗口
                        ShowImagePreview(hwnd, item);
                    }
                }
            } else if (wID == ID_BATCH_EDIT_BUTTON && wNotifyCode == BN_CLICKED) {
                // 切换批量编辑模式（启动动画）
                g_batchEditAnimDirection = !g_isBatchEditMode;  // 根据当前状态决定动画方向
                g_batchEditAnimProgress = 0.0f;
                g_batchEditAnimating = true;
                g_isBatchEditMode = !g_isBatchEditMode;
                // 清空选中的记录
                g_selectedItems.clear();
                SetTimer(hwnd, ID_BATCH_EDIT_ANIM_TIMER, 16, NULL);  // 约60fps
                if (g_isNotificationEnabled) {
                    ShowTrayBalloon(hwnd, L"提示", g_isBatchEditMode ? L"批量编辑模式已开启" : L"批量编辑模式已关闭");
                }
                // 重绘列表框
                InvalidateRect(g_hwndListBox, NULL, TRUE);
            } else if (wID == ID_TOPMOST_BUTTON && wNotifyCode == BN_CLICKED) {
                // 切换置顶状态（启动动画）
                g_topmostAnimDirection = !g_isTopmost;  // 根据当前状态决定动画方向
                g_topmostAnimProgress = 0.0f;
                g_topmostAnimating = true;
                g_isTopmost = !g_isTopmost;
                SetTimer(hwnd, ID_TOPMOST_ANIM_TIMER, 16, NULL);  // 约60fps
                if (g_isTopmost) {
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                    SetWindowTextW(GetDlgItem(hwnd, ID_TOPMOST_BUTTON), L"取消置顶");
                    if (g_isNotificationEnabled) {
                        ShowTrayBalloon(hwnd, L"提示", L"窗口已置顶");
                    }
                } else {
                    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                    SetWindowTextW(GetDlgItem(hwnd, ID_TOPMOST_BUTTON), L"置顶");
                    if (g_isNotificationEnabled) {
                        ShowTrayBalloon(hwnd, L"提示", L"已取消置顶");
                    }
                }
            } else if (wID == ID_DARKMODE_BUTTON && wNotifyCode == BN_CLICKED) {
                // 切换暗黑模式
                g_isDarkMode = !g_isDarkMode;

                // 更新窗口背景色
                SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)CreateSolidBrush(GetBgColor()));

                // 更新列表框背景色
                if (g_hwndListBox) {
                    InvalidateRect(g_hwndListBox, NULL, TRUE);
                }

                // 更新搜索框背景色
                if (g_hwndSearchBox) {
                    InvalidateRect(g_hwndSearchBox, NULL, TRUE);
                }

                if (g_isNotificationEnabled) {
                    ShowTrayBalloon(hwnd, L"提示", g_isDarkMode ? L"已切换到暗黑模式" : L"已切换到明亮模式");
                }

                // 强制重绘窗口
                InvalidateRect(hwnd, NULL, TRUE);
                UpdateWindow(hwnd);
            } else if (wID == ID_PAGE_UP_BTN && wNotifyCode == BN_CLICKED) {
                // 上一页 - 使用 g_listBoxTopIndex 判断，与禁用逻辑一致
                if (g_listBoxTopIndex > 0) {
                    // 计算目标位置：基于可视区域高度向上翻页
                    int topIndex = CalculatePrevPageIndex(g_listBoxTopIndex);

                    // 更新页码
                    g_currentPage = topIndex / ITEMS_PER_PAGE;

                    // 平滑滚动处理
                    if (g_isSmoothScrollEnabled) {
                        g_smoothScrollTarget = (float)topIndex;
                        g_smoothScrollCurrent = (float)g_listBoxTopIndex;
                        g_smoothScrollActive = true;
                        g_smoothScrollListBox = g_hwndListBox;
                        SetTimer(g_hwndListBox, ID_SMOOTH_SCROLL_TIMER, 16, NULL);
                    } else {
                        SendMessageW(g_hwndListBox, LB_SETTOPINDEX, topIndex, 0);
                        g_listBoxTopIndex = topIndex;
                        InvalidateRect(g_hwndListBox, NULL, FALSE);
                    }
                    // 更新按钮状态
                    InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
                    InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
                }
            } else if (wID == ID_PAGE_DOWN_BTN && wNotifyCode == BN_CLICKED) {
                // 下一页 - 基于可视区域高度计算，确保未完整显示的图像成为下一页第一个
                int nextPageIndex = CalculateNextPageIndex(g_listBoxTopIndex);
                if (nextPageIndex < (int)g_displayIndexMap.size() && nextPageIndex > g_listBoxTopIndex) {
                    int topIndex = nextPageIndex;

                    // 更新页码
                    g_currentPage = topIndex / ITEMS_PER_PAGE;

                    // 平滑滚动处理
                    if (g_isSmoothScrollEnabled) {
                        g_smoothScrollTarget = (float)topIndex;
                        g_smoothScrollCurrent = (float)g_listBoxTopIndex;
                        g_smoothScrollActive = true;
                        g_smoothScrollListBox = g_hwndListBox;
                        SetTimer(g_hwndListBox, ID_SMOOTH_SCROLL_TIMER, 16, NULL);
                    } else {
                        SendMessageW(g_hwndListBox, LB_SETTOPINDEX, topIndex, 0);
                        g_listBoxTopIndex = topIndex;
                        InvalidateRect(g_hwndListBox, NULL, FALSE);
                    }
                    // 更新按钮状态
                    InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
                    InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
                }
            } else if (wID == ID_TITLEBAR_TOPMOST && wNotifyCode == BN_CLICKED) {
                // 标题栏置顶按钮
                g_isTopmost = !g_isTopmost;
                if (g_isTopmost) {
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                    if (g_isNotificationEnabled) {
                        ShowTrayBalloon(hwnd, L"提示", L"窗口已置顶");
                    }
                } else {
                    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                    if (g_isNotificationEnabled) {
                        ShowTrayBalloon(hwnd, L"提示", L"已取消置顶");
                    }
                }
                InvalidateRect(g_hwndTitleTopmost, NULL, TRUE);
            } else if (wID == ID_TITLEBAR_MINIMIZE && wNotifyCode == BN_CLICKED) {
                if (!g_isTopmost) {
                    ShowWindow(hwnd, SW_MINIMIZE);
                }
            } else if (wID == ID_TITLEBAR_MAXIMIZE && wNotifyCode == BN_CLICKED) {
                // 标题栏最大化/还原按钮
                if (IsZoomed(hwnd)) {
                    ShowWindow(hwnd, SW_RESTORE);
                } else {
                    ShowWindow(hwnd, SW_MAXIMIZE);
                }
                InvalidateRect(g_hwndTitleMaximize, NULL, TRUE);
            } else if (wID == ID_TITLEBAR_CLOSE && wNotifyCode == BN_CLICKED) {
                // 标题栏关闭按钮 - 隐藏窗口而不是退出
                if (g_hwndTagPopup) {
                    DestroyWindow(g_hwndTagPopup);
                    g_hwndTagPopup = NULL;
                }
                ShowWindow(hwnd, SW_HIDE);
            } else if (wID == IDM_EXIT) {
                DestroyWindow(hwnd);
            } else if (wID == IDM_SETTINGS) {
                // 显示模态设置对话框
                ShowSettingsDialog(hwnd);
            } else if (wID == IDM_COPY) {
                // 右键菜单：复制
                if (g_contextMenuIndex >= 0 && g_contextMenuIndex < (int)g_displayIndexMap.size()) {
                    int actualIndex = g_displayIndexMap[g_contextMenuIndex];  // 获取实际索引
                    const ClipboardItem& item = g_history[actualIndex];
                    if (OpenClipboard(NULL)) {
                        EmptyClipboard();

                        if (item.type == TYPE_TEXT || item.type == TYPE_FILE) {
                            // 文本和文件类型：复制文本内容
                            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, (item.content.length() + 1) * sizeof(wchar_t));
                            if (hGlobal != NULL) {
                                wchar_t* pData = (wchar_t*)GlobalLock(hGlobal);
                                if (pData != NULL) {
                                    wcscpy_s(pData, item.content.length() + 1, item.content.c_str());
                                    GlobalUnlock(hGlobal);
                                    SetClipboardData(CF_UNICODETEXT, hGlobal);
                                }
                            }
                        } else if (item.type == TYPE_IMAGE) {
                            // 图像类型：复制图像数据（从原图文件加载）
                            std::vector<BYTE> originalData;
                            int origWidth = 0, origHeight = 0;
                            bool loadedFromFile = false;

                            if (!item.imageFileName.empty()) {
                                loadedFromFile = LoadOriginalImage(item.imageFileName, originalData, origWidth, origHeight);
                            }

                            if (loadedFromFile) {
                                // 使用原图数据
                                BITMAPINFO bmi = {};
                                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                                bmi.bmiHeader.biWidth = origWidth;
                                bmi.bmiHeader.biHeight = -origHeight;
                                bmi.bmiHeader.biPlanes = 1;
                                bmi.bmiHeader.biBitCount = 24;
                                bmi.bmiHeader.biCompression = BI_RGB;

                                DWORD imageSize = originalData.size();
                                HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + imageSize);
                                if (hGlobal != NULL) {
                                    BYTE* pData = (BYTE*)GlobalLock(hGlobal);
                                    if (pData != NULL) {
                                        memcpy(pData, &bmi.bmiHeader, sizeof(BITMAPINFOHEADER));
                                        memcpy(pData + sizeof(BITMAPINFOHEADER), &originalData[0], imageSize);
                                        GlobalUnlock(hGlobal);
                                        SetClipboardData(CF_DIB, hGlobal);
                                    }
                                }
                            } else {
                                // 回退到使用缩略图数据（兼容旧数据）
                                int srcWidth = item.thumbWidth > 0 ? item.thumbWidth : item.imageWidth;
                                int srcHeight = item.thumbHeight > 0 ? item.thumbHeight : item.imageHeight;

                                BITMAPINFO bmi = {};
                                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                                bmi.bmiHeader.biWidth = srcWidth;
                                bmi.bmiHeader.biHeight = -srcHeight;
                                bmi.bmiHeader.biPlanes = 1;
                                bmi.bmiHeader.biBitCount = 24;
                                bmi.bmiHeader.biCompression = BI_RGB;

                                DWORD imageSize = item.imageData.size();
                                HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + imageSize);
                                if (hGlobal != NULL) {
                                    BYTE* pData = (BYTE*)GlobalLock(hGlobal);
                                    if (pData != NULL) {
                                        memcpy(pData, &bmi.bmiHeader, sizeof(BITMAPINFOHEADER));
                                        memcpy(pData + sizeof(BITMAPINFOHEADER), &item.imageData[0], imageSize);
                                        GlobalUnlock(hGlobal);
                                        SetClipboardData(CF_DIB, hGlobal);
                                    }
                                }
                            }
                        }

                        g_isRestoringClipboard = true;
                        CloseClipboard();
                        SetTimer(hwnd, 1, 100, NULL);
                        if (g_isNotificationEnabled) {
                            ShowTrayBalloon(hwnd, L"提示", L"已复制");
                        }
                    }
                }
            } else if (wID == IDM_PASTE) {
                // 右键菜单：执行粘贴（模拟Ctrl+V）
                if (g_contextMenuIndex >= 0 && g_contextMenuIndex < (int)g_displayIndexMap.size()) {
                    int actualIndex = g_displayIndexMap[g_contextMenuIndex];  // 获取实际索引
                    const ClipboardItem& item = g_history[actualIndex];
                    if (OpenClipboard(NULL)) {
                        EmptyClipboard();

                        if (item.type == TYPE_TEXT || item.type == TYPE_FILE) {
                            // 文本和文件类型：复制文本内容
                            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, (item.content.length() + 1) * sizeof(wchar_t));
                            if (hGlobal != NULL) {
                                wchar_t* pData = (wchar_t*)GlobalLock(hGlobal);
                                if (pData != NULL) {
                                    wcscpy_s(pData, item.content.length() + 1, item.content.c_str());
                                    GlobalUnlock(hGlobal);
                                    SetClipboardData(CF_UNICODETEXT, hGlobal);
                                }
                            }
                        } else if (item.type == TYPE_IMAGE) {
                            // 图像类型：复制图像数据（从原图文件加载）
                            std::vector<BYTE> originalData;
                            int origWidth = 0, origHeight = 0;
                            bool loadedFromFile = false;

                            if (!item.imageFileName.empty()) {
                                loadedFromFile = LoadOriginalImage(item.imageFileName, originalData, origWidth, origHeight);
                            }

                            if (loadedFromFile) {
                                // 使用原图数据
                                BITMAPINFO bmi = {};
                                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                                bmi.bmiHeader.biWidth = origWidth;
                                bmi.bmiHeader.biHeight = -origHeight;
                                bmi.bmiHeader.biPlanes = 1;
                                bmi.bmiHeader.biBitCount = 24;
                                bmi.bmiHeader.biCompression = BI_RGB;

                                DWORD imageSize = originalData.size();
                                HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + imageSize);
                                if (hGlobal != NULL) {
                                    BYTE* pData = (BYTE*)GlobalLock(hGlobal);
                                    if (pData != NULL) {
                                        memcpy(pData, &bmi.bmiHeader, sizeof(BITMAPINFOHEADER));
                                        memcpy(pData + sizeof(BITMAPINFOHEADER), &originalData[0], imageSize);
                                        GlobalUnlock(hGlobal);
                                        SetClipboardData(CF_DIB, hGlobal);
                                    }
                                }
                            } else {
                                // 回退到使用缩略图数据（兼容旧数据）
                                int srcWidth = item.thumbWidth > 0 ? item.thumbWidth : item.imageWidth;
                                int srcHeight = item.thumbHeight > 0 ? item.thumbHeight : item.imageHeight;

                                BITMAPINFO bmi = {};
                                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                                bmi.bmiHeader.biWidth = srcWidth;
                                bmi.bmiHeader.biHeight = -srcHeight;
                                bmi.bmiHeader.biPlanes = 1;
                                bmi.bmiHeader.biBitCount = 24;
                                bmi.bmiHeader.biCompression = BI_RGB;

                                DWORD imageSize = item.imageData.size();
                                HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + imageSize);
                                if (hGlobal != NULL) {
                                    BYTE* pData = (BYTE*)GlobalLock(hGlobal);
                                    if (pData != NULL) {
                                        memcpy(pData, &bmi.bmiHeader, sizeof(BITMAPINFOHEADER));
                                        memcpy(pData + sizeof(BITMAPINFOHEADER), &item.imageData[0], imageSize);
                                        GlobalUnlock(hGlobal);
                                        SetClipboardData(CF_DIB, hGlobal);
                                    }
                                }
                            }
                        }

                        g_isRestoringClipboard = true;
                        CloseClipboard();
                        SetTimer(hwnd, 1, 100, NULL);
                    }

                    // 如果不是置顶状态，隐藏剪贴板窗口
                    if (!g_isTopmost) {
                        if (g_hwndTagPopup) {
                            DestroyWindow(g_hwndTagPopup);
                            g_hwndTagPopup = NULL;
                        }
                        ShowWindow(hwnd, SW_HIDE);
                    }

                    // 等待剪贴板数据设置完成
                    Sleep(100);

                    // 激活之前的窗口
                    if (g_previousActiveWindow != NULL && IsWindow(g_previousActiveWindow)) {
                        SetForegroundWindow(g_previousActiveWindow);
                        // 等待窗口激活
                        Sleep(100);
                    }

                    // 模拟Ctrl+V
                    keybd_event(VK_CONTROL, 0, 0, 0);
                    keybd_event('V', 0, 0, 0);
                    keybd_event('V', 0, KEYEVENTF_KEYUP, 0);
                    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);

                    if (g_isNotificationEnabled) {
                        ShowTrayBalloon(hwnd, L"提示", L"已粘贴");
                    }
                }
            } else if (wID == IDM_FAVORITE) {
                // 保留兼容性：旧的收藏/取消收藏逻辑
                if (g_contextMenuIndex >= 0 && g_contextMenuIndex < (int)g_displayIndexMap.size()) {
                    int actualIndex = g_displayIndexMap[g_contextMenuIndex];
                    g_history[actualIndex].isFavorite = !g_history[actualIndex].isFavorite;
                    SaveHistory();
                    UpdateListBox();
                    if (g_isNotificationEnabled) {
                        ShowTrayBalloon(hwnd, L"提示",
                            g_history[actualIndex].isFavorite ? L"已收藏" : L"已取消收藏");
                    }
                }
            } else if (wID >= IDM_TAG_BASE && wID < IDM_TAG_FILTER_ALL) {
                // 标签菜单项点击：添加/移除标签
                int tagId = wID - IDM_TAG_BASE;
                if (g_contextMenuIndex >= 0 && g_contextMenuIndex < (int)g_displayIndexMap.size()) {
                    int actualIndex = g_displayIndexMap[g_contextMenuIndex];
                    if (ItemHasTag(actualIndex, tagId)) {
                        RemoveTagFromItem(actualIndex, tagId);
                        if (g_isNotificationEnabled) {
                            Tag* tag = GetTagById(tagId);
                            if (tag) {
                                ShowTrayBalloon(hwnd, L"提示", (L"已移除标签: " + tag->name).c_str());
                            }
                        }
                    } else {
                        AddTagToItem(actualIndex, tagId);
                        if (g_isNotificationEnabled) {
                            Tag* tag = GetTagById(tagId);
                            if (tag) {
                                ShowTrayBalloon(hwnd, L"提示", (L"已添加标签: " + tag->name).c_str());
                            }
                        }
                    }
                    SaveHistory();
                    UpdateListBox();
                }
            } else if (wID == IDM_OPEN_LOCATION) {
                // 右键菜单：打开所在位置
                if (g_contextMenuIndex >= 0 && g_contextMenuIndex < (int)g_displayIndexMap.size()) {
                    int actualIndex = g_displayIndexMap[g_contextMenuIndex];
                    const ClipboardItem& item = g_history[actualIndex];
                    std::wstring filePath;

                    if (item.type == TYPE_FILE) {
                        filePath = item.content;
                    } else if (item.type == TYPE_IMAGE && !item.imageFilePath.empty()) {
                        filePath = item.imageFilePath;
                    } else if (item.type == TYPE_IMAGE && !item.imageFileName.empty()) {
                        // 截图类型：构建完整路径
                        filePath = GetImagesPath() + L"\\" + item.imageFileName;
                    }

                    if (!filePath.empty()) {
                        // 使用 explorer /select 命令打开文件所在位置并选中文件
                        std::wstring cmd = L"/select,\"" + filePath + L"\"";
                        ShellExecuteW(NULL, L"open", L"explorer.exe", cmd.c_str(), NULL, SW_SHOWNORMAL);
                    }
                }
            } else if (wID == IDM_DELETE) {
                // 右键菜单：删除
                if (g_isBatchEditMode && !g_selectedItems.empty()) {
                    // 批量删除
                    int result = MessageBoxW(hwnd, L"确定要删除选中的项目吗？", L"确认删除", MB_YESNO | MB_ICONQUESTION);
                    if (result == IDYES) {
                        // 按索引从大到小排序，避免删除时索引变化
                        std::sort(g_selectedItems.rbegin(), g_selectedItems.rend());
                        for (int actualIndex : g_selectedItems) {
                            if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
                                g_history.erase(g_history.begin() + actualIndex);
                            }
                        }
                        SaveHistory();
                        g_selectedItems.clear();  // 清空选中列表
                        UpdateListBox();
                        if (g_isNotificationEnabled) {
                            ShowTrayBalloon(hwnd, L"提示", L"已批量删除");
                        }
                    }
                } else if (g_contextMenuIndex >= 0 && g_contextMenuIndex < (int)g_displayIndexMap.size()) {
                    int actualIndex = g_displayIndexMap[g_contextMenuIndex];  // 获取实际索引
                    bool shouldDelete = true;

                    if (g_history[actualIndex].isFavorite) {
                        // 收藏项目需要弹窗确认
                        int result = MessageBoxW(hwnd, L"该项目已收藏，确定要删除吗？", L"确认删除", MB_YESNO | MB_ICONQUESTION);
                        shouldDelete = (result == IDYES);
                    }

                    if (shouldDelete) {
                        g_history.erase(g_history.begin() + actualIndex);
                        SaveHistory();
                        UpdateListBox();
                        if (g_isNotificationEnabled) {
                            ShowTrayBalloon(hwnd, L"提示", L"已删除");
                        }
                    }
                }
            } else if (wID == IDM_ADD_TO_STATION) {
                // 右键菜单：加入中转站
                if (g_isBatchEditMode && !g_selectedItems.empty()) {
                    // 批量加入中转站
                    for (int actualIndex : g_selectedItems) {
                        if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
                            AddToTransferStation(actualIndex);
                        }
                    }
                    g_selectedItems.clear();  // 清空选中列表
                    UpdateListBox();  // 更新列表框显示
                    if (g_isNotificationEnabled) {
                        ShowTrayBalloon(hwnd, L"提示", L"已批量加入中转站");
                    }
                } else if (g_contextMenuIndex >= 0 && g_contextMenuIndex < (int)g_displayIndexMap.size()) {
                    int actualIndex = g_displayIndexMap[g_contextMenuIndex];
                    AddToTransferStation(actualIndex);
                    UpdateListBox();  // 更新列表框显示
                }
            } else if (wID == IDM_REMOVE_FROM_STATION) {
                // 右键菜单：从中转站移除
                if (g_contextMenuIndex >= 0 && g_contextMenuIndex < (int)g_displayIndexMap.size()) {
                    int actualIndex = g_displayIndexMap[g_contextMenuIndex];
                    RemoveFromTransferStation(actualIndex);
                    UpdateListBox();  // 更新列表框显示
                }
            } else if (wID >= IDM_BATCH_ADD_TAG) {
                // 批量编辑模式：批量加入标签（处理二级菜单选择）
                if (g_isBatchEditMode && !g_selectedItems.empty()) {
                    int tagId = wID - IDM_BATCH_ADD_TAG;
                    // 为所有选中的项目添加标签
                    for (int actualIndex : g_selectedItems) {
                        if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
                            g_history[actualIndex].tagIds.insert(tagId);
                        }
                    }
                    SaveHistory();
                    g_selectedItems.clear();
                    UpdateListBox();
                    if (g_isNotificationEnabled) {
                        ShowTrayBalloon(hwnd, L"提示", L"已批量加入标签");
                    }
                }
            } else if (wID == IDM_NOTIFICATION) {
                // 托盘菜单：切换消息通知
                g_isNotificationEnabled = !g_isNotificationEnabled;
                if (g_isNotificationEnabled) {
                    ShowTrayBalloon(hwnd, L"设置已更新", L"消息通知已启用");
                }
            } else if (wID == IDM_THEME_LIGHT) {
                // 托盘菜单：切换到日间模式
                if (g_themeMode != THEME_LIGHT || g_isDarkMode) {
                    g_themeMode = THEME_LIGHT;
                    g_isDarkMode = true;  // 设置为相反值，让ApplyTheme触发更新
                    ApplyTheme();
                    SaveHotkeySettings();
                }
            } else if (wID == IDM_THEME_DARK) {
                // 托盘菜单：切换到夜间模式
                if (g_themeMode != THEME_DARK || !g_isDarkMode) {
                    g_themeMode = THEME_DARK;
                    g_isDarkMode = false;  // 设置为相反值，让ApplyTheme触发更新
                    ApplyTheme();
                    SaveHotkeySettings();
                }
            }
            break;
        }
        case WM_NOTIFY: {
            // 预留给其他通知处理
            break;
        }
        case WM_TIMER: {
            if (wParam == 1) {
                g_isRestoringClipboard = false;
                KillTimer(hwnd, 1);
            } else if (wParam == 2) {
                // 延迟刷新列表项高度
                if (g_hwndListBox) {
                    int itemCount = SendMessageW(g_hwndListBox, LB_GETCOUNT, 0, 0);
                    for (int i = 0; i < itemCount; i++) {
                        SendMessageW(g_hwndListBox, LB_SETITEMHEIGHT, i, 0);
                    }
                    InvalidateRect(g_hwndListBox, NULL, TRUE);
                }
                KillTimer(hwnd, 2);
            } else if (wParam == ID_TOPMOST_ANIM_TIMER) {
                // 置顶按钮波浪动画
                g_topmostAnimProgress += 0.08f;  // 动画速度
                if (g_topmostAnimProgress >= 1.0f) {
                    g_topmostAnimProgress = 1.0f;
                    g_topmostAnimating = false;
                    KillTimer(hwnd, ID_TOPMOST_ANIM_TIMER);
                }
                InvalidateRect(g_hwndTopmostBtn, NULL, TRUE);
            } else if (wParam == ID_BATCH_EDIT_ANIM_TIMER) {
                // 批量编辑按钮波浪动画
                g_batchEditAnimProgress += 0.08f;
                if (g_batchEditAnimProgress >= 1.0f) {
                    g_batchEditAnimProgress = 1.0f;
                    g_batchEditAnimating = false;
                    KillTimer(hwnd, ID_BATCH_EDIT_ANIM_TIMER);
                }
                InvalidateRect(g_hwndBatchEditBtn, NULL, TRUE);
            }
            break;
        }
        case WM_CONTEXTMENU: {
            // 处理收藏按钮右键菜单
            HWND hwndCtrl = (HWND)wParam;
            if (hwndCtrl == g_hwndFilterFavorite) {
                // 右键点击收藏按钮，显示自定义标签弹出窗口（编辑模式，可编辑颜色和名称）
                RECT btnRect;
                GetWindowRect(g_hwndFilterFavorite, &btnRect);
                g_tagPopupParentBtnWidth = btnRect.right - btnRect.left;  // 保存按钮宽度
                g_tagPopupFilterMode = false;  // 编辑模式
                ShowTagPopup(hwnd, btnRect.left, btnRect.bottom, g_tagPopupParentBtnWidth);
                return 0;
            }

            // 处理列表框右键菜单
            if (hwndCtrl == g_hwndListBox) {
                POINT pt;
                pt.x = LOWORD(lParam);
                pt.y = HIWORD(lParam);

                // 如果是键盘触发（lParam == -1），使用当前选中项的位置
                if (lParam == -1) {
                    int index = SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0);
                    if (index != LB_ERR) {
                        RECT itemRect;
                        SendMessageW(g_hwndListBox, LB_GETITEMRECT, index, (LPARAM)&itemRect);
                        pt.x = itemRect.left;
                        pt.y = itemRect.top;
                        ClientToScreen(g_hwndListBox, &pt);
                    }
                } else {
                    // 鼠标右键，找到点击的项
                    POINT clientPt = pt;
                    ScreenToClient(g_hwndListBox, &clientPt);
                    int index = SendMessageW(g_hwndListBox, LB_ITEMFROMPOINT, 0, MAKELPARAM(clientPt.x, clientPt.y));
                    if (HIWORD(index) == 0) { // 在列表框内
                        g_contextMenuIndex = LOWORD(index);
                        SendMessageW(g_hwndListBox, LB_SETCURSEL, g_contextMenuIndex, 0);
                    } else {
                        return 0; // 不在项上，不显示菜单
                    }
                }

                // 创建右键菜单
                HMENU hMenu = CreatePopupMenu();
                if (g_isBatchEditMode && !g_selectedItems.empty()) {
                    // 批量编辑模式下的右键菜单
                    // 创建菜单图标
                    HBITMAP hDeleteIcon = CreateMenuIconBitmap(L"\uE74D", RGB(200, 60, 60));  // Delete (红色)
                    HBITMAP hStationIcon = CreateMenuIconBitmap(L"\uE710");  // Add to station
                    HBITMAP hTagIcon = CreateMenuIconBitmap(L"\uE719");  // Tag

                    MENUITEMINFOW mii = {};
                    mii.cbSize = sizeof(MENUITEMINFOW);
                    mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;

                    // 批量加入中转站
                    mii.wID = IDM_ADD_TO_STATION;
                    mii.dwTypeData = (LPWSTR)L"批量加入中转站";
                    mii.hbmpItem = hStationIcon;
                    InsertMenuItemW(hMenu, 0, TRUE, &mii);

                    // 批量加入标签（二级菜单）
                    HMENU hTagSubMenu = CreatePopupMenu();
                    if (!g_tags.empty()) {
                        for (const auto& tag : g_tags) {
                            AppendMenuW(hTagSubMenu, MF_STRING, IDM_BATCH_ADD_TAG + tag.id, tag.name.c_str());
                        }
                    } else {
                        AppendMenuW(hTagSubMenu, MF_GRAYED, 0, L"无可用标签");
                    }
                    mii.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_BITMAP;
                    mii.hSubMenu = hTagSubMenu;
                    mii.dwTypeData = (LPWSTR)L"批量加入标签";
                    mii.hbmpItem = hTagIcon;
                    InsertMenuItemW(hMenu, 1, TRUE, &mii);

                    // 分隔符
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

                    // 批量删除
                    mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;
                    mii.hSubMenu = NULL;
                    mii.wID = IDM_DELETE;
                    mii.dwTypeData = (LPWSTR)L"批量删除";
                    mii.hbmpItem = hDeleteIcon;
                    InsertMenuItemW(hMenu, 3, TRUE, &mii);

                    SetForegroundWindow(hwnd);
                    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);

                    // 释放位图资源
                    DeleteObject(hDeleteIcon);
                    DeleteObject(hStationIcon);
                    DeleteObject(hTagIcon);
                } else if (g_contextMenuIndex >= 0 && g_contextMenuIndex < (int)g_displayIndexMap.size()) {
                    int actualIndex = g_displayIndexMap[g_contextMenuIndex];  // 获取实际索引
                    const ClipboardItem& item = g_history[actualIndex];

                    // 创建菜单图标
                    HBITMAP hCopyIcon = CreateMenuIconBitmap(L"\uE8C8");      // Copy
                    HBITMAP hPasteIcon = CreateMenuIconBitmap(L"\uE77F");     // Paste
                    HBITMAP hStationIcon = CreateMenuIconBitmap(item.isInTransferStation ? L"\uE738" : L"\uE710");  // Remove/Add
                    HBITMAP hFavoriteIcon = CreateMenuIconBitmap(item.isFavorite ? L"\uE735" : L"\uE734");  // FavoriteStar/FavoriteStarFill
                    HBITMAP hDeleteIcon = CreateMenuIconBitmap(L"\uE74D", RGB(200, 60, 60));  // Delete (红色)
                    HBITMAP hOpenLocationIcon = CreateMenuIconBitmap(L"\uE838");  // OpenFolderHorizontal

                    MENUITEMINFOW mii = {};
                    mii.cbSize = sizeof(MENUITEMINFOW);
                    mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;

                    // 复制
                    mii.wID = IDM_COPY;
                    mii.dwTypeData = (LPWSTR)L"复制";
                    mii.hbmpItem = hCopyIcon;
                    InsertMenuItemW(hMenu, 0, TRUE, &mii);

                    // 执行粘贴
                    mii.wID = IDM_PASTE;
                    mii.dwTypeData = (LPWSTR)L"执行粘贴";
                    mii.hbmpItem = hPasteIcon;
                    InsertMenuItemW(hMenu, 1, TRUE, &mii);

                    // 分隔符
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

                    // 中转站选项
                    if (item.isInTransferStation) {
                        mii.wID = IDM_REMOVE_FROM_STATION;
                        mii.dwTypeData = (LPWSTR)L"从中转站移除";
                    } else {
                        mii.wID = IDM_ADD_TO_STATION;
                        mii.dwTypeData = (LPWSTR)L"加入中转站";
                    }
                    mii.hbmpItem = hStationIcon;
                    InsertMenuItemW(hMenu, 3, TRUE, &mii);

                    // 分隔符
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

                    // 标签子菜单
                    HMENU hTagMenu = CreatePopupMenu();
                    for (const auto& tag : g_tags) {
                        UINT flags = MF_STRING;
                        // 如果项目已有该标签，显示勾选
                        if (item.tagIds.count(tag.id) > 0) {
                            flags |= MF_CHECKED;
                        }
                        AppendMenuW(hTagMenu, flags, IDM_TAG_BASE + tag.id, tag.name.c_str());
                    }

                    // 添加标签子菜单到主菜单
                    mii.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_BITMAP;
                    mii.hSubMenu = hTagMenu;
                    mii.dwTypeData = (LPWSTR)L"标签";
                    mii.hbmpItem = hFavoriteIcon;
                    InsertMenuItemW(hMenu, 5, TRUE, &mii);

                    // 恢复 mii 设置
                    mii.fMask = MIIM_STRING | MIIM_ID | MIIM_BITMAP;
                    mii.hSubMenu = NULL;

                    // 打开所在位置（对文件类型、图片文件和截图显示）
                    bool hasFilePath = false;
                    if (item.type == TYPE_FILE) {
                        hasFilePath = true;
                    } else if (item.type == TYPE_IMAGE && !item.imageFilePath.empty()) {
                        hasFilePath = true;
                    } else if (item.type == TYPE_IMAGE && !item.imageFileName.empty()) {
                        // 截图类型：检查原图文件是否存在
                        std::wstring screenshotPath = GetImagesPath() + L"\\" + item.imageFileName;
                        DWORD attrs = GetFileAttributesW(screenshotPath.c_str());
                        hasFilePath = (attrs != INVALID_FILE_ATTRIBUTES);
                    }
                    if (hasFilePath) {
                        // 分隔符
                        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

                        mii.wID = IDM_OPEN_LOCATION;
                        mii.dwTypeData = (LPWSTR)L"打开所在位置";
                        mii.hbmpItem = hOpenLocationIcon;
                        InsertMenuItemW(hMenu, 7, TRUE, &mii);
                    }

                    // 分隔符
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

                    // 删除
                    mii.wID = IDM_DELETE;
                    mii.dwTypeData = (LPWSTR)L"删除";
                    mii.hbmpItem = hDeleteIcon;
                    InsertMenuItemW(hMenu, hasFilePath ? 9 : 7, TRUE, &mii);

                    SetForegroundWindow(hwnd);
                    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);

                    // 释放位图资源
                    DeleteObject(hCopyIcon);
                    DeleteObject(hPasteIcon);
                    DeleteObject(hStationIcon);
                    DeleteObject(hFavoriteIcon);
                    DeleteObject(hDeleteIcon);
                    DeleteObject(hOpenLocationIcon);
                }
                DestroyMenu(hMenu);
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            // 点击主窗口空白区域时，让搜索框失焦
            SetFocus(hwnd);
            break;
        }
        case WM_CTLCOLOREDIT: {
            // 设置搜索框背景色（支持暗黑模式）
            HDC hdcEdit = (HDC)wParam;
            HWND hwndEdit = (HWND)lParam;
            if (hwndEdit == g_hwndSearchBox) {
                COLORREF bgColor = GetWhiteColor();
                COLORREF textColor = GetTextColor();
                SetBkColor(hdcEdit, bgColor);
                SetTextColor(hdcEdit, textColor);
                static HBRUSH hBrushLight = NULL;
                static HBRUSH hBrushDark = NULL;
                if (g_isDarkMode) {
                    if (!hBrushDark) hBrushDark = CreateSolidBrush(COLOR_DARK_WHITE);
                    return (LRESULT)hBrushDark;
                } else {
                    if (!hBrushLight) hBrushLight = CreateSolidBrush(COLOR_LIGHT_WHITE);
                    return (LRESULT)hBrushLight;
                }
            }
            break;
        }
        case WM_CTLCOLORLISTBOX: {
            // 设置列表框背景色（支持暗黑模式，解决底部空白问题）
            HDC hdcListBox = (HDC)wParam;
            HWND hwndListBox = (HWND)lParam;
            if (hwndListBox == g_hwndListBox) {
                COLORREF bgColor = GetWhiteColor();
                SetBkColor(hdcListBox, bgColor);
                static HBRUSH hListBrushLight = NULL;
                static HBRUSH hListBrushDark = NULL;
                if (g_isDarkMode) {
                    if (!hListBrushDark) hListBrushDark = CreateSolidBrush(COLOR_DARK_WHITE);
                    return (LRESULT)hListBrushDark;
                } else {
                    if (!hListBrushLight) hListBrushLight = CreateSolidBrush(COLOR_LIGHT_WHITE);
                    return (LRESULT)hListBrushLight;
                }
            }
            break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // 获取客户区大小
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            int clientWidth = clientRect.right - clientRect.left;

            // 绘制标题栏背景
            RECT rcTitle = {0, 0, clientWidth, TITLEBAR_HEIGHT};
            HBRUSH hTitleBrush = CreateSolidBrush(GetBgColor());
            FillRect(hdc, &rcTitle, hTitleBrush);
            DeleteObject(hTitleBrush);

            // 绘制窗口图标
            HICON hIcon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
            if (!hIcon) {
                hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_ICON1));
            }
            if (hIcon) {
                DrawIconEx(hdc, 10, (TITLEBAR_HEIGHT - 16) / 2, hIcon, 16, 16, 0, NULL, DI_NORMAL);
            }

            // 绘制标题文字
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetTextColor());
            HFONT hTitleFont = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
            HFONT hOldFont = (HFONT)SelectObject(hdc, hTitleFont);
            RECT rcTitleText = {32, 0, clientWidth - 46 * 4, TITLEBAR_HEIGHT};
            DrawTextW(hdc, L"Smart Clip", -1, &rcTitleText, DT_SINGLELINE | DT_VCENTER);
            SelectObject(hdc, hOldFont);
            DeleteObject(hTitleFont);

            // 绘制搜索框渐变边框
            if (g_hwndSearchBox) {
                RECT searchRect;
                GetWindowRect(g_hwndSearchBox, &searchRect);
                MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&searchRect, 2);

                // 扩展矩形以绘制边框
                RECT borderRect = {
                    searchRect.left - 3,
                    searchRect.top - 3,
                    searchRect.right + 3,
                    searchRect.bottom + 3
                };

                Graphics graphics(hdc);
                graphics.SetSmoothingMode(SmoothingModeAntiAlias);

                // 创建渐变画刷 (#6547FF -> #4769FF -> #0090FE)
                LinearGradientBrush gradientBrush(
                    Point(borderRect.left, borderRect.top),
                    Point(borderRect.right, borderRect.top),
                    Color(255, 0x65, 0x47, 0xFF),
                    Color(255, 0x00, 0x90, 0xFE)
                );

                // 设置三色渐变
                Color colors[] = {
                    Color(255, 0x65, 0x47, 0xFF),
                    Color(255, 0x47, 0x69, 0xFF),
                    Color(255, 0x00, 0x90, 0xFE)
                };
                REAL positions[] = {0.0f, 0.5f, 1.0f};
                gradientBrush.SetInterpolationColors(colors, positions, 3);

                // 创建渐变画笔
                Pen gradientPen(&gradientBrush, 1.0f);

                // 绘制圆角矩形边框
                int radius = 8;
                GraphicsPath path;
                path.AddArc(borderRect.left, borderRect.top, radius * 2, radius * 2, 180, 90);
                path.AddArc(borderRect.right - radius * 2, borderRect.top, radius * 2, radius * 2, 270, 90);
                path.AddArc(borderRect.right - radius * 2, borderRect.bottom - radius * 2, radius * 2, radius * 2, 0, 90);
                path.AddArc(borderRect.left, borderRect.bottom - radius * 2, radius * 2, radius * 2, 90, 90);
                path.CloseFigure();

                graphics.DrawPath(&gradientPen, &path);
            }

            EndPaint(hwnd, &ps);
            break;
        }
        case WM_CLOSE: {
            if (g_hwndTagPopup) {
                DestroyWindow(g_hwndTagPopup);
                g_hwndTagPopup = NULL;
            }
            ShowWindow(hwnd, SW_HIDE); // 窗口关闭时只隐藏，不退出程序
            return 0;
        }
        case WM_DESTROY: {
            // 注销拖放目标
            RevokeDragDrop(hwnd);
            if (g_pDropTarget) {
                g_pDropTarget->Release();
                g_pDropTarget = NULL;
            }
            RemoveClipboardFormatListener(hwnd);
            RemoveTrayIcon();
            UnregisterHotkey(hwnd);
            UnregisterQuickPasteHotkeys(hwnd);
            SaveHistory();
            // 清理图标缓存
            ClearIconCache();
            // 释放按钮图片资源
            FreeButtonImages();
            // 确保重置剪贴板恢复标志，避免下次启动时无法录入
            g_isRestoringClipboard = false;
            PostQuitMessage(0);
            break;
        }
        case WM_CLIPBOARDUPDATE: {
            if (!g_isRestoringClipboard && !g_isTransferStationPasting && OpenClipboard(NULL)) {
                // 优先处理文件路径
                if (IsClipboardFormatAvailable(CF_HDROP)) {
                    HGLOBAL hGlobal = GetClipboardData(CF_HDROP);
                    if (hGlobal != NULL) {
                        HDROP hDrop = (HDROP)GlobalLock(hGlobal);
                        if (hDrop != NULL) {
                            UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
                            if (fileCount > 0) {
                                WCHAR filePath[MAX_PATH];
                                if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH) > 0) {
                                    // 检查是否为文件夹
                                    DWORD attrs = GetFileAttributesW(filePath);
                                    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                                        // 是文件夹，保存路径
                                        AddFileToHistory(filePath);
                                    }
                                    // 检查是否为图像文件
                                    else if (IsImageFile(filePath)) {
                                        // 加载图像文件
                                        std::vector<BYTE> imageData;
                                        int width = 0, height = 0;
                                        if (LoadImageFile(filePath, imageData, width, height)) {
                                            // 使用新函数：只保存缩略图和原始路径，不复制原图
                                            AddImageFileToHistory(filePath, imageData, width, height);
                                        } else {
                                            // 加载失败，保存文件路径
                                            AddFileToHistory(filePath);
                                        }
                                    } else {
                                        // 非图像文件，保存文件路径
                                        AddFileToHistory(filePath);
                                    }
                                }
                            }
                            GlobalUnlock(hGlobal);
                        }
                    }
                }
                // 处理位图图像
                else if (IsClipboardFormatAvailable(CF_DIB)) {
                    HGLOBAL hGlobal = GetClipboardData(CF_DIB);
                    if (hGlobal != NULL) {
                        SIZE_T globalSize = GlobalSize(hGlobal);
                        BITMAPINFO* pBitmapInfo = (BITMAPINFO*)GlobalLock(hGlobal);
                        if (pBitmapInfo != NULL && globalSize > sizeof(BITMAPINFOHEADER)) {
                            int width = pBitmapInfo->bmiHeader.biWidth;
                            int height = abs(pBitmapInfo->bmiHeader.biHeight);
                            bool isBottomUp = pBitmapInfo->bmiHeader.biHeight > 0; // 自底向上
                            int bpp = pBitmapInfo->bmiHeader.biBitCount;

                            // 验证图像尺寸
                            if (width > 0 && width <= 10000 && height > 0 && height <= 10000 && (bpp == 24 || bpp == 32)) {
                                // 计算调色板大小
                                int paletteSize = 0;
                                if (bpp <= 8) {
                                    paletteSize = (pBitmapInfo->bmiHeader.biClrUsed ? pBitmapInfo->bmiHeader.biClrUsed : (1 << bpp)) * sizeof(RGBQUAD);
                                } else if (pBitmapInfo->bmiHeader.biClrUsed > 0) {
                                    paletteSize = pBitmapInfo->bmiHeader.biClrUsed * sizeof(RGBQUAD);
                                }

                                // 计算源图像每行字节数（4字节对齐）
                                int srcRowBytes = ((width * bpp + 31) / 32) * 4;
                                DWORD srcImageSize = srcRowBytes * height;

                                // 计算数据偏移
                                SIZE_T dataOffset = pBitmapInfo->bmiHeader.biSize + paletteSize;

                                // 验证数据大小
                                if (globalSize >= dataOffset + srcImageSize) {
                                    BYTE* pImageData = (BYTE*)pBitmapInfo + dataOffset;

                                    // 目标格式：24位
                                    int dstRowBytes = ((width * 24 + 31) / 32) * 4;
                                    DWORD dstImageSize = dstRowBytes * height;
                                    std::vector<BYTE> imageData(dstImageSize);

                                    // 转换并复制图像数据
                                    for (int y = 0; y < height; y++) {
                                        int srcY = isBottomUp ? (height - 1 - y) : y;
                                        BYTE* pSrcRow = pImageData + srcY * srcRowBytes;
                                        BYTE* pDstRow = &imageData[y * dstRowBytes];

                                        if (bpp == 32) {
                                            // 32位转24位：跳过alpha通道
                                            for (int x = 0; x < width; x++) {
                                                pDstRow[x * 3 + 0] = pSrcRow[x * 4 + 0]; // B
                                                pDstRow[x * 3 + 1] = pSrcRow[x * 4 + 1]; // G
                                                pDstRow[x * 3 + 2] = pSrcRow[x * 4 + 2]; // R
                                            }
                                        } else {
                                            // 24位直接复制
                                            memcpy(pDstRow, pSrcRow, width * 3);
                                        }
                                    }

                                    AddImageToHistory(imageData, width, height);
                                }
                            }
                            GlobalUnlock(hGlobal);
                        }
                    }
                }
                // 处理Unicode文本格式
                else if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
                    HGLOBAL hGlobal = GetClipboardData(CF_UNICODETEXT);
                    if (hGlobal != NULL) {
                        wchar_t* pData = (wchar_t*)GlobalLock(hGlobal);
                        if (pData != NULL) {
                            std::wstring content(pData);
                            AddToHistory(content);
                            GlobalUnlock(hGlobal);
                        }
                    }
                }
                // 兼容ANSI文本格式
                else if (IsClipboardFormatAvailable(CF_TEXT)) {
                    HGLOBAL hGlobal = GetClipboardData(CF_TEXT);
                    if (hGlobal != NULL) {
                        char* pData = (char*)GlobalLock(hGlobal);
                        if (pData != NULL) {
                            // 转换为Unicode
                            int wLen = MultiByteToWideChar(CP_ACP, 0, pData, -1, NULL, 0);
                            if (wLen > 0) {
                                std::vector<wchar_t> buffer(wLen);
                                MultiByteToWideChar(CP_ACP, 0, pData, -1, &buffer[0], wLen);
                                std::wstring content(&buffer[0]);
                                AddToHistory(content);
                            }
                            GlobalUnlock(hGlobal);
                        }
                    }
                }
                CloseClipboard();
            }
            break;
        }
        case WM_TRAYICON: {
            if (lParam == WM_LBUTTONDOWN) {
                if (IsWindowVisible(hwnd)) {
                    if (g_hwndTagPopup) {
                        DestroyWindow(g_hwndTagPopup);
                        g_hwndTagPopup = NULL;
                    }
                    ShowWindow(hwnd, SW_HIDE);
                } else {
                    ShowWindow(hwnd, SW_SHOW);
                    SetForegroundWindow(hwnd);
                }
            } else if (lParam == WM_RBUTTONDOWN) {
                // 创建独立的托盘菜单，仅包含设置和退出
                HMENU hTrayMenu = CreatePopupMenu();

                // 创建菜单图标
                HBITMAP hSettingsIcon = CreateMenuIconBitmap(L"\uE713");  // Settings
                HBITMAP hNotificationIcon = CreateMenuIconBitmap(g_isNotificationEnabled ? L"\uEA8F" : L"\uE7ED");  // Ringer/RingerOff
                HBITMAP hLightModeIcon = CreateMenuIconBitmap(L"\uE706");  // Brightness (太阳)
                HBITMAP hDarkModeIcon = CreateMenuIconBitmap(L"\uE708");  // Moon (月亮)
                HBITMAP hExitIcon = CreateMenuIconBitmap(L"\uE7E8", RGB(200, 60, 60));  // Power (红色)

                // 添加设置和退出到托盘菜单（带图标）
                MENUITEMINFOW mii = {};
                mii.cbSize = sizeof(MENUITEMINFOW);
                mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;

                mii.wID = IDM_SETTINGS;
                mii.dwTypeData = (LPWSTR)L"设置";
                mii.hbmpItem = hSettingsIcon;
                InsertMenuItemW(hTrayMenu, 0, TRUE, &mii);

                mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP | MIIM_STATE;
                mii.wID = IDM_NOTIFICATION;
                mii.dwTypeData = (LPWSTR)L"消息通知";
                mii.hbmpItem = hNotificationIcon;
                mii.fState = g_isNotificationEnabled ? MFS_CHECKED : MFS_UNCHECKED;
                InsertMenuItemW(hTrayMenu, 1, TRUE, &mii);

                // 日间模式
                mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP | MIIM_STATE;
                mii.wID = IDM_THEME_LIGHT;
                mii.dwTypeData = (LPWSTR)L"日间模式";
                mii.hbmpItem = hLightModeIcon;
                mii.fState = (g_themeMode == THEME_LIGHT || (g_themeMode == THEME_SYSTEM && !g_isDarkMode)) ? MFS_CHECKED : MFS_UNCHECKED;
                InsertMenuItemW(hTrayMenu, 2, TRUE, &mii);

                // 夜间模式
                mii.wID = IDM_THEME_DARK;
                mii.dwTypeData = (LPWSTR)L"夜间模式";
                mii.hbmpItem = hDarkModeIcon;
                mii.fState = (g_themeMode == THEME_DARK || (g_themeMode == THEME_SYSTEM && g_isDarkMode)) ? MFS_CHECKED : MFS_UNCHECKED;
                InsertMenuItemW(hTrayMenu, 3, TRUE, &mii);

                mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;
                mii.fState = 0;
                mii.wID = IDM_EXIT;
                mii.dwTypeData = (LPWSTR)L"退出";
                mii.hbmpItem = hExitIcon;
                InsertMenuItemW(hTrayMenu, 4, TRUE, &mii);

                // 显示托盘菜单
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hTrayMenu, TPM_RIGHTBUTTON | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd, NULL);

                // 释放菜单资源和位图
                DestroyMenu(hTrayMenu);
                DeleteObject(hSettingsIcon);
                DeleteObject(hNotificationIcon);
                DeleteObject(hLightModeIcon);
                DeleteObject(hDarkModeIcon);
                DeleteObject(hExitIcon);
            }
            break;
        }
        case WM_HOTKEY: {
            // 处理快捷键按下事件
            if (wParam == ID_HOTKEY_TOGGLE) {
                // 切换窗口可见性
                if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
                    if (g_hwndTagPopup) {
                        DestroyWindow(g_hwndTagPopup);
                        g_hwndTagPopup = NULL;
                    }
                    ShowWindow(hwnd, SW_HIDE);
                } else {
                    // 记录当前活动窗口（呼出剪贴板前的窗口）
                    g_previousActiveWindow = GetForegroundWindow();
                    ShowWindow(hwnd, SW_RESTORE);
                    ShowWindow(hwnd, SW_SHOW);
                    // 强制置顶显示，避免被其他窗口遮挡
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                    SetForegroundWindow(hwnd);
                    if (!g_isTopmost) {
                        // 非置顶模式下，短暂置顶后恢复
                        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                    }
                }
            }
            // 处理快捷粘贴快捷键（修饰键+1~9）
            else if (wParam >= ID_HOTKEY_PASTE_1 && wParam <= ID_HOTKEY_PASTE_9) {
                int pasteOffset = (int)(wParam - ID_HOTKEY_PASTE_1);  // 0-8，相对于可见区域的偏移

                // 使用缓存的顶部索引（避免额外的消息调用）
                int topIndex = g_listBoxTopIndex;

                // 计算实际要粘贴的项在 g_displayIndexMap 中的索引
                int visibleItemIndex = topIndex + pasteOffset;

                // 使用 g_displayIndexMap 获取实际的历史记录索引
                if (visibleItemIndex < (int)g_displayIndexMap.size()) {
                    int actualIndex = g_displayIndexMap[visibleItemIndex];
                    const ClipboardItem& item = g_history[actualIndex];

                    // 记录当前活动窗口
                    HWND hwndTarget = GetForegroundWindow();

                    // 复制内容到剪贴板
                    if (OpenClipboard(NULL)) {
                        EmptyClipboard();

                        if (item.type == TYPE_TEXT) {
                            // 文本类型：粘贴文本内容
                            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, (item.content.length() + 1) * sizeof(wchar_t));
                            if (hGlobal != NULL) {
                                wchar_t* pData = (wchar_t*)GlobalLock(hGlobal);
                                if (pData != NULL) {
                                    wcscpy_s(pData, item.content.length() + 1, item.content.c_str());
                                    GlobalUnlock(hGlobal);
                                    SetClipboardData(CF_UNICODETEXT, hGlobal);
                                }
                            }
                        } else if (item.type == TYPE_FILE) {
                            // 文件类型：粘贴文件路径
                            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, (item.content.length() + 1) * sizeof(wchar_t));
                            if (hGlobal != NULL) {
                                wchar_t* pData = (wchar_t*)GlobalLock(hGlobal);
                                if (pData != NULL) {
                                    wcscpy_s(pData, item.content.length() + 1, item.content.c_str());
                                    GlobalUnlock(hGlobal);
                                    SetClipboardData(CF_UNICODETEXT, hGlobal);
                                }
                            }
                        } else if (item.type == TYPE_IMAGE) {
                            // 图像类型：粘贴文件路径
                            std::wstring imagePath;
                            if (!item.imageFilePath.empty()) {
                                imagePath = item.imageFilePath;
                            } else if (!item.imageFileName.empty()) {
                                imagePath = GetImagesPath() + L"\\" + item.imageFileName;
                            }

                            if (!imagePath.empty()) {
                                HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, (imagePath.length() + 1) * sizeof(wchar_t));
                                if (hGlobal != NULL) {
                                    wchar_t* pData = (wchar_t*)GlobalLock(hGlobal);
                                    if (pData != NULL) {
                                        wcscpy_s(pData, imagePath.length() + 1, imagePath.c_str());
                                        GlobalUnlock(hGlobal);
                                        SetClipboardData(CF_UNICODETEXT, hGlobal);
                                    }
                                }
                            }
                        }

                        g_isRestoringClipboard = true;
                        CloseClipboard();
                    }

                    // 等待用户释放快捷键的修饰键
                    Sleep(100);

                    // 确保修饰键已释放（等待 Alt/Ctrl/Shift 键释放）
                    while ((GetAsyncKeyState(VK_MENU) & 0x8000) ||
                           (GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
                           (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
                        Sleep(10);
                    }

                    // 确保目标窗口在前台
                    if (hwndTarget != NULL && IsWindow(hwndTarget)) {
                        SetForegroundWindow(hwndTarget);
                        Sleep(50);
                    }

                    // 模拟 Ctrl+V 粘贴
                    keybd_event(VK_CONTROL, 0, 0, 0);
                    keybd_event('V', 0, 0, 0);
                    keybd_event('V', 0, KEYEVENTF_KEYUP, 0);
                    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);

                    if (g_isNotificationEnabled) {
                        ShowTrayBalloon(hwnd, L"快捷粘贴", L"已粘贴");
                    }
                }
            }
            break;
        }
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return 0;
}

// 注册窗口类
ATOM RegisterWindowClass(HINSTANCE hInstance) {
    WNDCLASSEXW wcex;
    
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW; // 不使用 CS_DROPSHADOW，避免阴影
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    
    // 使用资源文件中的自定义图标
    wcex.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON1));
    if (wcex.hIcon == NULL) {
        // 如果加载自定义图标失败，使用默认图标
        wcex.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    }

    wcex.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(RGB(245, 245, 245));  // 自定义背景色
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = L"SmartClip";

    // 使用资源文件中的自定义小图标
    wcex.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON1));
    if (wcex.hIconSm == NULL) {
        // 如果加载自定义小图标失败，使用默认图标
        wcex.hIconSm = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    }
    
    return RegisterClassExW(&wcex);
}

// 初始化应用程序
BOOL InitApplication(HINSTANCE hInstance, int nCmdShow) {
    if (!RegisterWindowClass(hInstance)) {
        return FALSE;
    }

    // 注册中转站窗口类
    RegisterTransferStationContainerClass(hInstance);
    RegisterTransferStationCardClass(hInstance);
    RegisterFlagpoleClass(hInstance);

    // 计算屏幕中央位置
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int windowWidth = 600;
    int windowHeight = 700;
    int x = (screenWidth - windowWidth) / 2;
    int y = (screenHeight - windowHeight) / 2;

    g_hwndMain = CreateWindowExW(WS_EX_COMPOSITED, L"SmartClip", L"Smart Clip", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              x, y, windowWidth, windowHeight, NULL, NULL, hInstance, NULL);

    if (!g_hwndMain) {
        return FALSE;
    }

    // 修改窗口样式：移除系统标题栏但保留边框
    LONG_PTR style = GetWindowLongPtrW(g_hwndMain, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    style |= WS_THICKFRAME;  // 重新添加可调整大小的边框
    SetWindowLongPtrW(g_hwndMain, GWL_STYLE, style);

    // 强制重新计算窗口框架
    SetWindowPos(g_hwndMain, NULL, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);

    // 移除窗口阴影
    BOOL bEnable = FALSE;
    DwmSetWindowAttribute(g_hwndMain, DWMWA_NCRENDERING_POLICY, &bEnable, sizeof(bEnable));
    
    ShowWindow(g_hwndMain, nCmdShow);
    UpdateWindow(g_hwndMain);
    
    return TRUE;
}

// 运行应用程序
int RunApplication() {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        // 处理应用内搜索框快捷键
        if (msg.message == WM_KEYDOWN && g_isSearchHotkeyEnabled && IsWindowVisible(g_hwndMain)) {
            UINT modifiers = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
            if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= MOD_SHIFT;
            if (GetKeyState(VK_MENU) & 0x8000) modifiers |= MOD_ALT;

            if (modifiers == g_searchHotkeyModifiers && msg.wParam == g_searchHotkeyVirtualKey) {
                SetFocus(g_hwndSearchBox);
                SendMessageW(g_hwndSearchBox, EM_SETSEL, 0, -1);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

// 简单的输入对话框函数
struct InitParam {
    const wchar_t* title;
    const wchar_t* prompt;
    wchar_t* result;
    int maxLen;
};

INT_PTR CALLBACK InputBoxProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static wchar_t* result = NULL;
    static int maxLen = 0;
    
    switch (message) {
        case WM_INITDIALOG: {
            if (lParam) {
                InitParam* initParam = (InitParam*)lParam;
                SetWindowTextW(hDlg, initParam->title);
                SetDlgItemTextW(hDlg, 1001, initParam->prompt);
                result = initParam->result;
                maxLen = initParam->maxLen;
                SendDlgItemMessageW(hDlg, 1002, EM_LIMITTEXT, maxLen - 1, 0);
            }
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                GetDlgItemTextW(hDlg, 1002, result, maxLen);
                EndDialog(hDlg, TRUE);
                return TRUE;
            } else if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hDlg, FALSE);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

bool InputBox(HWND hwnd, const wchar_t* title, const wchar_t* prompt, wchar_t* result, int maxLen) {
    HINSTANCE hInstance = GetModuleHandle(NULL);
    
    // 简化实现：使用创建窗口的方式
    HWND hDialog = CreateWindowExW(0, L"#32770", title, WS_POPUP | WS_CAPTION | DS_MODALFRAME | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 250, 120, hwnd, NULL, hInstance, NULL);
    
    if (!hDialog) return false;
    
    // 创建静态文本
    HWND hStatic = CreateWindowExW(0, L"STATIC", prompt, WS_CHILD | WS_VISIBLE,
        10, 10, 230, 20, hDialog, (HMENU)1001, hInstance, NULL);
    
    // 创建编辑框
    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        10, 35, 230, 20, hDialog, (HMENU)1002, hInstance, NULL);
    SendDlgItemMessageW(hDialog, 1002, EM_LIMITTEXT, maxLen - 1, 0);
    
    // 创建确定按钮
    HWND hOK = CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        150, 65, 80, 25, hDialog, (HMENU)IDOK, hInstance, NULL);
    
    // 创建取消按钮
    HWND hCancel = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        60, 65, 80, 25, hDialog, (HMENU)IDCANCEL, hInstance, NULL);
    
    // 设置字体
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessageW(hStatic, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hOK, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // 居中显示
    RECT rectParent, rectDialog;
    GetWindowRect(hwnd, &rectParent);
    GetWindowRect(hDialog, &rectDialog);
    int x = rectParent.left + (rectParent.right - rectParent.left) / 2 - (rectDialog.right - rectDialog.left) / 2;
    int y = rectParent.top + (rectParent.bottom - rectParent.top) / 2 - (rectDialog.bottom - rectDialog.top) / 2;
    SetWindowPos(hDialog, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    
    // 显示对话框并处理消息
    ShowWindow(hDialog, SW_SHOW);
    SetFocus(hEdit);
    
    MSG msg;
    bool resultValue = false;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hDialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        
        if (msg.message == WM_COMMAND && msg.hwnd == hDialog) {
            if (LOWORD(msg.wParam) == IDOK) {
                GetDlgItemTextW(hDialog, 1002, result, maxLen);
                resultValue = true;
                break;
            } else if (LOWORD(msg.wParam) == IDCANCEL) {
                resultValue = false;
                break;
            }
        }
    }
    
    DestroyWindow(hDialog);
    return resultValue;
}

// 入口函数
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPWSTR lpCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(lpCmdLine);
    // 创建命名互斥量，检测是否已有实例在运行
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Global\\SmartClipMutex");
    if (hMutex == NULL) {
        // 创建互斥量失败，退出程序
        return 1;
    }
    
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // 显示"本程序已在运行"的消息提示
        MessageBoxW(NULL, L"本程序已在运行", L"Smart Clip", MB_OK | MB_ICONINFORMATION);
        
        // 已有实例在运行，尝试找到并激活它
        HWND hExistingWindow = FindWindowW(L"SmartClip", L"Smart Clip");
        if (hExistingWindow != NULL) {
            // 显示窗口（如果隐藏）并设置为前台窗口
            ShowWindow(hExistingWindow, SW_SHOW);
            SetForegroundWindow(hExistingWindow);
        }
        CloseHandle(hMutex);
        return 0;
    }
    
    // 初始化 GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) != Ok) {
        MessageBoxW(NULL, L"GDI+ 初始化失败", L"Smart Clip", MB_OK | MB_ICONERROR);
        CloseHandle(hMutex);
        return 1;
    }

    // 初始化 OLE（用于拖放）
    OleInitialize(NULL);

    INITCOMMONCONTROLSEX iccex;
    iccex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    iccex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;  // ICC_WIN95_CLASSES 包含 tooltip 控件
    InitCommonControlsEx(&iccex);

    if (!InitApplication(hInstance, nCmdShow)) {
        GdiplusShutdown(gdiplusToken);
        CloseHandle(hMutex);
        return FALSE;
    }

    int result = RunApplication();

    // 程序结束时释放 OLE、GDI+ 和互斥量
    OleUninitialize();
    GdiplusShutdown(gdiplusToken);
    CloseHandle(hMutex);
    return result;
}
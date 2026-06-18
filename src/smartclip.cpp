#include "card_renderer.h"
#include "graphics_utils.h"
#include "history.h"
#include "hotkey.h"
#include "i18n.h"
#include "image_handler.h"
#include "resource.h" // 添加资源头文件
#include "search.h"
#include "settings.h"
#include "tag_popup.h"
#include "text_utils.h"
#include "theme.h"
#include "themed_dialog.h"
#include "tray.h"
#include <algorithm> // 用于std::remove_if
#include <cmath>     // 用于sin函数
#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <objidl.h> // MinGW 下必须在 gdiplus.h 之前,提供 PROPID
#include <ole2.h>   // 用于OLE拖放
#include <regex>
#include <set>
#include <shellapi.h> // 用于 ShellExecuteW
#include <shlobj.h>   // 用于拖放
#include <shlwapi.h>
#include <windows.h>
#include <windowsx.h> // 用于 GET_X_LPARAM, GET_Y_LPARAM

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
bool InputBox(HWND hwnd, const wchar_t *title, const wchar_t *prompt,
              wchar_t *result, int maxLen);

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
#define IDM_OPEN_LOCATION 3007 // 打开所在位置
#define IDM_BATCH_ADD_TAG 3008 // 批量加入标签

// 标签菜单ID（动态分配，从3100开始）
#define IDM_TAG_BASE 3100
#define IDM_TAG_FILTER_ALL 3200   // 全部收藏筛选
#define IDM_TAG_FILTER_BASE 3201  // 标签筛选基础ID
#define IDM_TAG_ADD_NEW 3300      // 新增标签
#define IDM_BATCH_PASTE_ASC 3400  // 连续粘贴-正序
#define IDM_BATCH_PASTE_DESC 3401 // 连续粘贴-反序

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
static bool g_hotkeyRegisterPendingRetry = false;
// 标题栏按钮原始窗口过程
WNDPROC g_oldTitleTopmostProc = NULL;
WNDPROC g_oldTitleMinimizeProc = NULL;
WNDPROC g_oldTitleMaximizeProc = NULL;
WNDPROC g_oldTitleCloseProc = NULL;

// 获取当前模式的颜色
inline COLORREF GetBgColor() { return GetThemeWindowBgColor(); }
inline COLORREF GetWhiteColor() { return GetThemeSurfaceColor(); }
inline COLORREF GetTextColor() { return GetThemeTextPrimaryColor(); }
inline COLORREF GetAccentColor() { return GetThemeAccentColor(); }
inline COLORREF GetAccentStrongColor() { return GetThemeAccentStrongColor(); }

// 当前右键选中的索引
int g_contextMenuIndex = -1;
// 记录呼出剪贴板前的活动窗口
HWND g_previousActiveWindow = NULL;

// 中转站相关全局变量

// 列表框子类化
WNDPROC g_oldListBoxProc = NULL;
HWND g_hwndListBoxTooltip = NULL;         // 列表框 Tooltip
int g_lastTooltipIndex = -1;              // 上次显示 Tooltip 的项目索引
int g_hoverIconIndex = -1;                // 鼠标悬浮的图标所在项目索引
bool g_isHoveringIcon = false;            // 鼠标是否悬浮在图标上
int g_hoverImageIndex = -1;               // 鼠标悬浮的图像所在项目索引
bool g_isHoveringImage = false;           // 鼠标是否悬浮在图像上
int g_hoverFolderIndex = -1;              // 鼠标悬浮的文件夹名称索引
bool g_isHoveringFolder = false;          // 鼠标是否悬浮在文件夹名称上
float g_folderUnderlineProgress = 0.0f;   // 文件夹下划线动画进度 0.0-1.0
bool g_folderUnderlineAnimating = false;  // 是否正在下划线动画
static std::wstring g_listBoxTooltipText; // 跟踪 Tooltip 文本缓存

// 文件拖放相关
bool g_isDragging = false;       // 是否正在拖拽
POINT g_dragStartPoint = {0, 0}; // 拖拽起始点
int g_dragItemIndex = -1;        // 正在拖拽的项目索引
bool g_dragOccurred = false;     // 本次按下是否发生了拖拽
#define DRAG_THRESHOLD 5         // 拖拽阈值（像素）

// IDropSource 实现
class CDropSource : public IDropSource {
private:
  LONG m_refCount;

public:
  CDropSource() : m_refCount(1) {}
  virtual ~CDropSource() = default;

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) override {
    if (riid == IID_IUnknown || riid == IID_IDropSource) {
      *ppvObject = this;
      AddRef();
      return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return InterlockedIncrement(&m_refCount);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    LONG count = InterlockedDecrement(&m_refCount);
    if (count == 0)
      delete this;
    return count;
  }

  // IDropSource
  HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscapePressed,
                                              DWORD grfKeyState) override {
    if (fEscapePressed)
      return DRAGDROP_S_CANCEL;
    if (!(grfKeyState & MK_LBUTTON))
      return DRAGDROP_S_DROP;
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
  IDropTargetHelper *m_pDropTargetHelper;

public:
  CDropTarget() : m_refCount(1), m_pDropTargetHelper(NULL) {
    CoCreateInstance(CLSID_DragDropHelper, NULL, CLSCTX_INPROC_SERVER,
                     IID_IDropTargetHelper, (void **)&m_pDropTargetHelper);
  }
  virtual ~CDropTarget() {
    if (m_pDropTargetHelper)
      m_pDropTargetHelper->Release();
  }

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
      *ppv = static_cast<IDropTarget *>(this);
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return InterlockedIncrement(&m_refCount);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    LONG count = InterlockedDecrement(&m_refCount);
    if (count == 0)
      delete this;
    return count;
  }

  // IDropTarget
  HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *pDataObj,
                                      DWORD /*grfKeyState*/, POINTL pt,
                                      DWORD *pdwEffect) override {
    // 先设置效果为 COPY，再通知 helper
    *pdwEffect = DROPEFFECT_COPY;
    if (m_pDropTargetHelper) {
      POINT point = {pt.x, pt.y};
      m_pDropTargetHelper->DragEnter(g_hwndMain, pDataObj, &point, *pdwEffect);
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragOver(DWORD /*grfKeyState*/, POINTL pt,
                                     DWORD *pdwEffect) override {
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
  HRESULT STDMETHODCALLTYPE Drop(IDataObject *pDataObj, DWORD /*grfKeyState*/,
                                 POINTL pt, DWORD *pdwEffect) override {
    if (m_pDropTargetHelper) {
      POINT point = {pt.x, pt.y};
      m_pDropTargetHelper->Drop(pDataObj, &point, *pdwEffect);
    }
    *pdwEffect = DROPEFFECT_NONE;
    return S_OK;
  }
};

static CDropTarget *g_pDropTarget = NULL;

// 创建文件拖放数据对象
IDataObject *CreateFileDataObject(const std::wstring &filePath) {
  IDataObject *pDataObject = NULL;

  // 使用 Shell 创建数据对象
  PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(filePath.c_str());
  if (pidl) {
    IShellFolder *pDesktop = NULL;
    if (SUCCEEDED(SHGetDesktopFolder(&pDesktop))) {
      PIDLIST_RELATIVE pidlChild = ILFindLastID(pidl);
      PIDLIST_ABSOLUTE pidlParent = ILClone(pidl);
      ILRemoveLastID(pidlParent);

      IShellFolder *pFolder = NULL;
      if (SUCCEEDED(pDesktop->BindToObject(pidlParent, NULL, IID_IShellFolder,
                                           (void **)&pFolder))) {
        pFolder->GetUIObjectOf(NULL, 1, (PCUITEMID_CHILD *)&pidlChild,
                               IID_IDataObject, NULL, (void **)&pDataObject);
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
void SetDragImage(IDataObject *pDataObject, const std::wstring &filePath,
                  POINT /*ptStart*/) {
  IDragSourceHelper *pDragSourceHelper = NULL;
  if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, NULL,
                                 CLSCTX_INPROC_SERVER, IID_IDragSourceHelper,
                                 (void **)&pDragSourceHelper))) {
    IDragSourceHelper2 *pDragSourceHelper2 = NULL;
    if (SUCCEEDED(pDragSourceHelper->QueryInterface(
            IID_PPV_ARGS(&pDragSourceHelper2)))) {
      pDragSourceHelper2->SetFlags(0x0001);
      pDragSourceHelper2->Release();
    }

    SHFILEINFOW sfi = {};
    SHGetFileInfoW(filePath.c_str(), 0, &sfi, sizeof(sfi),
                   SHGFI_ICON | SHGFI_LARGEICON);

    if (sfi.hIcon) {
      std::wstring fileName = filePath;
      size_t pos = fileName.find_last_of(L"\\/");
      if (pos != std::wstring::npos)
        fileName = fileName.substr(pos + 1);

      int iconSize = 32;
      int textHeight = 20;
      int hPadding = 16;

      // 测量文件名宽度，动态计算位图宽度
      HDC hdcScreen = GetDC(NULL);
      HDC hdcMeasure = CreateCompatibleDC(hdcScreen);
      Gdiplus::Graphics gMeasure(hdcMeasure);
      Gdiplus::Font font(L"Microsoft YaHei", 9.0f);
      Gdiplus::RectF bounds;
      gMeasure.MeasureString(fileName.c_str(), -1, &font, Gdiplus::PointF(0, 0),
                             &bounds);
      DeleteDC(hdcMeasure);

      int textWidth = (int)(bounds.Width + 0.5f) + hPadding * 2;
      int minWidth = iconSize + hPadding * 2;
      int maxWidth = 320;
      int bmpWidth = textWidth;
      if (bmpWidth < minWidth)
        bmpWidth = minWidth;
      if (bmpWidth > maxWidth)
        bmpWidth = maxWidth;
      int bmpHeight = iconSize + textHeight + 4;

      // 使用 32 位 ARGB 位图实现真正的 alpha 透明
      BITMAPINFO bmi = {};
      bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bmi.bmiHeader.biWidth = bmpWidth;
      bmi.bmiHeader.biHeight = -bmpHeight;
      bmi.bmiHeader.biPlanes = 1;
      bmi.bmiHeader.biBitCount = 32;
      bmi.bmiHeader.biCompression = BI_RGB;

      void *pBits = NULL;
      HBITMAP hBitmap =
          CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
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
      DrawIconEx(hdcMem, iconX, 2, sfi.hIcon, iconSize, iconSize, 0, NULL,
                 DI_NORMAL);

      // 文件名
      Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 50, 50, 50));
      Gdiplus::StringFormat sf;
      sf.SetAlignment(Gdiplus::StringAlignmentCenter);
      sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
      sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
      Gdiplus::RectF textRect(2.0f, (float)(iconSize + 2),
                              (float)(bmpWidth - 4), (float)textHeight);
      g.DrawString(fileName.c_str(), -1, &font, textRect, &sf, &textBrush);

      g.Flush();

      // 修正预乘 alpha（GDI 绘制的像素需要预乘）
      BYTE *pixels = (BYTE *)pBits;
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
#define ID_FAVORITE_TOOLTIP_TIMER 102
HWND g_hwndSearchClearBtn = NULL;
WNDPROC g_oldSearchClearBtnProc = NULL;
bool g_isSearchClearBtnHover = false;

// 置顶按钮子类化（悬浮效果）
WNDPROC g_oldTopmostBtnProc = NULL;
HWND g_hwndTopmostBtn = NULL;

// 批量编辑按钮子类化（悬浮效果）
WNDPROC g_oldBatchEditBtnProc = NULL;
HWND g_hwndBatchEditBtn = NULL;
bool g_isBatchEditBtnHover = false;
bool g_isBatchEditMode = false;                  // 批量编辑模式状态
std::vector<int> g_selectedItems;                // 批量编辑模式下选中的记录索引
int g_batchSelectionAnchorDisplayIndex = LB_ERR; // Shift 范围选择锚点
WNDPROC g_oldFilterFavoriteProc = NULL;
HWND g_hwndMainTooltip = NULL;
bool g_isFavoriteTooltipVisible = false;

// 按钮图片句柄
Gdiplus::Image *g_imgTopmostSelected = NULL;
Gdiplus::Image *g_imgTopmostUnselected = NULL;
Gdiplus::Image *g_imgFolderIcon = NULL;  // 文件夹图标
Gdiplus::Image *g_imgNoExistIcon = NULL; // 文件不存在图标

// 置顶按钮波浪动画
#define ID_TOPMOST_ANIM_TIMER 201
#define ID_BATCH_EDIT_ANIM_TIMER 202
#define ID_FOLDER_UNDERLINE_TIMER 203
float g_topmostAnimProgress = 0.0f; // 动画进度 0.0-1.0
bool g_topmostAnimating = false;    // 是否正在动画
bool g_topmostAnimDirection = true; // true=选中动画, false=取消选中动画
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
bool g_isScrollbarHovered = false;
bool g_isScrollbarDragging = false;
int g_scrollbarDragOffsetY = 0;

// 列表框顶部索引缓存（用于快捷键提示，避免频繁调用LB_GETTOPINDEX）
int g_listBoxTopIndex = 0;
int g_shortcutStartDisplayIndex = -1; // 翻到下一页时，从新记录开始显示快捷键
int g_shortcutEndDisplayIndexExclusive = -1; // 向上翻页补位时，旧记录的截止索引
bool g_quickPasteHintVisible = false;

enum QuickPasteHotkeyId {
  ID_HOTKEY_PASTE_1 = 4101,
  ID_HOTKEY_PASTE_2,
  ID_HOTKEY_PASTE_3,
  ID_HOTKEY_PASTE_4,
  ID_HOTKEY_PASTE_5,
  ID_HOTKEY_PASTE_6,
  ID_HOTKEY_PASTE_7,
  ID_HOTKEY_PASTE_8,
  ID_HOTKEY_PASTE_9,
  ID_HOTKEY_PASTE_10
};

// 翻页相关
#define ITEMS_PER_PAGE 9             // 每页显示的项目数
int g_currentPage = 0;               // 当前页码（从0开始）
int g_totalPages = 1;                // 总页数
HWND g_hwndPageUpBtn = NULL;         // 上一页按钮句柄
HWND g_hwndPageDownBtn = NULL;       // 下一页按钮句柄
bool g_isPageUpBtnHover = false;     // 上一页按钮悬浮状态
bool g_isPageDownBtnHover = false;   // 下一页按钮悬浮状态
WNDPROC g_oldPageUpBtnProc = NULL;   // 上一页按钮原始窗口过程
WNDPROC g_oldPageDownBtnProc = NULL; // 下一页按钮原始窗口过程

// 平滑滚动相关
#define ID_SMOOTH_SCROLL_TIMER 101
static float g_smoothScrollTarget = 0.0f;  // 目标滚动位置
static float g_smoothScrollCurrent = 0.0f; // 当前滚动位置
static bool g_smoothScrollActive = false;  // 是否正在平滑滚动
static HWND g_smoothScrollListBox = NULL;  // 正在滚动的列表框
static int g_smoothScrollExpectedTop = -1; // 平滑翻页后快捷键编号的起始索引
static int g_smoothScrollExpectedEndExclusive =
    -1; // 平滑向上翻页后的旧记录截止索引
static DWORD g_vimNavPendingGTick = 0;

static void ClearShortcutDisplayBounds() {
  g_shortcutStartDisplayIndex = -1;
  g_shortcutEndDisplayIndexExclusive = -1;
}

static int GetListBoxVisibleHeight(HWND hwnd);
static int CalculateVisibleItemCount(int startIndex);
static bool IsSelectableDisplayIndex(int index);

static std::wstring GetQuickPasteModifierText() {
  switch (g_quickPasteModifiers) {
  case MOD_ALT:
    return L"Alt+";
  case MOD_CONTROL:
    return L"Ctrl+";
  case MOD_SHIFT:
    return L"Shift+";
  case MOD_CONTROL | MOD_ALT:
    return L"Ctrl+Alt+";
  case MOD_CONTROL | MOD_SHIFT:
    return L"Ctrl+Shift+";
  case MOD_ALT | MOD_SHIFT:
    return L"Alt+Shift+";
  default:
    return L"Alt+";
  }
}

void RegisterQuickPasteHotkeys(HWND hwnd) {
  for (int i = 0; i < 10; ++i) {
    UINT vk = (i == 9) ? '0' : (UINT)('1' + i);
    RegisterHotKey(hwnd, ID_HOTKEY_PASTE_1 + i, g_quickPasteModifiers, vk);
  }
}

void UnregisterQuickPasteHotkeys(HWND hwnd) {
  for (int i = 0; i < 10; ++i) {
    ::UnregisterHotKey(hwnd, ID_HOTKEY_PASTE_1 + i);
  }
}

static int CollectVisibleShortcutDisplayIndices(int *outIds, int maxCount);
static int GetShortcutIndexForDisplayIndex(int displayIndex) {
  int visibleIds[10] = {};
  int visibleCount = CollectVisibleShortcutDisplayIndices(visibleIds, 10);
  for (int i = 0; i < visibleCount; ++i) {
    if (visibleIds[i] == displayIndex)
      return i;
  }
  return -1;
}

static int CollectVisibleShortcutDisplayIndices(int *outIds, int maxCount) {
  if (!outIds || maxCount <= 0 || g_displayIndexMap.empty())
    return 0;

  int startHint = g_shortcutStartDisplayIndex;
  int endHint = g_shortcutEndDisplayIndexExclusive;
  int visibleLimit = CalculateVisibleItemCount(g_listBoxTopIndex);
  int totalItems = (int)g_displayIndexMap.size();
  int count = 0;
  const int headerVisibleThreshold = 9;
  int visibleHeight =
      g_hwndListBox ? GetListBoxVisibleHeight(g_hwndListBox) : 0;
  int startIndex = g_listBoxTopIndex;

  if (startHint >= 0)
    startIndex = std::max(startIndex, startHint);

  if (g_hwndListBox) {
    while (startIndex > 0) {
      RECT rcPrev = {};
      if (SendMessageW(g_hwndListBox, LB_GETITEMRECT, startIndex - 1,
                       (LPARAM)&rcPrev) == LB_ERR) {
        break;
      }
      if (rcPrev.bottom <= 0)
        break;
      if (startHint >= 0 && startIndex - 1 < startHint)
        break;
      --startIndex;
    }
  }

  for (int i = startIndex; i < totalItems && count < maxCount; ++i) {
    if (endHint >= 0 && i >= endHint)
      break;

    RECT rcItem = {};
    if (g_hwndListBox && SendMessageW(g_hwndListBox, LB_GETITEMRECT, i,
                                      (LPARAM)&rcItem) != LB_ERR) {
      int itemHeight = rcItem.bottom - rcItem.top;
      if (rcItem.bottom <= 0)
        continue;
      if (rcItem.top >= 0 && count == 0) {
      } else if (rcItem.top + headerVisibleThreshold <= 0) {
        continue;
      }
      if (visibleHeight > 0 && rcItem.top + itemHeight / 2 > visibleHeight)
        break;
    }

    if (!IsSelectableDisplayIndex(i))
      continue;

    outIds[count++] = i;
    if (count >= visibleLimit)
      break;
  }
  return count;
}

static bool PasteHistoryItemByDisplayIndex(HWND hwnd, int displayIndex) {
  if (displayIndex < 0 || displayIndex >= (int)g_displayIndexMap.size())
    return false;

  int actualIndex = g_displayIndexMap[displayIndex];
  if (actualIndex < 0 || actualIndex >= (int)g_history.size())
    return false;

  const ClipboardItem &item = g_history[actualIndex];
  HWND hwndTarget = GetForegroundWindow();

  if (!OpenClipboard(NULL))
    return false;

  EmptyClipboard();

  if (item.type == TYPE_TEXT || item.type == TYPE_FILE) {
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, (item.content.length() + 1) *
                                                     sizeof(wchar_t));
    if (hGlobal != NULL) {
      wchar_t *pData = (wchar_t *)GlobalLock(hGlobal);
      if (pData != NULL) {
        wcscpy_s(pData, item.content.length() + 1, item.content.c_str());
        GlobalUnlock(hGlobal);
        SetClipboardData(CF_UNICODETEXT, hGlobal);
      }
    }
  } else if (item.type == TYPE_IMAGE) {
    std::wstring imagePath;
    if (!item.imageFilePath.empty()) {
      imagePath = item.imageFilePath;
    } else if (!item.imageFileName.empty()) {
      imagePath = GetImagesPath() + L"\\" + item.imageFileName;
    }

    if (!imagePath.empty()) {
      HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE,
                                    (imagePath.length() + 1) * sizeof(wchar_t));
      if (hGlobal != NULL) {
        wchar_t *pData = (wchar_t *)GlobalLock(hGlobal);
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

  Sleep(100);
  while ((GetAsyncKeyState(VK_MENU) & 0x8000) ||
         (GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
         (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
    Sleep(10);
  }

  if (hwndTarget != NULL && IsWindow(hwndTarget)) {
    SetForegroundWindow(hwndTarget);
    Sleep(50);
  }

  keybd_event(VK_CONTROL, 0, 0, 0);
  keybd_event('V', 0, 0, 0);
  keybd_event('V', 0, KEYEVENTF_KEYUP, 0);
  keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);

  IncrementPasteCount();
  if (g_isNotificationEnabled) {
    ShowTrayBalloon(hwnd, L"快捷粘贴", L"已粘贴");
  }
  return true;
}

// 计算单个项目的高度（基于显示索引）
int GetItemDisplayHeight(int displayIndex) {
  if (displayIndex < 0 || displayIndex >= (int)g_displayIndexMap.size()) {
    return 57; // 默认文本高度
  }

  int actualIndex = g_displayIndexMap[displayIndex];
  if (actualIndex < 0 || actualIndex >= (int)g_history.size()) {
    return 57;
  }

  const ClipboardItem &item = g_history[actualIndex];

  if (item.type == TYPE_IMAGE) {
    // 检查图像尺寸是否有效
    if (item.imageWidth <= 0 || item.imageHeight <= 0) {
      return 87; // 默认图像高度
    }

    // 获取列表框宽度
    RECT rcListBox;
    GetClientRect(g_hwndListBox, &rcListBox);
    int listBoxWidth = rcListBox.right - rcListBox.left - 20;
    if (listBoxWidth < 100)
      listBoxWidth = 560;

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

static int GetListBoxVisibleHeight(HWND hwnd);
static void UpdateShortcutEndForUpwardFill(int oldTop);

// 计算从指定索引开始，在可视区域内能完整显示的项目数
int CalculateVisibleItemCount(int startIndex) {
  if (g_hwndListBox == NULL)
    return ITEMS_PER_PAGE;

  RECT rcListBox;
  GetClientRect(g_hwndListBox, &rcListBox);
  int visibleHeight = rcListBox.bottom - rcListBox.top;

  int count = 0;
  int totalItems = (int)g_displayIndexMap.size();

  // 使用 LB_GETITEMRECT 获取每个项目的实际矩形
  for (int i = startIndex; i < totalItems && i < startIndex + 10; i++) {
    RECT rcItem;
    if (SendMessageW(g_hwndListBox, LB_GETITEMRECT, i, (LPARAM)&rcItem) !=
        LB_ERR) {
      // 检查项目头部区域是否在可视范围内（快捷键显示在头部）
      int itemHeight = rcItem.bottom - rcItem.top;
      if (rcItem.top + itemHeight / 2 > visibleHeight) {
        break; // 项目大部分不可见，停止计数
      }
      count++;
    } else {
      break;
    }
  }

  return count > 0 ? count : 1; // 至少返回1
}

static void UpdateShortcutEndForUpwardFill(int oldTop) {
  g_shortcutEndDisplayIndexExclusive = -1;
  if (!g_hwndListBox || oldTop <= g_listBoxTopIndex)
    return;

  int visibleCount = CalculateVisibleItemCount(g_listBoxTopIndex);
  if (g_listBoxTopIndex < visibleCount)
    g_shortcutEndDisplayIndexExclusive = oldTop;
}

// 计算下一页的起始索引（确保当前页最后一个不完整显示的项目成为下一页第一个）
int CalculateNextPageIndex(int currentTopIndex) {
  if (g_hwndListBox == NULL)
    return currentTopIndex + ITEMS_PER_PAGE;

  RECT rcListBox;
  GetClientRect(g_hwndListBox, &rcListBox);
  int visibleHeight = rcListBox.bottom - rcListBox.top;

  int totalItems = (int)g_displayIndexMap.size();

  // 使用 LB_GETITEMRECT 获取每个项目的实际矩形
  for (int i = currentTopIndex; i < totalItems; i++) {
    RECT rcItem;
    if (SendMessageW(g_hwndListBox, LB_GETITEMRECT, i, (LPARAM)&rcItem) !=
        LB_ERR) {
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
  if (g_hwndListBox == NULL || currentTopIndex <= 0)
    return 0;

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

static int GetListBoxVisibleHeight(HWND hwnd) {
  if (!hwnd)
    return 0;
  RECT rcListBox;
  GetClientRect(hwnd, &rcListBox);
  int height = (int)(rcListBox.bottom - rcListBox.top);
  return (height > 0) ? height : 0;
}

static int GetTotalListContentHeight() {
  int totalHeight = 0;
  for (int i = 0; i < (int)g_displayIndexMap.size(); ++i)
    totalHeight += GetItemDisplayHeight(i);
  return totalHeight;
}

static int GetContentOffsetForTopIndex(int topIndex) {
  if (topIndex <= 0)
    return 0;
  if (topIndex > (int)g_displayIndexMap.size())
    topIndex = (int)g_displayIndexMap.size();

  int offset = 0;
  for (int i = 0; i < topIndex; ++i)
    offset += GetItemDisplayHeight(i);
  return offset;
}

static int GetMaxListScrollOffset(HWND hwnd) {
  int visibleHeight = GetListBoxVisibleHeight(hwnd);
  int totalHeight = GetTotalListContentHeight();
  return std::max(0, totalHeight - visibleHeight);
}

static int GetTopIndexForContentOffset(int contentOffset) {
  if (contentOffset <= 0)
    return 0;

  int totalHeight = 0;
  int totalItems = (int)g_displayIndexMap.size();
  for (int i = 0; i < totalItems; ++i) {
    int itemHeight = GetItemDisplayHeight(i);
    if (totalHeight + itemHeight > contentOffset)
      return i;
    totalHeight += itemHeight;
  }
  return std::max(0, totalItems - 1);
}

static int GetListBoxMaxTopIndex() {
  if (g_hwndListBox == NULL)
    return 0;

  RECT rcListBox;
  GetClientRect(g_hwndListBox, &rcListBox);
  int visibleHeight = rcListBox.bottom - rcListBox.top;
  if (visibleHeight <= 0)
    return 0;

  int totalItems = (int)g_displayIndexMap.size();
  if (totalItems <= 0)
    return 0;

  int totalHeight = 0;
  for (int i = totalItems - 1; i >= 0; i--) {
    int itemHeight = GetItemDisplayHeight(i);
    if (totalHeight + itemHeight > visibleHeight) {
      return i + 1;
    }
    totalHeight += itemHeight;
  }
  return 0;
}

static bool NeedsCustomScrollbar() {
  return g_isCustomScrollbarEnabled && GetListBoxMaxTopIndex() > 0;
}

static void HideNativeListBoxScrollbar(HWND hwnd);

static int GetCustomScrollbarTrackWidth() {
  return NeedsCustomScrollbar() ? 12 : 0;
}

static int GetCustomScrollbarReservedWidth() {
  if (!NeedsCustomScrollbar())
    return 0;
  return GetCustomScrollbarTrackWidth() + 2;
}

static bool GetCustomScrollbarTrackRect(HWND hwnd, RECT *rcTrack) {
  if (!hwnd || !rcTrack || !NeedsCustomScrollbar())
    return false;

  RECT rcClient;
  GetClientRect(hwnd, &rcClient);
  int scrollbarWidth = GetCustomScrollbarTrackWidth();
  rcTrack->left = rcClient.right - scrollbarWidth;
  rcTrack->top = rcClient.top;
  rcTrack->right = rcClient.right;
  rcTrack->bottom = rcClient.bottom;
  return true;
}

static bool GetCustomScrollbarThumbRect(HWND hwnd, RECT *rcThumb) {
  if (!hwnd || !rcThumb)
    return false;

  RECT rcTrack;
  if (!GetCustomScrollbarTrackRect(hwnd, &rcTrack))
    return false;

  int maxScrollOffset = GetMaxListScrollOffset(hwnd);
  if (maxScrollOffset <= 0)
    return false;

  int currentTop = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
  int trackHeight = rcTrack.bottom - rcTrack.top;
  int visibleHeight = GetListBoxVisibleHeight(hwnd);
  int totalContentHeight = GetTotalListContentHeight();
  if (trackHeight <= 0 || visibleHeight <= 0 || totalContentHeight <= 0)
    return false;

  int drawableTrackHeight = std::max(0, trackHeight - 4);
  int thumbHeight = (visibleHeight * drawableTrackHeight) / totalContentHeight;
  if (thumbHeight < 30)
    thumbHeight = 30;
  if (thumbHeight > drawableTrackHeight)
    thumbHeight = drawableTrackHeight;

  int thumbY = 0;
  int travel = drawableTrackHeight - thumbHeight;
  if (travel > 0) {
    int currentOffset = GetContentOffsetForTopIndex(currentTop);
    thumbY = (currentOffset * travel + maxScrollOffset / 2) / maxScrollOffset;
  }

  rcThumb->left = std::max(rcTrack.left + 2, rcTrack.right - 8);
  rcThumb->top = rcTrack.top + thumbY + 2;
  rcThumb->right = rcTrack.right - 2;
  rcThumb->bottom = rcThumb->top + thumbHeight;
  if (rcThumb->bottom < rcThumb->top + 12)
    rcThumb->bottom = rcThumb->top + 12;
  if (rcThumb->bottom > rcTrack.bottom - 2)
    rcThumb->bottom = rcTrack.bottom - 2;
  return true;
}

static void StartScrollbarHideTimer(HWND hwnd) {
  KillTimer(hwnd, ID_SCROLLBAR_HIDE_TIMER);
  int hideDelay = g_customScrollbarHideDelayMs;
  if (hideDelay < 600)
    hideDelay = 600;
  if (hideDelay > 2000)
    hideDelay = 2000;
  if (NeedsCustomScrollbar())
    SetTimer(hwnd, ID_SCROLLBAR_HIDE_TIMER, (UINT)hideDelay, NULL);
}

static void InvalidateCustomScrollbarArea(HWND hwnd, BOOL erase = FALSE) {
  RECT rcTrack = {};
  if (GetCustomScrollbarTrackRect(hwnd, &rcTrack))
    InvalidateRect(hwnd, &rcTrack, erase);
}

// 滑块状态缓存：仅当状态真的发生变化时才触发重绘，避免重复失效产生闪烁
static RECT g_lastThumbRect = {0, 0, 0, 0};
static bool g_lastThumbValid = false;
static bool g_lastThumbVisible = false;
static bool g_lastThumbHovered = false;
static bool g_lastThumbDragging = false;

static void UpdateScrollbarCacheSnapshot(const RECT *thumbRect, bool hasThumb) {
  if (thumbRect) {
    g_lastThumbRect = *thumbRect;
  } else {
    SetRectEmpty(&g_lastThumbRect);
  }
  g_lastThumbValid = hasThumb;
  g_lastThumbVisible = g_scrollbarVisible;
  g_lastThumbHovered = g_isScrollbarHovered;
  g_lastThumbDragging = g_isScrollbarDragging;
}

static void RefreshScrollbarIfChanged(HWND hwnd) {
  if (!hwnd)
    return;
  RECT rcNew = {};
  bool hasNew = g_scrollbarVisible && GetCustomScrollbarThumbRect(hwnd, &rcNew);
  bool stateChanged = (g_lastThumbVisible != g_scrollbarVisible) ||
                      (g_lastThumbHovered != g_isScrollbarHovered) ||
                      (g_lastThumbDragging != g_isScrollbarDragging);
  bool rectChanged =
      (hasNew != g_lastThumbValid) ||
      (hasNew && g_lastThumbValid && !EqualRect(&rcNew, &g_lastThumbRect));
  if (!stateChanged && !rectChanged)
    return;

  // 拖动时滑块可能跨越大范围移动，仅失效旧/新滑块的并集会留下残影。
  // 直接失效整个滚动条轨道，确保旧位置被彻底清除。
  if (g_isScrollbarDragging && g_scrollbarVisible) {
    InvalidateCustomScrollbarArea(hwnd, FALSE);
  } else {
    RECT rcDirty = {0, 0, 0, 0};
    bool hasDirty = false;
    if (g_lastThumbValid) {
      rcDirty = g_lastThumbRect;
      hasDirty = true;
    }
    if (hasNew) {
      if (hasDirty) {
        rcDirty.left = std::min(rcDirty.left, rcNew.left);
        rcDirty.top = std::min(rcDirty.top, rcNew.top);
        rcDirty.right = std::max(rcDirty.right, rcNew.right);
        rcDirty.bottom = std::max(rcDirty.bottom, rcNew.bottom);
      } else {
        rcDirty = rcNew;
        hasDirty = true;
      }
    }

    if (hasDirty)
      InvalidateRect(hwnd, &rcDirty, FALSE);
    else if (stateChanged)
      InvalidateCustomScrollbarArea(hwnd, FALSE);
  }

  UpdateScrollbarCacheSnapshot(hasNew ? &rcNew : NULL, hasNew);
}

static void PaintCustomScrollbarOverlay(HWND hwnd, HDC hdc) {
  if (!hwnd || !hdc)
    return;

  int maxTop = GetListBoxMaxTopIndex();
  RECT rcClip = {};
  if (GetClipBox(hdc, &rcClip) == NULLREGION)
    return;

  RECT rcTrack = {};
  if (!GetCustomScrollbarTrackRect(hwnd, &rcTrack))
    return;

  RECT rcTrackPaint = {};
  if (!IntersectRect(&rcTrackPaint, &rcTrack, &rcClip))
    return;

  HBRUSH hTrackBrush = CreateSolidBrush(GetWhiteColor());
  FillRect(hdc, &rcTrackPaint, hTrackBrush);
  DeleteObject(hTrackBrush);

  if (!g_scrollbarVisible || maxTop <= 0)
    return;

  RECT rcThumb = {};
  if (!GetCustomScrollbarThumbRect(hwnd, &rcThumb))
    return;

  RECT rcThumbPaint = {};
  if (!IntersectRect(&rcThumbPaint, &rcThumb, &rcClip))
    return;

  COLORREF thumbColor =
      g_isScrollbarDragging
          ? GetAccentStrongColor()
          : (g_isScrollbarHovered
                 ? (g_isDarkMode ? RGB(142, 148, 158) : RGB(155, 155, 160))
                 : (g_isDarkMode ? RGB(102, 108, 118) : RGB(180, 180, 180)));
  HBRUSH hThumbBrush = CreateSolidBrush(thumbColor);
  FillRect(hdc, &rcThumbPaint, hThumbBrush);
  DeleteObject(hThumbBrush);
}

static void ShowCustomScrollbar(HWND hwnd, bool showQuickPasteHint = true) {
  (void)showQuickPasteHint;
  if (!NeedsCustomScrollbar())
    return;
  g_scrollbarVisible = true;
  if (!g_isScrollbarDragging)
    StartScrollbarHideTimer(hwnd);
}

static void ApplyListBoxTopIndex(HWND hwnd, int newTop) {
  int maxTop = GetListBoxMaxTopIndex();
  if (newTop < 0)
    newTop = 0;
  if (newTop > maxTop)
    newTop = maxTop;

  SendMessageW(hwnd, LB_SETTOPINDEX, newTop, 0);
  g_listBoxTopIndex = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
  if (g_listBoxTopIndex < 0)
    g_listBoxTopIndex = 0;
  HideNativeListBoxScrollbar(hwnd);

  int newPage = g_listBoxTopIndex / ITEMS_PER_PAGE;
  if (newPage != g_currentPage)
    g_currentPage = newPage;

  InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
  InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
}

static bool IsSelectableDisplayIndex(int index) {
  if (index < 0 || index >= (int)g_displayIndexMap.size())
    return false;
  return true;
}

static int FindSelectableDisplayIndex(int startIndex, int step) {
  if (step == 0)
    return LB_ERR;
  for (int i = startIndex; i >= 0 && i < (int)g_displayIndexMap.size();
       i += step) {
    if (IsSelectableDisplayIndex(i))
      return i;
  }
  return LB_ERR;
}

static int GetFirstSelectableDisplayIndex() {
  return FindSelectableDisplayIndex(0, 1);
}

static int GetLastSelectableDisplayIndex() {
  return FindSelectableDisplayIndex((int)g_displayIndexMap.size() - 1, -1);
}

static void RedrawBatchSelectionUI() {
  if (g_hwndListBox) {
    RedrawWindow(g_hwndListBox, NULL, NULL,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
  }
  if (g_hwndMain) {
    RedrawWindow(g_hwndMain, NULL, NULL,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
  }
}

static void HideFavoriteFilterTooltip() {
  if (!g_hwndMainTooltip || !g_hwndFilterFavorite)
    return;

  TOOLINFOW ti = {};
  ti.cbSize = TTTOOLINFOW_V1_SIZE;
  ti.uFlags = TTF_IDISHWND;
  ti.hwnd = g_hwndMain;
  ti.uId = (UINT_PTR)g_hwndFilterFavorite;
  SendMessageW(g_hwndMainTooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
  g_isFavoriteTooltipVisible = false;
}

static void ShowFavoriteFilterTooltip(HWND hwndOwner) {
  if (!g_hwndMainTooltip || !g_hwndFilterFavorite)
    return;

  RECT rcBtn = {};
  GetWindowRect(g_hwndFilterFavorite, &rcBtn);

  TOOLINFOW ti = {};
  ti.cbSize = TTTOOLINFOW_V1_SIZE;
  ti.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_IDISHWND;
  ti.hwnd = hwndOwner;
  ti.uId = (UINT_PTR)g_hwndFilterFavorite;
  ti.lpszText = (LPWSTR)L"单击显示，右击编辑";

  SendMessageW(g_hwndMainTooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
  SendMessageW(g_hwndMainTooltip, TTM_TRACKPOSITION, 0,
               MAKELPARAM(rcBtn.left + 8, rcBtn.bottom + 10));
  SendMessageW(g_hwndMainTooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
  SetCursor(LoadCursor(NULL, IDC_HAND));
  g_isFavoriteTooltipVisible = true;
}

static int GetBatchSelectableItemIndexFromDisplayIndex(int displayIndex) {
  if (displayIndex < 0 || displayIndex >= (int)g_displayIndexMap.size())
    return -1;

  int itemIndex = g_displayIndexMap[displayIndex];
  return (itemIndex >= 0 && itemIndex < (int)g_history.size()) ? itemIndex : -1;
}

static bool IsBatchSelectableDisplayIndex(int displayIndex) {
  return GetBatchSelectableItemIndexFromDisplayIndex(displayIndex) >= 0;
}

static void AddBatchSelectionItem(int itemIndex) {
  if (itemIndex < 0)
    return;
  if (std::find(g_selectedItems.begin(), g_selectedItems.end(), itemIndex) ==
      g_selectedItems.end()) {
    g_selectedItems.push_back(itemIndex);
  }
}

static void RemoveBatchSelectionItem(int itemIndex) {
  auto it =
      std::find(g_selectedItems.begin(), g_selectedItems.end(), itemIndex);
  if (it != g_selectedItems.end()) {
    g_selectedItems.erase(it);
  }
}

static void SelectBatchRangeByDisplayIndex(int anchorDisplayIndex,
                                           int targetDisplayIndex,
                                           bool preserveExistingSelection) {
  if (!preserveExistingSelection)
    g_selectedItems.clear();

  int start = std::min(anchorDisplayIndex, targetDisplayIndex);
  int end = std::max(anchorDisplayIndex, targetDisplayIndex);
  for (int i = start; i <= end; ++i) {
    int itemIndex = GetBatchSelectableItemIndexFromDisplayIndex(i);
    if (itemIndex >= 0)
      AddBatchSelectionItem(itemIndex);
  }
}

static void ApplyBatchSelectionFromDisplayIndex(int displayIndex,
                                                bool shiftPressed,
                                                bool ctrlPressed) {
  int itemIndex = GetBatchSelectableItemIndexFromDisplayIndex(displayIndex);
  if (itemIndex < 0)
    return;

  if (shiftPressed) {
    int anchorDisplayIndex = g_batchSelectionAnchorDisplayIndex;
    if (!IsBatchSelectableDisplayIndex(anchorDisplayIndex)) {
      int currentDisplayIndex =
          g_hwndListBox ? (int)SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0)
                        : LB_ERR;
      anchorDisplayIndex = IsBatchSelectableDisplayIndex(currentDisplayIndex)
                               ? currentDisplayIndex
                               : displayIndex;
      g_batchSelectionAnchorDisplayIndex = anchorDisplayIndex;
    }
    SelectBatchRangeByDisplayIndex(anchorDisplayIndex, displayIndex,
                                   ctrlPressed);
    return;
  }

  g_batchSelectionAnchorDisplayIndex = displayIndex;
  if (ctrlPressed) {
    auto it =
        std::find(g_selectedItems.begin(), g_selectedItems.end(), itemIndex);
    if (it != g_selectedItems.end()) {
      g_selectedItems.erase(it);
    } else {
      g_selectedItems.push_back(itemIndex);
    }
    return;
  }

  AddBatchSelectionItem(itemIndex);
}

static void EnsureListSelectionVisible(int index) {
  if (!g_hwndListBox || index < 0)
    return;

  RECT rcListBox;
  GetClientRect(g_hwndListBox, &rcListBox);
  int visibleHeight = rcListBox.bottom - rcListBox.top;
  if (visibleHeight <= 0)
    return;

  RECT rcItem = {};
  if (SendMessageW(g_hwndListBox, LB_GETITEMRECT, index, (LPARAM)&rcItem) ==
      LB_ERR)
    return;

  int newTop = -1;
  if (rcItem.top < 0) {
    newTop = index;
  } else if (rcItem.bottom > visibleHeight) {
    newTop = index;
    int totalHeight = GetItemDisplayHeight(index);
    while (newTop > 0) {
      int prevHeight = GetItemDisplayHeight(newTop - 1);
      if (totalHeight + prevHeight > visibleHeight)
        break;
      totalHeight += prevHeight;
      --newTop;
    }
  }

  if (newTop >= 0) {
    ClearShortcutDisplayBounds();
    g_smoothScrollExpectedTop = -1;
    g_smoothScrollExpectedEndExclusive = -1;
    ApplyListBoxTopIndex(g_hwndListBox, newTop);
    ShowCustomScrollbar(g_hwndListBox);
    RefreshScrollbarIfChanged(g_hwndListBox);
  }
}

static bool SelectListDisplayIndex(int index) {
  if (!g_hwndListBox || !IsSelectableDisplayIndex(index))
    return false;
  SendMessageW(g_hwndListBox, LB_SETCURSEL, index, 0);
  EnsureListSelectionVisible(index);
  InvalidateRect(g_hwndListBox, NULL, TRUE);
  return true;
}

static bool MoveListSelection(int delta) {
  if (!g_hwndListBox || g_displayIndexMap.empty() || delta == 0)
    return false;

  int current = (int)SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0);
  int step = (delta > 0) ? 1 : -1;
  int target = LB_ERR;

  if (current == LB_ERR) {
    target = (step > 0) ? GetFirstSelectableDisplayIndex()
                        : GetLastSelectableDisplayIndex();
  } else {
    target = FindSelectableDisplayIndex(current + step, step);
    if (target == LB_ERR)
      target = current;
  }

  if (target == LB_ERR)
    return false;
  bool selected = SelectListDisplayIndex(target);
  if (selected && g_isBatchEditMode && (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
    if (!IsBatchSelectableDisplayIndex(g_batchSelectionAnchorDisplayIndex) &&
        IsBatchSelectableDisplayIndex(current)) {
      g_batchSelectionAnchorDisplayIndex = current;
    }
    ApplyBatchSelectionFromDisplayIndex(target, true, false);
    RedrawBatchSelectionUI();
  }
  return selected;
}

static bool JumpListSelectionToBoundary(bool toBottom) {
  int target = toBottom ? GetLastSelectableDisplayIndex()
                        : GetFirstSelectableDisplayIndex();
  if (target == LB_ERR)
    return false;
  int current = g_hwndListBox
                    ? (int)SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0)
                    : LB_ERR;
  bool selected = SelectListDisplayIndex(target);
  if (selected && g_isBatchEditMode && (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
    if (!IsBatchSelectableDisplayIndex(g_batchSelectionAnchorDisplayIndex) &&
        IsBatchSelectableDisplayIndex(current)) {
      g_batchSelectionAnchorDisplayIndex = current;
    }
    ApplyBatchSelectionFromDisplayIndex(target, true, false);
    RedrawBatchSelectionUI();
  }
  return selected;
}

static void InvalidateMainFilterButtons() {
  if (g_hwndFilterAll)
    InvalidateRect(g_hwndFilterAll, NULL, TRUE);
  if (g_hwndFilterText)
    InvalidateRect(g_hwndFilterText, NULL, TRUE);
  if (g_hwndFilterImage)
    InvalidateRect(g_hwndFilterImage, NULL, TRUE);
  if (g_hwndFilterFile)
    InvalidateRect(g_hwndFilterFile, NULL, TRUE);
  if (g_hwndFilterFavorite)
    InvalidateRect(g_hwndFilterFavorite, NULL, TRUE);
}

static bool SwitchMainPanel(HWND /*hwnd*/, int newTab,
                            bool resetFavoriteFilter) {
  if (newTab < 0 || newTab > 4)
    return false;

  int oldTab = g_currentTab;

  g_currentTab = newTab;
  if (newTab == 4 && oldTab != 4 && resetFavoriteFilter)
    g_currentFilterTagId = 0;
  InvalidateMainFilterButtons();
  UpdateListBox();
  if (g_hwndListBox) {
    RedrawWindow(g_hwndListBox, NULL, NULL,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
  }
  SelectListDisplayIndex(GetFirstSelectableDisplayIndex());
  g_vimNavPendingGTick = 0;
  return true;
}

static bool CycleMainPanel(HWND hwnd, int delta) {
  if (delta == 0)
    return false;

  for (int attempt = 0; attempt < 5; ++attempt) {
    int nextTab = (g_currentTab + delta + 5) % 5;
    if (SwitchMainPanel(hwnd, nextTab, true))
      return true;
    if (nextTab == g_currentTab)
      break;
  }
  return false;
}

static bool IsMainNavigationEditFocus(HWND hwndFocus) {
  if (!hwndFocus)
    return false;
  wchar_t className[32] = {};
  GetClassNameW(hwndFocus, className, _countof(className));
  return wcscmp(className, L"Edit") == 0;
}

static bool HandleMainNavigationKey(const MSG &msg) {
  if (msg.message != WM_KEYDOWN || !g_hwndMain || !IsWindowVisible(g_hwndMain))
    return false;
  if (msg.hwnd != g_hwndMain && !IsChild(g_hwndMain, msg.hwnd))
    return false;

  HWND hwndFocus = GetFocus();
  if (IsMainNavigationEditFocus(hwndFocus))
    return false;

  if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000))
    return false;

  bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  WPARAM vk = msg.wParam;
  DWORD now = GetTickCount();
  bool handled = false;

  switch (vk) {
  case VK_LEFT:
  case 'H':
    handled = CycleMainPanel(g_hwndMain, -1);
    break;
  case VK_RIGHT:
  case 'L':
    handled = CycleMainPanel(g_hwndMain, 1);
    break;
  case VK_UP:
  case 'K':
    handled = MoveListSelection(-1);
    break;
  case VK_DOWN:
  case 'J':
    handled = MoveListSelection(1);
    break;
  case VK_HOME:
    handled = JumpListSelectionToBoundary(false);
    break;
  case VK_END:
    handled = JumpListSelectionToBoundary(true);
    break;
  case VK_PRIOR:
    if (g_hwndListBox && g_listBoxTopIndex > 0) {
      int oldTop = g_listBoxTopIndex;
      g_smoothScrollExpectedTop = -1;
      g_smoothScrollExpectedEndExclusive = -1;
      ApplyListBoxTopIndex(g_hwndListBox,
                           CalculatePrevPageIndex(g_listBoxTopIndex));
      ClearShortcutDisplayBounds();
      UpdateShortcutEndForUpwardFill(oldTop);
      RedrawWindow(g_hwndListBox, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
      ShowCustomScrollbar(g_hwndListBox);
      RefreshScrollbarIfChanged(g_hwndListBox);
      handled = true;
    }
    break;
  case VK_NEXT: {
    if (g_hwndListBox) {
      int visibleCount = CalculateVisibleItemCount(g_listBoxTopIndex);
      int expectedNextTop = g_listBoxTopIndex + visibleCount;
      if (expectedNextTop < (int)g_displayIndexMap.size() &&
          expectedNextTop > g_listBoxTopIndex) {
        ApplyListBoxTopIndex(g_hwndListBox, expectedNextTop);
        g_shortcutStartDisplayIndex = expectedNextTop;
        g_smoothScrollExpectedTop = -1;
        g_shortcutEndDisplayIndexExclusive = -1;
        g_smoothScrollExpectedEndExclusive = -1;
        RedrawWindow(g_hwndListBox, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
        ShowCustomScrollbar(g_hwndListBox);
        RefreshScrollbarIfChanged(g_hwndListBox);
        handled = true;
      }
    }
    break;
  }
  case 'G':
    if (shiftPressed) {
      handled = JumpListSelectionToBoundary(true);
      g_vimNavPendingGTick = 0;
    } else {
      if (g_vimNavPendingGTick != 0 && now - g_vimNavPendingGTick <= 500) {
        handled = JumpListSelectionToBoundary(false);
        g_vimNavPendingGTick = 0;
      } else {
        g_vimNavPendingGTick = now;
        handled = true;
      }
    }
    break;
  default:
    g_vimNavPendingGTick = 0;
    return false;
  }

  if (handled && g_hwndListBox && vk != 'G')
    SetFocus(g_hwndListBox);
  if (handled && vk != 'G')
    g_vimNavPendingGTick = 0;
  return handled;
}

static void HideListBoxTrackingTooltip() {
  if (g_hwndListBoxTooltip == NULL)
    return;
  TOOLINFOW ti = {};
  ti.cbSize = TTTOOLINFOW_V1_SIZE;
  ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
  ti.hwnd = g_hwndListBox;
  ti.uId = 0;
  SendMessageW(g_hwndListBoxTooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
}

static COLORREF BlendColorWithBackground(COLORREF color, BYTE alpha,
                                         COLORREF bgColor) {
  int r = (GetRValue(color) * alpha + GetRValue(bgColor) * (255 - alpha)) / 255;
  int g = (GetGValue(color) * alpha + GetGValue(bgColor) * (255 - alpha)) / 255;
  int b = (GetBValue(color) * alpha + GetBValue(bgColor) * (255 - alpha)) / 255;
  return RGB(r, g, b);
}

static int HexCharToInt(wchar_t ch) {
  if (ch >= L'0' && ch <= L'9')
    return ch - L'0';
  if (ch >= L'a' && ch <= L'f')
    return ch - L'a' + 10;
  if (ch >= L'A' && ch <= L'F')
    return ch - L'A' + 10;
  return -1;
}

static BYTE ParseHexByte(wchar_t high, wchar_t low) {
  int hi = HexCharToInt(high);
  int lo = HexCharToInt(low);
  if (hi < 0 || lo < 0)
    return 0;
  return (BYTE)((hi << 4) | lo);
}

static bool TryParseHexColorToken(const std::wstring &token,
                                  COLORREF *outColor) {
  if (!outColor || token.empty() || token[0] != L'#')
    return false;

  COLORREF color = 0;
  if (token.length() == 4) {
    int r = HexCharToInt(token[1]);
    int g = HexCharToInt(token[2]);
    int b = HexCharToInt(token[3]);
    if (r < 0 || g < 0 || b < 0)
      return false;
    color = RGB(r * 17, g * 17, b * 17);
  } else if (token.length() == 7) {
    color =
        RGB(ParseHexByte(token[1], token[2]), ParseHexByte(token[3], token[4]),
            ParseHexByte(token[5], token[6]));
  } else if (token.length() == 9) {
    COLORREF raw =
        RGB(ParseHexByte(token[1], token[2]), ParseHexByte(token[3], token[4]),
            ParseHexByte(token[5], token[6]));
    BYTE alpha = ParseHexByte(token[7], token[8]);
    color = BlendColorWithBackground(raw, alpha, GetWhiteColor());
  } else {
    return false;
  }

  *outColor = color;
  return true;
}

static bool TryParseRgbColorMatch(const std::wsmatch &match,
                                  COLORREF *outColor) {
  if (!outColor || match.size() < 4)
    return false;

  int r = _wtoi(match[1].str().c_str());
  int g = _wtoi(match[2].str().c_str());
  int b = _wtoi(match[3].str().c_str());
  r = std::max(0, std::min(255, r));
  g = std::max(0, std::min(255, g));
  b = std::max(0, std::min(255, b));
  COLORREF color = RGB(r, g, b);

  if (match.size() >= 5 && match[4].matched) {
    double alpha = _wtof(match[4].str().c_str());
    if (alpha < 0.0)
      alpha = 0.0;
    if (alpha > 1.0)
      alpha = 1.0;
    color = BlendColorWithBackground(color, (BYTE)(alpha * 255.0 + 0.5),
                                     GetWhiteColor());
  }

  *outColor = color;
  return true;
}

static bool TryFindColorCodeInText(const std::wstring &text, size_t *outStart,
                                   size_t *outLength, COLORREF *outColor) {
  if (text.empty() || !outStart || !outLength || !outColor)
    return false;

  static const std::wregex kHexColorRe(
      L"#(?:[0-9A-Fa-f]{3}|[0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})(?![0-9A-Fa-f])");
  static const std::wregex kRgbColorRe(
      L"\\brgba?\\(\\s*(\\d{1,3})\\s*,\\s*(\\d{1,3})\\s*,\\s*(\\d{1,3})"
      L"(?:\\s*,\\s*([01](?:\\.\\d+)?|0?\\.\\d+))?\\s*\\)",
      std::regex_constants::icase);

  std::wsmatch hexMatch;
  std::wsmatch rgbMatch;
  bool foundHex = std::regex_search(text, hexMatch, kHexColorRe);
  bool foundRgb = std::regex_search(text, rgbMatch, kRgbColorRe);
  if (!foundHex && !foundRgb)
    return false;

  size_t bestStart = std::wstring::npos;
  size_t bestLength = 0;
  COLORREF bestColor = 0;

  if (foundHex) {
    COLORREF parsed = 0;
    std::wstring token = hexMatch.str();
    if (TryParseHexColorToken(token, &parsed)) {
      bestStart = (size_t)hexMatch.position();
      bestLength = token.length();
      bestColor = parsed;
    }
  }

  if (foundRgb) {
    COLORREF parsed = 0;
    if (TryParseRgbColorMatch(rgbMatch, &parsed)) {
      size_t rgbStart = (size_t)rgbMatch.position();
      if (bestStart == std::wstring::npos || rgbStart < bestStart) {
        bestStart = rgbStart;
        bestLength = rgbMatch.length();
        bestColor = parsed;
      }
    }
  }

  if (bestStart == std::wstring::npos || bestLength == 0)
    return false;

  *outStart = bestStart;
  *outLength = bestLength;
  *outColor = bestColor;
  return true;
}

static void DrawDetectedColorDot(HDC hdc, const RECT &rcText,
                                 const std::wstring &text) {
  if (!hdc || text.empty() || !g_isColorDotEnabled)
    return;

  size_t matchStart = 0;
  size_t matchLength = 0;
  COLORREF dotColor = 0;
  if (!TryFindColorCodeInText(text, &matchStart, &matchLength, &dotColor))
    return;

  std::wstring prefix = text.substr(0, matchStart);
  std::wstring colorToken = text.substr(matchStart, matchLength);
  SIZE prefixSize = {};
  SIZE tokenSize = {};
  if (!prefix.empty()) {
    GetTextExtentPoint32W(hdc, prefix.c_str(), (int)prefix.length(),
                          &prefixSize);
  }
  if (!colorToken.empty()) {
    GetTextExtentPoint32W(hdc, colorToken.c_str(), (int)colorToken.length(),
                          &tokenSize);
  }

  const int dotDiameter = 10;
  const int dotGap = 6;
  int dotX = rcText.left + prefixSize.cx + tokenSize.cx + dotGap;
  int verticalOffset = (int)(((rcText.bottom - rcText.top) - dotDiameter) / 2);
  int dotY = rcText.top + std::max(0, verticalOffset);
  if (dotX + dotDiameter > rcText.right - 2)
    return;

  Gdiplus::Graphics graphics(hdc);
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  Gdiplus::SolidBrush fillBrush(Gdiplus::Color(
      255, GetRValue(dotColor), GetGValue(dotColor), GetBValue(dotColor)));
  graphics.FillEllipse(&fillBrush, dotX, dotY, dotDiameter, dotDiameter);
}

static void
PromoteHistoryItemsToFrontInOrder(const std::vector<int> &selectionOrder) {
  if (selectionOrder.empty() || g_history.empty())
    return;

  std::vector<bool> picked(g_history.size(), false);
  std::vector<ClipboardItem> reordered;
  reordered.reserve(g_history.size());

  for (int actualIndex : selectionOrder) {
    if (actualIndex >= 0 && actualIndex < (int)g_history.size() &&
        !picked[actualIndex]) {
      reordered.push_back(g_history[actualIndex]);
      picked[actualIndex] = true;
    }
  }

  if (reordered.empty())
    return;

  for (int i = 0; i < (int)g_history.size(); ++i) {
    if (!picked[i])
      reordered.push_back(g_history[i]);
  }

  g_history.swap(reordered);
  g_expandedItems.clear();
}

static void UpdateSearchClearButtonVisibility() {
  if (g_hwndSearchBox)
    InvalidateRect(g_hwndSearchBox, NULL, FALSE);
}

static void DragCustomScrollbarTo(HWND hwnd, int mouseY) {
  RECT rcTrack;
  RECT rcThumb;
  if (!GetCustomScrollbarTrackRect(hwnd, &rcTrack) ||
      !GetCustomScrollbarThumbRect(hwnd, &rcThumb))
    return;

  int thumbHeight = rcThumb.bottom - rcThumb.top;
  int maxScrollOffset = GetMaxListScrollOffset(hwnd);
  if (maxScrollOffset <= 0)
    return;

  int thumbTop = mouseY - g_scrollbarDragOffsetY;
  int minThumbTop = rcTrack.top + 2;
  int maxThumbTop = rcTrack.bottom - thumbHeight - 2;
  if (thumbTop < minThumbTop)
    thumbTop = minThumbTop;
  if (thumbTop > maxThumbTop)
    thumbTop = maxThumbTop;

  int travel = (rcTrack.bottom - rcTrack.top) - thumbHeight - 4;
  if (travel <= 0)
    return;

  int relativeTop = thumbTop - minThumbTop;
  int contentOffset = (relativeTop * maxScrollOffset + travel / 2) / travel;
  int newTop = GetTopIndexForContentOffset(contentOffset);
  int maxTop = GetListBoxMaxTopIndex();
  if (thumbTop <= minThumbTop)
    newTop = 0;
  else if (thumbTop >= maxThumbTop)
    newTop = maxTop;
  int oldTop = g_listBoxTopIndex;
  if (newTop == oldTop) {
    if (g_shortcutStartDisplayIndex != -1 ||
        g_shortcutEndDisplayIndexExclusive != -1) {
      ClearShortcutDisplayBounds();
      RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
      return;
    }
  }
  if (newTop != oldTop) {
    // 在改变列表顶部索引前，先失效整个滚动条轨道。
    // LB_SETTOPINDEX 可能触发同步重绘，此时 PaintCustomScrollbarOverlay
    // 会用新位置绘制滑块；若旧滑块位置不在失效区域内就会留下残影。
    InvalidateCustomScrollbarArea(hwnd, FALSE);
    ApplyListBoxTopIndex(hwnd, newTop);
    ClearShortcutDisplayBounds();
  }
  g_scrollbarVisible = true;
  RefreshScrollbarIfChanged(hwnd);
}

static void HideNativeListBoxScrollbar(HWND hwnd) {
  if (!hwnd)
    return;
  ShowScrollBar(hwnd, SB_VERT, FALSE);
}

// ==================== 中转站核心功能函数 ====================

// 列表框子类化窗口过程 - 处理展开/收起按钮点击和自绘滚动条
LRESULT CALLBACK ListBoxProc(HWND hwnd, UINT message, WPARAM wParam,
                             LPARAM lParam) {
  static int s_lastScrollPos = -1; // 记录上次滚动位置

  // 处理背景擦除 - 用正确的颜色填充空白区域
  if (message == WM_ERASEBKGND) {
    HDC hdc = (HDC)wParam;
    RECT rcClient = {};
    GetClientRect(hwnd, &rcClient);
    // 排除自绘滚动条 track 区域，避免先涂白再画滑块导致闪烁
    RECT rcTrack = {};
    int savedDC = 0;
    if (g_scrollbarVisible && GetCustomScrollbarTrackRect(hwnd, &rcTrack)) {
      savedDC = SaveDC(hdc);
      ExcludeClipRect(hdc, rcTrack.left, rcTrack.top, rcTrack.right,
                      rcTrack.bottom);
    }
    HBRUSH hBrush = CreateSolidBrush(GetWhiteColor());
    FillRect(hdc, &rcClient, hBrush);
    DeleteObject(hBrush);
    if (savedDC)
      RestoreDC(hdc, savedDC);
    return 1;
  }

  // 处理鼠标滚轮 - 在边界时阻止消息传递以避免闪烁
  if (message == WM_MOUSEWHEEL) {
    int maxTop = GetListBoxMaxTopIndex();
    if (maxTop <= 0) {
      return 0; // 阻止消息传递，避免闪烁
    }

    // 检查是否需要滚动
    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    int currentTop = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);

    // 如果在顶部向上滚动，或在底部向下滚动，阻止消息传递
    if ((currentTop <= 0 && delta > 0) || (currentTop >= maxTop && delta < 0)) {
      return 0; // 阻止消息传递，避免闪烁
    }

    // 正常滚动时更新滚动条和快捷键提示状态
    ShowCustomScrollbar(hwnd);

    // 自定义滚轮滚动：每次只滚动1个项目（对图像更友好）
    int newTop = currentTop;
    if (delta > 0) {
      // 向上滚动
      newTop = currentTop - 1;
      if (newTop < 0)
        newTop = 0;
    } else {
      // 向下滚动
      newTop = currentTop + 1;
      if (newTop > maxTop)
        newTop = maxTop;
    }

    // 设置新的顶部索引
    if (newTop != currentTop) {
      g_smoothScrollExpectedTop = -1;
      g_smoothScrollExpectedEndExclusive = -1;
      ApplyListBoxTopIndex(hwnd, newTop);
      ClearShortcutDisplayBounds();
      RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
      return 0;
    }
    return 0; // 已处理，不再传递给默认处理
  }

  // 处理平滑滚动定时器
  if (message == WM_TIMER && wParam == ID_SMOOTH_SCROLL_TIMER) {
    if (g_smoothScrollActive) {
      // 使用缓动函数实现平滑效果
      float diff = g_smoothScrollTarget - g_smoothScrollCurrent;
      float step = diff * 0.25f; // 缓动系数

      // 如果差值很小，直接到达目标
      if (fabs(diff) < 0.5f) {
        g_smoothScrollCurrent = g_smoothScrollTarget;
        g_smoothScrollActive = false;
        KillTimer(hwnd, ID_SMOOTH_SCROLL_TIMER);
        // 最终位置设置
        int finalPos = (int)(g_smoothScrollTarget + 0.5f);
        ApplyListBoxTopIndex(hwnd, finalPos);
        if (g_smoothScrollExpectedTop >= 0) {
          g_shortcutStartDisplayIndex = g_smoothScrollExpectedTop;
          g_smoothScrollExpectedTop = -1;
          if (g_smoothScrollExpectedEndExclusive >= 0) {
            int oldTop = g_smoothScrollExpectedEndExclusive;
            g_smoothScrollExpectedEndExclusive = -1;
            UpdateShortcutEndForUpwardFill(oldTop);
          } else {
            g_shortcutEndDisplayIndexExclusive = -1;
          }
        } else {
          ClearShortcutDisplayBounds();
        }
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
      } else {
        g_smoothScrollCurrent += step;
        // 设置滚动位置
        int newPos = (int)(g_smoothScrollCurrent + 0.5f);
        ApplyListBoxTopIndex(hwnd, newPos);
      }
    }
    return 0;
  }

  // 处理垂直滚动
  if (message == WM_VSCROLL) {
    int oldTop = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
    LRESULT result =
        CallWindowProcW(g_oldListBoxProc, hwnd, message, wParam, lParam);
    HideNativeListBoxScrollbar(hwnd);
    g_listBoxTopIndex = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
    if (g_listBoxTopIndex < 0)
      g_listBoxTopIndex = 0;

    if (NeedsCustomScrollbar()) {
      if (s_lastScrollPos != g_listBoxTopIndex || oldTop != g_listBoxTopIndex) {
        s_lastScrollPos = g_listBoxTopIndex;
        ShowCustomScrollbar(hwnd);
        RefreshScrollbarIfChanged(hwnd);
      }
    }
    if (!g_smoothScrollActive)
      ClearShortcutDisplayBounds();
    if (!g_smoothScrollActive)
      g_shortcutEndDisplayIndexExclusive = -1;
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
    if (g_isScrollbarDragging || g_isScrollbarHovered) {
      StartScrollbarHideTimer(hwnd);
      return 0;
    }
    KillTimer(hwnd, ID_SCROLLBAR_HIDE_TIMER);
    g_scrollbarVisible = false;
    HideNativeListBoxScrollbar(hwnd);
    RefreshScrollbarIfChanged(hwnd);
    return 0;
  }

  // 文件夹下划线动画定时器
  if (message == WM_TIMER && wParam == ID_FOLDER_UNDERLINE_TIMER) {
    if (g_folderUnderlineAnimating) {
      if (g_isHoveringFolder) {
        g_folderUnderlineProgress += 0.08f;
        if (g_folderUnderlineProgress >= 1.0f) {
          g_folderUnderlineProgress = 1.0f;
          g_folderUnderlineAnimating = false;
          KillTimer(hwnd, ID_FOLDER_UNDERLINE_TIMER);
        }
      } else {
        g_folderUnderlineProgress -= 0.08f;
        if (g_folderUnderlineProgress <= 0.0f) {
          g_folderUnderlineProgress = 0.0f;
          g_folderUnderlineAnimating = false;
          KillTimer(hwnd, ID_FOLDER_UNDERLINE_TIMER);
        }
      }
      if (g_hoverFolderIndex >= 0) {
        RECT rc;
        SendMessageW(hwnd, LB_GETITEMRECT, g_hoverFolderIndex, (LPARAM)&rc);
        InvalidateRect(hwnd, &rc, FALSE);
      }
    }
    return 0;
  }

  if (message == WM_PAINT) {
    PAINTSTRUCT ps = {};
    HDC hdc = BeginPaint(hwnd, &ps);
    if (!hdc)
      return 0;

    RECT rcClient = {};
    GetClientRect(hwnd, &rcClient);
    int width = rcClient.right - rcClient.left;
    int height = rcClient.bottom - rcClient.top;
    if (width <= 0 || height <= 0) {
      EndPaint(hwnd, &ps);
      return 0;
    }

    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ hOldBmp = NULL;
    if (hdcMem && hbmMem) {
      hOldBmp = SelectObject(hdcMem, hbmMem);
      RECT rcPaint = ps.rcPaint;
      HBRUSH hBgBrush = CreateSolidBrush(GetWhiteColor());
      FillRect(hdcMem, &rcPaint, hBgBrush);
      DeleteObject(hBgBrush);

      SendMessageW(hwnd, WM_PRINTCLIENT, (WPARAM)hdcMem,
                   PRF_CLIENT | PRF_ERASEBKGND);
      PaintCustomScrollbarOverlay(hwnd, hdcMem);
      BitBlt(hdc, rcPaint.left, rcPaint.top, rcPaint.right - rcPaint.left,
             rcPaint.bottom - rcPaint.top, hdcMem, rcPaint.left, rcPaint.top,
             SRCCOPY);
    }

    if (hOldBmp)
      SelectObject(hdcMem, hOldBmp);
    if (hbmMem)
      DeleteObject(hbmMem);
    if (hdcMem)
      DeleteDC(hdcMem);
    EndPaint(hwnd, &ps);
    return 0;
  }

  // 处理光标设置，防止手指光标闪烁
  if (message == WM_SETCURSOR) {
    if (g_isScrollbarDragging) {
      SetCursor(LoadCursor(NULL, IDC_ARROW));
      return TRUE;
    }
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    RECT rcTrack;
    RECT rcThumb;
    if ((GetCustomScrollbarThumbRect(hwnd, &rcThumb) &&
         PtInRect(&rcThumb, pt)) ||
        (GetCustomScrollbarTrackRect(hwnd, &rcTrack) &&
         PtInRect(&rcTrack, pt))) {
      SetCursor(LoadCursor(NULL, IDC_ARROW));
      return TRUE;
    }
    if (g_isHoveringIcon || g_isHoveringFolder) {
      SetCursor(LoadCursor(NULL, IDC_HAND));
      return TRUE;
    }
  }

  // 鼠标移动时检测是否悬浮在图标上，并更新 Tooltip
  if (message == WM_MOUSEMOVE) {
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);

    RECT rcTrack;
    bool isOverScrollbar =
        GetCustomScrollbarTrackRect(hwnd, &rcTrack) && PtInRect(&rcTrack, pt);
    if (g_isScrollbarDragging) {
      g_isScrollbarHovered = true;
      DragCustomScrollbarTo(hwnd, pt.y);
      return 0;
    }
    if (g_isScrollbarHovered != isOverScrollbar) {
      g_isScrollbarHovered = isOverScrollbar;
      if (g_isScrollbarHovered) {
        ShowCustomScrollbar(hwnd, false);
      } else if (g_scrollbarVisible) {
        StartScrollbarHideTimer(hwnd);
      }
      RefreshScrollbarIfChanged(hwnd);
    }
    if (!g_isScrollbarHovered && !g_scrollbarVisible &&
        NeedsCustomScrollbar()) {
      ShowCustomScrollbar(hwnd, false);
      RefreshScrollbarIfChanged(hwnd);
    }

    // 启用鼠标追踪以接收 WM_MOUSELEAVE
    TRACKMOUSEEVENT tme = {};
    tme.cbSize = sizeof(TRACKMOUSEEVENT);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd;
    TrackMouseEvent(&tme);

    // 获取鼠标所在的列表项索引
    int index = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
    bool wasHoveringIcon = g_isHoveringIcon;
    int oldHoverIndex = g_hoverIconIndex;

    // 重置图像悬浮状态
    g_isHoveringImage = false;
    g_hoverImageIndex = -1;

    // 保存文件夹悬浮旧状态（不在开头重置，避免 GetFileAttributesW
    // 瞬态失败导致闪烁）
    bool wasHoveringFolder = g_isHoveringFolder;
    int oldFolderHoverIndex = g_hoverFolderIndex;
    bool folderHoverFound = false;

    if (HIWORD(index) == 0) {
      index = LOWORD(index);

      bool iconFound = false;
      if (index >= 0 && index < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[index];

        if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
          const ClipboardItem &item = g_history[actualIndex];

          // 获取列表项的矩形
          RECT rcItem;
          SendMessageW(hwnd, LB_GETITEMRECT, index, (LPARAM)&rcItem);

          // 计算图标区域（需要与绘制代码保持一致）
          // 时间文本宽度需要计算
          HDC hdc = GetDC(hwnd);
          HFONT hHeaderFont = CreateFontW(
              16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
              DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
          HFONT hOldFont = (HFONT)SelectObject(hdc, hHeaderFont);

          std::wstring headerText =
              GetRelativeTimeString(item.timestamp) + L" -";
          SIZE textSize;
          GetTextExtentPoint32W(hdc, headerText.c_str(),
                                (int)headerText.length(), &textSize);

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
              SendMessageW(g_hwndListBoxTooltip, TTM_UPDATETIPTEXTW, 0,
                           (LPARAM)&ti);

              // 获取鼠标当前屏幕位置
              POINT ptMouse;
              GetCursorPos(&ptMouse);

              // 设置 tooltip 位置（在鼠标上方）
              POINT ptScreen = {ptMouse.x - 20, ptMouse.y - 25};
              SendMessageW(g_hwndListBoxTooltip, TTM_TRACKPOSITION, 0,
                           MAKELPARAM(ptScreen.x, ptScreen.y));

              // 激活 tooltip
              SendMessageW(g_hwndListBoxTooltip, TTM_TRACKACTIVATE, TRUE,
                           (LPARAM)&ti);
            }
          }

          // 检查是否悬浮在图像区域（图片文件或截图）
          if (!iconFound && item.type == TYPE_IMAGE &&
              !item.imageData.empty()) {
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
              rcContent.top += 2 + 20; // 顶部边距 + 标题高度

              int availableWidth = rcContent.right - rcContent.left;
              int availableHeight = rcItem.bottom - rcContent.top - 10;

              int srcWidth =
                  item.thumbWidth > 0 ? item.thumbWidth : item.imageWidth;
              int srcHeight =
                  item.thumbHeight > 0 ? item.thumbHeight : item.imageHeight;

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
                    SendMessageW(g_hwndListBoxTooltip, TTM_UPDATETIPTEXTW, 0,
                                 (LPARAM)&ti);

                    // 获取鼠标当前屏幕位置
                    POINT ptMouse;
                    GetCursorPos(&ptMouse);

                    // 设置 tooltip 位置（在鼠标上方）
                    POINT ptScreen = {ptMouse.x - 20, ptMouse.y - 25};
                    SendMessageW(g_hwndListBoxTooltip, TTM_TRACKPOSITION, 0,
                                 MAKELPARAM(ptScreen.x, ptScreen.y));

                    // 激活 tooltip
                    SendMessageW(g_hwndListBoxTooltip, TTM_TRACKACTIVATE, TRUE,
                                 (LPARAM)&ti);
                  }
                }
              }
            }
          }

          // 文件/文件夹名称悬浮检测（仅检测文字区域）
          // 同时检查 TYPE_FILE 和 TYPE_TEXT，因为文件路径可能作为文本复制
          if (!iconFound && !g_isHoveringImage &&
              (item.type == TYPE_FILE || item.type == TYPE_TEXT)) {
            // 如果已经在悬浮当前项，保持状态（避免 GetFileAttributesW
            // 瞬态失败导致闪烁）
            if (g_isHoveringFolder && g_hoverFolderIndex == index) {
              RECT rcTextApprox;
              rcTextApprox.top = rcItem.top + 2 + 20;
              rcTextApprox.bottom = rcTextApprox.top + 22;
              rcTextApprox.left = rcItem.left + 10;
              rcTextApprox.right = rcItem.right - 10;
              if (PtInRect(&rcTextApprox, pt)) {
                folderHoverFound = true;
                SetCursor(LoadCursor(NULL, IDC_HAND));
              }
            } else {
              DWORD attrs = GetFileAttributesW(item.content.c_str());
              if (attrs != INVALID_FILE_ATTRIBUTES) {
                bool isFolder = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
                // 文字区域：与绘制代码保持一致
                RECT rcFolderText;
                rcFolderText.top = rcItem.top + 2 + 20;
                rcFolderText.bottom = rcFolderText.top + 22;
                if (isFolder) {
                  rcFolderText.left = rcItem.left + 10 + 22;
                } else {
                  rcFolderText.left = rcItem.left + 10;
                }
                rcFolderText.right = rcItem.right - 10;
                if (PtInRect(&rcFolderText, pt)) {
                  folderHoverFound = true;
                  g_isHoveringFolder = true;
                  g_hoverFolderIndex = index;
                  SetCursor(LoadCursor(NULL, IDC_HAND));
                  if (!g_folderUnderlineAnimating) {
                    g_folderUnderlineAnimating = true;
                    g_folderUnderlineProgress = 0.0f;
                    SetTimer(hwnd, ID_FOLDER_UNDERLINE_TIMER, 16, NULL);
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
        HideListBoxTrackingTooltip();
      }
    } else {
      g_isHoveringIcon = false;
      g_hoverIconIndex = -1;
      HideListBoxTrackingTooltip();
    }

    // 如果本轮未检测到文件夹悬浮，重置状态
    if (!folderHoverFound) {
      g_isHoveringFolder = false;
      g_hoverFolderIndex = -1;
    }

    // 如果悬浮状态变化，重绘相关项目
    if (wasHoveringIcon != g_isHoveringIcon ||
        oldHoverIndex != g_hoverIconIndex) {
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

    // 文件夹悬浮状态变化时重绘
    if (wasHoveringFolder != g_isHoveringFolder ||
        oldFolderHoverIndex != g_hoverFolderIndex) {
      if (oldFolderHoverIndex >= 0) {
        RECT rcOld;
        SendMessageW(hwnd, LB_GETITEMRECT, oldFolderHoverIndex, (LPARAM)&rcOld);
        InvalidateRect(hwnd, &rcOld, FALSE);
      }
      if (g_hoverFolderIndex >= 0 &&
          g_hoverFolderIndex != oldFolderHoverIndex) {
        RECT rcNew;
        SendMessageW(hwnd, LB_GETITEMRECT, g_hoverFolderIndex, (LPARAM)&rcNew);
        InvalidateRect(hwnd, &rcNew, FALSE);
      }
      if (!g_isHoveringFolder && !g_folderUnderlineAnimating) {
        g_folderUnderlineProgress = 0.0f;
      }
    }
  }

  // 鼠标离开时隐藏 Tooltip 并重置悬浮状态
  if (message == WM_MOUSELEAVE) {
    g_isScrollbarHovered = false;
    if (g_scrollbarVisible && !g_isScrollbarDragging)
      StartScrollbarHideTimer(hwnd);
    int oldHoverIndex = g_hoverIconIndex;
    g_lastTooltipIndex = -1;
    g_isHoveringIcon = false;
    g_hoverIconIndex = -1;

    // 注意：不重置文件夹悬浮状态，保留给 WM_LBUTTONUP 使用

    HideListBoxTrackingTooltip();
    // 重绘之前悬浮的项目
    if (oldHoverIndex >= 0) {
      RECT rcOld;
      SendMessageW(hwnd, LB_GETITEMRECT, oldHoverIndex, (LPARAM)&rcOld);
      InvalidateRect(hwnd, &rcOld, FALSE);
    }
  }

  // 处理鼠标按下 - 记录拖拽起始点
  if (message == WM_LBUTTONDOWN) {
    g_dragOccurred = false; // 重置拖拽标志
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);

    RECT rcTrack;
    RECT rcThumb;
    if (GetCustomScrollbarTrackRect(hwnd, &rcTrack) && PtInRect(&rcTrack, pt)) {
      g_isScrollbarHovered = true;
      g_isScrollbarDragging = true;
      g_smoothScrollActive = false;
      KillTimer(hwnd, ID_SMOOTH_SCROLL_TIMER);
      ShowCustomScrollbar(hwnd, false);
      if (GetCustomScrollbarThumbRect(hwnd, &rcThumb) &&
          PtInRect(&rcThumb, pt)) {
        g_scrollbarDragOffsetY = pt.y - rcThumb.top;
        RefreshScrollbarIfChanged(hwnd);
      } else {
        g_scrollbarDragOffsetY =
            std::max(0, (int)((rcThumb.bottom - rcThumb.top) / 2));
        DragCustomScrollbarTo(hwnd, pt.y);
      }
      SetCapture(hwnd);
      return 0;
    }

    int index = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
    if (HIWORD(index) == 0) {
      index = LOWORD(index);
      if (index >= 0 && index < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[index];
        if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
          const ClipboardItem &item = g_history[actualIndex];
          // 文件类型（非文件夹）支持拖拽
          if (item.type == TYPE_FILE) {
            DWORD attrs = GetFileAttributesW(item.content.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES &&
                !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
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

            bool canDrag = false;
            if (!imagePath.empty()) {
              DWORD attrs = GetFileAttributesW(imagePath.c_str());
              if (attrs != INVALID_FILE_ATTRIBUTES) {
                canDrag = true;
              }
            }
            // 如果文件不存在但有内存图像数据，也允许拖拽（后续创建临时文件）
            if (!canDrag && !item.imageData.empty()) {
              canDrag = true;
            }

            if (canDrag) {
              g_dragStartPoint = pt;
              g_dragItemIndex = index;
            }
          }
        }
      }
    }
  }

  // 处理鼠标移动 - 检测拖拽
  if (message == WM_MOUSEMOVE && (wParam & MK_LBUTTON) &&
      g_dragItemIndex >= 0) {
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
          const ClipboardItem &item = g_history[actualIndex];
          std::wstring dragFilePath;
          bool isTempFile = false;

          // 获取拖拽文件路径
          if (item.type == TYPE_FILE) {
            DWORD attrs = GetFileAttributesW(item.content.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES &&
                !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
              dragFilePath = item.content;
            }
          } else if (item.type == TYPE_IMAGE) {
            if (!item.imageFilePath.empty()) {
              dragFilePath = item.imageFilePath;
            } else if (!item.imageFileName.empty()) {
              dragFilePath = GetImagesPath() + L"\\" + item.imageFileName;
            }
            // 如果文件不存在但有内存图像数据，创建临时文件用于拖拽
            if (!dragFilePath.empty()) {
              DWORD attrs = GetFileAttributesW(dragFilePath.c_str());
              if (attrs == INVALID_FILE_ATTRIBUTES && !item.imageData.empty()) {
                std::wstring tempPath = SaveImageToTempFile(
                    item.imageData, item.thumbWidth, item.thumbHeight);
                if (!tempPath.empty()) {
                  dragFilePath = tempPath;
                  isTempFile = true;
                } else {
                  dragFilePath.clear();
                }
              }
            } else if (!item.imageData.empty()) {
              dragFilePath = SaveImageToTempFile(
                  item.imageData, item.thumbWidth, item.thumbHeight);
              if (!dragFilePath.empty()) {
                isTempFile = true;
              }
            }
          }

          if (!dragFilePath.empty()) {
            // 创建数据对象
            IDataObject *pDataObject = CreateFileDataObject(dragFilePath);
            if (pDataObject) {
              // 设置拖放图像（显示文件图标和文件名）
              SetDragImage(pDataObject, dragFilePath, pt);

              // 创建拖放源
              CDropSource *pDropSource = new CDropSource();

              // 标记拖拽已发生
              g_dragOccurred = true;

              // 执行拖放
              DWORD dwEffect = 0;
              DoDragDrop(pDataObject, pDropSource,
                         DROPEFFECT_COPY | DROPEFFECT_MOVE, &dwEffect);

              pDropSource->Release();
              pDataObject->Release();
            }

            // 清理临时文件
            if (isTempFile) {
              DeleteFileW(dragFilePath.c_str());
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
    if (g_isScrollbarDragging) {
      g_isScrollbarDragging = false;
      if (GetCapture() == hwnd)
        ReleaseCapture();
      StartScrollbarHideTimer(hwnd);
      RefreshScrollbarIfChanged(hwnd);
      return 0;
    }
    g_dragItemIndex = -1;

    // 文件夹名称点击 - 在资源管理器中打开（拖拽后不触发）
    if (g_isHoveringFolder && g_hoverFolderIndex >= 0 && !g_dragOccurred) {
      int displayIndex = g_hoverFolderIndex;
      if (displayIndex >= 0 && displayIndex < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[displayIndex];
        if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
          const ClipboardItem &item = g_history[actualIndex];
          if (item.type == TYPE_FILE || item.type == TYPE_TEXT) {
            DWORD attrs = GetFileAttributesW(item.content.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES &&
                (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
              // 文件夹：直接打开
              ShellExecuteW(NULL, L"explore", item.content.c_str(), NULL, NULL,
                            SW_SHOWNORMAL);
            } else {
              // 文件：打开所在目录并选中
              std::wstring cmd = L"/select,\"" + item.content + L"\"";
              ShellExecuteW(NULL, NULL, L"explorer.exe", cmd.c_str(), NULL,
                            SW_SHOWNORMAL);
            }
          }
        }
      }
    }
  }

  if (message == WM_CAPTURECHANGED) {
    if (g_isScrollbarDragging) {
      g_isScrollbarDragging = false;
      StartScrollbarHideTimer(hwnd);
      RefreshScrollbarIfChanged(hwnd);
    }
  }

  return CallWindowProcW(g_oldListBoxProc, hwnd, message, wParam, lParam);
}

// 置顶按钮子类化窗口过程 - 处理悬浮效果
LRESULT CALLBACK TopmostBtnProc(HWND hwnd, UINT message, WPARAM wParam,
                                LPARAM lParam) {
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
LRESULT CALLBACK BatchEditBtnProc(HWND hwnd, UINT message, WPARAM wParam,
                                  LPARAM lParam) {
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

LRESULT CALLBACK FilterFavoriteBtnProc(HWND hwnd, UINT message, WPARAM wParam,
                                       LPARAM lParam) {
  switch (message) {
  case WM_SETCURSOR:
    if (g_isFavoriteTooltipVisible) {
      SetCursor(LoadCursor(NULL, IDC_HAND));
      return TRUE;
    }
    break;
  case WM_MOUSEMOVE:
    if (g_isFavoriteTooltipVisible) {
      SetCursor(LoadCursor(NULL, IDC_HAND));
    }
    break;
  }
  return CallWindowProcW(g_oldFilterFavoriteProc, hwnd, message, wParam,
                         lParam);
}

// 上一页按钮子类化窗口过程 - 处理悬浮效果
LRESULT CALLBACK PageUpBtnProc(HWND hwnd, UINT message, WPARAM wParam,
                               LPARAM lParam) {
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
LRESULT CALLBACK PageDownBtnProc(HWND hwnd, UINT message, WPARAM wParam,
                                 LPARAM lParam) {
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
LRESULT CALLBACK TitleTopmostBtnProc(HWND hwnd, UINT message, WPARAM wParam,
                                     LPARAM lParam) {
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
LRESULT CALLBACK TitleMinimizeBtnProc(HWND hwnd, UINT message, WPARAM wParam,
                                      LPARAM lParam) {
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
LRESULT CALLBACK TitleMaximizeBtnProc(HWND hwnd, UINT message, WPARAM wParam,
                                      LPARAM lParam) {
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
LRESULT CALLBACK TitleCloseBtnProc(HWND hwnd, UINT message, WPARAM wParam,
                                   LPARAM lParam) {
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
Gdiplus::Image *LoadImageFromResource(int resourceId) {
  HMODULE hModule = GetModuleHandle(NULL);
  HRSRC hResource =
      FindResource(hModule, MAKEINTRESOURCE(resourceId), RT_RCDATA);
  if (!hResource)
    return NULL;

  DWORD imageSize = SizeofResource(hModule, hResource);
  if (imageSize == 0)
    return NULL;

  HGLOBAL hGlobal = LoadResource(hModule, hResource);
  if (!hGlobal)
    return NULL;

  void *pResourceData = LockResource(hGlobal);
  if (!pResourceData)
    return NULL;

  // 创建内存流
  HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, imageSize);
  if (!hBuffer)
    return NULL;

  void *pBuffer = GlobalLock(hBuffer);
  if (!pBuffer) {
    GlobalFree(hBuffer);
    return NULL;
  }

  memcpy(pBuffer, pResourceData, imageSize);
  GlobalUnlock(hBuffer);

  IStream *pStream = NULL;
  if (CreateStreamOnHGlobal(hBuffer, TRUE, &pStream) != S_OK) {
    GlobalFree(hBuffer);
    return NULL;
  }

  Gdiplus::Image *image = Gdiplus::Image::FromStream(pStream);
  pStream->Release();

  return image;
}

// 加载按钮图片资源（从exe资源加载）
void LoadButtonImages() {
  g_imgTopmostSelected = LoadImageFromResource(IDB_TOPMOST_SELECTED);
  g_imgTopmostUnselected = LoadImageFromResource(IDB_TOPMOST_UNSELECTED);
  g_imgFolderIcon = LoadImageFromResource(IDB_FOLDER_ICON);
  g_imgNoExistIcon = LoadImageFromResource(IDB_NOEXIST_ICON);
}

// 释放按钮图片资源
void FreeButtonImages() {
  if (g_imgTopmostSelected) {
    delete g_imgTopmostSelected;
    g_imgTopmostSelected = NULL;
  }
  if (g_imgTopmostUnselected) {
    delete g_imgTopmostUnselected;
    g_imgTopmostUnselected = NULL;
  }
  if (g_imgFolderIcon) {
    delete g_imgFolderIcon;
    g_imgFolderIcon = NULL;
  }
  if (g_imgNoExistIcon) {
    delete g_imgNoExistIcon;
    g_imgNoExistIcon = NULL;
  }
}

// 搜索框子类化窗口过程 - 处理渐变光标
LRESULT CALLBACK SearchBoxProc(HWND hwnd, UINT message, WPARAM wParam,
                               LPARAM lParam) {
  auto getClearButtonRect = [&](RECT *rcClear) -> bool {
    if (!rcClear)
      return false;
    int textLen = GetWindowTextLengthW(hwnd);
    if (textLen <= 0)
      return false;
    RECT rcClient = {};
    GetClientRect(hwnd, &rcClient);
    const int size = 18;
    const int rightInset = 8;
    rcClear->right = rcClient.right - rightInset;
    rcClear->left = rcClear->right - size;
    rcClear->top = rcClient.top + (rcClient.bottom - rcClient.top - size) / 2;
    rcClear->bottom = rcClear->top + size;
    return true;
  };

  switch (message) {
  case WM_MOUSEMOVE: {
    TRACKMOUSEEVENT tme = {};
    tme.cbSize = sizeof(TRACKMOUSEEVENT);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd;
    TrackMouseEvent(&tme);

    RECT rcClear = {};
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    bool hover = getClearButtonRect(&rcClear) && PtInRect(&rcClear, pt);
    if (hover != g_isSearchClearBtnHover) {
      g_isSearchClearBtnHover = hover;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    break;
  }
  case WM_MOUSELEAVE:
    if (g_isSearchClearBtnHover) {
      g_isSearchClearBtnHover = false;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    break;
  case WM_SETCURSOR: {
    POINT pt = {};
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    RECT rcClear = {};
    if (getClearButtonRect(&rcClear) && PtInRect(&rcClear, pt)) {
      SetCursor(LoadCursor(NULL, IDC_HAND));
      return TRUE;
    }
    break;
  }
  case WM_LBUTTONDOWN: {
    RECT rcClear = {};
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (getClearButtonRect(&rcClear) && PtInRect(&rcClear, pt)) {
      SetWindowTextW(hwnd, L"");
      PerformSearch(g_hwndMain);
      SetFocus(g_hwndMain);
      g_isSearchClearBtnHover = false;
      UpdateSearchClearButtonVisibility();
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }
    break;
  }
  case WM_KEYDOWN: {
    if (wParam == VK_ESCAPE) {
      SetWindowTextW(hwnd, L"");
      PerformSearch(g_hwndMain);
      SetFocus(g_hwndMain);
      UpdateSearchClearButtonVisibility();
      InvalidateRect(hwnd, NULL, TRUE);
      return 0;
    }
    break;
  }
  case WM_LBUTTONDBLCLK: {
    // 双击全选文本
    SendMessageW(hwnd, EM_SETSEL, 0, -1);
    return 0;
  }
  case WM_CHAR: {
    // Ctrl+A 全选
    if (wParam == 1) { // Ctrl+A
      SendMessageW(hwnd, EM_SETSEL, 0, -1);
      return 0;
    }
    break;
  }
  case WM_SETFOCUS: {
    // 隐藏默认光标，启动自定义光标定时器
    LRESULT result =
        CallWindowProcW(g_oldSearchBoxProc, hwnd, message, wParam, lParam);
    HideCaret(hwnd);
    DestroyCaret(); // 销毁默认光标
    g_caretVisible = true;
    g_caretGradientPos = 0.0f;
    g_caretBlinkCounter = 0;
    g_caretShowState = true;
    SetTimer(hwnd, ID_CARET_TIMER, 50, NULL);
    InvalidateRect(hwnd, NULL, FALSE);
    UpdateSearchClearButtonVisibility();
    return result;
  }
  case WM_KILLFOCUS: {
    KillTimer(hwnd, ID_CARET_TIMER);
    g_caretVisible = false;
    InvalidateRect(hwnd, NULL, FALSE);
    UpdateSearchClearButtonVisibility();
    return CallWindowProcW(g_oldSearchBoxProc, hwnd, message, wParam, lParam);
  }
  case WM_TIMER: {
    if (wParam == ID_CARET_TIMER) {
      g_caretGradientPos += 0.15f;
      if (g_caretGradientPos > 2.0f)
        g_caretGradientPos = 0.0f;

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
    LRESULT result =
        CallWindowProcW(g_oldSearchBoxProc, hwnd, message, wParam, lParam);

    HDC hdc = GetDC(hwnd);
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

    // 获取文本长度
    int textLen = GetWindowTextLengthW(hwnd);

    // 失焦且无文本时显示占位符
    if (GetFocus() != hwnd && textLen == 0) {
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, RGB(160, 160, 160)); // 灰色
      HFONT hFont = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
      HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

      RECT textRect = rcClient;
      textRect.left += 4;
      DrawTextW(hdc, T(STR_SEARCH_PLACEHOLDER), -1, &textRect,
                DT_SINGLELINE | DT_VCENTER);

      SelectObject(hdc, hOldFont);
    }
    // 获取焦点时显示渐变光标
    else if (g_caretVisible && g_caretShowState && GetFocus() == hwnd) {
      // 获取光标位置
      DWORD sel = SendMessageW(hwnd, EM_GETSEL, 0, 0);
      int charIndex = HIWORD(sel); // 光标位置（选择结束位置）

      // 获取文本区域
      RECT rcText;
      SendMessageW(hwnd, EM_GETRECT, 0, (LPARAM)&rcText);
      int textLeft = rcText.left;

      // 计算光标X位置
      int caretX = textLeft; // 默认起始位置（textLeft已经是4）
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
        caretX = textLeft + textSize.cx; // 文本宽度（无额外偏移）

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
      LinearGradientBrush brush(Point(caretX, caretY),
                                Point(caretX, caretY + caretHeight),
                                g_isDarkMode ? Color(255, 255, 255, 255)
                                             : Color(255, 0x65, 0x47, 0xFF),
                                g_isDarkMode ? Color(255, 255, 255, 255)
                                             : Color(255, 0x00, 0x90, 0xFE));

      Pen pen(&brush, 1.0f);
      graphics.DrawLine(&pen, caretX, caretY, caretX, caretY + caretHeight);
    }

    RECT rcClear = {};
    if (getClearButtonRect(&rcClear)) {
      Graphics graphics(hdc);
      graphics.SetSmoothingMode(SmoothingModeAntiAlias);
      COLORREF fill = g_isDarkMode ? RGB(118, 122, 132) : RGB(210, 214, 220);
      if (g_isSearchClearBtnHover) {
        fill = g_isDarkMode ? RGB(148, 152, 162) : RGB(188, 194, 202);
      }
      SolidBrush fillBrush(Color(g_isDarkMode ? 235 : 210, GetRValue(fill),
                                 GetGValue(fill), GetBValue(fill)));
      graphics.FillEllipse(&fillBrush, (INT)rcClear.left, (INT)rcClear.top,
                           (INT)(rcClear.right - rcClear.left),
                           (INT)(rcClear.bottom - rcClear.top));
      Pen xPen(g_isDarkMode ? Color(255, 24, 26, 30) : Color(220, 88, 96, 108),
               1.6f);
      int inset = 6;
      graphics.DrawLine(
          &xPen, (INT)(rcClear.left + inset), (INT)(rcClear.top + inset),
          (INT)(rcClear.right - inset), (INT)(rcClear.bottom - inset));
      graphics.DrawLine(&xPen, (INT)(rcClear.right - inset),
                        (INT)(rcClear.top + inset), (INT)(rcClear.left + inset),
                        (INT)(rcClear.bottom - inset));
    }

    ReleaseDC(hwnd, hdc);
    return result;
  }
  }
  return CallWindowProcW(g_oldSearchBoxProc, hwnd, message, wParam, lParam);
}

LRESULT CALLBACK SearchClearBtnProc(HWND hwnd, UINT message, WPARAM wParam,
                                    LPARAM lParam) {
  switch (message) {
  case WM_MOUSEMOVE: {
    if (!g_isSearchClearBtnHover) {
      g_isSearchClearBtnHover = true;
      TRACKMOUSEEVENT tme = {};
      tme.cbSize = sizeof(TRACKMOUSEEVENT);
      tme.dwFlags = TME_LEAVE;
      tme.hwndTrack = hwnd;
      TrackMouseEvent(&tme);
      InvalidateRect(hwnd, NULL, TRUE);
    }
    break;
  }
  case WM_MOUSELEAVE: {
    g_isSearchClearBtnHover = false;
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  }
  case WM_SETCURSOR: {
    SetCursor(LoadCursor(NULL, IDC_HAND));
    return TRUE;
  }
  case WM_LBUTTONDOWN: {
    if (g_hwndSearchBox) {
      SetWindowTextW(g_hwndSearchBox, L"");
      PerformSearch(g_hwndMain);
      SetFocus(g_hwndMain);
      UpdateSearchClearButtonVisibility();
      InvalidateRect(g_hwndSearchBox, NULL, TRUE);
    }
    return 0;
  }
  }
  return CallWindowProcW(g_oldSearchClearBtnProc, hwnd, message, wParam,
                         lParam);
}

// 窗口过程
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam,
                         LPARAM lParam) {
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
      RECT rcTop = {0, 0, rcWin.right, borderTop};
      FillRect(hdc, &rcTop, hBrush);
      // 左
      RECT rcLeft = {0, borderTop, borderLeft, rcWin.bottom};
      FillRect(hdc, &rcLeft, hBrush);
      // 右
      RECT rcRight = {rcWin.right - borderRight, borderTop, rcWin.right,
                      rcWin.bottom};
      FillRect(hdc, &rcRight, hBrush);
      // 下
      RECT rcBottom = {borderLeft, rcWin.bottom - borderBottom,
                       rcWin.right - borderRight, rcWin.bottom};
      FillRect(hdc, &rcBottom, hBrush);

      DeleteObject(hBrush);
      ReleaseDC(hwnd, hdc);
    }
    return 0;
  }
  case WM_NCCALCSIZE: {
    if (wParam == TRUE) {
      NCCALCSIZE_PARAMS *pParams = (NCCALCSIZE_PARAMS *)lParam;
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
          if (PtInRect(&rcBtn, pt))
            return HTCLIENT;
        }
        if (g_hwndTitleMaximize) {
          GetWindowRect(g_hwndTitleMaximize, &rcBtn);
          MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rcBtn, 2);
          if (PtInRect(&rcBtn, pt))
            return HTCLIENT;
        }
        if (g_hwndTitleMinimize) {
          GetWindowRect(g_hwndTitleMinimize, &rcBtn);
          MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rcBtn, 2);
          if (PtInRect(&rcBtn, pt))
            return HTCLIENT;
        }
        if (g_hwndTitleTopmost) {
          GetWindowRect(g_hwndTitleTopmost, &rcBtn);
          MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rcBtn, 2);
          if (PtInRect(&rcBtn, pt))
            return HTCLIENT;
        }
        // 不在按钮上，返回 HTCAPTION 允许拖动
        // 如果标签弹出窗口正在显示，禁止拖动主窗口
        if (IsTagPopupVisible()) {
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

    // 加载粘贴次数统计
    LoadPasteCount();

    // 不再创建主菜单

    // 注册快捷键，如果默认快捷键冲突则禁用
    if (!RegisterHotkey(hwnd)) {
      // 首次创建阶段可能因窗口尚未稳定而短暂失败，延迟到首次显示后重试。
      g_hotkeyRegisterPendingRetry = g_isHotkeyEnabled;
    }
    if (g_isQuickPasteEnabled) {
      RegisterQuickPasteHotkeys(hwnd);
    }

    // 创建搜索栏（使用ES_MULTILINE以支持EM_SETRECT垂直居中）
    g_hwndSearchBox = CreateWindowExW(
        0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_MULTILINE,
        0, 0, 0, 0, hwnd, (HMENU)ID_SEARCH_BOX, GetModuleHandleW(NULL), NULL);

    // 占位符文本将在SearchBoxProc中自绘

    // 设置搜索框字体（比UI字体大3px）
    HFONT hSearchFont = CreateFontW(
        g_fontSize + 3, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_fontName.c_str());
    SendMessageW(g_hwndSearchBox, WM_SETFONT, (WPARAM)hSearchFont, TRUE);

    // 设置搜索框左边距
    SendMessageW(g_hwndSearchBox, EM_SETMARGINS, EC_LEFTMARGIN,
                 MAKELPARAM(1, 0));

    // 设置UI控件字体
    HFONT hUIFont = CreateFontW(
        g_fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, g_fontName.c_str());

    // 子类化搜索框以处理渐变光标
    g_oldSearchBoxProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndSearchBox, GWLP_WNDPROC, (LONG_PTR)SearchBoxProc);

    g_hwndSearchClearBtn =
        CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0,
                        hwnd, NULL, GetModuleHandleW(NULL), NULL);
    g_oldSearchClearBtnProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndSearchClearBtn, GWLP_WNDPROC, (LONG_PTR)SearchClearBtnProc);
    ShowWindow(g_hwndSearchClearBtn, SW_HIDE);

    // 创建筛选按钮（自绘样式）
    g_hwndFilterAll = CreateWindowExW(
        0, L"BUTTON", T(STR_FILTER_ALL), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_FILTER_ALL, GetModuleHandleW(NULL), NULL);
    g_hwndFilterText = CreateWindowExW(
        0, L"BUTTON", T(STR_FILTER_TEXT), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_FILTER_TEXT, GetModuleHandleW(NULL), NULL);
    g_hwndFilterImage = CreateWindowExW(
        0, L"BUTTON", T(STR_FILTER_IMAGE), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_FILTER_IMAGE, GetModuleHandleW(NULL), NULL);
    g_hwndFilterFile = CreateWindowExW(
        0, L"BUTTON", T(STR_FILTER_FILE), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_FILTER_FILE, GetModuleHandleW(NULL), NULL);
    g_hwndFilterFavorite = CreateWindowExW(
        0, L"BUTTON", T(STR_FILTER_FAVORITE),
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd,
        (HMENU)ID_FILTER_FAVORITE, GetModuleHandleW(NULL), NULL);
    // 设置筛选按钮字体（比UI字体大4px）
    HFONT hFilterFont = CreateFontW(
        g_fontSize + 4, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_fontName.c_str());
    SendMessageW(g_hwndFilterAll, WM_SETFONT, (WPARAM)hFilterFont, TRUE);
    SendMessageW(g_hwndFilterText, WM_SETFONT, (WPARAM)hFilterFont, TRUE);
    SendMessageW(g_hwndFilterImage, WM_SETFONT, (WPARAM)hFilterFont, TRUE);
    SendMessageW(g_hwndFilterFile, WM_SETFONT, (WPARAM)hFilterFont, TRUE);
    SendMessageW(g_hwndFilterFavorite, WM_SETFONT, (WPARAM)hFilterFont, TRUE);
    ApplyLanguage();
    g_oldFilterFavoriteProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndFilterFavorite, GWLP_WNDPROC, (LONG_PTR)FilterFavoriteBtnProc);

    // 创建剪贴板内容列表（使用 owner-drawn 模式，自绘滚动条）
    g_hwndListBox = CreateWindowExW(
        0, L"LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY |
            LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS,
        0, 0, 0, 0, hwnd, (HMENU)ID_LISTBOX, GetModuleHandleW(NULL), NULL);

    // 子类化列表框以处理展开/收起按钮点击和自绘滚动条
    g_oldListBoxProc = (WNDPROC)SetWindowLongPtrW(g_hwndListBox, GWLP_WNDPROC,
                                                  (LONG_PTR)ListBoxProc);
    HideNativeListBoxScrollbar(g_hwndListBox);

    // 注册主窗口为拖放目标（用于显示拖拽图像）
    g_pDropTarget = new CDropTarget();
    RegisterDragDrop(hwnd, g_pDropTarget);

    // 创建列表框 Tooltip（用于显示来源应用名）
    g_hwndListBoxTooltip =
        CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                        g_hwndListBox, NULL, GetModuleHandleW(NULL), NULL);

    // 设置 tooltip 最大宽度（允许多行显示）
    SendMessageW(g_hwndListBoxTooltip, TTM_SETMAXTIPWIDTH, 0, 300);

    // 设置 tooltip 颜色：淡灰色底纹，白色字体
    SendMessageW(g_hwndListBoxTooltip, TTM_SETTIPBKCOLOR,
                 (WPARAM)RGB(140, 140, 140), 0);
    SendMessageW(g_hwndListBoxTooltip, TTM_SETTIPTEXTCOLOR,
                 (WPARAM)RGB(255, 255, 255), 0);

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
    SendMessageW(g_hwndListBoxTooltip, TTM_SETDELAYTIME, TTDT_INITIAL,
                 0); // 立即显示

    // 加载按钮图片资源
    LoadButtonImages();

    // 创建功能按钮（自绘样式，只显示图标）
    // 创建置顶按钮（已移至标题栏，此处隐藏）
    HWND hwndTopmostButton = CreateWindowExW(
        0, L"BUTTON", L"置顶", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd,
        (HMENU)ID_TOPMOST_BUTTON, GetModuleHandleW(NULL), NULL);
    // 子类化置顶按钮以处理悬浮效果
    g_hwndTopmostBtn = hwndTopmostButton;
    g_oldTopmostBtnProc = (WNDPROC)SetWindowLongPtrW(
        hwndTopmostButton, GWLP_WNDPROC, (LONG_PTR)TopmostBtnProc);

    // 创建批量编辑按钮
    // 创建暗黑模式按钮（已移至设置对话框，此处隐藏）
    HWND hwndDarkmodeButton = CreateWindowExW(
        0, L"BUTTON", L"暗黑", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd,
        (HMENU)ID_DARKMODE_BUTTON, GetModuleHandleW(NULL), NULL);

    // 创建翻页按钮（上一页）
    g_hwndPageUpBtn = CreateWindowExW(
        0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0,
        hwnd, (HMENU)ID_PAGE_UP_BTN, GetModuleHandleW(NULL), NULL);
    // 子类化上一页按钮以处理悬浮效果
    g_oldPageUpBtnProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndPageUpBtn, GWLP_WNDPROC, (LONG_PTR)PageUpBtnProc);

    // 创建翻页按钮（下一页）
    g_hwndPageDownBtn = CreateWindowExW(
        0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0,
        hwnd, (HMENU)ID_PAGE_DOWN_BTN, GetModuleHandleW(NULL), NULL);
    // 子类化下一页按钮以处理悬浮效果
    g_oldPageDownBtnProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndPageDownBtn, GWLP_WNDPROC, (LONG_PTR)PageDownBtnProc);

    // 创建Tooltip控件
    HWND hwndTooltip = CreateWindowExW(
        0, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, NULL,
        GetModuleHandleW(NULL), NULL);
    g_hwndMainTooltip = hwndTooltip;

    // 设置 tooltip 颜色：淡灰色底纹，白色字体（与列表框tooltip样式一致）
    SendMessageW(hwndTooltip, TTM_SETTIPBKCOLOR, (WPARAM)RGB(140, 140, 140), 0);
    SendMessageW(hwndTooltip, TTM_SETTIPTEXTCOLOR, (WPARAM)RGB(255, 255, 255),
                 0);

    // 为每个按钮添加Tooltip
    TOOLINFOW ti = {};
    ti.cbSize = TTTOOLINFOW_V1_SIZE;
    ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
    ti.hwnd = hwnd;

    ti.uId = (UINT_PTR)hwndTopmostButton;
    ti.lpszText = (LPWSTR)L"置顶";
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

    // 为收藏按钮添加手动 tooltip
    ti.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_IDISHWND;
    ti.uId = (UINT_PTR)g_hwndFilterFavorite;
    ti.lpszText = (LPWSTR)L"单击显示，右击编辑";
    SendMessageW(hwndTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

    SendMessageW(hwndTooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 0);

    // 创建标题栏按钮
    g_hwndTitleTopmost = CreateWindowExW(
        0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 46,
        TITLEBAR_HEIGHT, hwnd, (HMENU)ID_TITLEBAR_TOPMOST,
        GetModuleHandleW(NULL), NULL);
    g_oldTitleTopmostProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndTitleTopmost, GWLP_WNDPROC, (LONG_PTR)TitleTopmostBtnProc);

    g_hwndTitleMinimize = CreateWindowExW(
        0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 46,
        TITLEBAR_HEIGHT, hwnd, (HMENU)ID_TITLEBAR_MINIMIZE,
        GetModuleHandleW(NULL), NULL);
    g_oldTitleMinimizeProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndTitleMinimize, GWLP_WNDPROC, (LONG_PTR)TitleMinimizeBtnProc);

    g_hwndTitleMaximize = CreateWindowExW(
        0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 46,
        TITLEBAR_HEIGHT, hwnd, (HMENU)ID_TITLEBAR_MAXIMIZE,
        GetModuleHandleW(NULL), NULL);
    g_oldTitleMaximizeProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndTitleMaximize, GWLP_WNDPROC, (LONG_PTR)TitleMaximizeBtnProc);

    g_hwndTitleClose =
        CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                        0, 0, 46, TITLEBAR_HEIGHT, hwnd,
                        (HMENU)ID_TITLEBAR_CLOSE, GetModuleHandleW(NULL), NULL);
    g_oldTitleCloseProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndTitleClose, GWLP_WNDPROC, (LONG_PTR)TitleCloseBtnProc);

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
    SendMessageW(hwndDarkmodeButton, WM_SETFONT, (WPARAM)hUIFont, TRUE);

    LoadCustomDataDir(); // 加载自定义数据目录配置
    LoadTags();          // 加载标签列表
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
    SetTimer(hwnd, 2, 200, NULL); // 增加到200ms

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
    // 首次显示时确保快捷键已注册
    static bool s_firstShow = true;
    if (s_firstShow) {
      s_firstShow = false;
      if (g_isHotkeyEnabled) {
        if (!RegisterHotkey(hwnd)) {
          if (g_hotkeyRegisterPendingRetry) {
            g_isHotkeyEnabled = false;
            SaveHotkeySettings();
          }
        } else {
          g_hotkeyRegisterPendingRetry = false;
        }
      }
      if (g_isQuickPasteEnabled) {
        RegisterQuickPasteHotkeys(hwnd);
      }
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
    MoveWindow(g_hwndTitleClose, clientWidth - titleBtnWidth, 0, titleBtnWidth,
               TITLEBAR_HEIGHT, TRUE);
    MoveWindow(g_hwndTitleMaximize, clientWidth - titleBtnWidth * 2, 0,
               titleBtnWidth, TITLEBAR_HEIGHT, TRUE);
    MoveWindow(g_hwndTitleMinimize, clientWidth - titleBtnWidth * 3, 0,
               titleBtnWidth, TITLEBAR_HEIGHT, TRUE);
    MoveWindow(g_hwndTitleTopmost, clientWidth - titleBtnWidth * 4, 0,
               titleBtnWidth, TITLEBAR_HEIGHT, TRUE);

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
    const int searchX = margin + borderPadding;
    const int searchY = contentTop + margin + borderPadding;
    const int searchW = clientWidth - margin * 2 - borderPadding * 2;
    const int searchH = searchHeight - borderPadding * 2;
    MoveWindow(g_hwndSearchBox, searchX, searchY, searchW, searchH, TRUE);

    const int clearBtnSize = 18;
    const int clearBtnRightInset = 8;
    const int clearBtnX = searchX + searchW - clearBtnSize - clearBtnRightInset;
    const int clearBtnY = searchY + (searchH - clearBtnSize) / 2;
    MoveWindow(g_hwndSearchClearBtn, clearBtnX, clearBtnY, clearBtnSize,
               clearBtnSize, TRUE);

    // 设置搜索框文本区域以实现垂直居中
    {
      RECT rcEdit;
      GetClientRect(g_hwndSearchBox, &rcEdit);
      int editHeight = rcEdit.bottom - rcEdit.top;
      int fontHeight = g_fontSize + 3;
      int topMargin = (editHeight - fontHeight) / 2;
      if (topMargin < 0)
        topMargin = 0;
      rcEdit.left = 4; // 与光标初始位置偏移一致
      rcEdit.top = topMargin;
      rcEdit.right -= (clearBtnSize + clearBtnRightInset + 8);
      rcEdit.bottom = rcEdit.top + fontHeight + 4;
      SendMessageW(g_hwndSearchBox, EM_SETRECT, 0, (LPARAM)&rcEdit);
      SendMessageW(g_hwndSearchBox, EM_SETMARGINS,
                   EC_LEFTMARGIN | EC_RIGHTMARGIN,
                   MAKELONG(4, clearBtnSize + clearBtnRightInset + 8));
    }
    UpdateSearchClearButtonVisibility();

    // 调整筛选按钮位置（5个按钮，总宽度与列表框对齐）
    const int filterBtnSpacing = 4;
    const int iconBtnSize = 32; // 图标按钮大小
    int filterTotalWidth = clientWidth - margin * 2 - iconBtnSize - margin;
    int filterBtnWidth = (filterTotalWidth - filterBtnSpacing * 4) / 5;
    int filterY = contentTop + margin + searchHeight + margin;
    MoveWindow(g_hwndFilterAll, margin, filterY, filterBtnWidth, tabHeight,
               TRUE);
    MoveWindow(g_hwndFilterText, margin + (filterBtnWidth + filterBtnSpacing),
               filterY, filterBtnWidth, tabHeight, TRUE);
    MoveWindow(g_hwndFilterImage,
               margin + (filterBtnWidth + filterBtnSpacing) * 2, filterY,
               filterBtnWidth, tabHeight, TRUE);
    MoveWindow(g_hwndFilterFile,
               margin + (filterBtnWidth + filterBtnSpacing) * 3, filterY,
               filterBtnWidth, tabHeight, TRUE);
    MoveWindow(g_hwndFilterFavorite,
               margin + (filterBtnWidth + filterBtnSpacing) * 4, filterY,
               filterTotalWidth - (filterBtnWidth + filterBtnSpacing) * 4,
               tabHeight, TRUE);

    // 调整列表框大小（右侧留出按钮空间）
    int listBoxTop = contentTop + margin + searchHeight + margin + tabHeight;
    MoveWindow(g_hwndListBox, margin, listBoxTop,
               clientWidth - margin * 2 - iconBtnSize - margin,
               clientHeight - listBoxTop - margin, TRUE);

    // 右侧垂直排列图标按钮
    int btnX = clientWidth - margin - iconBtnSize;
    int btnY = listBoxTop;

    MoveWindow(GetDlgItem(hwnd, ID_DARKMODE_BUTTON), btnX, btnY, iconBtnSize,
               iconBtnSize, TRUE);

    // 翻页按钮位置（在列表框右侧，垂直居中）
    int listBoxHeight = clientHeight - listBoxTop - margin;
    int listBoxCenterY = listBoxTop + listBoxHeight / 2;
    int pageUpY = listBoxCenterY - iconBtnSize; // 中心线上方
    int pageDownY = listBoxCenterY;             // 中心线下方

    MoveWindow(g_hwndPageUpBtn, btnX, pageUpY, iconBtnSize, iconBtnSize, TRUE);
    MoveWindow(g_hwndPageDownBtn, btnX, pageDownY, iconBtnSize, iconBtnSize,
               TRUE);

    // 窗口大小改变后，重新计算所有列表项高度
    if (g_hwndListBox) {
      int itemCount = SendMessageW(g_hwndListBox, LB_GETCOUNT, 0, 0);
      for (int i = 0; i < itemCount; i++) {
        SendMessageW(g_hwndListBox, LB_SETITEMHEIGHT, i, 0);
      }
      HideNativeListBoxScrollbar(g_hwndListBox);
      InvalidateRect(g_hwndListBox, NULL, TRUE);
    }

    break;
  }
  case WM_GETMINMAXINFO: {
    // 设置窗口最小尺寸
    LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
    lpMMI->ptMinTrackSize.x = 600; // 最小宽度
    lpMMI->ptMinTrackSize.y = 694; // 最小高度
    return 0;
  }
  case WM_MEASUREITEM: {
    LPMEASUREITEMSTRUCT lpMIS = (LPMEASUREITEMSTRUCT)lParam;
    if (lpMIS->CtlID == ID_LISTBOX) {
      // 动态获取列表框宽度
      RECT rcListBox;
      GetClientRect(g_hwndListBox, &rcListBox);
      int listBoxWidth = rcListBox.right - rcListBox.left - 20; // 减去左右边距
      if (listBoxWidth < 100)
        listBoxWidth = 560; // 初始化时的默认值

      // 获取列表项对应的实际数据
      if (lpMIS->itemID != (UINT)-1 &&
          lpMIS->itemID < g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[lpMIS->itemID];
        if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
          const ClipboardItem &item = g_history[actualIndex];

          if (item.type == TYPE_IMAGE) {
            // 检查图片文件是否存在（仅对图片文件类型，非截图）
            bool imageFileExists = true;
            if (!item.imageFilePath.empty()) {
              DWORD attrs = GetFileAttributesW(item.imageFilePath.c_str());
              imageFileExists = (attrs != INVALID_FILE_ATTRIBUTES);
            }

            if (!imageFileExists || g_imagePreviewQuality == PREVIEW_OFF) {
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
      const wchar_t *icon = L"";
      switch (lpDIS->CtlID) {
      case ID_FILTER_ALL:
        icon = L"\uE8FD";
        break; // List
      case ID_FILTER_TEXT:
        icon = L"\uE8D2";
        break; // Font
      case ID_FILTER_IMAGE:
        icon = L"\uEB9F";
        break; // Photo
      case ID_FILTER_FILE:
        icon = L"\uE8B7";
        break; // Document
      case ID_FILTER_FAVORITE:
        icon = L"\uE734";
        break; // FavoriteStar
      }

      // 创建图标字体
      HFONT hIconFont = CreateFontW(
          14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
          DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");

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
      TextOutW(hdc, startX + iconSize.cx + 4, centerY - textSize.cy / 2, text,
               (int)wcslen(text));

      DeleteObject(hIconFont);
      return TRUE;
    }

    // 处理功能按钮绘制（置顶、暗黑）
    if (lpDIS->CtlID == ID_TOPMOST_BUTTON ||
        lpDIS->CtlID == ID_DARKMODE_BUTTON) {
      HDC hdc = lpDIS->hDC;
      RECT rc = lpDIS->rcItem;

      // 设置背景色（跟随主题）
      COLORREF bgColor = GetBgColor();
      HBRUSH hBrush = CreateSolidBrush(bgColor);
      FillRect(hdc, &rc, hBrush);
      DeleteObject(hBrush);

      // 置顶按钮：使用图片绘制（带波浪动画）
      if (lpDIS->CtlID == ID_TOPMOST_BUTTON) {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetInterpolationMode(
            Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        int btnW = rc.right - rc.left;
        int btnH = rc.bottom - rc.top;

        // 动画进行中：绘制两张图片的混合效果
        if (g_topmostAnimating) {
          Gdiplus::Image *imgFrom = g_topmostAnimDirection
                                        ? g_imgTopmostUnselected
                                        : g_imgTopmostSelected;
          Gdiplus::Image *imgTo = g_topmostAnimDirection
                                      ? g_imgTopmostSelected
                                      : g_imgTopmostUnselected;

          if (imgFrom && imgTo && imgFrom->GetLastStatus() == Gdiplus::Ok &&
              imgTo->GetLastStatus() == Gdiplus::Ok) {
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
            clipPath.AddEllipse(centerX - currentRadius,
                                centerY - currentRadius, currentRadius * 2,
                                currentRadius * 2);

            graphics.SetClip(&clipPath);
            graphics.DrawImage(imgTo, x, y, drawW, drawH);
            graphics.ResetClip();
          }
        } else {
          // 非动画状态：正常绘制
          Gdiplus::Image *img =
              g_isTopmost ? g_imgTopmostSelected : g_imgTopmostUnselected;
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
        HFONT hIconFont = CreateFontW(
            18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(80, 80, 80));
        HFONT hOldFont = (HFONT)SelectObject(hdc, hIconFont);
        DrawTextW(hdc, L"\uE708", 1, &rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
        int visibleCount = CalculateVisibleItemCount(g_listBoxTopIndex);
        int nextStart = g_listBoxTopIndex + visibleCount;
        isDisabled = (nextStart >= (int)g_displayIndexMap.size() ||
                      nextStart <= g_listBoxTopIndex);
      }
      bool isHover = isPageUp ? g_isPageUpBtnHover : g_isPageDownBtnHover;

      // 设置背景色（支持暗黑模式）
      COLORREF bgColor = GetBgColor();
      HBRUSH hBrush = CreateSolidBrush(bgColor);
      FillRect(hdc, &rc, hBrush);
      DeleteObject(hBrush);

      if (g_isDarkMode && isHover && !isDisabled) {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

        const int btnW = rc.right - rc.left;
        const int btnH = rc.bottom - rc.top;
        const Gdiplus::REAL centerX = (Gdiplus::REAL)(rc.left + btnW / 2.0f);
        const Gdiplus::REAL centerY = (Gdiplus::REAL)(rc.top + btnH / 2.0f);
        const Gdiplus::REAL glowRadius =
            (Gdiplus::REAL)(std::min(btnW, btnH) * 0.48f);

        Gdiplus::GraphicsPath clipPath;
        clipPath.AddEllipse(
            (Gdiplus::REAL)rc.left + 1.0f, (Gdiplus::REAL)rc.top + 1.0f,
            (Gdiplus::REAL)btnW - 2.0f, (Gdiplus::REAL)btnH - 2.0f);
        graphics.SetClip(&clipPath, Gdiplus::CombineModeReplace);

        Gdiplus::GraphicsPath glowPath;
        glowPath.AddEllipse(centerX - glowRadius, centerY - glowRadius,
                            glowRadius * 2.0f, glowRadius * 2.0f);
        Gdiplus::PathGradientBrush glowBrush(&glowPath);
        Gdiplus::Color centerColor(88, 90, 156, 235);
        Gdiplus::Color surroundColor(0, 90, 156, 235);
        glowBrush.SetCenterColor(centerColor);
        INT surroundCount = 1;
        glowBrush.SetSurroundColors(&surroundColor, &surroundCount);
        graphics.FillEllipse(&glowBrush, centerX - glowRadius,
                             centerY - glowRadius, glowRadius * 2.0f,
                             glowRadius * 2.0f);
        graphics.ResetClip();
      }

      // 图标字体
      HFONT hIconFont = CreateFontW(
          18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
          DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
      SetBkMode(hdc, TRANSPARENT);

      // 图标颜色：暗黑模式下统一使用柔和蓝系，明显区分可用/禁用状态
      COLORREF iconColor;
      if (g_isDarkMode) {
        if (isDisabled) {
          iconColor = RGB(92, 110, 136);
        } else if (isHover) {
          iconColor = RGB(148, 176, 214);
        } else {
          iconColor = RGB(118, 148, 192);
        }
      } else if (isDisabled) {
        iconColor = RGB(180, 180, 180);
      } else if (isHover) {
        iconColor = GetAccentColor();
      } else {
        iconColor = GetTextColor();
      }
      SetTextColor(hdc, iconColor);

      // 绘制图标：上箭头 \uE70E，下箭头 \uE70D
      const wchar_t *icon = isPageUp ? L"\uE70E" : L"\uE70D";
      HFONT hOldFont = (HFONT)SelectObject(hdc, hIconFont);
      DrawTextW(hdc, icon, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      SelectObject(hdc, hOldFont);
      DeleteObject(hIconFont);

      return TRUE;
    }

    // 处理标题栏按钮绘制
    if (lpDIS->CtlID == ID_TITLEBAR_TOPMOST ||
        lpDIS->CtlID == ID_TITLEBAR_MINIMIZE ||
        lpDIS->CtlID == ID_TITLEBAR_MAXIMIZE ||
        lpDIS->CtlID == ID_TITLEBAR_CLOSE) {
      HDC hdc = lpDIS->hDC;
      RECT rc = lpDIS->rcItem;

      bool isHover = false;
      const wchar_t *icon = L"";
      // 暗黑模式下使用深色悬浮背景，避免浅色图标在浅色背景上看不清
      COLORREF hoverBgColor =
          g_isDarkMode ? RGB(60, 60, 60) : RGB(229, 229, 229);

      switch (lpDIS->CtlID) {
      case ID_TITLEBAR_TOPMOST:
        isHover = g_isTitleTopmostHover;
        icon = g_isTopmost ? L"\uE840" : L"\uE718"; // Pinned / Pin
        break;
      case ID_TITLEBAR_MINIMIZE:
        isHover = g_isTitleMinimizeHover;
        icon = L"\uE921"; // ChromeMinimize
        break;
      case ID_TITLEBAR_MAXIMIZE:
        isHover = g_isTitleMaximizeHover;
        icon = IsZoomed(GetParent(lpDIS->hwndItem))
                   ? L"\uE923"
                   : L"\uE922"; // ChromeRestore / ChromeMaximize
        break;
      case ID_TITLEBAR_CLOSE:
        isHover = g_isTitleCloseHover;
        icon = L"\uE8BB";                // ChromeClose
        hoverBgColor = RGB(232, 17, 35); // 关闭按钮悬浮红色
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
      HFONT hIconFont = CreateFontW(
          12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
          DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
      SetBkMode(hdc, TRANSPARENT);

      // 图标颜色
      COLORREF iconColor = GetTextColor();
      if (lpDIS->CtlID == ID_TITLEBAR_CLOSE && isHover) {
        iconColor = RGB(255, 255, 255);
      }
      if (lpDIS->CtlID == ID_TITLEBAR_TOPMOST && g_isTopmost) {
        iconColor = GetAccentColor();
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

    if (g_hwndSearchClearBtn != NULL &&
        lpDIS->hwndItem == g_hwndSearchClearBtn) {
      HDC hdc = lpDIS->hDC;
      RECT rc = lpDIS->rcItem;

      HBRUSH hBg = CreateSolidBrush(GetWhiteColor());
      FillRect(hdc, &rc, hBg);
      DeleteObject(hBg);

      Graphics graphics(hdc);
      graphics.SetSmoothingMode(SmoothingModeAntiAlias);

      COLORREF fill = g_isDarkMode ? RGB(118, 122, 132) : RGB(210, 214, 220);
      if (g_isSearchClearBtnHover) {
        fill = g_isDarkMode ? RGB(148, 152, 162) : RGB(188, 194, 202);
      }
      SolidBrush fillBrush(Color(g_isDarkMode ? 235 : 210, GetRValue(fill),
                                 GetGValue(fill), GetBValue(fill)));
      graphics.FillEllipse(&fillBrush, (INT)rc.left, (INT)rc.top,
                           (INT)(rc.right - rc.left),
                           (INT)(rc.bottom - rc.top));

      Pen xPen(g_isDarkMode ? Color(255, 24, 26, 30) : Color(220, 88, 96, 108),
               1.6f);
      int inset = 6;
      graphics.DrawLine(&xPen, (INT)(rc.left + inset), (INT)(rc.top + inset),
                        (INT)(rc.right - inset), (INT)(rc.bottom - inset));
      graphics.DrawLine(&xPen, (INT)(rc.right - inset), (INT)(rc.top + inset),
                        (INT)(rc.left + inset), (INT)(rc.bottom - inset));
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
        isSelected = (std::find(g_selectedItems.begin(), g_selectedItems.end(),
                                actualIndex) != g_selectedItems.end());
      }

      // 填充背景
      HBRUSH hBrush = CreateSolidBrush(bgColor);
      FillRect(hdc, &rcItem, hBrush);
      DeleteObject(hBrush);

      SetTextColor(hdc, textColor);

      // 绘制内容区域
      RECT rcContent = rcItem;
      rcContent.left += 10; // 左边距
      rcContent.right -= 6; // 右边距
      rcContent.top += 2;   // 顶部边距
      rcContent.right -= GetCustomScrollbarReservedWidth();
      if (rcContent.right < rcContent.left + 80)
        rcContent.right = rcContent.left + 80;

      // 如果选中，绘制蓝色边框，但给右侧自定义滚动条和底部分隔线留出空间
      if (isSelected) {
        RECT rcSelection = {rcItem.left + 1, rcItem.top + 1,
                            rcContent.right + 4, rcItem.bottom - 8};
        if (rcSelection.right <= rcSelection.left + 8)
          rcSelection.right = rcSelection.left + 8;
        if (rcSelection.bottom <= rcSelection.top + 8)
          rcSelection.bottom = rcSelection.top + 8;

        HPEN hBorderPen = CreatePen(PS_SOLID, 1, GetAccentColor());
        HPEN hOldPen = (HPEN)SelectObject(hdc, hBorderPen);
        HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hNullBrush);

        Rectangle(hdc, rcSelection.left, rcSelection.top, rcSelection.right,
                  rcSelection.bottom);

        SelectObject(hdc, hOldPen);
        SelectObject(hdc, hOldBrush);
        DeleteObject(hBorderPen);
      }

      // 获取列表项对应的实际数据
      if (lpDIS->itemID != (UINT)-1 &&
          lpDIS->itemID < g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[lpDIS->itemID];
        if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
          const ClipboardItem &item = g_history[actualIndex];

          // 创建字体
          HFONT hFont = CreateFontW(
              20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
              DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
          HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
          SetBkMode(hdc, TRANSPARENT);

          // 绘制时间戳和来源应用图标（使用16px字体）
          HFONT hHeaderFont = CreateFontW(
              16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
              DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
          HFONT hPrevFont = (HFONT)SelectObject(hdc, hHeaderFont);

          // 绘制时间戳
          std::wstring headerText =
              GetRelativeTimeString(item.timestamp) + L" -";
          RECT rcHeader = rcContent;
          rcHeader.bottom = rcHeader.top + 18;
          SetTextColor(hdc, RGB(148, 149, 148)); // #949594
          DrawTextW(hdc, headerText.c_str(), -1, &rcHeader,
                    DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

          // 计算时间文本宽度，在其后绘制图标
          SIZE textSize;
          GetTextExtentPoint32W(hdc, headerText.c_str(),
                                (int)headerText.length(), &textSize);

          // 绘制来源应用图标（在时间后面，灰度化 + 缩小尺寸 12x12）
          // 鼠标悬浮时显示彩色图标
          HICON hAppIcon = GetAppIcon(item.sourceAppPath);
          if (hAppIcon != NULL) {
            int iconX = rcContent.left + textSize.cx + 4; // 4px 间距
            int iconY = rcContent.top + 2;                // 垂直居中微调
            int iconSize = 12;                            // 缩小到 12x12

            // 检查是否鼠标悬浮在此图标上
            bool isIconHovered =
                (g_isHoveringIcon && g_hoverIconIndex == (int)lpDIS->itemID);

            if (isIconHovered) {
              // 悬浮时直接绘制彩色图标
              DrawIconEx(hdc, iconX, iconY, hAppIcon, iconSize, iconSize, 0,
                         NULL, DI_NORMAL);
            } else {
              // 非悬浮时使用 GDI+ 按 alpha 绘制灰度图标，避免出现矩形底色
              // 先用 DrawIconEx 将图标绘制到 32 位 ARGB 位图上，
              // 确保图标透明遮罩被正确转换为 alpha 通道（FromHICON 对部分
              // 图标格式会产生不正确的 alpha，导致矩形阴影）
              Gdiplus::Bitmap iconBitmap(iconSize, iconSize,
                                         PixelFormat32bppARGB);
              {
                Gdiplus::Graphics tempGraphics(&iconBitmap);
                tempGraphics.SetInterpolationMode(
                    Gdiplus::InterpolationModeHighQualityBicubic);
                HDC tempHdc = tempGraphics.GetHDC();
                DrawIconEx(tempHdc, 0, 0, hAppIcon, iconSize, iconSize, 0, NULL,
                           DI_NORMAL);
                tempGraphics.ReleaseHDC(tempHdc);
              }

              Gdiplus::Graphics graphics(hdc);
              graphics.SetInterpolationMode(
                  Gdiplus::InterpolationModeHighQualityBicubic);
              graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
              Gdiplus::ImageAttributes imageAttr;
              const float brighten = g_isDarkMode ? 0.14f : 0.10f;
              const Gdiplus::ColorMatrix colorMatrix = {
                  0.299f,   0.299f,   0.299f, 0.0f,   0.0f,   0.587f, 0.587f,
                  0.587f,   0.0f,     0.0f,   0.114f, 0.114f, 0.114f, 0.0f,
                  0.0f,     0.0f,     0.0f,   0.0f,   0.82f,  0.0f,   brighten,
                  brighten, brighten, 0.0f,   1.0f};
              imageAttr.SetColorMatrix(&colorMatrix,
                                       Gdiplus::ColorMatrixFlagsDefault,
                                       Gdiplus::ColorAdjustTypeBitmap);
              graphics.DrawImage(
                  &iconBitmap, Gdiplus::Rect(iconX, iconY, iconSize, iconSize),
                  0, 0, iconBitmap.GetWidth(), iconBitmap.GetHeight(),
                  Gdiplus::UnitPixel, &imageAttr);
            }
          }

          if (g_isQuickPasteEnabled) {
            int shortcutIndex =
                GetShortcutIndexForDisplayIndex((int)lpDIS->itemID);
            if (shortcutIndex >= 0 && shortcutIndex < 10) {
              wchar_t keyChar =
                  (shortcutIndex == 9) ? L'0' : (wchar_t)(L'1' + shortcutIndex);
              std::wstring shortcutText = GetQuickPasteModifierText() + keyChar;
              RECT rcShortcut = rcHeader;
              rcShortcut.right -= 4;
              SetTextColor(hdc, RGB(100, 149, 237));
              DrawTextW(hdc, shortcutText.c_str(), -1, &rcShortcut,
                        DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            }
          }

          // 在快捷键下方绘制分类标签（所有标签页都显示）
          if (!item.tagIds.empty()) {
            HFONT hTagFont = CreateFontW(
                18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
            HFONT hPrevTagFont = (HFONT)SelectObject(hdc, hTagFont);

            // 计算标签区域的中心位置（在快捷键提示和时间文本之间）
            int timeTextWidth = 100; // 时间文本大致宽度
            int shortcutWidth = 64;  // 快捷键提示大致宽度
            int availableWidth =
                rcHeader.right - rcHeader.left - timeTextWidth - shortcutWidth;
            int startX = rcHeader.left + timeTextWidth + availableWidth / 2;

            // 先计算所有标签的总宽度
            int totalTagWidth = 0;
            for (auto it = item.tagIds.begin(); it != item.tagIds.end(); ++it) {
              Tag *tag = GetTagById(*it);
              if (!tag)
                continue;
              SIZE tagTextSize;
              GetTextExtentPoint32W(hdc, tag->name.c_str(),
                                    (int)tag->name.length(), &tagTextSize);
              totalTagWidth += tagTextSize.cx + 14; // 标签宽度 + 内边距 + 间距
            }

            // 从中心位置开始排列标签
            int tagX = startX - totalTagWidth / 2;

            for (auto it = item.tagIds.begin(); it != item.tagIds.end(); ++it) {
              Tag *tag = GetTagById(*it);
              if (!tag)
                continue;

              SIZE tagTextSize;
              GetTextExtentPoint32W(hdc, tag->name.c_str(),
                                    (int)tag->name.length(), &tagTextSize);
              int tagPadH = 5; // 水平内边距
              int tagPadV = 1; // 垂直内边距
              int tagWidth = tagTextSize.cx + tagPadH * 2;
              int tagHeight = tagTextSize.cy + tagPadV * 2;
              int tagY = rcHeader.top + (18 - tagHeight) / 2; // 垂直居中

              if (tagX < rcHeader.left + timeTextWidth ||
                  tagX + tagWidth > rcHeader.right - shortcutWidth) {
                break; // 防止超出边界
              }

              // 绘制圆角背景
              HBRUSH hTagBrush = CreateSolidBrush(tag->color);
              HBRUSH hOldTagBrush = (HBRUSH)SelectObject(hdc, hTagBrush);
              HPEN hTagPen = CreatePen(PS_SOLID, 1, tag->color);
              HPEN hOldTagPen = (HPEN)SelectObject(hdc, hTagPen);
              RoundRect(hdc, tagX, tagY, tagX + tagWidth, tagY + tagHeight, 6,
                        6);
              SelectObject(hdc, hOldTagBrush);
              SelectObject(hdc, hOldTagPen);
              DeleteObject(hTagBrush);
              DeleteObject(hTagPen);

              // 绘制白色文字
              SetTextColor(hdc, RGB(255, 255, 255));
              RECT rcTagText = {tagX, tagY, tagX + tagWidth, tagY + tagHeight};
              DrawTextW(hdc, tag->name.c_str(), -1, &rcTagText,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

              tagX += tagWidth + 4; // 标签间距
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
              if (!imageFileExists &&
                  !g_history[actualIndex].imageData.empty()) {
                std::vector<BYTE>().swap(g_history[actualIndex].imageData);
                g_history[actualIndex].thumbWidth = 0;
                g_history[actualIndex].thumbHeight = 0;
                SaveHistory(); // 保存更新后的历史记录
              }
            }

            // 图片文件不存在的情况
            if (!imageFileExists) {
              // 显示 noexist.png 图标和浅色文件名
              if (g_imgNoExistIcon) {
                Gdiplus::Graphics graphics(hdc);
                graphics.SetInterpolationMode(
                    Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(g_imgNoExistIcon, rcContent.left,
                                   rcContent.top + 1, 18, 18);
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
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                            DT_NOPREFIX);
            }
            // 检查图片预览设置
            else if (g_imagePreviewQuality == PREVIEW_OFF) {
              // 关闭预览模式：只显示文件名和尺寸信息
              HFONT hTextFont = CreateFontW(
                  16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                  DEFAULT_PITCH | FF_DONTCARE, g_fontName.c_str());
              HFONT hPrevTextFont = (HFONT)SelectObject(hdc, hTextFont);

              // 显示图片图标和文件名
              std::wstring displayText = L"\U0001F5BC "; // 图片图标
              if (!item.imageFileName.empty()) {
                displayText += item.imageFileName;
              } else {
                displayText += L"图片";
              }
              displayText += L"  [" + std::to_wstring(item.imageWidth) + L"x" +
                             std::to_wstring(item.imageHeight) + L"]";

              SetTextColor(hdc, GetTextColor());
              DrawTextW(hdc, displayText.c_str(), -1, &rcContent,
                        DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX |
                            DT_END_ELLIPSIS);

              SelectObject(hdc, hPrevTextFont);
              DeleteObject(hTextFont);
            } else if (item.imageData.empty() || item.imageWidth <= 0 ||
                       item.imageHeight <= 0) {
              HFONT hTextFont = CreateFontW(
                  16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                  DEFAULT_PITCH | FF_DONTCARE, g_fontName.c_str());
              HFONT hPrevTextFont = (HFONT)SelectObject(hdc, hTextFont);
              std::wstring displayText = !item.imageFileName.empty()
                                             ? item.imageFileName
                                             : L"图片预览不可用";
              SetTextColor(hdc, RGB(150, 150, 150));
              DrawTextW(hdc, displayText.c_str(), -1, &rcContent,
                        DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX |
                            DT_END_ELLIPSIS);
              SelectObject(hdc, hPrevTextFont);
              DeleteObject(hTextFont);
            } else {
              // 绘制图片（使用缩略图数据）
              int availableWidth = rcContent.right - rcContent.left;
              int availableHeight =
                  rcItem.bottom - rcContent.top - 10; // 留出底部边距

              // 使用缩略图尺寸进行显示计算（如果有缩略图）
              int srcWidth =
                  item.thumbWidth > 0 ? item.thumbWidth : item.imageWidth;
              int srcHeight =
                  item.thumbHeight > 0 ? item.thumbHeight : item.imageHeight;

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
              int imageRadius = 10;

              // 创建位图并绘制（使用缩略图尺寸）
              BITMAPINFO bmi = {};
              bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
              bmi.bmiHeader.biWidth = srcWidth;
              bmi.bmiHeader.biHeight = -srcHeight; // 负值表示从上到下
              bmi.bmiHeader.biPlanes = 1;
              bmi.bmiHeader.biBitCount = 24;
              bmi.bmiHeader.biCompression = BI_RGB;

              SetStretchBltMode(hdc, HALFTONE);
              SaveDC(hdc);
              {
                Gdiplus::Graphics graphics(hdc);
                graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                Gdiplus::GraphicsPath clipPath;
                CreateRoundRectPath(&clipPath, x, y, displayWidth,
                                    displayHeight, imageRadius);
                graphics.SetClip(&clipPath, Gdiplus::CombineModeReplace);
              }
              StretchDIBits(hdc, x, y, displayWidth, displayHeight, 0, 0,
                            srcWidth, srcHeight, &item.imageData[0], &bmi,
                            DIB_RGB_COLORS, SRCCOPY);
              RestoreDC(hdc, -1);

              // 绘制图片尺寸信息（16px字体）
              HFONT hSizeFont = CreateFontW(
                  16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                  DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
              HFONT hPrevSizeFont = (HFONT)SelectObject(hdc, hSizeFont);

              std::wstring sizeText = L"[" + std::to_wstring(item.imageWidth) +
                                      L"x" + std::to_wstring(item.imageHeight) +
                                      L"]";
              RECT rcSize = rcContent;
              rcSize.top = y + displayHeight + 5;
              SetTextColor(hdc, RGB(128, 128, 128));
              DrawTextW(hdc, sizeText.c_str(), -1, &rcSize,
                        DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

              SelectObject(hdc, hPrevSizeFont);
              DeleteObject(hSizeFont);
            }

          } else {
            // 绘制文本或文件路径（单行显示，超出部分显示省略号）
            int textLen =
                SendMessageW(lpDIS->hwndItem, LB_GETTEXTLEN, lpDIS->itemID, 0);
            if (textLen > 0) {
              std::vector<wchar_t> buffer(textLen + 1);
              SendMessageW(lpDIS->hwndItem, LB_GETTEXT, lpDIS->itemID,
                           (LPARAM)&buffer[0]);
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
              } else if (item.type == TYPE_TEXT) {
                // 文本类型可能是文件路径，检查是否存在
                if (GetLinkType(item.content) == LINK_FILE_PATH) {
                  filePath = item.content;
                  DWORD attrs = GetFileAttributesW(filePath.c_str());
                  fileExists = (attrs != INVALID_FILE_ATTRIBUTES);
                }
              } else if (item.type == TYPE_IMAGE &&
                         !item.imageFilePath.empty()) {
                filePath = item.imageFilePath;
                DWORD attrs = GetFileAttributesW(filePath.c_str());
                fileExists = (attrs != INVALID_FILE_ATTRIBUTES);
              }

              // 检查是否为文件夹类型（同时检查 TYPE_FILE 和 TYPE_TEXT，
              // 因为文件路径可能作为文本复制）
              bool isFolder = false;
              if ((item.type == TYPE_FILE || item.type == TYPE_TEXT) &&
                  fileExists) {
                DWORD attrs = GetFileAttributesW(item.content.c_str());
                if (attrs != INVALID_FILE_ATTRIBUTES &&
                    (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                  isFolder = true;
                }
              }

              // 文件不存在的情况
              if (!fileExists &&
                  (item.type == TYPE_FILE || item.type == TYPE_TEXT ||
                   (item.type == TYPE_IMAGE && !item.imageFilePath.empty()))) {
                // 显示 noexist.png 图标
                if (g_imgNoExistIcon) {
                  Gdiplus::Graphics graphics(hdc);
                  graphics.SetInterpolationMode(
                      Gdiplus::InterpolationModeHighQualityBicubic);
                  graphics.DrawImage(g_imgNoExistIcon, rcText.left,
                                     rcText.top + 1, 18, 18);
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
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                              DT_END_ELLIPSIS | DT_NOPREFIX);
              } else if (isFolder) {
                // 文件夹类型：显示文件夹图标（使用 PNG 图片）
                if (g_imgFolderIcon) {
                  Gdiplus::Graphics graphics(hdc);
                  graphics.SetInterpolationMode(
                      Gdiplus::InterpolationModeHighQualityBicubic);
                  graphics.DrawImage(g_imgFolderIcon, rcText.left,
                                     rcText.top + 1, 18, 18);
                }

                // 调整文本位置（图标后面）
                rcText.left += 22;

                RECT rcPathText = rcText;
                rcPathText.right -= 20;

                // 文件/文件夹悬浮时字体变蓝动画
                COLORREF folderTextColor = GetTextColor();
                if (g_isHoveringFolder &&
                    (int)lpDIS->itemID == g_hoverFolderIndex &&
                    g_folderUnderlineProgress > 0.0f) {
                  COLORREF blueColor = RGB(0, 120, 215);
                  int normalR = GetRValue(folderTextColor);
                  int normalG = GetGValue(folderTextColor);
                  int normalB = GetBValue(folderTextColor);
                  int r = normalR + (int)((GetRValue(blueColor) - normalR) *
                                          g_folderUnderlineProgress);
                  int g = normalG + (int)((GetGValue(blueColor) - normalG) *
                                          g_folderUnderlineProgress);
                  int b = normalB + (int)((GetBValue(blueColor) - normalB) *
                                          g_folderUnderlineProgress);
                  folderTextColor = RGB(r, g, b);
                }
                SetTextColor(hdc, folderTextColor);
                DrawTextW(hdc, text.c_str(), -1, &rcPathText,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                              DT_END_ELLIPSIS | DT_NOPREFIX);
                DrawDetectedColorDot(hdc, rcPathText, text);
              } else {
                // 普通文本或文件
                // 文件类型（非文件夹）悬浮时字体变蓝动画
                // 同时检查 TYPE_FILE 和 TYPE_TEXT，因为文件路径可能作为文本复制
                if ((item.type == TYPE_FILE || item.type == TYPE_TEXT) &&
                    g_isHoveringFolder &&
                    (int)lpDIS->itemID == g_hoverFolderIndex &&
                    g_folderUnderlineProgress > 0.0f) {
                  COLORREF normalColor = GetTextColor();
                  COLORREF blueColor = RGB(0, 120, 215);
                  int r =
                      GetRValue(normalColor) +
                      (int)((GetRValue(blueColor) - GetRValue(normalColor)) *
                            g_folderUnderlineProgress);
                  int g =
                      GetGValue(normalColor) +
                      (int)((GetGValue(blueColor) - GetGValue(normalColor)) *
                            g_folderUnderlineProgress);
                  int b =
                      GetBValue(normalColor) +
                      (int)((GetBValue(blueColor) - GetBValue(normalColor)) *
                            g_folderUnderlineProgress);
                  SetTextColor(hdc, RGB(r, g, b));
                }
                DrawTextW(hdc, text.c_str(), -1, &rcText,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                              DT_END_ELLIPSIS | DT_NOPREFIX);
                DrawDetectedColorDot(hdc, rcText, text);
              }
            }
          }

          SelectObject(hdc, hOldFont);
          DeleteObject(hFont);
        }
      }

      // 绘制底部分隔线；选中项不画，避免与蓝色边框重叠闪烁
      if (!isSelected) {
        int separatorRight =
            rcItem.right - 10 - GetCustomScrollbarReservedWidth();
        if (separatorRight < rcItem.left + 10)
          separatorRight = rcItem.left + 10;
        HPEN hPen = CreatePen(
            PS_DOT, 1, g_isDarkMode ? RGB(96, 96, 102) : RGB(200, 200, 200));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

        MoveToEx(hdc, rcItem.left + 10, rcItem.bottom - 5, NULL);
        LineTo(hdc, separatorRight, rcItem.bottom - 5);

        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);
      }

      return TRUE;
    }
    return TRUE;
  }
  // 移除LBN_SELCHANGE事件中的按钮点击处理逻辑
  case WM_COMMAND: {
    WORD wNotifyCode = HIWORD(wParam);
    WORD wID = LOWORD(wParam);

    if (wNotifyCode == BN_CLICKED && wID == ID_FILTER_FAVORITE) {
      KillTimer(hwnd, ID_FAVORITE_TOOLTIP_TIMER);
      ShowFavoriteFilterTooltip(hwnd);
      SetTimer(hwnd, ID_FAVORITE_TOOLTIP_TIMER, 1400, NULL);
    }

    // 处理搜索框文本变化 - 实时搜索
    if (wID == ID_SEARCH_BOX && wNotifyCode == EN_CHANGE) {
      PerformSearch(hwnd);
      UpdateSearchClearButtonVisibility();
    }

    // 处理列表框选择变化
    if (wID == ID_LISTBOX && wNotifyCode == LBN_SELCHANGE) {
      if (g_isBatchEditMode) {
        bool isShiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool isCtrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        int index = SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0);
        if (index != LB_ERR && index < (int)g_displayIndexMap.size()) {
          ApplyBatchSelectionFromDisplayIndex(index, isShiftPressed,
                                              isCtrlPressed);
          RedrawBatchSelectionUI();
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
          ShowTagPopup(hwnd, btnRect.left, btnRect.bottom,
                       btnRect.right - btnRect.left, true);
        } else {
          // 首次单击 → 切换到收藏页，显示全部
          g_currentTab = 4;
          g_currentFilterTagId = 0; // 重置为显示全部收藏
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
          if (std::find(g_selectedItems.begin(), g_selectedItems.end(),
                        actualIndex) != g_selectedItems.end()) {
            RemoveBatchSelectionItem(actualIndex);
          } else {
            AddBatchSelectionItem(actualIndex);
          }
          g_batchSelectionAnchorDisplayIndex = index;
          RedrawBatchSelectionUI();
          return 0;
        }

        int actualIndex = g_displayIndexMap[index]; // 获取实际索引
        const ClipboardItem &item = g_history[actualIndex];

        if (item.type == TYPE_TEXT) {
          // 文本类型：双击直接粘贴
          if (OpenClipboard(NULL)) {
            EmptyClipboard();

            HGLOBAL hGlobal = GlobalAlloc(
                GMEM_MOVEABLE, (item.content.length() + 1) * sizeof(wchar_t));
            if (hGlobal != NULL) {
              wchar_t *pData = (wchar_t *)GlobalLock(hGlobal);
              if (pData != NULL) {
                wcscpy_s(pData, item.content.length() + 1,
                         item.content.c_str());
                GlobalUnlock(hGlobal);
                SetClipboardData(CF_UNICODETEXT, hGlobal);
              }
            }

            g_isRestoringClipboard = true;
            CloseClipboard();
            SetTimer(hwnd, 1, 100, NULL);
          }

          if (!g_isTopmost) {
            CloseTagPopup();
            ShowWindow(hwnd, SW_HIDE);
          }

          Sleep(100);

          if (g_previousActiveWindow != NULL &&
              IsWindow(g_previousActiveWindow)) {
            SetForegroundWindow(g_previousActiveWindow);
            Sleep(100);
          }

          keybd_event(VK_CONTROL, 0, 0, 0);
          keybd_event('V', 0, 0, 0);
          keybd_event('V', 0, KEYEVENTF_KEYUP, 0);
          keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);

          IncrementPasteCount();
          if (g_isNotificationEnabled) {
            ShowTrayBalloon(hwnd, L"提示", L"已粘贴");
          }
        } else if (item.type == TYPE_FILE) {
          // 文件类型：复制文件路径到剪贴板
          if (OpenClipboard(NULL)) {
            EmptyClipboard();

            HGLOBAL hGlobal = GlobalAlloc(
                GMEM_MOVEABLE, (item.content.length() + 1) * sizeof(wchar_t));
            if (hGlobal != NULL) {
              wchar_t *pData = (wchar_t *)GlobalLock(hGlobal);
              if (pData != NULL) {
                wcscpy_s(pData, item.content.length() + 1,
                         item.content.c_str());
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
      g_batchEditAnimDirection = !g_isBatchEditMode; // 根据当前状态决定动画方向
      g_batchEditAnimProgress = 0.0f;
      g_batchEditAnimating = true;
      g_isBatchEditMode = !g_isBatchEditMode;
      // 清空选中的记录
      g_selectedItems.clear();
      g_batchSelectionAnchorDisplayIndex = LB_ERR;
      SetTimer(hwnd, ID_BATCH_EDIT_ANIM_TIMER, 16, NULL); // 约60fps
      if (g_isNotificationEnabled) {
        ShowTrayBalloon(hwnd, L"提示",
                        g_isBatchEditMode ? L"批量编辑模式已开启"
                                          : L"批量编辑模式已关闭");
      }
      // 重绘列表框
      InvalidateRect(g_hwndListBox, NULL, TRUE);
    } else if (wID == ID_TOPMOST_BUTTON && wNotifyCode == BN_CLICKED) {
      // 切换置顶状态（启动动画）
      g_topmostAnimDirection = !g_isTopmost; // 根据当前状态决定动画方向
      g_topmostAnimProgress = 0.0f;
      g_topmostAnimating = true;
      g_isTopmost = !g_isTopmost;
      SetTimer(hwnd, ID_TOPMOST_ANIM_TIMER, 16, NULL); // 约60fps
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
      g_themeMode = g_isDarkMode ? THEME_DARK : THEME_LIGHT;

      // 更新窗口背景色
      SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND,
                       (LONG_PTR)CreateSolidBrush(GetBgColor()));

      // 更新列表框背景色
      if (g_hwndListBox) {
        InvalidateRect(g_hwndListBox, NULL, TRUE);
      }

      // 更新搜索框背景色
      if (g_hwndSearchBox) {
        InvalidateRect(g_hwndSearchBox, NULL, TRUE);
      }

      if (g_isNotificationEnabled) {
        ShowTrayBalloon(hwnd, L"提示",
                        g_isDarkMode ? L"已切换到暗黑模式"
                                     : L"已切换到明亮模式");
      }
      SaveHotkeySettings();

      // 强制重绘窗口
      InvalidateRect(hwnd, NULL, TRUE);
      UpdateWindow(hwnd);
    } else if (wID == ID_PAGE_UP_BTN && wNotifyCode == BN_CLICKED) {
      // 上一页 - 使用 g_listBoxTopIndex 判断，与禁用逻辑一致
      if (g_listBoxTopIndex > 0) {
        int oldTop = g_listBoxTopIndex;
        g_smoothScrollExpectedTop = -1;
        g_smoothScrollExpectedEndExclusive = -1;
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
          g_smoothScrollExpectedTop = topIndex;
          g_smoothScrollExpectedEndExclusive = oldTop;
          ShowCustomScrollbar(g_hwndListBox);
          RefreshScrollbarIfChanged(g_hwndListBox);
          SetTimer(g_hwndListBox, ID_SMOOTH_SCROLL_TIMER, 16, NULL);
        } else {
          ApplyListBoxTopIndex(g_hwndListBox, topIndex);
          ClearShortcutDisplayBounds();
          UpdateShortcutEndForUpwardFill(oldTop);
          RedrawWindow(g_hwndListBox, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
        }
        // 更新按钮状态
        InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
        InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
      }
    } else if (wID == ID_PAGE_DOWN_BTN && wNotifyCode == BN_CLICKED) {
      // 下一页 - 新页从新的内容开始，但顶部可能保留上一页已显示过的项
      int visibleCount = CalculateVisibleItemCount(g_listBoxTopIndex);
      int expectedNextTop = g_listBoxTopIndex + visibleCount;
      if (expectedNextTop < (int)g_displayIndexMap.size() &&
          expectedNextTop > g_listBoxTopIndex) {
        int topIndex = expectedNextTop;

        // 更新页码
        g_currentPage = topIndex / ITEMS_PER_PAGE;

        // 平滑滚动处理
        if (g_isSmoothScrollEnabled) {
          g_smoothScrollTarget = (float)topIndex;
          g_smoothScrollCurrent = (float)g_listBoxTopIndex;
          g_smoothScrollActive = true;
          g_smoothScrollListBox = g_hwndListBox;
          g_smoothScrollExpectedTop = expectedNextTop;
          g_smoothScrollExpectedEndExclusive = -1;
          ShowCustomScrollbar(g_hwndListBox);
          RefreshScrollbarIfChanged(g_hwndListBox);
          SetTimer(g_hwndListBox, ID_SMOOTH_SCROLL_TIMER, 16, NULL);
        } else {
          ApplyListBoxTopIndex(g_hwndListBox, topIndex);
          g_shortcutStartDisplayIndex = expectedNextTop;
          g_shortcutEndDisplayIndexExclusive = -1;
          RedrawWindow(g_hwndListBox, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
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
      CloseTagPopup();
      ShowWindow(hwnd, SW_HIDE);
    } else if (wID == IDM_EXIT) {
      DestroyWindow(hwnd);
    } else if (wID == IDM_SETTINGS) {
      // 显示模态设置对话框
      ShowSettingsDialog(hwnd);
    } else if (wID == IDM_COPY) {
      // 右键菜单：复制
      if (g_contextMenuIndex >= 0 &&
          g_contextMenuIndex < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[g_contextMenuIndex]; // 获取实际索引
        const ClipboardItem &item = g_history[actualIndex];
        if (OpenClipboard(NULL)) {
          EmptyClipboard();

          if (item.type == TYPE_TEXT || item.type == TYPE_FILE) {
            // 文本和文件类型：复制文本内容
            HGLOBAL hGlobal = GlobalAlloc(
                GMEM_MOVEABLE, (item.content.length() + 1) * sizeof(wchar_t));
            if (hGlobal != NULL) {
              wchar_t *pData = (wchar_t *)GlobalLock(hGlobal);
              if (pData != NULL) {
                wcscpy_s(pData, item.content.length() + 1,
                         item.content.c_str());
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
              loadedFromFile = LoadOriginalImage(
                  item.imageFileName, originalData, origWidth, origHeight);
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
              HGLOBAL hGlobal = GlobalAlloc(
                  GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + imageSize);
              if (hGlobal != NULL) {
                BYTE *pData = (BYTE *)GlobalLock(hGlobal);
                if (pData != NULL) {
                  memcpy(pData, &bmi.bmiHeader, sizeof(BITMAPINFOHEADER));
                  memcpy(pData + sizeof(BITMAPINFOHEADER), &originalData[0],
                         imageSize);
                  GlobalUnlock(hGlobal);
                  SetClipboardData(CF_DIB, hGlobal);
                }
              }
            } else {
              // 回退到使用缩略图数据（兼容旧数据）
              int srcWidth =
                  item.thumbWidth > 0 ? item.thumbWidth : item.imageWidth;
              int srcHeight =
                  item.thumbHeight > 0 ? item.thumbHeight : item.imageHeight;

              BITMAPINFO bmi = {};
              bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
              bmi.bmiHeader.biWidth = srcWidth;
              bmi.bmiHeader.biHeight = -srcHeight;
              bmi.bmiHeader.biPlanes = 1;
              bmi.bmiHeader.biBitCount = 24;
              bmi.bmiHeader.biCompression = BI_RGB;

              DWORD imageSize = item.imageData.size();
              HGLOBAL hGlobal = GlobalAlloc(
                  GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + imageSize);
              if (hGlobal != NULL) {
                BYTE *pData = (BYTE *)GlobalLock(hGlobal);
                if (pData != NULL) {
                  memcpy(pData, &bmi.bmiHeader, sizeof(BITMAPINFOHEADER));
                  memcpy(pData + sizeof(BITMAPINFOHEADER), &item.imageData[0],
                         imageSize);
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
      if (g_contextMenuIndex >= 0 &&
          g_contextMenuIndex < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[g_contextMenuIndex]; // 获取实际索引
        const ClipboardItem &item = g_history[actualIndex];
        if (OpenClipboard(NULL)) {
          EmptyClipboard();

          if (item.type == TYPE_TEXT || item.type == TYPE_FILE) {
            // 文本和文件类型：复制文本内容
            HGLOBAL hGlobal = GlobalAlloc(
                GMEM_MOVEABLE, (item.content.length() + 1) * sizeof(wchar_t));
            if (hGlobal != NULL) {
              wchar_t *pData = (wchar_t *)GlobalLock(hGlobal);
              if (pData != NULL) {
                wcscpy_s(pData, item.content.length() + 1,
                         item.content.c_str());
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
              loadedFromFile = LoadOriginalImage(
                  item.imageFileName, originalData, origWidth, origHeight);
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
              HGLOBAL hGlobal = GlobalAlloc(
                  GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + imageSize);
              if (hGlobal != NULL) {
                BYTE *pData = (BYTE *)GlobalLock(hGlobal);
                if (pData != NULL) {
                  memcpy(pData, &bmi.bmiHeader, sizeof(BITMAPINFOHEADER));
                  memcpy(pData + sizeof(BITMAPINFOHEADER), &originalData[0],
                         imageSize);
                  GlobalUnlock(hGlobal);
                  SetClipboardData(CF_DIB, hGlobal);
                }
              }
            } else {
              // 回退到使用缩略图数据（兼容旧数据）
              int srcWidth =
                  item.thumbWidth > 0 ? item.thumbWidth : item.imageWidth;
              int srcHeight =
                  item.thumbHeight > 0 ? item.thumbHeight : item.imageHeight;

              BITMAPINFO bmi = {};
              bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
              bmi.bmiHeader.biWidth = srcWidth;
              bmi.bmiHeader.biHeight = -srcHeight;
              bmi.bmiHeader.biPlanes = 1;
              bmi.bmiHeader.biBitCount = 24;
              bmi.bmiHeader.biCompression = BI_RGB;

              DWORD imageSize = item.imageData.size();
              HGLOBAL hGlobal = GlobalAlloc(
                  GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + imageSize);
              if (hGlobal != NULL) {
                BYTE *pData = (BYTE *)GlobalLock(hGlobal);
                if (pData != NULL) {
                  memcpy(pData, &bmi.bmiHeader, sizeof(BITMAPINFOHEADER));
                  memcpy(pData + sizeof(BITMAPINFOHEADER), &item.imageData[0],
                         imageSize);
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
          CloseTagPopup();
          ShowWindow(hwnd, SW_HIDE);
        }

        // 等待剪贴板数据设置完成
        Sleep(100);

        // 激活之前的窗口
        if (g_previousActiveWindow != NULL &&
            IsWindow(g_previousActiveWindow)) {
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
        IncrementPasteCount();
      }
    } else if (wID == IDM_FAVORITE) {
      // 保留兼容性：旧的收藏/取消收藏逻辑
      if (g_contextMenuIndex >= 0 &&
          g_contextMenuIndex < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[g_contextMenuIndex];
        g_history[actualIndex].isFavorite = !g_history[actualIndex].isFavorite;
        SaveHistory();
        UpdateListBox();
        if (g_isNotificationEnabled) {
          ShowTrayBalloon(hwnd, L"提示",
                          g_history[actualIndex].isFavorite ? L"已收藏"
                                                            : L"已取消收藏");
        }
      }
    } else if (wID >= IDM_TAG_BASE && wID < IDM_TAG_FILTER_ALL) {
      // 标签菜单项点击：添加/移除标签
      int tagId = wID - IDM_TAG_BASE;
      if (g_contextMenuIndex >= 0 &&
          g_contextMenuIndex < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[g_contextMenuIndex];
        if (ItemHasTag(actualIndex, tagId)) {
          RemoveTagFromItem(actualIndex, tagId);
          if (g_isNotificationEnabled) {
            Tag *tag = GetTagById(tagId);
            if (tag) {
              ShowTrayBalloon(hwnd, L"提示",
                              (L"已移除标签: " + tag->name).c_str());
            }
          }
        } else {
          AddTagToItem(actualIndex, tagId);
          if (g_isNotificationEnabled) {
            Tag *tag = GetTagById(tagId);
            if (tag) {
              ShowTrayBalloon(hwnd, L"提示",
                              (L"已添加标签: " + tag->name).c_str());
            }
          }
        }
        SaveHistory();
        UpdateListBox();
      }
    } else if (wID == IDM_OPEN_LOCATION) {
      // 右键菜单：打开所在位置
      if (g_contextMenuIndex >= 0 &&
          g_contextMenuIndex < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[g_contextMenuIndex];
        const ClipboardItem &item = g_history[actualIndex];
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
          ShellExecuteW(NULL, L"open", L"explorer.exe", cmd.c_str(), NULL,
                        SW_SHOWNORMAL);
        }
      }
    } else if (wID == IDM_DELETE) {
      // 右键菜单：删除
      if (g_isBatchEditMode && !g_selectedItems.empty()) {
        // 批量删除
        ThemedConfirmDialogConfig dialog = {
            L"批量删除项目",
            L"删除选中的剪贴板项目",
            L"该操作不可撤销",
            L"所选项目及其关联的本地预览文件将被永久删除，请确认是否继续。",
            L"删除",
            L"取消",
            424,
            246,
            {14, 78, 410, 180},
            true,
            false,
            true};
        if (ShowThemedConfirmDialog(hwnd, dialog)) {
          // 按索引从大到小排序，避免删除时索引变化
          std::sort(g_selectedItems.rbegin(), g_selectedItems.rend());
          for (int actualIndex : g_selectedItems) {
            if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
              // 删除关联的图片文件
              const ClipboardItem &delItem = g_history[actualIndex];
              if (delItem.type == TYPE_IMAGE &&
                  !delItem.imageFileName.empty()) {
                std::wstring imgFile =
                    GetImagesPath() + L"\\" + delItem.imageFileName;
                DeleteFileW(imgFile.c_str());
                std::wstring thumbFile =
                    GetThumbsPath() + L"\\" + delItem.imageFileName;
                DeleteFileW(thumbFile.c_str());
              }
              g_history.erase(g_history.begin() + actualIndex);
            }
          }
          SaveHistory();
          g_selectedItems.clear(); // 清空选中列表
          g_batchSelectionAnchorDisplayIndex = LB_ERR;
          UpdateListBox();
          if (g_isNotificationEnabled) {
            ShowTrayBalloon(hwnd, L"提示", L"已批量删除");
          }
        }
      } else if (g_contextMenuIndex >= 0 &&
                 g_contextMenuIndex < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[g_contextMenuIndex]; // 获取实际索引
        bool shouldDelete = true;

        if (g_history[actualIndex].isFavorite) {
          ThemedConfirmDialogConfig dialog = {
              L"删除收藏记录",
              L"删除当前收藏",
              L"这条收藏会从历史中移除",
              L"该项目当前已收藏，删除后无法恢复。",
              L"删除记录",
              L"取消",
              424,
              246,
              {14, 78, 410, 180},
              true,
              false,
              true};
          shouldDelete = ShowThemedConfirmDialog(hwnd, dialog);
        }

        if (shouldDelete) {
          // 删除关联的图片文件
          const ClipboardItem &delItem = g_history[actualIndex];
          if (delItem.type == TYPE_IMAGE && !delItem.imageFileName.empty()) {
            std::wstring imgFile =
                GetImagesPath() + L"\\" + delItem.imageFileName;
            DeleteFileW(imgFile.c_str());
            std::wstring thumbFile =
                GetThumbsPath() + L"\\" + delItem.imageFileName;
            DeleteFileW(thumbFile.c_str());
          }
          g_history.erase(g_history.begin() + actualIndex);
          SaveHistory();
          UpdateListBox();
          if (g_isNotificationEnabled) {
            ShowTrayBalloon(hwnd, L"提示", L"已删除");
          }
        }
      }
    } else if (wID >= IDM_BATCH_ADD_TAG) {
      // 批量编辑模式：批量加入标签（处理二级菜单选择）
      if (g_isBatchEditMode && !g_selectedItems.empty()) {
        int tagId = wID - IDM_BATCH_ADD_TAG;
        std::vector<int> selectionOrder = g_selectedItems;
        // 为所有选中的项目添加标签
        for (int actualIndex : selectionOrder) {
          if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
            g_history[actualIndex].tagIds.insert(tagId);
            g_history[actualIndex].isFavorite = true;
          }
        }
        PromoteHistoryItemsToFrontInOrder(selectionOrder);
        SaveHistory();
        g_selectedItems.clear();
        g_batchSelectionAnchorDisplayIndex = LB_ERR;
        UpdateListBox();
        if (g_isNotificationEnabled) {
          ShowTrayBalloon(hwnd, L"提示", L"已批量加入标签");
        }
      }
    } else if (wID == IDM_NOTIFICATION) {
      // 托盘菜单：切换消息通知
      g_isNotificationEnabled = !g_isNotificationEnabled;
      if (g_isNotificationEnabled) {
        ShowTrayBalloon(hwnd, T(STR_TRAY_NOTIFY_UPDATED),
                        T(STR_TRAY_NOTIFICATIONS_ENABLED));
      }
    } else if (wID == IDM_THEME_LIGHT) {
      // 托盘菜单：切换到日间模式
      if (g_themeMode != THEME_LIGHT || g_isDarkMode) {
        g_themeMode = THEME_LIGHT;
        ApplyTheme();
        SaveHotkeySettings();
        if (g_hwndSettingsDlg && IsWindow(g_hwndSettingsDlg)) {
          SendMessageW(g_hwndSettingsDlg, WM_THEMECHANGED, 0, 0);
        }
      }
    } else if (wID == IDM_THEME_DARK) {
      // 托盘菜单：切换到夜间模式
      if (g_themeMode != THEME_DARK || !g_isDarkMode) {
        g_themeMode = THEME_DARK;
        ApplyTheme();
        SaveHotkeySettings();
        if (g_hwndSettingsDlg && IsWindow(g_hwndSettingsDlg)) {
          SendMessageW(g_hwndSettingsDlg, WM_THEMECHANGED, 0, 0);
        }
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
    } else if (wParam == ID_FAVORITE_TOOLTIP_TIMER) {
      KillTimer(hwnd, ID_FAVORITE_TOOLTIP_TIMER);
      HideFavoriteFilterTooltip();
    } else if (wParam == ID_TOPMOST_ANIM_TIMER) {
      // 置顶按钮波浪动画
      g_topmostAnimProgress += 0.08f; // 动画速度
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
      ShowTagPopup(hwnd, btnRect.left, btnRect.bottom,
                   btnRect.right - btnRect.left, false);
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
        int index = SendMessageW(g_hwndListBox, LB_ITEMFROMPOINT, 0,
                                 MAKELPARAM(clientPt.x, clientPt.y));
        if (HIWORD(index) == 0) { // 在列表框内
          g_contextMenuIndex = LOWORD(index);
          if (g_isBatchEditMode) {
            int actualIndex = -1;
            if (g_contextMenuIndex >= 0 &&
                g_contextMenuIndex < (int)g_displayIndexMap.size()) {
              actualIndex = g_displayIndexMap[g_contextMenuIndex];
            }
            if (actualIndex >= 0) {
              auto it = std::find(g_selectedItems.begin(),
                                  g_selectedItems.end(), actualIndex);
              if (it == g_selectedItems.end()) {
                g_selectedItems.clear();
                g_selectedItems.push_back(actualIndex);
                g_batchSelectionAnchorDisplayIndex = g_contextMenuIndex;
                InvalidateRect(g_hwndListBox, NULL, TRUE);
              }
            }
          } else {
            SendMessageW(g_hwndListBox, LB_SETCURSEL, g_contextMenuIndex, 0);
          }
        } else {
          return 0; // 不在项上，不显示菜单
        }
      }

      // 创建右键菜单
      HMENU hMenu = CreatePopupMenu();
      if (g_isBatchEditMode && !g_selectedItems.empty()) {
        // 批量编辑模式下的右键菜单
        // 创建菜单图标
        HBITMAP hDeleteIcon =
            CreateMenuIconBitmap(L"\uE74D", RGB(200, 60, 60)); // Delete (红色)
        HBITMAP hTagIcon = CreateMenuIconBitmap(L"\uE719");    // Tag

        MENUITEMINFOW mii = {};
        mii.cbSize = sizeof(MENUITEMINFOW);
        mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;

        // 批量加入标签（二级菜单）
        HMENU hTagSubMenu = CreatePopupMenu();
        // 收集标签颜色位图，用于后续释放
        std::vector<HBITMAP> batchTagColorBitmaps;
        if (!g_tags.empty()) {
          for (const auto &tag : g_tags) {
            AppendMenuW(hTagSubMenu, MF_STRING, IDM_BATCH_ADD_TAG + tag.id,
                        tag.name.c_str());
            // 为标签菜单项添加颜色方块位图
            HBITMAP hColorBmp = CreateMenuColorBitmap(tag.color);
            batchTagColorBitmaps.push_back(hColorBmp);
            MENUITEMINFOW tagMii = {};
            tagMii.cbSize = sizeof(MENUITEMINFOW);
            tagMii.fMask = MIIM_BITMAP;
            tagMii.hbmpItem = hColorBmp;
            SetMenuItemInfoW(hTagSubMenu, IDM_BATCH_ADD_TAG + tag.id, FALSE,
                             &tagMii);
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
        DeleteObject(hTagIcon);
        // 释放批量标签颜色位图
        for (HBITMAP hBmp : batchTagColorBitmaps) {
          DeleteObject(hBmp);
        }
      } else if (g_contextMenuIndex >= 0 &&
                 g_contextMenuIndex < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[g_contextMenuIndex]; // 获取实际索引
        const ClipboardItem &item = g_history[actualIndex];

        // 创建菜单图标
        HBITMAP hCopyIcon = CreateMenuIconBitmap(L"\uE8C8");  // Copy
        HBITMAP hPasteIcon = CreateMenuIconBitmap(L"\uE77F"); // Paste
        HBITMAP hFavoriteIcon = CreateMenuIconBitmap(
            item.isFavorite ? L"\uE735"
                            : L"\uE734"); // FavoriteStar/FavoriteStarFill
        HBITMAP hDeleteIcon =
            CreateMenuIconBitmap(L"\uE74D", RGB(200, 60, 60)); // Delete (红色)
        HBITMAP hOpenLocationIcon =
            CreateMenuIconBitmap(L"\uE838"); // OpenFolderHorizontal

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

        // 标签子菜单
        HMENU hTagMenu = CreatePopupMenu();
        // 收集标签颜色位图，用于后续释放
        std::vector<HBITMAP> tagColorBitmaps;
        for (const auto &tag : g_tags) {
          UINT flags = MF_STRING;
          // 如果项目已有该标签，显示勾选
          if (item.tagIds.count(tag.id) > 0) {
            flags |= MF_CHECKED;
          }
          AppendMenuW(hTagMenu, flags, IDM_TAG_BASE + tag.id, tag.name.c_str());
          // 为标签菜单项添加颜色方块位图
          HBITMAP hColorBmp = CreateMenuColorBitmap(tag.color);
          tagColorBitmaps.push_back(hColorBmp);
          MENUITEMINFOW tagMii = {};
          tagMii.cbSize = sizeof(MENUITEMINFOW);
          tagMii.fMask = MIIM_BITMAP;
          tagMii.hbmpItem = hColorBmp;
          SetMenuItemInfoW(hTagMenu, IDM_TAG_BASE + tag.id, FALSE, &tagMii);
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
          std::wstring screenshotPath =
              GetImagesPath() + L"\\" + item.imageFileName;
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
        DeleteObject(hFavoriteIcon);
        DeleteObject(hDeleteIcon);
        DeleteObject(hOpenLocationIcon);
        // 释放标签颜色位图
        for (HBITMAP hBmp : tagColorBitmaps) {
          DeleteObject(hBmp);
        }
      }
      DestroyMenu(hMenu);
    }
    break;
  }
  case WM_LBUTTONDOWN: {
    if (g_isFavoriteTooltipVisible) {
      HideFavoriteFilterTooltip();
      KillTimer(hwnd, ID_FAVORITE_TOOLTIP_TIMER);
    }
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
      static HBRUSH hBrush = NULL;
      static COLORREF s_lastColor = CLR_INVALID;
      if (!hBrush || s_lastColor != bgColor) {
        if (hBrush)
          DeleteObject(hBrush);
        hBrush = CreateSolidBrush(bgColor);
        s_lastColor = bgColor;
      }
      return (LRESULT)hBrush;
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
      static HBRUSH hListBrush = NULL;
      static COLORREF s_lastColor = CLR_INVALID;
      if (!hListBrush || s_lastColor != bgColor) {
        if (hListBrush)
          DeleteObject(hListBrush);
        hListBrush = CreateSolidBrush(bgColor);
        s_lastColor = bgColor;
      }
      return (LRESULT)hListBrush;
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
      DrawIconEx(hdc, 10, (TITLEBAR_HEIGHT - 16) / 2, hIcon, 16, 16, 0, NULL,
                 DI_NORMAL);
    }

    // 绘制标题文字
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetTextColor());
    HFONT hTitleFont = CreateFontW(
        17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hTitleFont);
    RECT rcTitleText = {32, 0, clientWidth - 46 * 4, TITLEBAR_HEIGHT};
    DrawTextW(hdc, L"Smart Clip", -1, &rcTitleText, DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, hOldFont);
    DeleteObject(hTitleFont);

    // 绘制搜索框边框
    if (g_hwndSearchBox) {
      RECT searchRect;
      GetWindowRect(g_hwndSearchBox, &searchRect);
      MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&searchRect, 2);

      // 扩展矩形以绘制边框
      RECT borderRect = {searchRect.left - 3, searchRect.top - 3,
                         searchRect.right + 3, searchRect.bottom + 3};

      Graphics graphics(hdc);
      graphics.SetSmoothingMode(SmoothingModeAntiAlias);

      // 绘制圆角矩形边框
      int radius = 8;
      GraphicsPath path;
      path.AddArc(borderRect.left, borderRect.top, radius * 2, radius * 2, 180,
                  90);
      path.AddArc(borderRect.right - radius * 2, borderRect.top, radius * 2,
                  radius * 2, 270, 90);
      path.AddArc(borderRect.right - radius * 2, borderRect.bottom - radius * 2,
                  radius * 2, radius * 2, 0, 90);
      path.AddArc(borderRect.left, borderRect.bottom - radius * 2, radius * 2,
                  radius * 2, 90, 90);
      path.CloseFigure();

      SolidBrush fillBrush(Color(255, GetRValue(GetWhiteColor()),
                                 GetGValue(GetWhiteColor()),
                                 GetBValue(GetWhiteColor())));
      graphics.FillPath(&fillBrush, &path);

      if (g_isDarkMode) {
        // 暗黑模式：与背景一致的边框色
        Pen borderPen(Color(255, 60, 60, 64), 1.0f);
        graphics.DrawPath(&borderPen, &path);
      } else {
        // 日间模式：渐变边框
        LinearGradientBrush gradientBrush(
            Point(borderRect.left, borderRect.top),
            Point(borderRect.right, borderRect.top),
            Color(255, 0x65, 0x47, 0xFF), Color(255, 0x00, 0x90, 0xFE));
        Color colors[] = {Color(255, 0x65, 0x47, 0xFF),
                          Color(255, 0x47, 0x69, 0xFF),
                          Color(255, 0x00, 0x90, 0xFE)};
        REAL positions[] = {0.0f, 0.5f, 1.0f};
        gradientBrush.SetInterpolationColors(colors, positions, 3);
        Pen gradientPen(&gradientBrush, 1.0f);
        graphics.DrawPath(&gradientPen, &path);
      }
    }

    EndPaint(hwnd, &ps);
    break;
  }
  case WM_CLOSE: {
    KillTimer(hwnd, ID_FAVORITE_TOOLTIP_TIMER);
    HideFavoriteFilterTooltip();
    CloseTagPopup();
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
    if (!g_isRestoringClipboard && OpenClipboard(NULL)) {
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
                if (attrs != INVALID_FILE_ATTRIBUTES &&
                    (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
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
          BITMAPINFO *pBitmapInfo = (BITMAPINFO *)GlobalLock(hGlobal);
          if (pBitmapInfo != NULL && globalSize > sizeof(BITMAPINFOHEADER)) {
            int width = pBitmapInfo->bmiHeader.biWidth;
            int height = abs(pBitmapInfo->bmiHeader.biHeight);
            bool isBottomUp = pBitmapInfo->bmiHeader.biHeight > 0; // 自底向上
            int bpp = pBitmapInfo->bmiHeader.biBitCount;

            // 验证图像尺寸
            if (width > 0 && width <= 10000 && height > 0 && height <= 10000 &&
                (bpp == 24 || bpp == 32)) {
              // 计算调色板大小
              int paletteSize = 0;
              if (bpp <= 8) {
                paletteSize = (pBitmapInfo->bmiHeader.biClrUsed
                                   ? pBitmapInfo->bmiHeader.biClrUsed
                                   : (1 << bpp)) *
                              sizeof(RGBQUAD);
              } else if (pBitmapInfo->bmiHeader.biClrUsed > 0) {
                paletteSize =
                    pBitmapInfo->bmiHeader.biClrUsed * sizeof(RGBQUAD);
              }

              // 计算源图像每行字节数（4字节对齐）
              int srcRowBytes = ((width * bpp + 31) / 32) * 4;
              DWORD srcImageSize = srcRowBytes * height;

              // 计算数据偏移
              SIZE_T dataOffset = pBitmapInfo->bmiHeader.biSize + paletteSize;

              // 验证数据大小
              if (globalSize >= dataOffset + srcImageSize) {
                BYTE *pImageData = (BYTE *)pBitmapInfo + dataOffset;

                // 目标格式：24位
                int dstRowBytes = ((width * 24 + 31) / 32) * 4;
                DWORD dstImageSize = dstRowBytes * height;
                std::vector<BYTE> imageData(dstImageSize);

                // 转换并复制图像数据
                for (int y = 0; y < height; y++) {
                  int srcY = isBottomUp ? (height - 1 - y) : y;
                  BYTE *pSrcRow = pImageData + srcY * srcRowBytes;
                  BYTE *pDstRow = &imageData[y * dstRowBytes];

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
          wchar_t *pData = (wchar_t *)GlobalLock(hGlobal);
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
          char *pData = (char *)GlobalLock(hGlobal);
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
        CloseTagPopup();
        ShowWindow(hwnd, SW_HIDE);
      } else {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
      }
    } else if (lParam == WM_RBUTTONDOWN) {
      // 创建独立的托盘菜单，仅包含设置和退出
      HMENU hTrayMenu = CreatePopupMenu();

      // 创建菜单图标
      HBITMAP hSettingsIcon =
          CreateMenuIconBitmap(L"\uE713", RGB(60, 60, 60), 3); // Settings
      HBITMAP hNotificationIcon = CreateMenuIconBitmap(
          g_isNotificationEnabled ? L"\uEA8F" : L"\uE7ED", RGB(60, 60, 60),
          3); // Ringer/RingerOff
      HBITMAP hLightModeIcon = CreateMenuIconBitmap(L"\uE706", RGB(60, 60, 60),
                                                    3); // Brightness (太阳)
      HBITMAP hDarkModeIcon =
          CreateMenuIconBitmap(L"\uE708", RGB(60, 60, 60), 3); // Moon (月亮)
      HBITMAP hExitIcon =
          CreateMenuIconBitmap(L"\uE7E8", RGB(200, 60, 60), 3); // Power (红色)

      // 添加设置和退出到托盘菜单（带图标）
      MENUITEMINFOW mii = {};
      mii.cbSize = sizeof(MENUITEMINFOW);
      mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;

      mii.wID = IDM_SETTINGS;
      mii.dwTypeData = (LPWSTR)T(STR_TRAY_MENU_SETTINGS);
      mii.hbmpItem = hSettingsIcon;
      InsertMenuItemW(hTrayMenu, 0, TRUE, &mii);

      mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP | MIIM_STATE;
      mii.wID = IDM_NOTIFICATION;
      mii.dwTypeData = (LPWSTR)T(STR_TRAY_MENU_NOTIFICATIONS);
      mii.hbmpItem = hNotificationIcon;
      mii.fState = g_isNotificationEnabled ? MFS_CHECKED : MFS_UNCHECKED;
      InsertMenuItemW(hTrayMenu, 1, TRUE, &mii);

      int insertIndex = 2;

      // 主题切换：只显示对立模式
      mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;
      if (g_isDarkMode) {
        mii.wID = IDM_THEME_LIGHT;
        mii.dwTypeData = (LPWSTR)T(STR_TRAY_MENU_LIGHT);
        mii.hbmpItem = hLightModeIcon;
      } else {
        mii.wID = IDM_THEME_DARK;
        mii.dwTypeData = (LPWSTR)T(STR_TRAY_MENU_DARK);
        mii.hbmpItem = hDarkModeIcon;
      }
      InsertMenuItemW(hTrayMenu, insertIndex++, TRUE, &mii);

      mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;
      mii.fState = 0;
      mii.wID = IDM_EXIT;
      mii.dwTypeData = (LPWSTR)T(STR_TRAY_MENU_EXIT);
      mii.hbmpItem = hExitIcon;
      InsertMenuItemW(hTrayMenu, insertIndex, TRUE, &mii);

      // 显示托盘菜单
      POINT pt;
      GetCursorPos(&pt);
      SetForegroundWindow(hwnd);
      TrackPopupMenu(hTrayMenu, TPM_RIGHTBUTTON | TPM_TOPALIGN, pt.x, pt.y, 0,
                     hwnd, NULL);

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
        CloseTagPopup();
        ShowWindow(hwnd, SW_HIDE);
      } else {
        // 记录当前活动窗口（呼出剪贴板前的窗口）
        g_previousActiveWindow = GetForegroundWindow();
        ShowWindow(hwnd, SW_RESTORE);
        ShowWindow(hwnd, SW_SHOW);
        // 强制置顶显示，避免被其他窗口遮挡
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
        if (!g_isTopmost) {
          // 非置顶模式下，短暂置顶后恢复
          SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE);
        }
        if (g_hwndSearchBox) {
          SetFocus(g_hwndSearchBox);
          SendMessageW(g_hwndSearchBox, EM_SETSEL, 0, -1);
        }
      }
    } else if (wParam >= ID_HOTKEY_PASTE_1 && wParam <= ID_HOTKEY_PASTE_10) {
      int pasteOffset = (int)(wParam - ID_HOTKEY_PASTE_1);
      int visibleIds[10] = {};
      int visibleCount = CollectVisibleShortcutDisplayIndices(visibleIds, 10);
      if (pasteOffset >= 0 && pasteOffset < visibleCount) {
        PasteHistoryItemByDisplayIndex(hwnd, visibleIds[pasteOffset]);
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
  wcex.hbrBackground = CreateSolidBrush(RGB(245, 245, 245)); // 自定义背景色
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

  // 计算屏幕中央位置
  int screenWidth = GetSystemMetrics(SM_CXSCREEN);
  int screenHeight = GetSystemMetrics(SM_CYSCREEN);
  int windowWidth = 600;
  int windowHeight = 694;
  int x = (screenWidth - windowWidth) / 2;
  int y = (screenHeight - windowHeight) / 2;

  g_hwndMain =
      CreateWindowExW(WS_EX_COMPOSITED, L"SmartClip", L"Smart Clip",
                      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, windowWidth,
                      windowHeight, NULL, NULL, hInstance, NULL);

  if (!g_hwndMain) {
    return FALSE;
  }

  // 修改窗口样式：移除系统标题栏但保留边框
  LONG_PTR style = GetWindowLongPtrW(g_hwndMain, GWL_STYLE);
  style &= ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX |
             WS_MAXIMIZEBOX);
  style |= WS_THICKFRAME; // 重新添加可调整大小的边框
  SetWindowLongPtrW(g_hwndMain, GWL_STYLE, style);

  // 强制重新计算窗口框架
  SetWindowPos(g_hwndMain, NULL, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                   SWP_NOOWNERZORDER);

  // 移除窗口阴影
  BOOL bEnable = FALSE;
  DwmSetWindowAttribute(g_hwndMain, DWMWA_NCRENDERING_POLICY, &bEnable,
                        sizeof(bEnable));

  ApplyTheme();

  ShowWindow(g_hwndMain, nCmdShow);
  UpdateWindow(g_hwndMain);

  return TRUE;
}

// 运行应用程序
int RunApplication() {
  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0)) {
    if (HandleMainNavigationKey(msg))
      continue;

    // 处理应用内搜索框快捷键
    if (msg.message == WM_KEYDOWN && g_isSearchHotkeyEnabled &&
        IsWindowVisible(g_hwndMain)) {
      UINT modifiers = 0;
      if (GetKeyState(VK_CONTROL) & 0x8000)
        modifiers |= MOD_CONTROL;
      if (GetKeyState(VK_SHIFT) & 0x8000)
        modifiers |= MOD_SHIFT;
      if (GetKeyState(VK_MENU) & 0x8000)
        modifiers |= MOD_ALT;

      if (modifiers == g_searchHotkeyModifiers &&
          msg.wParam == g_searchHotkeyVirtualKey) {
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
  const wchar_t *title;
  const wchar_t *prompt;
  wchar_t *result;
  int maxLen;
};

INT_PTR CALLBACK InputBoxProc(HWND hDlg, UINT message, WPARAM wParam,
                              LPARAM lParam) {
  static wchar_t *result = NULL;
  static int maxLen = 0;

  switch (message) {
  case WM_INITDIALOG: {
    if (lParam) {
      InitParam *initParam = (InitParam *)lParam;
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

bool InputBox(HWND hwnd, const wchar_t *title, const wchar_t *prompt,
              wchar_t *result, int maxLen) {
  HINSTANCE hInstance = GetModuleHandle(NULL);

  // 简化实现：使用创建窗口的方式
  HWND hDialog = CreateWindowExW(
      0, L"#32770", title, WS_POPUP | WS_CAPTION | DS_MODALFRAME | WS_SYSMENU,
      CW_USEDEFAULT, CW_USEDEFAULT, 250, 120, hwnd, NULL, hInstance, NULL);

  if (!hDialog)
    return false;

  // 创建静态文本
  HWND hStatic =
      CreateWindowExW(0, L"STATIC", prompt, WS_CHILD | WS_VISIBLE, 10, 10, 230,
                      20, hDialog, (HMENU)1001, hInstance, NULL);

  // 创建编辑框
  HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 10, 35,
                               230, 20, hDialog, (HMENU)1002, hInstance, NULL);
  SendDlgItemMessageW(hDialog, 1002, EM_LIMITTEXT, maxLen - 1, 0);

  // 创建确定按钮
  HWND hOK = CreateWindowExW(0, L"BUTTON", L"确定",
                             WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 150, 65,
                             80, 25, hDialog, (HMENU)IDOK, hInstance, NULL);

  // 创建取消按钮
  HWND hCancel = CreateWindowExW(
      0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 60, 65, 80,
      25, hDialog, (HMENU)IDCANCEL, hInstance, NULL);

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
  int x = rectParent.left + (rectParent.right - rectParent.left) / 2 -
          (rectDialog.right - rectDialog.left) / 2;
  int y = rectParent.top + (rectParent.bottom - rectParent.top) / 2 -
          (rectDialog.bottom - rectDialog.top) / 2;
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
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                    LPWSTR lpCmdLine, int nCmdShow) {
  UNREFERENCED_PARAMETER(lpCmdLine);
  // 创建命名互斥量，检测是否已有实例在运行
  HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Global\\SmartClipMutex");
  if (hMutex == NULL) {
    // 创建互斥量失败，退出程序
    return 1;
  }

  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    // 显示"本程序已在运行"的消息提示
    MessageBoxW(NULL, L"本程序已在运行", L"Smart Clip",
                MB_OK | MB_ICONINFORMATION);

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
  iccex.dwICC = ICC_STANDARD_CLASSES |
                ICC_WIN95_CLASSES; // ICC_WIN95_CLASSES 包含 tooltip 控件
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

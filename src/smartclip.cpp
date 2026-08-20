#include "smartclip.h"
#include "card_renderer.h"
#include "drag_drop.h"
#include "graphics_utils.h"
#include "history.h"
#include "hotkey.h"
#include "i18n.h"
#include "image_handler.h"
#include "resource.h" // 添加资源头文件
#include "scrollbar.h"
#include "search.h"
#include "settings.h"
#include "tag_popup.h"
#include "text_editor.h"
#include "text_utils.h"
#include "theme.h"
#include "themed_dialog.h"
#include "tray.h"
#include "version.h"
#include <algorithm> // 用于std::remove_if
#include <cmath>     // 用于sin函数

// 部分 MinGW 头文件未定义 DT_CLIP（值 0x4：文本裁剪到矩形内）
#ifndef DT_CLIP
#define DT_CLIP 0x4
#endif
#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <map>      // 用于文件图标缓存
#include <objidl.h> // MinGW 下必须在 gdiplus.h 之前,提供 PROPID
#include <ole2.h>   // 用于OLE拖放
#include <regex>
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
#define IDM_PAUSE_RESUME 2009       // 托盘菜单：暂停/恢复剪贴板监听
#define IDM_QUICK_PASTE_TOGGLE 2010 // 托盘菜单：启用/关闭快捷键（本 app 所有）
#define IDM_RESTART 2011            // 托盘菜单：重启应用
#define IDM_VIEW_AGREEMENT 2012     // 托盘菜单：查看用户协议
#define ID_TOPMOST_BUTTON 1006
#define ID_DARKMODE_BUTTON 1007
#define IDM_COPY 3001
#define IDM_PASTE 3002
#define IDM_FAVORITE 3003
#define IDM_DELETE 3004
#define IDM_SELECT_IN_EXPLORER 3005 // 在资源管理器中选中多文件
#define IDM_DELETE_SUBITEM 3006     // 删除多文件记录中的单个子行
#define IDM_OPEN_LOCATION 3007      // 打开所在位置
#define IDM_BATCH_ADD_TAG 3008      // 批量加入标签
#define IDM_EDIT 3009               // 编辑文本记录

// 标签菜单ID（动态分配，从3100开始）
#define IDM_TAG_BASE 3100
#define IDM_TAG_FILTER_ALL 3200   // 全部收藏筛选
#define IDM_TAG_FILTER_BASE 3201  // 标签筛选基础ID
#define IDM_TAG_ADD_NEW 3300      // 新增标签
#define IDM_BATCH_PASTE_ASC 3400  // 连续粘贴-正序
#define IDM_BATCH_PASTE_DESC 3401 // 连续粘贴-反序
#define ID_RESTORE_TOPMOST_AFTER_PASTE 209
#define ID_RESTORE_FOCUS_FOR_PASTE 210
#define ID_SEND_DEFERRED_PASTE 211

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

static UINT g_mainUiDpi = 96;
static int MScale(int value) { return ScaleForDpi(value, g_mainUiDpi); }
static int MainTitlebarHeight() { return MScale(TITLEBAR_HEIGHT); }

// 筛选按钮句柄
HWND g_hwndFilterAll = NULL;
HWND g_hwndFilterText = NULL;
HWND g_hwndFilterImage = NULL;
HWND g_hwndFilterFile = NULL;
HWND g_hwndFilterFavorite = NULL;
// 剪贴板恢复标志
bool g_isRestoringClipboard = false;
// 剪贴板监听暂停标志（托盘菜单切换，暂停期间不录入新剪贴板内容）
bool g_isClipboardPaused = false;
static bool g_deferredPasteSimulate = false;
static bool g_deferredPasteWaitForModifierRelease = false;
static bool g_restoreMainWindowAfterPaste = false;
static bool g_deferredPasteKeepsTopmostVisible = false;
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
COLORREF GetWhiteColor() { return GetThemeSurfaceColor(); }
inline COLORREF GetTextColor() { return GetThemeTextPrimaryColor(); }
inline COLORREF GetAccentColor() { return GetThemeAccentColor(); }
COLORREF GetAccentStrongColor() { return GetThemeAccentStrongColor(); }

// 当前右键选中的索引
int g_contextMenuIndex = -1;
// 右键选中的多文件子项显示索引（-1=非子项）
int g_contextSubItemDisplay = -1;
// 记录呼出剪贴板前的活动窗口
HWND g_previousActiveWindow = NULL;
// 记录呼出剪贴板前拥有焦点的子窗口（用于精确恢复焦点，如资源管理器地址栏）
HWND g_previousFocusWindow = NULL;
// 不抢焦点模式：通过快捷键呼出时不激活窗口，保留原输入框焦点
bool g_isNoActivateMode = false;

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
int g_hoverTimestampIndex = -1;           // 鼠标悬浮的时间戳所在项目索引
bool g_isHoveringTimestamp = false;       // 鼠标是否悬浮在时间戳上
float g_folderUnderlineProgress = 0.0f;   // 文件夹下划线动画进度 0.0-1.0
bool g_folderUnderlineAnimating = false;  // 是否正在下划线动画
static std::wstring g_listBoxTooltipText; // 跟踪 Tooltip 文本缓存

// ===== 快速筛选药丸（搜索框内） =====
struct QuickFilterPill {
  std::wstring text;
  RECT rect;      // 药丸完整区域
  RECT closeRect; // 关闭按钮区域（右上角）
  int type;       // 1=日期, 2=应用, 3=收藏分类
  COLORREF color; // 药丸底色（日期/应用=默认蓝；分类=分类色块颜色）
};
static QuickFilterPill g_pills[3]; // 最多3个药丸（日期+应用+收藏分类）
static int g_pillCount = 0;
static int g_hoveredPill = -1;          // 当前悬浮的药丸索引
static bool g_pillCloseHovered = false; // 关闭按钮是否被悬浮

// ===== 文本选中复制状态（鼠标拖选文本内容 + Ctrl+C 复制） =====
static int g_textSelItem = -1;         // 选中文本项的显示索引（-1=无）
static int g_textSelAnchor = -1;       // 选中锚点（内容字符位置）
static int g_textSelEnd = -1;          // 选中末端（内容字符位置）
static bool g_textSelDragging = false; // 是否正在拖选（超过阈值后置 true）
static POINT g_textSelDownPt = {};     // 按下点（用于拖选阈值判断）

// ===== 悬浮选中后的单击粘贴（与 PRO 版行为一致） =====
// LBUTTONDOWN 时记录"按下前已选中"（通常由悬浮选中产生）的显示索引；
// LBUTTONUP 时若点击位置仍在该记录上且未发生拖拽，则直接粘贴。-1=不触发。
static int g_singleClickPasteIndex = -1;
// UP 处理中主动 ReleaseCapture 的标志：ReleaseCapture 会同步触发
// WM_CAPTURECHANGED，若不区分，会把"单击粘贴标记"一并清除，导致
// 文本项单击不粘贴（须双击走 LBN_DBLCLK 才粘贴）。
static bool g_releasingCaptureForClick = false;

// 快速筛选是否激活（有日期、应用或分类筛选）
static bool IsQuickFilterActive() {
  return !g_quickFilterApp.empty() || !g_quickFilterDate.empty() ||
         g_currentFilterTagId > 0;
}

// 清除所有快速筛选
static void ClearQuickFilter() {
  g_quickFilterApp.clear();
  g_quickFilterDate.clear();
  g_currentFilterTagId = 0;
}

// 计算搜索框内药丸布局，返回药丸占据的总宽度
// 对称布局: [padX] text [gap] closeBtn [padX]
static int UpdateSearchPillLayout(HWND hwndSearch, HDC hdc) {
  g_pillCount = 0;
  if (!IsQuickFilterActive())
    return 0;

  HFONT hPillFont = CreateFontW(
      MScale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
  HFONT hOldFont = (HFONT)SelectObject(hdc, hPillFont);

  RECT rcClient = {};
  GetClientRect(hwndSearch, &rcClient);

  int pillH = MScale(22);
  int padX = MScale(8);          // 左右对称内边距
  int closeBtnSize = MScale(12); // 关闭按钮尺寸
  int gap = MScale(4);           // 文字与关闭按钮间距
  int pillGap = MScale(6);       // 药丸间距
  int startX = padX;
  int pillY = (rcClient.bottom - pillH) / 2;

  // 药丸1：日期 #日期
  if (!g_quickFilterDate.empty()) {
    g_pills[g_pillCount].text = L"#" + g_quickFilterDate;
    g_pills[g_pillCount].type = 1;
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(hdc, g_pills[g_pillCount].text.c_str(),
                          (int)g_pills[g_pillCount].text.length(), &sz);
    int pillW = padX + sz.cx + gap + closeBtnSize + padX;
    int pillX = startX;
    g_pills[g_pillCount].rect = {pillX, pillY, pillX + pillW, pillY + pillH};
    // 关闭按钮：右侧垂直居中
    g_pills[g_pillCount].closeRect = {
        pillX + pillW - padX - closeBtnSize, pillY + (pillH - closeBtnSize) / 2,
        pillX + pillW - padX, pillY + (pillH + closeBtnSize) / 2};
    startX += pillW + pillGap;
    g_pillCount++;
  }
  // 药丸2：应用 @应用
  if (!g_quickFilterApp.empty()) {
    std::wstring appDisplay = g_quickFilterApp;
    size_t dotPos = appDisplay.rfind(L".exe");
    if (dotPos != std::wstring::npos)
      appDisplay = appDisplay.substr(0, dotPos);
    g_pills[g_pillCount].text = L"@" + appDisplay;
    g_pills[g_pillCount].type = 2;
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(hdc, g_pills[g_pillCount].text.c_str(),
                          (int)g_pills[g_pillCount].text.length(), &sz);
    int pillW = padX + sz.cx + gap + closeBtnSize + padX;
    int pillX = startX;
    g_pills[g_pillCount].rect = {pillX, pillY, pillX + pillW, pillY + pillH};
    g_pills[g_pillCount].closeRect = {
        pillX + pillW - padX - closeBtnSize, pillY + (pillH - closeBtnSize) / 2,
        pillX + pillW - padX, pillY + (pillH + closeBtnSize) / 2};
    startX += pillW + pillGap;
    g_pillCount++;
  }
  // 药丸3：收藏分类 #分类名（底色=分类色块颜色）
  if (g_currentFilterTagId > 0) {
    Tag *favTag = GetTagById(g_currentFilterTagId);
    if (favTag) {
      g_pills[g_pillCount].text = L"#" + favTag->name;
      g_pills[g_pillCount].type = 3;
      g_pills[g_pillCount].color = favTag->color;
      SIZE sz = {0, 0};
      GetTextExtentPoint32W(hdc, g_pills[g_pillCount].text.c_str(),
                            (int)g_pills[g_pillCount].text.length(), &sz);
      int pillW = padX + sz.cx + gap + closeBtnSize + padX;
      int pillX = startX;
      g_pills[g_pillCount].rect = {pillX, pillY, pillX + pillW, pillY + pillH};
      g_pills[g_pillCount].closeRect = {pillX + pillW - padX - closeBtnSize,
                                        pillY + (pillH - closeBtnSize) / 2,
                                        pillX + pillW - padX,
                                        pillY + (pillH + closeBtnSize) / 2};
      startX += pillW + pillGap;
      g_pillCount++;
    }
  }

  SelectObject(hdc, hOldFont);
  DeleteObject(hPillFont);
  return startX - pillGap;
}

static CDropTarget *g_pDropTarget = NULL;

// 前置声明（定义在下方）
IDataObject *CreateFileDataObject(const std::wstring &filePath);

// 创建多文件拖放数据对象（用于多文件记录拖拽到资源管理器等外部窗口）
// 通过 Shell 的 GetUIObjectOf 一次性传入多个文件 PIDL，得到原生 IDataObject，
// 与资源管理器拖拽行为完全一致。要求所有文件位于同一目录（资源管理器多选
// 复制的常见场景）；若不在同一目录则回退到单文件拖拽。
IDataObject *CreateMultiFileDataObject(const std::vector<std::wstring> &paths) {
  if (paths.empty())
    return NULL;
  if (paths.size() == 1)
    return CreateFileDataObject(paths[0]);

  // 取第一个文件的父目录作为公共父目录
  size_t lastSep = paths[0].find_last_of(L"\\/");
  if (lastSep == std::wstring::npos)
    return CreateFileDataObject(paths[0]);
  std::wstring folderPath = paths[0].substr(0, lastSep);

  PIDLIST_ABSOLUTE pidlFolder = ILCreateFromPathW(folderPath.c_str());
  if (!pidlFolder)
    return CreateFileDataObject(paths[0]);

  IDataObject *pDataObject = NULL;
  IShellFolder *pDesktop = NULL;
  if (SUCCEEDED(SHGetDesktopFolder(&pDesktop))) {
    IShellFolder *pFolder = NULL;
    if (SUCCEEDED(pDesktop->BindToObject(pidlFolder, NULL, IID_IShellFolder,
                                         (void **)&pFolder))) {
      // 收集每个文件的相对 PIDL
      std::vector<PIDLIST_RELATIVE> childPidls;
      bool allOk = true;
      for (const auto &p : paths) {
        PIDLIST_ABSOLUTE pidlFull = ILCreateFromPathW(p.c_str());
        if (!pidlFull) {
          allOk = false;
          break;
        }
        PIDLIST_RELATIVE pidlChild = ILFindChild(pidlFolder, pidlFull);
        if (!pidlChild) {
          ILFree(pidlFull);
          allOk = false;
          break;
        }
        childPidls.push_back(
            (PIDLIST_RELATIVE)ILClone((LPCITEMIDLIST)pidlChild));
        ILFree(pidlFull);
      }

      if (allOk && !childPidls.empty()) {
        std::vector<PCUITEMID_CHILD> pidlPtrs;
        pidlPtrs.reserve(childPidls.size());
        for (auto &c : childPidls)
          pidlPtrs.push_back((PCUITEMID_CHILD)c);
        pFolder->GetUIObjectOf(NULL, (UINT)pidlPtrs.size(), pidlPtrs.data(),
                               IID_IDataObject, NULL, (void **)&pDataObject);
      }

      for (auto &c : childPidls)
        ILFree((PIDLIST_ABSOLUTE)c);
      pFolder->Release();
    }
    pDesktop->Release();
  }
  ILFree(pidlFolder);

  // 回退到单文件
  if (!pDataObject)
    return CreateFileDataObject(paths[0]);
  return pDataObject;
}

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

      int iconSize = MScale(32);
      int textHeight = MScale(20);
      int hPadding = MScale(16);

      // 测量文件名宽度，动态计算位图宽度
      HDC hdcScreen = GetDC(NULL);
      HDC hdcMeasure = CreateCompatibleDC(hdcScreen);
      Gdiplus::Graphics gMeasure(hdcMeasure);
      Gdiplus::Font font(L"Microsoft YaHei", (Gdiplus::REAL)MScale(9),
                         Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
      Gdiplus::RectF bounds;
      gMeasure.MeasureString(fileName.c_str(), -1, &font, Gdiplus::PointF(0, 0),
                             &bounds);
      DeleteDC(hdcMeasure);

      int textWidth = (int)(bounds.Width + 0.5f) + hPadding * 2;
      int minWidth = iconSize + hPadding * 2;
      int maxWidth = MScale(320);
      int bmpWidth = textWidth;
      if (bmpWidth < minWidth)
        bmpWidth = minWidth;
      if (bmpWidth > maxWidth)
        bmpWidth = maxWidth;
      int bmpHeight = iconSize + textHeight + MScale(4);

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
      CreateRoundRectPath(&bgPath, 0, 0, bmpWidth, bmpHeight, MScale(8));
      Gdiplus::SolidBrush bgBrush(Gdiplus::Color(240, 255, 255, 255));
      g.FillPath(&bgBrush, &bgPath);

      // 边框
      Gdiplus::Pen borderPen(Gdiplus::Color(60, 0, 0, 0), 1.0f);
      g.DrawPath(&borderPen, &bgPath);

      // 图标
      int iconX = (bmpWidth - iconSize) / 2;
      DrawIconEx(hdcMem, iconX, MScale(2), sfi.hIcon, iconSize, iconSize, 0,
                 NULL, DI_NORMAL);

      // 文件名
      Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 50, 50, 50));
      Gdiplus::StringFormat sf;
      sf.SetAlignment(Gdiplus::StringAlignmentCenter);
      sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
      sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
      Gdiplus::RectF textRect(
          (Gdiplus::REAL)MScale(2), (Gdiplus::REAL)(iconSize + MScale(2)),
          (Gdiplus::REAL)(bmpWidth - MScale(4)), (Gdiplus::REAL)textHeight);
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
// 最近一次键盘选中(↑/↓/j/k/Home/End 等)的时间戳。
// 悬浮选中在快捷键后短时间内不抢占，避免鼠标微动覆盖键盘选中。
DWORD g_lastKeyboardSelectTick = 0;
DWORD g_lastSearchInputTick = 0; // 搜索框最近输入时刻（空格预览用）
WNDPROC g_oldFilterFavoriteProc = NULL;
HWND g_hwndMainTooltip = NULL;
bool g_isFavoriteTooltipVisible = false;

// 按钮图片句柄
Gdiplus::Image *g_imgTopmostSelected = NULL;
Gdiplus::Image *g_imgTopmostUnselected = NULL;
Gdiplus::Image *g_imgNoExistIcon = NULL; // 文件不存在图标
Gdiplus::Image *g_imgTextIcon = NULL;    // 文本类型图标
Gdiplus::Image *g_imgNetIcon = NULL;     // 网址类型图标
Gdiplus::Image *g_imgMailIcon = NULL;    // 邮箱类型图标
Gdiplus::Image *g_imgFileIcon = NULL;    // 文件图标（file.png，蒙版用）

// 文件图标缓存：按扩展名缓存系统图标（与资源管理器一致），
// 避免 WM_DRAWITEM 每次都调用 SHGetFileInfoW（该 API 较慢）。
static std::map<std::wstring, HICON> g_fileIconCache;
static HICON g_defaultFileIcon = NULL;   // 默认文件图标（无扩展名或获取失败时）
static HICON g_defaultFolderIcon = NULL; // 默认文件夹图标（系统资源管理器图标）

// 获取文件扩展名（小写，含点号），如 L".txt"
static std::wstring GetFileExtensionLower(const std::wstring &path) {
  size_t dotPos = path.find_last_of(L'.');
  if (dotPos == std::wstring::npos)
    return L"";
  size_t sepPos = path.find_last_of(L"\\/");
  if (sepPos != std::wstring::npos && dotPos < sepPos)
    return L""; // 点号在路径分隔符之前，不是扩展名
  std::wstring ext = path.substr(dotPos);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
  return ext;
}

// 按扩展名获取系统文件图标（与资源管理器一致）。使用 SHGFI_USEFILEATTRIBUTES
// 使其不需要文件实际存在，仅根据扩展名获取图标，性能好且可缓存。
static HICON GetCachedFileIcon(const std::wstring &filePath) {
  std::wstring ext = GetFileExtensionLower(filePath);
  auto it = g_fileIconCache.find(ext);
  if (it != g_fileIconCache.end())
    return it->second;

  SHFILEINFOW sfi = {};
  // 使用小图标（16px）作为源图，绘制到 16px 目标尺寸时 1:1 无缩放，最清晰。
  DWORD flags = SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;
  SHGetFileInfoW(filePath.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                 flags);
  if (sfi.hIcon) {
    g_fileIconCache[ext] = sfi.hIcon;
    return sfi.hIcon;
  }
  // 获取失败：返回默认文件图标
  if (!g_defaultFileIcon) {
    SHGetFileInfoW(L"file", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                   SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    g_defaultFileIcon = sfi.hIcon;
  }
  g_fileIconCache[ext] = g_defaultFileIcon;
  return g_defaultFileIcon;
}

// 获取系统文件夹图标（与资源管理器一致）。文件夹图标全局相同，
// 仅需获取一次并缓存。
static HICON GetCachedFolderIcon(const std::wstring &folderPath) {
  if (g_defaultFolderIcon)
    return g_defaultFolderIcon;
  SHFILEINFOW sfi = {};
  DWORD attrs = FILE_ATTRIBUTE_DIRECTORY;
  // 优先用真实路径（可获得该文件夹自定义图标），失败回退到目录特性
  UINT flags = SHGFI_ICON | SHGFI_SMALLICON;
  if (!folderPath.empty())
    SHGetFileInfoW(folderPath.c_str(), attrs, &sfi, sizeof(sfi), flags);
  if (!sfi.hIcon) {
    // 回退：SHGFI_USEFILEATTRIBUTES + 空路径 + FILE_ATTRIBUTE_DIRECTORY
    // 获取系统默认文件夹图标（与资源管理器一致），避免回退到 C:
    // 根目录得到盘符图标
    SHGetFileInfoW(L"", attrs, &sfi, sizeof(sfi),
                   flags | SHGFI_USEFILEATTRIBUTES);
  }
  if (sfi.hIcon) {
    g_defaultFolderIcon = sfi.hIcon;
    return g_defaultFolderIcon;
  }
  return NULL;
}

// 释放文件图标缓存（程序退出时调用）
static void FreeFileIconCache() {
  for (auto &pair : g_fileIconCache) {
    if (pair.second && pair.second != g_defaultFileIcon)
      DestroyIcon(pair.second);
  }
  g_fileIconCache.clear();
  if (g_defaultFileIcon) {
    DestroyIcon(g_defaultFileIcon);
    g_defaultFileIcon = NULL;
  }
  if (g_defaultFolderIcon) {
    DestroyIcon(g_defaultFolderIcon);
    g_defaultFolderIcon = NULL;
  }
}

// 置顶按钮波浪动画
#define ID_TOPMOST_ANIM_TIMER 201
#define ID_BATCH_EDIT_ANIM_TIMER 202
#define ID_FOLDER_UNDERLINE_TIMER 203
#define ID_DELETE_SLIDE_TIMER 204
#define ID_CLIPBOARD_DEBOUNCE_TIMER 205 // 剪贴板文本防抖定时器（合并多步写入）

// 剪贴板文本防抖：VSCode 等应用复制 markdown 时会分多步写入剪贴板
// （先写原始文本，再写带 md 格式的文本）。300ms 内的多次写入合并为一条记录，
// 优先保留带 markdown 格式的内容。
static std::wstring g_pendingClipboardText;
static bool g_clipboardTextPending = false;

// 检测文本是否包含 markdown 格式标记
static bool HasMarkdownFormatting(const std::wstring &text) {
  if (text.empty())
    return false;
  // 代码块
  if (text.find(L"```") != std::wstring::npos)
    return true;
  // 粗体/斜体
  if (text.find(L"**") != std::wstring::npos)
    return true;
  if (text.find(L"__") != std::wstring::npos)
    return true;
  // 行首标题 # / ## / ###
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == L'#' && (i == 0 || text[i - 1] == L'\n')) {
      // 确认是标题：# 后面跟空格或更多 #
      if (i + 1 < text.size() && (text[i + 1] == L' ' || text[i + 1] == L'#'))
        return true;
    }
  }
  // 链接 [text](url)
  if (text.find(L"](") != std::wstring::npos &&
      text.find(L"[") != std::wstring::npos)
    return true;
  // 任务列表 - [ ] / - [x]
  if (text.find(L"- [") != std::wstring::npos)
    return true;
  return false;
}

// 剥离 markdown 行内标记：图片/链接/代码/粗体/斜体/删除线
static std::wstring StripInlineMarkdown(const std::wstring &s) {
  std::wstring out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    wchar_t c = s[i];

    // 行内代码 `code`
    if (c == L'`') {
      size_t end = s.find(L'`', i + 1);
      if (end != std::wstring::npos) {
        out += s.substr(i + 1, end - i - 1);
        i = end + 1;
        continue;
      }
    }
    // 图片 ![alt](url) -> alt
    if (c == L'!' && i + 1 < s.size() && s[i + 1] == L'[') {
      size_t cb = s.find(L']', i + 2);
      if (cb != std::wstring::npos && cb + 1 < s.size() && s[cb + 1] == L'(') {
        size_t rp = s.find(L')', cb + 2);
        if (rp != std::wstring::npos) {
          out += s.substr(i + 2, cb - (i + 2));
          i = rp + 1;
          continue;
        }
      }
    }
    // 链接 [text](url) -> text
    if (c == L'[') {
      size_t cb = s.find(L']', i + 1);
      if (cb != std::wstring::npos && cb + 1 < s.size() && s[cb + 1] == L'(') {
        size_t rp = s.find(L')', cb + 2);
        if (rp != std::wstring::npos) {
          out += s.substr(i + 1, cb - (i + 1));
          i = rp + 1;
          continue;
        }
      }
    }
    // 删除线 ~~text~~
    if (c == L'~' && i + 1 < s.size() && s[i + 1] == L'~') {
      size_t end = s.find(L"~~", i + 2);
      if (end != std::wstring::npos) {
        out += s.substr(i + 2, end - (i + 2));
        i = end + 2;
        continue;
      }
    }
    // 粗体 **text** 或 __text__
    if ((c == L'*' && i + 1 < s.size() && s[i + 1] == L'*') ||
        (c == L'_' && i + 1 < s.size() && s[i + 1] == L'_')) {
      std::wstring delim(2, c);
      size_t end = s.find(delim, i + 2);
      if (end != std::wstring::npos) {
        out += s.substr(i + 2, end - (i + 2));
        i = end + 2;
        continue;
      }
    }
    // 斜体 *text* 或 _text_：要求紧邻非空白字符，避免误伤 "5 * 3"
    if ((c == L'*' || c == L'_') && i + 1 < s.size() && s[i + 1] != L' ' &&
        s[i + 1] != L'\t') {
      size_t end = s.find(c, i + 1);
      if (end != std::wstring::npos && end > i + 1 && end < s.size() &&
          s[end - 1] != L' ' && s[end - 1] != L'\t') {
        out += s.substr(i + 1, end - (i + 1));
        i = end + 1;
        continue;
      }
    }

    out += c;
    ++i;
  }
  return out;
}

// 剥离 markdown 语法标记，返回纯文本（仅剥离语法，不渲染富文本）。
// 用于 md 双记录：原 md 入一条，剥离后纯文本入另一条。
static std::wstring StripMarkdownToPlainText(const std::wstring &md) {
  std::wstring result;
  result.reserve(md.size());
  bool inFence = false;
  size_t lineStart = 0;
  while (lineStart <= md.size()) {
    size_t nl = md.find(L'\n', lineStart);
    std::wstring line = (nl == std::wstring::npos)
                            ? md.substr(lineStart)
                            : md.substr(lineStart, nl - lineStart);
    if (!line.empty() && line.back() == L'\r')
      line.pop_back();

    size_t lead = line.find_first_not_of(L" \t");
    std::wstring head = (lead == std::wstring::npos) ? L"" : line.substr(lead);
    std::wstring body = head;
    bool dropLine = false;

    // 代码围栏 ``` / ~~~
    if (head.size() >= 3 &&
        (head.compare(0, 3, L"```") == 0 || head.compare(0, 3, L"~~~") == 0)) {
      inFence = !inFence;
      dropLine = true; // 围栏行本身不保留
    } else if (inFence) {
      body = line; // 围栏内原样保留
    } else {
      // 标题 # / ## ...
      if (!body.empty() && body[0] == L'#') {
        size_t k = 0;
        while (k < body.size() && body[k] == L'#')
          ++k;
        if (k <= 6 && k < body.size() && (body[k] == L' ' || body[k] == L'\t'))
          body = body.substr(k + 1);
      }
      // 引用 >
      if (!body.empty() && body[0] == L'>') {
        size_t k = 0;
        while (k < body.size() && (body[k] == L'>' || body[k] == L' '))
          ++k;
        body = body.substr(k);
      }
      // 任务列表 - [ ] / - [x]
      if (body.size() >= 3 &&
          (body[0] == L'-' || body[0] == L'*' || body[0] == L'+') &&
          body[1] == L' ' && body[2] == L'[') {
        size_t cb = body.find(L']', 2);
        if (cb != std::wstring::npos && cb + 1 < body.size() &&
            body[cb + 1] == L' ')
          body = body.substr(cb + 2);
      }
      // 无序列表 - / * / +
      else if (body.size() >= 2 &&
               (body[0] == L'-' || body[0] == L'*' || body[0] == L'+') &&
               body[1] == L' ') {
        body = body.substr(2);
      }
      // 有序列表 1.
      else {
        size_t k = 0;
        while (k < body.size() && body[k] >= L'0' && body[k] <= L'9')
          ++k;
        if (k > 0 && k + 1 < body.size() && body[k] == L'.' &&
            body[k + 1] == L' ')
          body = body.substr(k + 2);
      }
      // 水平线
      if (body == L"---" || body == L"***" || body == L"___" ||
          body == L"- - -")
        dropLine = true;

      body = StripInlineMarkdown(body);
    }

    std::wstring out =
        dropLine
            ? std::wstring()
            : std::wstring(lead == std::wstring::npos ? 0 : lead, L' ') + body;
    if (!result.empty())
      result += L"\n";
    result += out;

    if (nl == std::wstring::npos)
      break;
    lineStart = nl + 1;
  }
  return result;
}

// 立即提交挂起的剪贴板文本（用于非文本格式到来时强制刷新）
static void FlushPendingClipboardText() {
  if (g_clipboardTextPending) {
    g_clipboardTextPending = false;
    if (!g_pendingClipboardText.empty()) {
      const std::wstring &text = g_pendingClipboardText;
      // md 双记录：复制 md 内容时，先生成剥离 md 的纯文本（在下），
      // 再生成 md 原文（在上），便于用户按场景粘贴。
      if (HasMarkdownFormatting(text)) {
        std::wstring plain = StripMarkdownToPlainText(text);
        if (plain != text && !plain.empty()) {
          AddToHistory(plain); // 先入：旧、在下
          AddToHistory(text);  // 后入：新、在上（带 md 格式）
        } else {
          AddToHistory(text);
        }
      } else {
        AddToHistory(text);
      }
    }
    g_pendingClipboardText.clear();
  }
}

// 调度防抖文本捕获：300ms 内多次写入合并为一条，优先保留 markdown 格式
static void ScheduleDebouncedTextCapture(HWND hwnd,
                                         const std::wstring &content) {
  if (g_clipboardTextPending) {
    bool oldMd = HasMarkdownFormatting(g_pendingClipboardText);
    bool newMd = HasMarkdownFormatting(content);
    if (newMd && !oldMd) {
      // 新内容带 md 格式，旧的不带：替换为新的
      g_pendingClipboardText = content;
    } else if (!newMd && oldMd) {
      // 旧内容带 md 格式，新的不带：保留旧的
    } else if (content.size() > g_pendingClipboardText.size()) {
      // 都带或都不带 md 格式：保留更长的
      g_pendingClipboardText = content;
    }
  } else {
    g_pendingClipboardText = content;
    g_clipboardTextPending = true;
  }
  // 重置 300ms 定时器（SetTimer 同 ID 会重置）
  SetTimer(hwnd, ID_CLIPBOARD_DEBOUNCE_TIMER, 300, NULL);
}
float g_topmostAnimProgress = 0.0f; // 动画进度 0.0-1.0
bool g_topmostAnimating = false;    // 是否正在动画
bool g_topmostAnimDirection = true; // true=选中动画, false=取消选中动画
float g_batchEditAnimProgress = 0.0f;
bool g_batchEditAnimating = false;
bool g_batchEditAnimDirection = true;

// 删除滑出动画
bool g_deleteSlideAnimating = false;
int g_deleteSlideDisplayIndex = -1;
int g_deleteSlideOffset = 0;
int g_deleteSlideTargetWidth = 0;
int g_deleteSlideActualIndex = -1;

bool g_caretVisible = false;
float g_caretGradientPos = 0.0f;
int g_caretBlinkCounter = 0;  // 闪烁计数器
bool g_caretShowState = true; // 光标显示状态

int g_scrollbarDragOffsetY = 0;

// 多文件记录展开态拖拽的子文件行号（-1 = 非子行拖拽）
int g_dragSubItemIndex = -1;

// 列表框顶部索引缓存（用于快捷键提示，避免频繁调用LB_GETTOPINDEX）
int g_listBoxTopIndex = 0;
bool g_quickPasteHintVisible = false;

// 当前绘制帧的快捷键缓存，每帧只计算一次，避免每绘制一项都 O(n) 扫描
static int g_cachedShortcutIds[10] = {};
static int g_cachedShortcutCount = 0;
static bool g_shortcutCacheDirty = true;

// 列表项绘制用字体缓存，避免每次 WM_DRAWITEM 都 CreateFontW
static HFONT g_hListMainFont = NULL;     // 20px Microsoft YaHei
static HFONT g_hListHeaderFont = NULL;   // 16px Microsoft YaHei
static HFONT g_hListTagFont = NULL;      // 14px Microsoft YaHei
static HFONT g_hListSizeFont = NULL;     // 16px Microsoft YaHei（尺寸信息）
static HFONT g_hListTextFont = NULL;     // 16px g_fontName（图片文本）
static std::wstring g_hListTextFontName; // 用于检测字体名变化时重建
static UINT g_hListMainFontDpi = 0;
static UINT g_hListHeaderFontDpi = 0;
static UINT g_hListTagFontDpi = 0;
static UINT g_hListSizeFontDpi = 0;
static UINT g_hListTextFontDpi = 0;

static HFONT GetListMainFont() {
  if (g_hListMainFont && g_hListMainFontDpi != g_mainUiDpi) {
    DeleteObject(g_hListMainFont);
    g_hListMainFont = NULL;
  }
  if (!g_hListMainFont) {
    g_hListMainFont = CreateFontW(
        MScale(20), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hListMainFontDpi = g_mainUiDpi;
  }
  return g_hListMainFont;
}
static HFONT GetListHeaderFont() {
  if (g_hListHeaderFont && g_hListHeaderFontDpi != g_mainUiDpi) {
    DeleteObject(g_hListHeaderFont);
    g_hListHeaderFont = NULL;
  }
  if (!g_hListHeaderFont) {
    g_hListHeaderFont = CreateFontW(
        MScale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hListHeaderFontDpi = g_mainUiDpi;
  }
  return g_hListHeaderFont;
}
static HFONT GetListTagFont() {
  if (g_hListTagFont && g_hListTagFontDpi != g_mainUiDpi) {
    DeleteObject(g_hListTagFont);
    g_hListTagFont = NULL;
  }
  if (!g_hListTagFont) {
    g_hListTagFont = CreateFontW(
        MScale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hListTagFontDpi = g_mainUiDpi;
  }
  return g_hListTagFont;
}
static HFONT GetListSizeFont() {
  if (g_hListSizeFont && g_hListSizeFontDpi != g_mainUiDpi) {
    DeleteObject(g_hListSizeFont);
    g_hListSizeFont = NULL;
  }
  if (!g_hListSizeFont) {
    g_hListSizeFont = CreateFontW(
        MScale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hListSizeFontDpi = g_mainUiDpi;
  }
  return g_hListSizeFont;
}
static HFONT GetListTextFont() {
  if (g_hListTextFont && (g_hListTextFontName != g_fontName ||
                          g_hListTextFontDpi != g_mainUiDpi)) {
    DeleteObject(g_hListTextFont);
    g_hListTextFont = NULL;
  }
  if (!g_hListTextFont) {
    g_hListTextFont = CreateFontW(
        MScale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, g_fontName.c_str());
    g_hListTextFontName = g_fontName;
    g_hListTextFontDpi = g_mainUiDpi;
  }
  return g_hListTextFont;
}

// 检测文本是否为 IPv4 地址（可选端口号，如 192.168.1.1 或 192.168.1.1:8080）
static bool IsIPv4Address(const std::wstring &text) {
  std::wstring s = text;
  // 去除首尾空白
  while (!s.empty() && (s.front() == L' ' || s.front() == L'\t' ||
                        s.front() == L'\r' || s.front() == L'\n'))
    s.erase(s.begin());
  while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' ||
                        s.back() == L'\r' || s.back() == L'\n'))
    s.pop_back();
  if (s.empty())
    return false;

  // 分离端口号
  std::wstring ipPart = s;
  size_t colonPos = s.rfind(L':');
  if (colonPos != std::wstring::npos) {
    std::wstring portStr = s.substr(colonPos + 1);
    if (!portStr.empty()) {
      bool allDigits = true;
      for (size_t i = 0; i < portStr.size(); ++i) {
        if (portStr[i] < L'0' || portStr[i] > L'9') {
          allDigits = false;
          break;
        }
      }
      if (allDigits) {
        int port = _wtoi(portStr.c_str());
        if (port < 1 || port > 65535)
          return false;
        ipPart = s.substr(0, colonPos);
      }
    }
  }

  // 解析 X.X.X.X
  int partCount = 0;
  size_t pos = 0;
  while (pos < ipPart.size() && partCount < 4) {
    if (ipPart[pos] < L'0' || ipPart[pos] > L'9')
      return false;
    int num = 0;
    int digits = 0;
    while (pos < ipPart.size() && ipPart[pos] >= L'0' && ipPart[pos] <= L'9') {
      num = num * 10 + (ipPart[pos] - L'0');
      ++digits;
      ++pos;
      if (num > 255)
        return false;
    }
    if (digits == 0 || digits > 3)
      return false;
    ++partCount;
    if (partCount < 4) {
      if (pos >= ipPart.size() || ipPart[pos] != L'.')
        return false;
      ++pos;
    }
  }

  return partCount == 4 && pos == ipPart.size();
}

// 检测文本是否为网址（http://、https://、www. 开头）
static bool IsUrl(const std::wstring &text) {
  std::wstring s = text;
  // 去除首尾空白
  while (!s.empty() && (s.front() == L' ' || s.front() == L'\t' ||
                        s.front() == L'\r' || s.front() == L'\n'))
    s.erase(s.begin());
  while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' ||
                        s.back() == L'\r' || s.back() == L'\n'))
    s.pop_back();
  if (s.empty())
    return false;

  // 不区分大小写比较
  if (_wcsnicmp(s.c_str(), L"http://", 7) == 0 && s.size() > 7)
    return true;
  if (_wcsnicmp(s.c_str(), L"https://", 8) == 0 && s.size() > 8)
    return true;
  // www. 开头且后面至少有一个点
  if (_wcsnicmp(s.c_str(), L"www.", 4) == 0) {
    size_t nextDot = s.find(L'.', 4);
    if (nextDot != std::wstring::npos && nextDot < s.size() - 1)
      return true;
  }
  return false;
}

// 计算文件/文件夹/网址等"可点击文字"的实际命中区域（只覆盖实际文字宽度，
// 不延伸到整行）。几何参数必须与 WM_DRAWITEM 绘制端一致：
//   iconOffset: 文字左侧图标占位（MScale 前整数）
//               folder/file 图标=20，net/mail/text 图标=22，无图标=0
//   rightInset: 文字右侧预留（MScale 前整数）
//               文件夹绘制时 rcPathText.right -= MScale(20)，故=20；其余=0
static RECT CalcFolderTextRect(HWND hwnd, const RECT &rcItem,
                               const std::wstring &text, int iconOffset,
                               int rightInset) {
  RECT rcText;
  rcText.top = rcItem.top + MScale(2) + MScale(20);
  rcText.bottom = rcText.top + MScale(22);
  // 内容区域基准（与 WM_DRAWITEM 中 rcContent 一致）
  int contentLeft = rcItem.left + MScale(10);
  int contentRight =
      rcItem.right - MScale(6) - GetCustomScrollbarReservedWidth();
  rcText.left = contentLeft + MScale(iconOffset);
  int maxRight = contentRight - MScale(rightInset);
  rcText.right = maxRight;
  // 测量实际文字宽度，命中区域不延伸到整行
  HDC hdc = GetDC(hwnd);
  if (hdc) {
    // 与绘制端一致使用 GetListMainFont(20px)，避免 16px 测量宽度偏小
    HFONT oldFont = (HFONT)SelectObject(hdc, GetListMainFont());
    SIZE textSize = {0, 0};
    std::wstring measureText = text;
    size_t nlPos = measureText.find(L'\n');
    if (nlPos != std::wstring::npos)
      measureText = measureText.substr(0, nlPos);
    if (GetTextExtentPoint32W(hdc, measureText.c_str(),
                              (int)measureText.length(), &textSize)) {
      int textRight = rcText.left + textSize.cx;
      if (textRight < maxRight)
        rcText.right = textRight;
    }
    SelectObject(hdc, oldFont);
    ReleaseDC(hwnd, hdc);
  }
  return rcText;
}

// 计算文件/文件夹/网址悬浮命中区域使用的文本与几何参数。
// 关键：TYPE_FILE 单路径在列表中只显示"文件名"（history.cpp
// 提取最后分隔符后部分），
//   绘制端从 LB_GETTEXT 取该文件名绘制。若用 item.content(完整路径) 测量宽度，
//   命中区域会远大于可见文字（几乎整行）。故此处必须用与绘制端一致的显示文本。
struct HoverTextGeom {
  std::wstring text;
  int iconOffset;
  int rightInset;
};
static HoverTextGeom GetHoverTextGeom(const ClipboardItem &item, bool isFolder,
                                      bool isUrlOrIp) {
  HoverTextGeom g;
  if (isUrlOrIp) {
    // 网址/IP：显示完整内容，net/mail/text 图标占 22
    g.text = item.content;
    g.iconOffset = 22;
    g.rightInset = 0;
  } else if (item.type == TYPE_FILE) {
    // 文件/文件夹：列表显示文件名（取最后分隔符后部分，与绘制端一致）
    std::wstring name = item.content;
    size_t nlPos = name.find(L'\n');
    if (nlPos == std::wstring::npos) {
      size_t lastSep = name.find_last_of(L"\\/");
      if (lastSep != std::wstring::npos)
        name = name.substr(lastSep + 1);
      g.text = name;
    } else {
      // 多文件记录（兜底，实际不触发文件悬浮）
      g.text = item.content;
    }
    g.iconOffset = 20; // 文件/文件夹图标占 20
    g.rightInset = isFolder ? 20 : 0;
  } else {
    // TYPE_TEXT 文件路径：显示完整路径
    // 文件夹走 folder 绘制分支(图标20+右侧20)；非文件夹文件路径无图标
    g.text = item.content;
    g.iconOffset = isFolder ? 20 : 0;
    g.rightInset = isFolder ? 20 : 0;
  }
  return g;
}
static void CleanupListFonts() {
  if (g_hListMainFont) {
    DeleteObject(g_hListMainFont);
    g_hListMainFont = NULL;
  }
  if (g_hListHeaderFont) {
    DeleteObject(g_hListHeaderFont);
    g_hListHeaderFont = NULL;
  }
  if (g_hListTagFont) {
    DeleteObject(g_hListTagFont);
    g_hListTagFont = NULL;
  }
  if (g_hListSizeFont) {
    DeleteObject(g_hListSizeFont);
    g_hListSizeFont = NULL;
  }
  if (g_hListTextFont) {
    DeleteObject(g_hListTextFont);
    g_hListTextFont = NULL;
  }
  g_hListTextFontName.clear();
  g_hListMainFontDpi = 0;
  g_hListHeaderFontDpi = 0;
  g_hListTagFontDpi = 0;
  g_hListSizeFontDpi = 0;
  g_hListTextFontDpi = 0;
}

// 各标签页的滚动位置记忆（0=全部, 1=文本, 2=图片, 3=文件, 4=收藏）
int g_tabTopIndex[5] = {0, 0, 0, 0, 0};
// 各标签页的选中索引记忆（0=全部, 1=文本, 2=图片, 3=文件, 4=收藏）
int g_tabSelIndex[5] = {-1, -1, -1, -1, -1};

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

enum FavoriteHotkeyId {
  ID_HOTKEY_FAVORITE_1 = 4201,
  ID_HOTKEY_FAVORITE_2,
  ID_HOTKEY_FAVORITE_3,
  ID_HOTKEY_FAVORITE_4,
  ID_HOTKEY_FAVORITE_5,
  ID_HOTKEY_FAVORITE_6,
  ID_HOTKEY_FAVORITE_7,
  ID_HOTKEY_FAVORITE_8,
  ID_HOTKEY_FAVORITE_9
};

bool g_isFavoriteHotkeyEnabled = true;
UINT g_favoriteHotkeyModifiers = MOD_CONTROL | MOD_ALT;

void RegisterFavoriteHotkeys(HWND hwnd) {
  for (int i = 0; i < 9; ++i) {
    UINT vk = (UINT)('1' + i);
    if (!RegisterHotKey(hwnd, ID_HOTKEY_FAVORITE_1 + i,
                        g_favoriteHotkeyModifiers, vk)) {
      UnregisterHotKey(hwnd, ID_HOTKEY_FAVORITE_1 + i);
      RegisterHotKey(hwnd, ID_HOTKEY_FAVORITE_1 + i, g_favoriteHotkeyModifiers,
                     vk);
    }
  }
}

void UnregisterFavoriteHotkeys(HWND hwnd) {
  for (int i = 0; i < 9; ++i) {
    ::UnregisterHotKey(hwnd, ID_HOTKEY_FAVORITE_1 + i);
  }
}

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

void ResetShortcutAssignment() { g_shortcutCacheDirty = true; }

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
    if (!RegisterHotKey(hwnd, ID_HOTKEY_PASTE_1 + i, g_quickPasteModifiers,
                        vk)) {
      // 注册失败时尝试注销后重新注册
      UnregisterHotKey(hwnd, ID_HOTKEY_PASTE_1 + i);
      RegisterHotKey(hwnd, ID_HOTKEY_PASTE_1 + i, g_quickPasteModifiers, vk);
    }
  }
}

void UnregisterQuickPasteHotkeys(HWND hwnd) {
  for (int i = 0; i < 10; ++i) {
    ::UnregisterHotKey(hwnd, ID_HOTKEY_PASTE_1 + i);
  }
}

// 注册本 app 所有快捷键（受 g_allHotkeysEnabled 总开关和各独立开关控制）
void RegisterAllHotkeys(HWND hwnd) {
  if (!g_allHotkeysEnabled)
    return;
  if (g_isHotkeyEnabled)
    RegisterHotkey(hwnd);
  if (g_isQuickPasteEnabled)
    RegisterQuickPasteHotkeys(hwnd);
  if (g_isFavoriteHotkeyEnabled)
    RegisterFavoriteHotkeys(hwnd);
  // 搜索快捷键非全局热键，通过 WM_KEYDOWN 中检查 g_isSearchHotkeyEnabled
  // && g_allHotkeysEnabled 生效，无需在此注册
}

// 注销本 app 所有全局快捷键
void UnregisterAllHotkeys(HWND hwnd) {
  UnregisterHotkey(hwnd);
  UnregisterQuickPasteHotkeys(hwnd);
  UnregisterFavoriteHotkeys(hwnd);
}

static int CollectVisibleShortcutDisplayIndices(int *outIds, int maxCount);
static int GetShortcutIndexForDisplayIndex(int displayIndex) {
  if (g_shortcutCacheDirty) {
    g_cachedShortcutCount =
        CollectVisibleShortcutDisplayIndices(g_cachedShortcutIds, 10);
    g_shortcutCacheDirty = false;
  }
  for (int i = 0; i < g_cachedShortcutCount; ++i) {
    if (g_cachedShortcutIds[i] == displayIndex)
      return i;
  }
  return -1;
}

static int CollectVisibleShortcutDisplayIndices(int *outIds, int maxCount) {
  if (!outIds || maxCount <= 0 || g_displayIndexMap.empty())
    return 0;

  int visibleLimit = CalculateVisibleItemCount(g_listBoxTopIndex);
  int totalItems = (int)g_displayIndexMap.size();
  int count = 0;
  const int headerVisibleThreshold = 9;
  int visibleHeight =
      g_hwndListBox ? GetListBoxVisibleHeight(g_hwndListBox) : 0;

  for (int i = g_listBoxTopIndex; i < totalItems && count < maxCount; ++i) {
    RECT rcItem = {};
    if (g_hwndListBox && SendMessageW(g_hwndListBox, LB_GETITEMRECT, i,
                                      (LPARAM)&rcItem) != LB_ERR) {
      if (rcItem.bottom <= 0)
        continue;
      if (rcItem.top >= 0 && count == 0) {
      } else if (rcItem.top + headerVisibleThreshold <= 0) {
        continue;
      }
      if (visibleHeight > 0 && rcItem.top >= visibleHeight)
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

static bool AreModifierKeysDown() {
  const int keys[] = {VK_LMENU,    VK_RMENU,  VK_LCONTROL,
                      VK_RCONTROL, VK_LSHIFT, VK_RSHIFT};
  for (int key : keys) {
    if ((GetAsyncKeyState(key) & 0x8000) != 0)
      return true;
  }
  return false;
}

static void ReleaseAllModifierKeys() {
  const int keys[] = {VK_LMENU,    VK_RMENU,  VK_LCONTROL,
                      VK_RCONTROL, VK_LSHIFT, VK_RSHIFT};
  for (int key : keys) {
    if ((GetAsyncKeyState(key) & 0x8000) != 0)
      keybd_event((BYTE)key, 0, KEYEVENTF_KEYUP, 0);
  }
}

static void RestoreForegroundWindow(HWND target) {
  if (!target || !IsWindow(target))
    return;
  DWORD targetThread = GetWindowThreadProcessId(target, NULL);
  DWORD currentThread = GetCurrentThreadId();
  bool needAttach = (targetThread != currentThread);
  if (needAttach)
    AttachThreadInput(currentThread, targetThread, TRUE);
  SetForegroundWindow(target);
  if (g_previousFocusWindow != NULL && IsWindow(g_previousFocusWindow)) {
    SetFocus(g_previousFocusWindow);
  }
  if (needAttach)
    AttachThreadInput(currentThread, targetThread, FALSE);
}

static void RememberPasteTarget(HWND mainWindow) {
  HWND foreground = GetForegroundWindow();
  if (!foreground || foreground == mainWindow)
    return;

  g_previousActiveWindow = foreground;
  g_previousFocusWindow = NULL;
  DWORD targetThread = GetWindowThreadProcessId(foreground, NULL);
  DWORD currentThread = GetCurrentThreadId();
  bool needAttach = targetThread != currentThread;
  if (needAttach)
    AttachThreadInput(currentThread, targetThread, TRUE);
  g_previousFocusWindow = GetFocus();
  if (needAttach)
    AttachThreadInput(currentThread, targetThread, FALSE);
}

static void SendCtrlVInput() {
  INPUT inputs[4] = {};
  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = VK_CONTROL;
  inputs[1].type = INPUT_KEYBOARD;
  inputs[1].ki.wVk = 'V';
  inputs[2].type = INPUT_KEYBOARD;
  inputs[2].ki.wVk = 'V';
  inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
  inputs[3].type = INPUT_KEYBOARD;
  inputs[3].ki.wVk = VK_CONTROL;
  inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(_countof(inputs), inputs, sizeof(INPUT));
}

static void RestoreFocusAndPaste(HWND hwnd, bool simulatePaste = true,
                                 bool waitForModifierRelease = false,
                                 bool restoreMainWindow = false) {
  CloseTagPopup();
  g_deferredPasteSimulate = simulatePaste;
  g_deferredPasteWaitForModifierRelease = waitForModifierRelease;
  g_restoreMainWindowAfterPaste = restoreMainWindow;
  g_deferredPasteKeepsTopmostVisible = g_isTopmost && restoreMainWindow;
  if (!g_deferredPasteKeepsTopmostVisible)
    ShowWindow(hwnd, SW_HIDE);
  KillTimer(hwnd, ID_RESTORE_TOPMOST_AFTER_PASTE);
  KillTimer(hwnd, ID_SEND_DEFERRED_PASTE);
  SetTimer(hwnd, ID_RESTORE_FOCUS_FOR_PASTE, 35, NULL);
}

static bool SetClipboardFromItem(const ClipboardItem &item) {
  if (!OpenClipboard(NULL))
    return false;

  EmptyClipboard();

  if (item.type == TYPE_TEXT) {
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
  } else if (item.type == TYPE_FILE) {
    size_t nlPos = item.content.find(L'\n');
    if (nlPos != std::wstring::npos) {
      std::vector<std::wstring> paths;
      size_t start = 0;
      while (start <= item.content.size()) {
        size_t end = item.content.find(L'\n', start);
        if (end == std::wstring::npos)
          end = item.content.size();
        if (end > start) {
          paths.push_back(item.content.substr(start, end - start));
        }
        if (end == item.content.size())
          break;
        start = end + 1;
      }
      size_t totalLen = 0;
      for (const auto &p : paths)
        totalLen += (p.size() + 1) * sizeof(wchar_t);
      totalLen += sizeof(wchar_t);
      HGLOBAL hGlobal =
          GlobalAlloc(GMEM_MOVEABLE, sizeof(DROPFILES) + totalLen);
      if (hGlobal != NULL) {
        DROPFILES *pDrop = (DROPFILES *)GlobalLock(hGlobal);
        if (pDrop != NULL) {
          pDrop->pFiles = sizeof(DROPFILES);
          pDrop->pt.x = 0;
          pDrop->pt.y = 0;
          pDrop->fNC = FALSE;
          pDrop->fWide = TRUE;
          wchar_t *pStr = (wchar_t *)((BYTE *)pDrop + sizeof(DROPFILES));
          for (size_t i = 0; i < paths.size(); ++i) {
            wcscpy_s(pStr, paths[i].size() + 1, paths[i].c_str());
            pStr += paths[i].size() + 1;
          }
          *pStr = L'\0';
          GlobalUnlock(hGlobal);
          SetClipboardData(CF_HDROP, hGlobal);
        }
      }
    } else {
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
  if (g_hwndMain)
    SetTimer(g_hwndMain, 1, 100, NULL);
  return true;
}

static bool PasteHistoryItemByActualIndex(HWND hwnd, int actualIndex) {
  if (actualIndex < 0 || actualIndex >= (int)g_history.size())
    return false;

  if (!SetClipboardFromItem(g_history[actualIndex]))
    return false;

  RememberPasteTarget(hwnd);
  bool wasVisible = IsWindowVisible(hwnd) && !IsIconic(hwnd);
  RestoreFocusAndPaste(hwnd, true, true, wasVisible);
  return true;
}

static bool PasteHistoryItemByDisplayIndex(HWND hwnd, int displayIndex) {
  if (displayIndex < 0 || displayIndex >= (int)g_displayIndexMap.size())
    return false;

  int actualIndex = g_displayIndexMap[displayIndex];
  return PasteHistoryItemByActualIndex(hwnd, actualIndex);
}

// 计算单个项目的高度（基于显示索引）
int GetItemDisplayHeight(int displayIndex) {
  if (displayIndex < 0 || displayIndex >= (int)g_displayIndexMap.size()) {
    return MScale(57); // 默认文本高度
  }

  int actualIndex = g_displayIndexMap[displayIndex];
  if (actualIndex < 0 || actualIndex >= (int)g_history.size()) {
    return MScale(57);
  }

  const ClipboardItem &item = g_history[actualIndex];

  if (item.type == TYPE_IMAGE) {
    // 检查图像尺寸是否有效
    if (item.imageWidth <= 0 || item.imageHeight <= 0) {
      return MScale(87); // 默认图像高度
    }

    // 获取列表框宽度
    RECT rcListBox;
    GetClientRect(g_hwndListBox, &rcListBox);
    int listBoxWidth = rcListBox.right - rcListBox.left - MScale(20);
    if (listBoxWidth < MScale(100))
      listBoxWidth = MScale(560);

    int availableWidth = listBoxWidth - MScale(20);
    float scale = (float)availableWidth / item.imageWidth;

    // 限制最大显示高度为150像素
    int maxImageHeight = MScale(150);
    if (scale * item.imageHeight > maxImageHeight) {
      scale = (float)maxImageHeight / item.imageHeight;
    }

    int displayHeight = (int)(item.imageHeight * scale);
    // 标题(25) + 图片高度 + 尺寸信息(20) + 底部边距(10)
    return MScale(25) + displayHeight + MScale(20) + MScale(10);
  } else if (item.type == TYPE_FILE &&
             item.content.find(L'\n') != std::wstring::npos &&
             IsMultiFileExpanded(actualIndex)) {
    // 展开的多文件记录：n 个文件各占一行
    int fileCount = GetMultiFilePathCount(item.content);
    return std::max(1, fileCount) * MScale(57);
  } else {
    // 文本或文件类型：固定高度
    return MScale(57);
  }
}

// 计算从指定索引开始，在可视区域内能完整显示的项目数
// 以 1/2 为临界点：可见部分 ≥ 1/2 计入，< 1/2 不计入（成为下一页首项）
int CalculateVisibleItemCount(int startIndex) {
  if (g_hwndListBox == NULL)
    return ITEMS_PER_PAGE;

  RECT rcListBox;
  GetClientRect(g_hwndListBox, &rcListBox);
  int visibleHeight = rcListBox.bottom - rcListBox.top;

  int count = 0;
  int totalItems = (int)g_displayIndexMap.size();

  // 使用 LB_GETITEMRECT 获取每个项目的实际矩形
  for (int i = startIndex; i < totalItems; i++) {
    RECT rcItem;
    if (SendMessageW(g_hwndListBox, LB_GETITEMRECT, i, (LPARAM)&rcItem) !=
        LB_ERR) {
      if (rcItem.top >= visibleHeight) {
        break; // 项目顶部不可见，停止计数
      }
      // 1/2 临界点：可见部分不足项目高度的一半时不计入，
      // 该项目会成为下一页的第一项并显示快捷键
      int itemHeight = rcItem.bottom - rcItem.top;
      int visiblePortion = visibleHeight - rcItem.top;
      if (itemHeight > 0 && visiblePortion < itemHeight / 2) {
        break; // 可见部分不足1/2，停止计数
      }
      count++;
    } else {
      break;
    }
  }

  return count > 0 ? count : 1; // 至少返回1
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

int GetListBoxVisibleHeight(HWND hwnd) {
  if (!hwnd)
    return 0;
  RECT rcListBox;
  GetClientRect(hwnd, &rcListBox);
  int height = (int)(rcListBox.bottom - rcListBox.top);
  return (height > 0) ? height : 0;
}

int GetTotalListContentHeight() {
  int totalHeight = 0;
  for (int i = 0; i < (int)g_displayIndexMap.size(); ++i)
    totalHeight += GetItemDisplayHeight(i);
  return totalHeight;
}

int GetContentOffsetForTopIndex(int topIndex) {
  if (topIndex <= 0)
    return 0;
  if (topIndex > (int)g_displayIndexMap.size())
    topIndex = (int)g_displayIndexMap.size();

  int offset = 0;
  for (int i = 0; i < topIndex; ++i)
    offset += GetItemDisplayHeight(i);
  return offset;
}

int GetMaxListScrollOffset(HWND hwnd) {
  int visibleHeight = GetListBoxVisibleHeight(hwnd);
  int totalHeight = GetTotalListContentHeight();
  return std::max(0, totalHeight - visibleHeight);
}

int GetTopIndexForContentOffset(int contentOffset) {
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

int GetListBoxMaxTopIndex() {
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

bool NeedsCustomScrollbar() {
  return g_isCustomScrollbarEnabled && GetListBoxMaxTopIndex() > 0;
}

static void HideNativeListBoxScrollbar(HWND hwnd);

int GetCustomScrollbarTrackWidth() {
  // 滚动条轨道宽度按主窗口 DPI 缩放，避免 4K/高 DPI 下显得过细
  return NeedsCustomScrollbar() ? MScale(12) : 0;
}

int GetCustomScrollbarReservedWidth() {
  if (!NeedsCustomScrollbar())
    return 0;
  return GetCustomScrollbarTrackWidth() + MScale(2);
}

void ApplyListBoxTopIndex(HWND hwnd, int newTop) {
  int maxTop = GetListBoxMaxTopIndex();
  if (newTop < 0)
    newTop = 0;
  if (newTop > maxTop)
    newTop = maxTop;

  g_listBoxTopIndex = newTop;
  // 注意：不要在 LB_SETTOPINDEX 之前标记 g_shortcutCacheDirty。
  // LB_SETTOPINDEX 内部可能触发同步 WM_PAINT，此时 LB_GETITEMRECT
  // 的状态可能与 g_listBoxTopIndex 不一致，导致
  // CollectVisibleShortcutDisplayIndices 收集到错误缓存并固化。
  // 同步 WM_PAINT 应使用旧缓存绘制，由后续异步 WM_PAINT（此时
  // LB 内部状态已稳定）重新收集正确缓存。
  int oldTop = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
  if (oldTop != newTop)
    SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
  SendMessageW(hwnd, LB_SETTOPINDEX, newTop, 0);
  if (oldTop != newTop)
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);

  g_listBoxTopIndex = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
  if (g_listBoxTopIndex < 0)
    g_listBoxTopIndex = 0;
  HideNativeListBoxScrollbar(hwnd);

  // LB 内部 top index 已稳定（LB_GETTOPINDEX 已返回最终值），
  // 此时标记脏 + 失效整个客户区，确保后续异步 WM_PAINT 用正确的
  // g_listBoxTopIndex 重新收集缓存并重绘所有可见项。
  g_shortcutCacheDirty = true;

  int newPage = g_listBoxTopIndex / ITEMS_PER_PAGE;
  if (newPage != g_currentPage)
    g_currentPage = newPage;

  // 失效整个客户区。ScrollWindowEx（LB_SETTOPINDEX 内部调用）会垂直
  // 移动整个客户区的像素，包括快捷键数字。但 WM_PAINT 的 ps.rcPaint
  // 只包含新暴露的区域，其他可见项的快捷键是滚动过来的旧像素，会导致
  // 快捷键重复或不是从1开始。失效整个客户区后，双缓冲 WM_PAINT 会
  // 用正确的缓存重新绘制所有可见项。
  InvalidateRect(hwnd, NULL, FALSE);

  InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
  InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
}

bool IsSelectableDisplayIndex(int index) {
  if (index < 0 || index >= (int)g_displayIndexMap.size())
    return false;
  return true;
}

int FindSelectableDisplayIndex(int startIndex, int step) {
  if (step == 0)
    return LB_ERR;
  for (int i = startIndex; i >= 0 && i < (int)g_displayIndexMap.size();
       i += step) {
    if (IsSelectableDisplayIndex(i))
      return i;
  }
  return LB_ERR;
}

int GetFirstSelectableDisplayIndex() {
  return FindSelectableDisplayIndex(0, 1);
}

int GetLastSelectableDisplayIndex() {
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

// 显示收藏筛选按钮的 tooltip 提示（再次点击可下拉分类），
// 定位在按钮下方，采用跟踪模式（TTM_TRACKPOSITION）。
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
  ti.lpszText = (LPWSTR)T(STR_TOOLTIP_FAVORITE_FILTER);

  SendMessageW(g_hwndMainTooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
  SendMessageW(g_hwndMainTooltip, TTM_TRACKPOSITION, 0,
               MAKELPARAM(rcBtn.left + 8, rcBtn.bottom + 10));
  SendMessageW(g_hwndMainTooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
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

// 退出批量编辑模式：清空选中集合并刷新批量按钮/列表 UI
static void ExitBatchMode() {
  g_isBatchEditMode = false;
  g_selectedItems.clear();
  g_batchSelectionAnchorDisplayIndex = LB_ERR;
  if (g_hwndListBox)
    InvalidateRect(g_hwndListBox, NULL, FALSE);
  if (g_hwndBatchEditBtn)
    InvalidateRect(g_hwndBatchEditBtn, NULL, TRUE);
}

// 批量模式下粘贴选中的文件（反选的自动排除）。
// 收集 g_selectedItems 中 TYPE_FILE 记录的单路径，按列表显示顺序构造 CF_HDROP。
// 粘贴成功后自动退出批量模式。返回是否粘贴成功。
static bool PasteSelectedFilesBatch(HWND hwnd) {
  if (g_selectedItems.empty() || !g_hwndListBox)
    return false;

  // 按显示顺序收集选中 TYPE_FILE 记录的路径（反选的即不在 g_selectedItems）
  std::vector<std::wstring> paths;
  for (size_t disp = 0; disp < g_displayIndexMap.size(); ++disp) {
    int actual = g_displayIndexMap[disp];
    if (actual < 0 || actual >= (int)g_history.size())
      continue;
    if (std::find(g_selectedItems.begin(), g_selectedItems.end(), actual) ==
        g_selectedItems.end())
      continue;
    const ClipboardItem &it = g_history[actual];
    if (it.type != TYPE_FILE)
      continue;
    // 新记录为单路径；旧合并记录(content 含\n)拆分兼容
    if (it.content.find(L'\n') != std::wstring::npos) {
      size_t s = 0;
      while (s <= it.content.size()) {
        size_t e = it.content.find(L'\n', s);
        if (e == std::wstring::npos)
          e = it.content.size();
        if (e > s)
          paths.push_back(it.content.substr(s, e - s));
        if (e == it.content.size())
          break;
        s = e + 1;
      }
    } else {
      paths.push_back(it.content);
    }
  }
  if (paths.empty())
    return false;

  if (!OpenClipboard(NULL))
    return false;
  EmptyClipboard();
  size_t totalLen = 0;
  for (const auto &p : paths)
    totalLen += (p.size() + 1) * sizeof(wchar_t);
  totalLen += sizeof(wchar_t); // 结尾双 \0
  HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(DROPFILES) + totalLen);
  bool ok = false;
  if (hGlobal != NULL) {
    DROPFILES *pDrop = (DROPFILES *)GlobalLock(hGlobal);
    if (pDrop != NULL) {
      pDrop->pFiles = sizeof(DROPFILES);
      pDrop->pt.x = 0;
      pDrop->pt.y = 0;
      pDrop->fNC = FALSE;
      pDrop->fWide = TRUE;
      wchar_t *pStr = (wchar_t *)((BYTE *)pDrop + sizeof(DROPFILES));
      for (const auto &p : paths) {
        wcscpy_s(pStr, p.size() + 1, p.c_str());
        pStr += p.size() + 1;
      }
      *pStr = L'\0';
      GlobalUnlock(hGlobal);
      SetClipboardData(CF_HDROP, hGlobal);
      g_isRestoringClipboard = true;
      ok = true;
    } else {
      GlobalFree(hGlobal);
    }
  }
  CloseClipboard();

  if (ok) {
    RememberPasteTarget(hwnd);
    RestoreFocusAndPaste(hwnd);
    if (g_isNotificationEnabled) {
      ShowTrayBalloon(hwnd, T(STR_TRAY_HINT), T(STR_TRAY_PASTED));
    }
    // 粘贴后自动退出批量模式
    ExitBatchMode();
  }
  return ok;
}

void EnsureListSelectionVisible(int index) {
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

  if (newTop >= 0 && newTop != g_listBoxTopIndex) {
    ResetShortcutAssignment();
    g_smoothScrollExpectedTop = -1;
    g_smoothScrollExpectedEndExclusive = -1;
    ApplyListBoxTopIndex(g_hwndListBox, newTop);
    RedrawWindow(g_hwndListBox, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
    ShowCustomScrollbar(g_hwndListBox);
    RefreshScrollbarIfChanged(g_hwndListBox);
  }
}

bool SelectListDisplayIndex(int index) {
  if (!g_hwndListBox || !IsSelectableDisplayIndex(index))
    return false;
  int topBefore = (int)SendMessageW(g_hwndListBox, LB_GETTOPINDEX, 0, 0);
  // 禁用重绘避免 LB_SETCURSEL 同步绘制选中项（绕过双缓冲导致闪烁）
  SendMessageW(g_hwndListBox, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_hwndListBox, LB_SETCURSEL, index, 0);
  SendMessageW(g_hwndListBox, WM_SETREDRAW, TRUE, 0);
  int topAfter = (int)SendMessageW(g_hwndListBox, LB_GETTOPINDEX, 0, 0);
  if (topAfter != topBefore) {
    // LB_SETCURSEL 内部自动滚动了（LBS 默认行为），绕过了
    // EnsureListSelectionVisible 的滚动检测。需要同步 g_listBoxTopIndex
    // 并标记缓存为脏，否则快捷键缓存对应旧可见项集合。
    g_listBoxTopIndex = topAfter;
    g_shortcutCacheDirty = true;
    // 同步更新页码并刷新翻页按钮/滚动条（与悬浮选中分支一致）
    g_currentPage = topAfter / ITEMS_PER_PAGE;
    InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
    InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
    ShowCustomScrollbar(g_hwndListBox);
    RefreshScrollbarIfChanged(g_hwndListBox);
  }
  EnsureListSelectionVisible(index);
  InvalidateRect(g_hwndListBox, NULL, FALSE);
  // 标记键盘选中时刻，悬浮选中在接下来短时间内不抢占，避免鼠标微动覆盖
  g_lastKeyboardSelectTick = GetTickCount();
  return true;
}

bool MoveListSelection(int delta) {
  if (!g_hwndListBox || g_displayIndexMap.empty() || delta == 0)
    return false;

  int current = (int)SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0);
  int step = (delta > 0) ? 1 : -1;
  int target = LB_ERR;

  if (current == LB_ERR) {
    target = (step > 0) ? GetFirstSelectableDisplayIndex()
                        : GetLastSelectableDisplayIndex();
  } else {
    // 支持 |delta| 步移动：从 current 开始逐步行进，到边界停止
    target = current;
    int remaining = (delta > 0) ? delta : -delta;
    for (int i = 0; i < remaining; ++i) {
      int next = FindSelectableDisplayIndex(target + step, step);
      if (next == LB_ERR)
        break;
      target = next;
    }
  }

  if (target == LB_ERR)
    return false;
  // 边界情况：目标与当前相同，不执行选中避免不必要的重绘导致闪烁
  if (target == current)
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
  // 刷新翻页按钮状态：j/k 移动可能未触发滚动，但选中项到达边界时
  // 翻页按钮的启用/禁用状态需要同步更新
  if (selected && g_hwndPageUpBtn)
    InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
  if (selected && g_hwndPageDownBtn)
    InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
  return selected;
}

bool JumpListSelectionToBoundary(bool toBottom) {
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
  // 刷新翻页按钮状态（同 MoveListSelection）
  if (selected && g_hwndPageUpBtn)
    InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
  if (selected && g_hwndPageDownBtn)
    InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
  return selected;
}

// 跳转到"当前页"首/尾（与 JumpListSelectionToBoundary 不同：
// 后者跳到整个列表首/尾；本函数跳到当前可见区域的首/末条目）。
// 与列表显示的 1~0 快捷键保持一致：使用 CollectVisibleShortcutDisplayIndices
// 收集的可见项集合，g(双击) 跳到首项，G(Shift+G) 跳到末项。
bool JumpListSelectionToPageBoundary(bool toBottom) {
  int displayCount = (int)g_displayIndexMap.size();
  if (displayCount <= 0)
    return false;
  // 与显示的 1~0 快捷键保持一致：使用当前可见项集合的首/末项，
  // 而非固定 ITEMS_PER_PAGE 分页，保证 g/G 跳转到当页可见的首/末条目。
  int visibleIds[10];
  int visibleCount = CollectVisibleShortcutDisplayIndices(visibleIds, 10);
  if (visibleCount <= 0)
    return false;
  int target = toBottom ? visibleIds[visibleCount - 1] : visibleIds[0];
  target = FindSelectableDisplayIndex(target, toBottom ? -1 : 1);
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
  if (selected && g_hwndPageUpBtn)
    InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
  if (selected && g_hwndPageDownBtn)
    InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
  return selected;
}

static void InvalidateMainFilterButtons() {
  if (g_hwndFilterAll)
    InvalidateRect(g_hwndFilterAll, NULL, FALSE);
  if (g_hwndFilterText)
    InvalidateRect(g_hwndFilterText, NULL, FALSE);
  if (g_hwndFilterImage)
    InvalidateRect(g_hwndFilterImage, NULL, FALSE);
  if (g_hwndFilterFile)
    InvalidateRect(g_hwndFilterFile, NULL, FALSE);
  if (g_hwndFilterFavorite)
    InvalidateRect(g_hwndFilterFavorite, NULL, FALSE);
}

static bool SwitchMainPanel(HWND hwnd, int newTab, bool resetFavoriteFilter) {
  if (newTab < 0 || newTab > 4)
    return false;

  int oldTab = g_currentTab;

  if (oldTab >= 0 && oldTab < 5) {
    g_tabTopIndex[oldTab] = g_listBoxTopIndex;
    if (g_hwndListBox) {
      int curSel = (int)SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0);
      g_tabSelIndex[oldTab] = (curSel != LB_ERR) ? curSel : -1;
    }
  }

  KillTimer(hwnd, ID_FAVORITE_TOOLTIP_TIMER);
  HideFavoriteFilterTooltip();

  g_currentTab = newTab;
  if (newTab == 4 && oldTab != 4 && resetFavoriteFilter)
    g_currentFilterTagId = 0;
  InvalidateMainFilterButtons();

  if (g_hwndListBox)
    SendMessageW(g_hwndListBox, WM_SETREDRAW, FALSE, 0);

  UpdateListBox();
  ResetShortcutAssignment();

  int savedTop = (newTab >= 0 && newTab < 5) ? g_tabTopIndex[newTab] : 0;
  bool restoredScroll = false;
  if (savedTop > 0 && g_hwndListBox) {
    int maxTop = GetListBoxMaxTopIndex();
    if (savedTop > maxTop)
      savedTop = maxTop;
    if (savedTop > 0) {
      ApplyListBoxTopIndex(g_hwndListBox, savedTop);
      restoredScroll = true;
    }
  }

  int savedSel = (newTab >= 0 && newTab < 5) ? g_tabSelIndex[newTab] : -1;
  if (savedSel >= 0 && savedSel < (int)g_displayIndexMap.size()) {
    SendMessageW(g_hwndListBox, LB_SETCURSEL, savedSel, 0);
  } else if (!restoredScroll) {
    int first = GetFirstSelectableDisplayIndex();
    if (first != LB_ERR)
      SendMessageW(g_hwndListBox, LB_SETCURSEL, first, 0);
  } else {
    int topIdx = (int)SendMessageW(g_hwndListBox, LB_GETTOPINDEX, 0, 0);
    if (topIdx >= 0 && topIdx < (int)g_displayIndexMap.size()) {
      SendMessageW(g_hwndListBox, LB_SETCURSEL, topIdx, 0);
    }
  }

  if (g_hwndListBox) {
    SendMessageW(g_hwndListBox, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(g_hwndListBox, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
  }

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

// ==================== 文本编辑弹窗（空格键打开，实现见 text_editor.cpp）
// ==================== 调用模式与 PRO
// 一致：由列表项矩形动画展开，支持编辑、保存关闭。

static bool HandleMainNavigationKey(const MSG &msg) {
  if (msg.message != WM_KEYDOWN || !g_hwndMain || !IsWindowVisible(g_hwndMain))
    return false;
  if (msg.hwnd != g_hwndMain && !IsChild(g_hwndMain, msg.hwnd))
    return false;

  // 预览/编辑窗口已打开且持有焦点时，不拦截任何按键——让按键送达编辑控件。
  // 否则空格会被 ShowTextEditorPopup 的 toggle
  // 逻辑关闭预览，无法在编辑框输入空格； 方向键等也会被主窗体导航抢走。
  HWND existingPreview = FindWindowW(L"SmartClipTextEditorPopup", NULL);
  if (existingPreview && IsWindow(existingPreview)) {
    HWND f = GetFocus();
    if (f == existingPreview || IsChild(existingPreview, f))
      return false;
  }

  // 空格预览特殊处理：
  // - 焦点不在搜索框（如悬浮选中已把焦点移到列表框）时，空格始终触发预览。
  // - 焦点在搜索框时，仅在停止输入 500ms 后触发预览；输入过程中（500ms 内）
  //   空格作为搜索词输入，不影响搜索词含空格。
  if (msg.wParam == VK_SPACE && g_hwndListBox && !g_isBatchEditMode &&
      !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
    HWND hwndFocus = GetFocus();
    bool searchBoxFocused = (g_hwndSearchBox && hwndFocus == g_hwndSearchBox);
    bool allowPreview =
        !searchBoxFocused || (GetTickCount() - g_lastSearchInputTick >= 500);
    if (allowPreview) {
      int sel = (int)SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0);
      if (sel != LB_ERR && sel < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[sel];
        if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
          const ClipboardItem &item = g_history[actualIndex];
          if (item.type == TYPE_TEXT) {
            RECT rcItem;
            if (SendMessageW(g_hwndListBox, LB_GETITEMRECT, sel,
                             (LPARAM)&rcItem) != LB_ERR) {
              POINT tl = {rcItem.left, rcItem.top};
              POINT br = {rcItem.right, rcItem.bottom};
              ClientToScreen(g_hwndListBox, &tl);
              ClientToScreen(g_hwndListBox, &br);
              RECT rcScreen = {tl.x, tl.y, br.x, br.y};
              ShowTextEditorPopup(g_hwndMain, actualIndex, rcScreen);
            }
            return true;
          } else if (item.type == TYPE_IMAGE) {
            ShowImagePreview(g_hwndMain, item);
            return true;
          }
        }
      }
    }
  }

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
    handled = MoveListSelection(-1);
    break;
  case VK_DOWN:
    handled = MoveListSelection(1);
    break;
  case 'K':
    if (shiftPressed) {
      // Shift+K 向上移动4个项目
      handled = MoveListSelection(-4);
    } else {
      handled = MoveListSelection(-1);
    }
    break;
  case 'J':
    if (shiftPressed) {
      // Shift+J 向下移动4个项目
      handled = MoveListSelection(4);
    } else {
      handled = MoveListSelection(1);
    }
    break;
  case 'V':
    // 选中当前可见区域第一行
    if (g_hwndListBox) {
      int target = FindSelectableDisplayIndex(g_listBoxTopIndex, 1);
      if (target != LB_ERR) {
        SelectListDisplayIndex(target);
        InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
        InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
      }
      handled = true;
    }
    break;
  case VK_HOME:
    handled = JumpListSelectionToBoundary(false);
    break;
  case VK_END:
    handled = JumpListSelectionToBoundary(true);
    break;
  case VK_PRIOR:
    if (g_hwndListBox && g_listBoxTopIndex > 0) {
      // 取消可能正在进行的平滑滚动，避免快捷键集合被中间帧污染
      if (g_smoothScrollActive) {
        g_smoothScrollActive = false;
        KillTimer(g_hwndListBox, ID_SMOOTH_SCROLL_TIMER);
      }
      g_smoothScrollExpectedTop = -1;
      g_smoothScrollExpectedEndExclusive = -1;
      ApplyListBoxTopIndex(g_hwndListBox,
                           CalculatePrevPageIndex(g_listBoxTopIndex));
      RedrawWindow(g_hwndListBox, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
      ShowCustomScrollbar(g_hwndListBox);
      RefreshScrollbarIfChanged(g_hwndListBox);
      handled = true;
    }
    break;
  case VK_NEXT: {
    if (g_hwndListBox) {
      // 取消可能正在进行的平滑滚动，避免快捷键集合被中间帧污染
      if (g_smoothScrollActive) {
        g_smoothScrollActive = false;
        KillTimer(g_hwndListBox, ID_SMOOTH_SCROLL_TIMER);
      }
      // 使用 CalculateNextPageIndex：未完整显示的项会成为下一页首项
      int expectedNextTop = CalculateNextPageIndex(g_listBoxTopIndex);
      if (expectedNextTop < (int)g_displayIndexMap.size() &&
          expectedNextTop > g_listBoxTopIndex) {
        ApplyListBoxTopIndex(g_hwndListBox, expectedNextTop);
        g_smoothScrollExpectedTop = -1;
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
      handled = JumpListSelectionToPageBoundary(true);
      g_vimNavPendingGTick = 0;
    } else {
      if (g_vimNavPendingGTick != 0 && now - g_vimNavPendingGTick <= 500) {
        handled = JumpListSelectionToPageBoundary(false);
        g_vimNavPendingGTick = 0;
      } else {
        g_vimNavPendingGTick = now;
        handled = true;
      }
    }
    break;
  case VK_SPACE: {
    // 空格键：文本记录打开预览；图片记录打开大图预览（批量模式不预览）
    if (g_hwndListBox && !g_isBatchEditMode) {
      int sel = (int)SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0);
      if (sel != LB_ERR && sel < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[sel];
        if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
          const ClipboardItem &item = g_history[actualIndex];
          if (item.type == TYPE_TEXT) {
            RECT rcItem;
            if (SendMessageW(g_hwndListBox, LB_GETITEMRECT, sel,
                             (LPARAM)&rcItem) != LB_ERR) {
              POINT tl = {rcItem.left, rcItem.top};
              POINT br = {rcItem.right, rcItem.bottom};
              ClientToScreen(g_hwndListBox, &tl);
              ClientToScreen(g_hwndListBox, &br);
              RECT rcScreen = {tl.x, tl.y, br.x, br.y};
              ShowTextEditorPopup(g_hwndMain, actualIndex, rcScreen);
            }
            handled = true;
          } else if (item.type == TYPE_IMAGE) {
            ShowImagePreview(g_hwndMain, item);
            handled = true;
          }
        }
      }
    }
    break;
  }
  case VK_DELETE: {
    // Del 键：删除当前选中项（与右键删除一致，含收藏弹窗确认）
    if (g_hwndListBox) {
      int sel = (int)SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0);
      if (sel != LB_ERR && sel < (int)g_displayIndexMap.size()) {
        g_contextMenuIndex = sel;
        SendMessageW(g_hwndMain, WM_COMMAND, IDM_DELETE, 0);
        handled = true;
      }
    }
    break;
  }
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
  g_listBoxTooltipText.clear();
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

  // 快速预检：文本中不含 '#' 也不含 'rgb' 时直接跳过正则，
  // 避免对普通文本执行昂贵的 std::wregex 匹配。
  bool mayHaveHex = false;
  bool mayHaveRgb = false;
  for (size_t i = 0; i < text.length(); ++i) {
    wchar_t ch = text[i];
    if (ch == L'#')
      mayHaveHex = true;
    else if (ch == L'r' || ch == L'R')
      mayHaveRgb = true;
    if (mayHaveHex && mayHaveRgb)
      break;
  }
  if (!mayHaveHex && !mayHaveRgb)
    return false;

  static const std::wregex kHexColorRe(
      L"#(?:[0-9A-Fa-f]{3}|[0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})(?![0-9A-Fa-f])");
  static const std::wregex kRgbColorRe(
      L"\\brgba?\\(\\s*(\\d{1,3})\\s*,\\s*(\\d{1,3})\\s*,\\s*(\\d{1,3})"
      L"(?:\\s*,\\s*([01](?:\\.\\d+)?|0?\\.\\d+))?\\s*\\)",
      std::regex_constants::icase);

  std::wsmatch hexMatch;
  std::wsmatch rgbMatch;
  bool foundHex = mayHaveHex && std::regex_search(text, hexMatch, kHexColorRe);
  bool foundRgb = mayHaveRgb && std::regex_search(text, rgbMatch, kRgbColorRe);
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
  if (newTop != oldTop) {
    // 滚动条拖拽：重置快捷键分配，所有可见项从1开始编号
    ResetShortcutAssignment();
    ApplyListBoxTopIndex(hwnd, newTop);
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

// 绘制带搜索关键词高亮的单行文本：
// 匹配到的关键词片段用金黄色绘制，其余用正常颜色；空格/换行已由调用方替换。
// 超宽时逐字符截断并在末尾绘制省略号（行为与 DT_END_ELLIPSIS 一致）。
static void DrawHighlightedText(HDC hdc, const std::wstring &text,
                                const std::wstring &keyword, const RECT &rcText,
                                COLORREF normalColor, COLORREF highlightColor) {
  if (text.empty() || rcText.right <= rcText.left)
    return;

  std::wstring lowerText = text;
  std::wstring lowerKeyword = keyword;
  if (!keyword.empty()) {
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(),
                   ::towlower);
    std::transform(lowerKeyword.begin(), lowerKeyword.end(),
                   lowerKeyword.begin(), ::towlower);
  }
  bool hasMatch = !lowerKeyword.empty() &&
                  lowerText.find(lowerKeyword) != std::wstring::npos;

  // 无匹配时走普通绘制，保持原路径外观与性能
  if (!hasMatch) {
    SetTextColor(hdc, normalColor);
    RECT rcDrawLocal = rcText; // DrawTextW 需要非 const LPRECT
    DrawTextW(hdc, text.c_str(), -1, &rcDrawLocal,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_CLIP);
    return;
  }

  // 省略号已由展开箭头（三角形）取代：超长文本直接硬截断，不再画 "..."
  int availWidth = rcText.right - rcText.left;
  if (availWidth <= 0)
    return;

  // 判断是否超宽，并取能放下的字符数
  int fittedChars = 0;
  SIZE szWhole = {};
  BOOL fitted = GetTextExtentExPointW(hdc, text.c_str(), (int)text.length(),
                                      availWidth, &fittedChars, NULL, &szWhole);
  bool truncated =
      !fitted || fittedChars < (int)text.length() || fittedChars <= 0;

  size_t visibleLen =
      truncated ? (size_t)std::max(0, fittedChars) : text.length();
  if (visibleLen == 0)
    return;

  // 逐段绘制：普通段用 normalColor，匹配段用 highlightColor
  auto drawSegment = [&](const std::wstring &seg, COLORREF color,
                         int curX) -> int {
    if (seg.empty())
      return curX;
    SIZE szSeg = {};
    GetTextExtentPoint32W(hdc, seg.c_str(), (int)seg.length(), &szSeg);
    if (curX + szSeg.cx > rcText.right)
      return curX;
    RECT rcSeg = rcText;
    rcSeg.left = curX;
    SetTextColor(hdc, color);
    DrawTextW(hdc, seg.c_str(), -1, &rcSeg,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    return curX + szSeg.cx;
  };

  std::wstring visibleText = text.substr(0, visibleLen);
  std::wstring visibleLower = lowerText.substr(0, visibleLen);
  size_t kwLen = lowerKeyword.length();
  size_t pos = 0;
  int x = rcText.left;
  while (pos < visibleLen) {
    size_t matchStart = visibleLower.find(lowerKeyword, pos);
    if (matchStart == std::wstring::npos) {
      // 剩余普通段
      x = drawSegment(visibleText.substr(pos), normalColor, x);
      break;
    }
    if (matchStart > pos) {
      x = drawSegment(visibleText.substr(pos, matchStart - pos), normalColor,
                      x);
    }
    size_t segLen = std::min(kwLen, visibleLen - matchStart);
    x = drawSegment(visibleText.substr(matchStart, segLen), highlightColor, x);
    pos = matchStart + segLen;
  }
}

// ==================== 文本选中复制（鼠标拖选 + Ctrl+C） ====================

// 文本内容是否支持拖选（含 emoji 的复杂彩色排版暂不支持选中）
static bool IsTextSelectableContent(const std::wstring &content) {
  return !TextContainsEmoji(content.c_str(), (int)content.length());
}

// 列表刷新后调用：文本选中状态随显示索引失效，直接清除
void ClearTextSelectionAfterRefresh() {
  g_textSelItem = -1;
  g_textSelAnchor = -1;
  g_textSelEnd = -1;
  g_textSelDragging = false;
}

// 获取当前选中范围（min/max，内容字符坐标），返回是否有效（非空）
static bool GetTextSelRange(int *selStart, int *selEnd) {
  if (g_textSelItem < 0 || g_textSelAnchor < 0 || g_textSelEnd < 0)
    return false;
  int a = std::min(g_textSelAnchor, g_textSelEnd);
  int b = std::max(g_textSelAnchor, g_textSelEnd);
  if (b <= a)
    return false;
  if (selStart)
    *selStart = a;
  if (selEnd)
    *selEnd = b;
  return true;
}

// 折叠视图文本的可见字符数（与绘制一致：按可用宽度硬截断）
static int TextSelCollapsedCharCount(HDC hdc, const std::wstring &text,
                                     int maxW) {
  if (text.empty() || maxW <= 0)
    return 0;
  SIZE sz = {};
  GetTextExtentPoint32W(hdc, text.c_str(), (int)text.length(), &sz);
  if (sz.cx <= maxW)
    return (int)text.length();
  int fitted = 0;
  GetTextExtentExPointW(hdc, text.c_str(), (int)text.length(), maxW, &fitted,
                        NULL, &sz);
  if (fitted < 0)
    fitted = 0;
  return fitted;
}

// 单行文本：由 X 坐标求字符位置（返回 [0, maxChars] 区间）
static int TextSelCharFromX(HDC hdc, const std::wstring &text, int startX,
                            int maxChars, int x) {
  if (maxChars <= 0)
    return 0;
  int cum = 0;
  for (int i = 0; i < maxChars; i++) {
    SIZE sc = {};
    GetTextExtentPoint32W(hdc, &text[i], 1, &sc);
    if (x < startX + cum + sc.cx / 2)
      return i;
    cum += sc.cx;
  }
  return maxChars;
}

// 折行布局：把文本按指定宽度拆成显示行（与 DrawTextW DT_WORDBREAK 近似）
struct TextSelLine {
  int start; // 行首字符位置
  int end;   // 行尾字符位置（不含）
};
static void TextSelLayoutLines(HDC hdc, const std::wstring &text, int maxW,
                               std::vector<TextSelLine> &lines) {
  if (text.empty())
    return;
  if (maxW <= 0)
    maxW = 1;
  size_t pos = 0;
  const size_t len = text.length();
  while (pos < len) {
    // 跳过显式换行符（\r\n 或 \n）
    if (text[pos] == L'\r' || text[pos] == L'\n') {
      pos++;
      continue;
    }
    size_t i = pos;
    size_t lastSpace = (size_t)-1;
    int cumW = 0;
    bool overflow = false;
    while (i < len) {
      wchar_t c = text[i];
      if (c == L'\r' || c == L'\n')
        break;
      SIZE sc = {};
      GetTextExtentPoint32W(hdc, &text[i], 1, &sc);
      cumW += sc.cx;
      if (cumW > maxW) {
        overflow = true;
        break;
      }
      if (c == L' ')
        lastSpace = i;
      i++;
    }
    if (!overflow && i >= len) {
      lines.push_back({(int)pos, (int)len});
      break;
    }
    if (!overflow) { // 在显式换行处断行
      lines.push_back({(int)pos, (int)i});
      if (text[i] == L'\r' && i + 1 < len && text[i + 1] == L'\n')
        i++;
      pos = i + 1;
      continue;
    }
    // 超宽断行：优先在最近空格处断，否则从当前字符断（单词超宽中段截断）
    if (lastSpace != (size_t)-1 && lastSpace > pos) {
      lines.push_back({(int)pos, (int)(lastSpace + 1)});
      pos = lastSpace + 1;
    } else if (i > pos) {
      lines.push_back({(int)pos, (int)i});
      pos = i;
    } else {
      // 单个字符即超宽：强制包含一个字符，避免死循环
      lines.push_back({(int)pos, (int)(pos + 1)});
      pos = pos + 1;
    }
  }
}

// 多行布局的行高（与 DrawTextW DT_WORDBREAK 行距一致）
static int TextSelLineHeight(HDC hdc) {
  TEXTMETRICW tm = {};
  GetTextMetricsW(hdc, &tm);
  return tm.tmHeight + tm.tmExternalLeading;
}

// 计算点击/拖选位置对应的内容字符位置（单行显示）
static int TextSelCharFromPoint(HWND hwnd, int displayIndex, POINT pt) {
  if (displayIndex < 0 || displayIndex >= (int)g_displayIndexMap.size())
    return 0;
  int actualIndex = g_displayIndexMap[displayIndex];
  if (actualIndex < 0 || actualIndex >= (int)g_history.size())
    return 0;
  const ClipboardItem &item = g_history[actualIndex];
  if (item.type != TYPE_TEXT)
    return 0;

  RECT rcItem;
  if (SendMessageW(hwnd, LB_GETITEMRECT, displayIndex, (LPARAM)&rcItem) ==
      LB_ERR)
    return 0;
  RECT rcContent = rcItem;
  rcContent.left += MScale(10);
  rcContent.right -= MScale(6);
  rcContent.top += MScale(2);
  rcContent.right -= GetCustomScrollbarReservedWidth();
  if (rcContent.right < rcContent.left + MScale(80))
    rcContent.right = rcContent.left + MScale(80);
  rcContent.top += MScale(20); // 标题区
  RECT rcText = rcContent;
  rcText.bottom = rcText.top + MScale(22);
  rcText.left += MScale(22); // 类型图标占位

  HDC hdc = GetDC(hwnd);
  HFONT hOldFont = (HFONT)SelectObject(hdc, GetListMainFont());
  int result = 0;

  // 单行显示（从列表框取显示文本，与绘制一致）
  int textLen = (int)SendMessageW(hwnd, LB_GETTEXTLEN, displayIndex, 0);
  std::wstring text;
  if (textLen > 0) {
    std::vector<wchar_t> buffer(textLen + 1);
    SendMessageW(hwnd, LB_GETTEXT, displayIndex, (LPARAM)&buffer[0]);
    text = &buffer[0];
    size_t pos = text.find(L"\r\n");
    if (pos != std::wstring::npos)
      text = text.substr(pos + 2);
    for (size_t i = 0; i < text.length(); i++)
      if (text[i] == L'\r' || text[i] == L'\n')
        text[i] = L' ';
  }
  int maxW = rcText.right - rcText.left;
  if (maxW < 0)
    maxW = 0;
  int cnt = TextSelCollapsedCharCount(hdc, text, maxW);
  result = TextSelCharFromX(hdc, text, rcText.left, cnt, pt.x);

  SelectObject(hdc, hOldFont);
  ReleaseDC(hwnd, hdc);
  return result;
}

// 清除文本选中状态并重绘原项
static void ClearTextSelectionForClick(HWND hwnd) {
  int oldItem = g_textSelItem;
  g_textSelItem = -1;
  g_textSelAnchor = -1;
  g_textSelEnd = -1;
  g_textSelDragging = false;
  if (hwnd && IsWindow(hwnd) && oldItem >= 0) {
    RECT rc;
    if (SendMessageW(hwnd, LB_GETITEMRECT, oldItem, (LPARAM)&rc) != LB_ERR)
      InvalidateRect(hwnd, &rc, FALSE);
  }
}

// 重绘选中文本项（拖选过程中更新高亮）
static void InvalidateTextSelItem(HWND hwnd) {
  if (!hwnd || g_textSelItem < 0)
    return;
  RECT rc;
  if (SendMessageW(hwnd, LB_GETITEMRECT, g_textSelItem, (LPARAM)&rc) != LB_ERR)
    InvalidateRect(hwnd, &rc, FALSE);
}

// Ctrl+C：复制选中的文本片段到剪贴板
static void CopyTextSelectionToClipboard() {
  int selStart = 0, selEnd = 0;
  if (!GetTextSelRange(&selStart, &selEnd))
    return;
  if (g_textSelItem < 0 || g_textSelItem >= (int)g_displayIndexMap.size())
    return;
  int actualIndex = g_displayIndexMap[g_textSelItem];
  if (actualIndex < 0 || actualIndex >= (int)g_history.size())
    return;
  const ClipboardItem &item = g_history[actualIndex];
  if (item.type != TYPE_TEXT)
    return;
  if (selEnd > (int)item.content.length())
    selEnd = (int)item.content.length();
  if (selStart >= selEnd)
    return;
  std::wstring sel = item.content.substr(selStart, selEnd - selStart);
  if (sel.empty())
    return;
  if (!OpenClipboard(g_hwndMain))
    return;
  EmptyClipboard();
  HGLOBAL hGlobal =
      GlobalAlloc(GMEM_MOVEABLE, (sel.length() + 1) * sizeof(wchar_t));
  if (hGlobal) {
    wchar_t *pData = (wchar_t *)GlobalLock(hGlobal);
    if (pData) {
      wcscpy_s(pData, sel.length() + 1, sel.c_str());
      GlobalUnlock(hGlobal);
      SetClipboardData(CF_UNICODETEXT, hGlobal);
    } else {
      GlobalFree(hGlobal);
    }
  }
  CloseClipboard();
}

// 绘制带选中高亮 + 搜索高亮的文本（单行或多行）
// rc: 文本区域；selStart/selEnd: 内容选中范围（含 start 不含 end）；
// multiLine: 多行折行；verticalCenter: 单行垂直居中；超长直接硬截断。
static void DrawTextSelectionContent(HDC hdc, const std::wstring &text,
                                     const std::wstring &keyword,
                                     const RECT &rc, bool multiLine,
                                     bool verticalCenter, int selStart,
                                     int selEnd, COLORREF normalColor,
                                     COLORREF highlightColor) {
  if (text.empty() || rc.right <= rc.left)
    return;

  // 选中背景色（浅色模式浅蓝 / 深色模式深蓝）
  COLORREF selBg = g_isDarkMode ? RGB(38, 79, 120) : RGB(173, 214, 255);
  HBRUSH hSelBrush = CreateSolidBrush(selBg);

  // 搜索关键词预处理（不区分大小写）
  std::wstring lowerText, lowerKeyword;
  bool hasMatch = false;
  if (!keyword.empty()) {
    lowerText = text;
    lowerKeyword = keyword;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(),
                   ::towlower);
    std::transform(lowerKeyword.begin(), lowerKeyword.end(),
                   lowerKeyword.begin(), ::towlower);
    hasMatch = lowerText.find(lowerKeyword) != std::wstring::npos;
  }

  auto drawLine = [&](int lineStart, int lineEnd, const RECT &rcLine) {
    // 该行可见字符范围（多行=整行；单行=按宽度截断）
    int visibleEndLocal = lineEnd;
    if (!multiLine) {
      int cnt = TextSelCollapsedCharCount(
          hdc, text.substr(lineStart, lineEnd - lineStart),
          rcLine.right - rcLine.left);
      visibleEndLocal = lineStart + cnt;
    }
    if (visibleEndLocal <= lineStart)
      return;

    // 垂直居中时按字体度量对齐（与 DT_VCENTER 一致）
    RECT rcTextLine = rcLine;
    if (verticalCenter) {
      TEXTMETRICW tm = {};
      GetTextMetricsW(hdc, &tm);
      int th = tm.tmHeight;
      int top = rcLine.top + (rcLine.bottom - rcLine.top - th) / 2;
      if (top < rcLine.top)
        top = rcLine.top;
      rcTextLine.top = top;
      rcTextLine.bottom = top + th;
    }

    // 逐字符状态：0=普通 1=搜索命中 2=选中 3=选中+命中
    int n = visibleEndLocal - lineStart;
    std::vector<int> state(n, 0);
    if (hasMatch) {
      size_t pos = lineStart;
      while (true) {
        size_t m = lowerText.find(lowerKeyword, pos);
        if (m == (size_t)-1 || (int)m >= visibleEndLocal)
          break;
        size_t kwEnd = m + lowerKeyword.length();
        if (kwEnd > (size_t)visibleEndLocal)
          kwEnd = visibleEndLocal;
        for (size_t k = m; k < kwEnd; k++)
          state[k - lineStart] |= 1;
        pos = kwEnd;
      }
    }
    for (int i = lineStart; i < visibleEndLocal; i++) {
      if (i >= selStart && i < selEnd)
        state[i - lineStart] |= 2;
    }

    int x = rcLine.left;
    int yTop = rcTextLine.top;
    int h = rcTextLine.bottom - rcTextLine.top;
    int i = 0;
    while (i < n) {
      int curState = state[i];
      int j = i + 1;
      while (j < n && state[j] == curState)
        j++;
      SIZE sc = {};
      GetTextExtentPoint32W(hdc, text.c_str() + lineStart + i, j - i, &sc);
      int segW = sc.cx;
      // 选中背景
      if (curState & 2) {
        RECT rcSelBg = {x, rcLine.top, x + segW, rcLine.bottom};
        FillRect(hdc, &rcSelBg, hSelBrush);
      }
      // 文本（命中段金黄色，其余正常色）
      SetTextColor(hdc, (curState & 1) ? highlightColor : normalColor);
      RECT rcSeg = {x, yTop, x + segW, yTop + h};
      DrawTextW(hdc, text.c_str() + lineStart + i, j - i, &rcSeg,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_CLIP);
      x += segW;
      i = j;
    }
  };

  if (multiLine) {
    std::vector<TextSelLine> lines;
    TextSelLayoutLines(hdc, text, rc.right - rc.left, lines);
    int lineH = TextSelLineHeight(hdc);
    int y = rc.top;
    for (size_t li = 0; li < lines.size(); li++) {
      RECT rcLine = rc;
      rcLine.top = y;
      rcLine.bottom = y + lineH;
      if (li + 1 == lines.size())
        rcLine.bottom = rc.bottom; // 末行延伸到底部
      drawLine(lines[li].start, lines[li].end, rcLine);
      y += lineH;
    }
  } else {
    drawLine(0, (int)text.length(), rc);
  }

  DeleteObject(hSelBrush);
}

// ===================== 多文件记录：展开/收起辅助函数 =====================
// 展开态：每个文件作为独立虚拟子项（listbox
// item），高度与普通记录一致（57px）。 头行显示来源应用图标 +
// 快捷键，末行显示上三角（收起按钮），所有行有分割线。

// 计算多文件记录三角形区域
// 展开态：仅末行（subIdx==fileCount-1）显示上三角（收起按钮）
// 收起态：始终显示下拉三角（即使内容一行能显示完也支持展开）
static bool GetMultiFileTriangleRect(HWND hwnd, int displayIndex,
                                     RECT &outRect) {
  RECT rcItem;
  if (SendMessageW(hwnd, LB_GETITEMRECT, displayIndex, (LPARAM)&rcItem) ==
      LB_ERR)
    return false;
  if (displayIndex < 0 || displayIndex >= (int)g_displayIndexMap.size())
    return false;
  int actualIndex = g_displayIndexMap[displayIndex];
  if (actualIndex < 0 || actualIndex >= (int)g_history.size())
    return false;
  const ClipboardItem &item = g_history[actualIndex];
  if (item.type != TYPE_FILE)
    return false;
  if (item.content.find(L'\n') == std::wstring::npos)
    return false;

  // 展开态：只有末行（subIdx==fileCount-1）才有上三角（收起按钮）
  int subIdx = (displayIndex < (int)g_displaySubIndexMap.size())
                   ? g_displaySubIndexMap[displayIndex]
                   : -1;
  if (IsMultiFileExpanded(actualIndex)) {
    int fileCount = GetMultiFilePathCount(item.content);
    if (subIdx != fileCount - 1)
      return false;
  }

  int triW = MScale(14);
  RECT rcContent = rcItem;
  rcContent.left += MScale(10);
  rcContent.right -= MScale(6);
  rcContent.top += MScale(2);
  rcContent.right -= GetCustomScrollbarReservedWidth();
  rcContent.top += MScale(20); // 标题下方为内容区

  // 收起态：始终显示三角（不再检查截断）

  outRect = rcContent;
  outRect.bottom = outRect.top + MScale(22);
  outRect.left = outRect.right - triW;
  return true;
}

// 多文件展开/收起后重建列表（触发 WM_MEASUREITEM 重算行高）并尽量保持滚动位置
static void RefreshListBoxPreservePosition() {
  if (g_hwndListBox == NULL)
    return;
  int oldTopDisplayIndex =
      (int)SendMessageW(g_hwndListBox, LB_GETTOPINDEX, 0, 0);
  int oldSelDisplayIndex = (int)SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0);
  int oldTopActualIndex = -1;
  int oldSelActualIndex = -1;
  if (oldTopDisplayIndex >= 0 &&
      oldTopDisplayIndex < (int)g_displayIndexMap.size())
    oldTopActualIndex = g_displayIndexMap[oldTopDisplayIndex];
  if (oldSelDisplayIndex >= 0 &&
      oldSelDisplayIndex < (int)g_displayIndexMap.size())
    oldSelActualIndex = g_displayIndexMap[oldSelDisplayIndex];

  UpdateListBox();

  // UpdateListBox 内 LB_ADDSTRING 已对每项触发 WM_MEASUREITEM 重算高度
  // （含展开/收起后的行高）。此处不再额外发 LB_SETITEMHEIGHT——对
  // LBS_OWNERDRAWVARIABLE 该消息会把各项高度置 0，破坏滚动状态，导致
  // 展开后头行被滚出可视区。

  int newTopDisplayIndex = -1;
  int newSelDisplayIndex = LB_ERR;
  for (int i = 0; i < (int)g_displayIndexMap.size(); ++i) {
    // 使用首匹配：展开多文件记录时选中头行而非末行，避免 LB_SETCURSEL 向下滚动
    // 注意：不能用 == 0 判断"未找到"，因为 0 是合法的 display index
    if (newTopDisplayIndex < 0 && g_displayIndexMap[i] == oldTopActualIndex &&
        oldTopActualIndex >= 0)
      newTopDisplayIndex = i;
    if (newSelDisplayIndex == LB_ERR &&
        g_displayIndexMap[i] == oldSelActualIndex && oldSelActualIndex >= 0)
      newSelDisplayIndex = i;
  }
  if (newTopDisplayIndex < 0)
    newTopDisplayIndex = 0;
  // 先恢复选中项（LB_SETCURSEL 内部可能滚动列表），再恢复滚动位置，
  // 确保 ApplyListBoxTopIndex 的 topIndex 不被 LB_SETCURSEL 覆盖
  if (newSelDisplayIndex != LB_ERR)
    SendMessageW(g_hwndListBox, LB_SETCURSEL, newSelDisplayIndex, 0);
  ApplyListBoxTopIndex(g_hwndListBox, newTopDisplayIndex);
}

// 确保指定项目完全可见（展开多文件记录时，头行可能被滚出可视区顶部，
// 末行/下拉三角可能超出底部）。双向滚动：上溢则向上滚到使项目成为首项，
// 下溢则向下滚到项目底部可见。
static void EnsureItemFullyVisible(HWND hwnd, int displayIndex) {
  RECT rcItem, rcClient;
  GetClientRect(hwnd, &rcClient);
  if (SendMessageW(hwnd, LB_GETITEMRECT, displayIndex, (LPARAM)&rcItem) ==
      LB_ERR)
    return;
  // 已完全可见则无需滚动
  if (rcItem.top >= 0 && rcItem.bottom <= rcClient.bottom)
    return;
  int targetTop = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
  // 项目顶部在可视区上方：向上滚到使项目成为首项
  if (rcItem.top < 0) {
    if (displayIndex < targetTop)
      targetTop = displayIndex;
  }
  // 项目底部超出可视区：逐项向下滚到项目底部可见
  while (rcItem.bottom > rcClient.bottom) {
    int topIndex = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
    if (topIndex >= displayIndex)
      break; // 项目已是可视区首项，无法再向上滚
    SendMessageW(hwnd, LB_SETTOPINDEX, topIndex + 1, 0);
    int newTopIndex = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
    if (newTopIndex <= topIndex)
      break; // 滚动未生效（已到末尾），退出避免死循环
    if (SendMessageW(hwnd, LB_GETITEMRECT, displayIndex, (LPARAM)&rcItem) ==
        LB_ERR)
      break;
    targetTop = newTopIndex;
  }
  // 用 ApplyListBoxTopIndex 同步 g_listBoxTopIndex 与自定义滚动条
  ApplyListBoxTopIndex(hwnd, targetTop);
}

// 绘制下拉/上拉三角形（pointingDown=false 时尖角朝上，用于展开态收起提示）
static void DrawMultiFileTriangle(HDC hdc, const RECT &rcTri, COLORREF color,
                                  bool pointingDown = true) {
  int cx = (rcTri.left + rcTri.right) / 2;
  int cy = (rcTri.top + rcTri.bottom) / 2;
  int w = MScale(8);
  int h = MScale(5);
  POINT pts[3];
  if (pointingDown) {
    pts[0] = {cx - w / 2, cy - h / 2};
    pts[1] = {cx + w / 2, cy - h / 2};
    pts[2] = {cx, cy + h / 2};
  } else {
    pts[0] = {cx - w / 2, cy + h / 2};
    pts[1] = {cx + w / 2, cy + h / 2};
    pts[2] = {cx, cy - h / 2};
  }
  HBRUSH hBrush = CreateSolidBrush(color);
  HPEN hPen = CreatePen(PS_SOLID, std::max(1, MScale(1)), color);
  HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
  HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
  Polygon(hdc, pts, 3);
  SelectObject(hdc, hOldBrush);
  SelectObject(hdc, hOldPen);
  DeleteObject(hBrush);
  DeleteObject(hPen);
}

// 前置声明：拖拽蒙版呼出标志（定义在中转站段落）。拖拽蒙版呼出期间
// 禁止 ExitNoActivateMode 调整 z-order，否则蒙版被拖拽源窗口压住，
// 表现为"关闭置顶时拖拽呼出看似未触发"
extern bool g_dragShelfSummoned;

// 退出"不抢焦点(悬浮置顶)"模式：
// 1) 清除 WS_EX_NOACTIVATE，恢复窗口可被激活；
// 2) 按置顶开关（g_isTopmost）归一化 z-order，清除悬浮置顶残留的
//    WS_EX_TOPMOST——此前退出时只清 WS_EX_NOACTIVATE 而残留 TOPMOST，
//    导致"最大化后主窗体意外置顶"。
// 注意：用 SetWindowPos(HWND_TOP) 归一化而非直接改 WS_EX_TOPMOST 位，
// 避免扩展样式变化导致最大化窗口重新布局/还原。
static void ExitNoActivateMode(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd))
    hwnd = g_hwndMain;
  // 拖拽蒙版呼出期间：只清标志与样式位，绝不动 z-order——窗口必须
  // 保持 TOPMOST 浮在拖拽源之上（拖拽结束后由 NormalizeDragShelfZOrder
  // 按置顶开关归一化）
  if (g_dragShelfSummoned) {
    g_isNoActivateMode = false;
    LONG_PTR exN = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (exN & WS_EX_NOACTIVATE) {
      exN &= ~WS_EX_NOACTIVATE;
      SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exN);
    }
    return;
  }
  g_isNoActivateMode = false;
  LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if (ex & WS_EX_NOACTIVATE) {
    ex &= ~WS_EX_NOACTIVATE;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
  }
  bool wantTopmost = g_isTopmost;
  if (wantTopmost != ((ex & WS_EX_TOPMOST) != 0)) {
    // 摘除置顶必须用 HWND_NOTOPMOST：HWND_TOP 只把窗口提到"当前所在层"
    // 顶部，已置顶的窗口仍留在置顶层（这正是必须"置顶→取消置顶"才能
    // 摘除悬浮残留的原因）。仍走 SetWindowPos 而非直接改样式位，避免
    // 最大化窗口因扩展样式变化而重新布局。
    SetWindowPos(hwnd, wantTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
  }
}

// 列表框子类化窗口过程 - 处理自绘滚动条
LRESULT CALLBACK ListBoxProc(HWND hwnd, UINT message, WPARAM wParam,
                             LPARAM lParam) {
  static int s_lastScrollPos = -1; // 记录上次滚动位置

  // 处理背景擦除 - 不在屏幕 DC 上填充背景，
  // WM_PAINT 的双缓冲（FillRect 内存 DC + BitBlt）会完整覆盖更新区域。
  // 若在此处填充屏幕 DC，会在 BitBlt 前短暂擦除选中框等高对比元素，造成闪烁。
  if (message == WM_ERASEBKGND) {
    return 1;
  }

  // 文本选中复制：Ctrl+C 复制选中片段；Esc 取消选中
  if (message == WM_KEYDOWN) {
    if (wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000) &&
        g_textSelItem >= 0) {
      CopyTextSelectionToClipboard();
      ClearTextSelectionForClick(hwnd);
      return 0;
    }
    if (wParam == VK_ESCAPE && g_textSelItem >= 0) {
      ClearTextSelectionForClick(hwnd);
      return 0;
    }
  }
  // 吞掉 Ctrl+C 产生的 WM_CHAR，避免默认过程蜂鸣
  if (message == WM_CHAR && wParam == 0x03 && g_textSelItem >= 0) {
    return 0;
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
      // 鼠标滑轮：重置快捷键分配，所有可见项从1开始编号
      ResetShortcutAssignment();
      ApplyListBoxTopIndex(hwnd, newTop);
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
        // 平滑翻页（来自上下页按钮）不清空快捷键分配记录；
        // 其他平滑滚动（滚动条/滑轮）清空记录，所有可见项从1开始编号
        if (g_smoothScrollExpectedTop < 0) {
          ResetShortcutAssignment();
        }
        g_smoothScrollExpectedTop = -1;
        g_smoothScrollExpectedEndExclusive = -1;
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
        ShowCustomScrollbar(hwnd);
        RefreshScrollbarIfChanged(hwnd);
      } else {
        g_smoothScrollCurrent += step;
        // 设置滚动位置
        int newPos = (int)(g_smoothScrollCurrent + 0.5f);
        ApplyListBoxTopIndex(hwnd, newPos);
        ShowCustomScrollbar(hwnd);
        RefreshScrollbarIfChanged(hwnd);
      }
    }
    return 0;
  }

  // 处理垂直滚动
  if (message == WM_VSCROLL) {
    int oldTop = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
    // 滚动条/轨道点击：重置快捷键缓存，所有可见项从1开始编号
    if (!g_smoothScrollActive)
      ResetShortcutAssignment();
    LRESULT result =
        CallWindowProcW(g_oldListBoxProc, hwnd, message, wParam, lParam);
    HideNativeListBoxScrollbar(hwnd);
    g_listBoxTopIndex = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
    if (g_listBoxTopIndex < 0)
      g_listBoxTopIndex = 0;
    // CallWindowProcW 内部的 LB_SETTOPINDEX 可能已触发同步 WM_PAINT，
    // 此时 g_listBoxTopIndex 尚未更新，快捷键缓存被用旧值刷新。
    // 此处重新标记脏 + 失效整个客户区，确保下一次 WM_PAINT 用正确的
    // g_listBoxTopIndex 刷新缓存，并重绘所有可见项的快捷键。
    g_shortcutCacheDirty = true;
    InvalidateRect(hwnd, NULL, FALSE);

    if (NeedsCustomScrollbar()) {
      if (s_lastScrollPos != g_listBoxTopIndex || oldTop != g_listBoxTopIndex) {
        s_lastScrollPos = g_listBoxTopIndex;
        ShowCustomScrollbar(hwnd);
        RefreshScrollbarIfChanged(hwnd);
      }
    }
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
        // 仅重绘文件夹名称所在的内容区域（头部之下的部分），
        // 避免反复重绘顶部快捷键提示导致其闪烁
        rc.top += 20;
        InvalidateRect(hwnd, &rc, FALSE);
      }
    }
    return 0;
  }

  // 删除滑出动画定时器
  if (message == WM_TIMER && wParam == ID_DELETE_SLIDE_TIMER) {
    if (g_deleteSlideAnimating) {
      g_deleteSlideOffset += 80;
      if (g_deleteSlideOffset >= g_deleteSlideTargetWidth) {
        // 动画完成，执行实际删除
        g_deleteSlideAnimating = false;
        g_deleteSlideOffset = 0;
        KillTimer(hwnd, ID_DELETE_SLIDE_TIMER);

        int savedTopIndex = g_listBoxTopIndex;
        int savedSelDisplay = g_deleteSlideDisplayIndex;
        int actualIndex = g_deleteSlideActualIndex;

        // 删除关联的图片文件
        if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
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
        }
        SaveHistory();
        UpdateListBox();
        // 恢复滚动位置
        if (savedTopIndex > 0 && g_hwndListBox) {
          int maxTop = GetListBoxMaxTopIndex();
          if (savedTopIndex > maxTop)
            savedTopIndex = maxTop;
          ApplyListBoxTopIndex(g_hwndListBox, savedTopIndex);
        }
        ResetShortcutAssignment();
        // 选中相邻项
        int fallback = savedSelDisplay;
        if (fallback >= (int)g_displayIndexMap.size())
          fallback = (int)g_displayIndexMap.size() - 1;
        if (fallback >= 0)
          SelectListDisplayIndex(fallback);

        g_deleteSlideDisplayIndex = -1;
        g_deleteSlideActualIndex = -1;

        if (g_isNotificationEnabled && GetParent(hwnd)) {
          ShowTrayBalloon(GetParent(hwnd), T(STR_TRAY_HINT),
                          T(STR_TRAY_DELETED));
        }
      } else {
        // 继续动画，重绘该项
        if (g_deleteSlideDisplayIndex >= 0 && g_hwndListBox) {
          RECT rcItem;
          if (SendMessageW(hwnd, LB_GETITEMRECT, g_deleteSlideDisplayIndex,
                           (LPARAM)&rcItem) != LB_ERR) {
            InvalidateRect(hwnd, &rcItem, FALSE);
          }
        }
      }
    }
    return 0;
  }

  // 自定义 WM_PRINTCLIENT：直接遍历可见项并发送 WM_DRAWITEM 给父窗口，
  // 完全绕开默认列表框过程。默认过程对选中项的内部处理（系统高亮背景、
  // 焦点框、ODS_SELECTED 传递不一致）是选中项闪烁的根因——即使 WM_PAINT
  // 使用双缓冲，默认过程在 WM_PRINTCLIENT 内部对选中项的绘制行为仍不可控。
  // 由我们手动构造 DRAWITEMSTRUCT 并发送 WM_DRAWITEM，确保选中项的
  // ODS_SELECTED 状态正确传递，背景与边框完全由 WM_DRAWITEM 掌控。
  if (message == WM_PRINTCLIENT) {
    HDC hdc = (HDC)wParam;
    if (!hdc)
      return 0;

    HWND hwndParent = GetParent(hwnd);
    if (!hwndParent)
      return 0;

    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

    int topIndex = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
    int count = (int)SendMessageW(hwnd, LB_GETCOUNT, 0, 0);
    int curSel = (int)SendMessageW(hwnd, LB_GETCURSEL, 0, 0);
    int caretIndex = (int)SendMessageW(hwnd, LB_GETCARETINDEX, 0, 0);

    for (int i = topIndex; i < count; i++) {
      RECT rcItem;
      if (SendMessageW(hwnd, LB_GETITEMRECT, i, (LPARAM)&rcItem) == LB_ERR)
        break;

      if (rcItem.top >= rcClient.bottom)
        break;
      if (rcItem.bottom <= rcClient.top)
        continue;

      UINT itemState = 0;
      if (i == curSel)
        itemState |= ODS_SELECTED;
      if (i == caretIndex)
        itemState |= ODS_FOCUS;

      DRAWITEMSTRUCT dis = {};
      dis.CtlType = ODT_LISTBOX;
      dis.CtlID = ID_LISTBOX;
      dis.itemID = (UINT)i;
      dis.itemAction = ODA_DRAWENTIRE;
      dis.itemState = itemState;
      dis.hwndItem = hwnd;
      dis.hDC = hdc;
      dis.rcItem = rcItem;
      dis.itemData = SendMessageW(hwnd, LB_GETITEMDATA, i, 0);

      SendMessageW(hwndParent, WM_DRAWITEM, (WPARAM)dis.CtlID, (LPARAM)&dis);
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
      // 填充整个客户区背景：默认列表框过程在 WM_PRINTCLIENT 中带
      // PRF_ERASEBKGND 时的擦除行为不受控，可能导致选中项边框等高对比
      // 元素在内存 DC 上被意外覆盖。由我们预先填充整个客户区背景，
      // 并去掉 PRF_ERASEBKGND，完全掌控背景擦除。
      FillRect(hdcMem, &rcClient, hBgBrush);
      DeleteObject(hBgBrush);

      SendMessageW(hwnd, WM_PRINTCLIENT, (WPARAM)hdcMem, PRF_CLIENT);
      PaintCustomScrollbarOverlay(hwnd, hdcMem, &rcPaint);

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
    if (g_isHoveringIcon || g_isHoveringFolder || g_isHoveringTimestamp) {
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
    // 移除：鼠标在列表内移动时自动显示滚动条的逻辑。
    // 该逻辑会在悬浮图片等场景下反复显示/隐藏滚动条，造成图片闪烁
    // 且不符合"无滚动操作时不出现滚动条"的预期。
    // 滚动条仍会在以下场景显示：
    //   1) 鼠标悬浮在滚动条轨道区域时（见上方 g_isScrollbarHovered 分支）
    //   2) 滚轮滚动时（见 WM_MOUSEWHEEL）
    //   3) 拖拽滚动条或点击翻页按钮时

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

    // 保存图像悬浮旧状态（不在开头重置，避免检测瞬态失败导致闪烁）
    bool imageHoverFound = false;

    // 保存文件夹悬浮旧状态（不在开头重置，避免 GetFileAttributesW
    // 瞬态失败导致闪烁）
    bool wasHoveringFolder = g_isHoveringFolder;
    int oldFolderHoverIndex = g_hoverFolderIndex;
    bool folderHoverFound = false;

    // 时间戳悬浮状态
    bool timestampHoverFound = false;

    if (HIWORD(index) == 0) {
      index = LOWORD(index);

      // 悬浮选中：鼠标悬浮在列表项上即自动选中（可在 通用设置 中开关）
      // 快捷键选中后 400ms 内不抢占，避免鼠标微动覆盖键盘选中且刷新不一致
      // 批量编辑/按住左键拖动中不触发，避免破坏多选与拖拽交互
      if (g_isHoverSelectEnabled && !g_isBatchEditMode &&
          !(wParam & MK_LBUTTON) && index >= 0 &&
          index < (int)g_displayIndexMap.size() &&
          IsSelectableDisplayIndex(index) &&
          GetTickCount() - g_lastKeyboardSelectTick >= 400) {
        int curSel = (int)SendMessageW(hwnd, LB_GETCURSEL, 0, 0);
        if (curSel != index) {
          int topBefore = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
          // 禁用重绘避免 LB_SETCURSEL 同步绘制造成闪烁
          SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
          SendMessageW(hwnd, LB_SETCURSEL, index, 0);
          SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
          // 与 SelectListDisplayIndex 对齐：同步 topIndex/缓存脏标记，
          // 并滚动到可见，否则悬浮选中状态错乱导致刷新不及时
          int topAfter = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
          if (topAfter != topBefore) {
            g_listBoxTopIndex = topAfter;
            g_shortcutCacheDirty = true;
            // LB_SETCURSEL 内部自动滚动绕过了 ApplyListBoxTopIndex/
            // ListBoxProc(LB_SETTOPINDEX)，此处补齐翻页按钮与滚动条刷新，
            // 否则上下页按钮状态/页码不会随悬浮选中滚动及时更新
            g_currentPage = topAfter / ITEMS_PER_PAGE;
            InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
            InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
            ShowCustomScrollbar(hwnd);
            RefreshScrollbarIfChanged(hwnd);
          }
          EnsureListSelectionVisible(index);
          InvalidateRect(hwnd, NULL, FALSE);
        }
        // 悬浮到记录上即让主窗体失焦：焦点还给目标应用，主窗体保持
        // 置顶显示但不抢焦点。后续单击该记录即可直接粘贴（Ctrl+V 能
        // 可靠送达目标应用，无需"第一击失焦、第二击粘贴"两击交互）。
        //
        // 但搜索框正持有焦点（用户刚输入完搜索词）时，不能把焦点还给
        // 目标应用——否则空格预览、方向键等导航键会发往目标应用而非
        // SmartClip。此时仅把焦点从搜索框转移到主窗体，保持 SmartClip
        // 活跃，使 HandleMainNavigationKey 能处理空格/方向键。同时置
        // g_isNoActivateMode=true 阻止后续悬浮重复进入此分支；单击记录
        // 粘贴（WM_LBUTTONUP）或单击搜索框（WM_MOUSEACTIVATE）会自行
        // 复位该标志。
        HWND hwndMain = GetParent(hwnd);
        if (!hwndMain || !IsWindow(hwndMain))
          hwndMain = g_hwndMain;
        if (g_hwndSearchBox && GetFocus() == g_hwndSearchBox) {
          // 焦点从搜索框转移到列表框（子窗口能可靠接收焦点），
          // 使 HandleMainNavigationKey 能处理空格预览/方向键导航
          SetFocus(hwnd);
          g_isNoActivateMode = true;
        } else if (!g_isNoActivateMode) {
          g_isNoActivateMode = true;
          if (hwndMain && IsWindow(hwndMain)) {
            bool wasForeground = (GetForegroundWindow() == hwndMain);
            LONG_PTR ex = GetWindowLongPtrW(hwndMain, GWL_EXSTYLE);
            ex |= WS_EX_NOACTIVATE;
            SetWindowLongPtrW(hwndMain, GWL_EXSTYLE, ex);
            // 保持窗口显示且置顶，供单击到达；同时不抢焦点。
            // 尊重置顶设置：仅当用户开启「置顶」或窗口未最大化时才强制置顶，
            // 置顶关闭且窗口最大化时不置顶，避免非置顶窗口被顶到最上层。
            // （最大化窗口 RECT 几乎覆盖全屏，鼠标离开轮询无法检测离开，
            // 若强制 TOPMOST 会永久残留，只能靠"置顶→取消置顶"手动归位）
            if (g_isTopmost || !IsZoomed(hwndMain)) {
              SetWindowPos(hwndMain, HWND_TOPMOST, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                               SWP_SHOWWINDOW);
            } else {
              SetWindowPos(hwndMain, HWND_NOTOPMOST, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                               SWP_SHOWWINDOW);
            }
            if (wasForeground && g_previousActiveWindow &&
                IsWindow(g_previousActiveWindow))
              RestoreForegroundWindow(g_previousActiveWindow);
          }
        }
      }

      bool iconFound = false;
      if (index >= 0 && index < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[index];

        if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
          const ClipboardItem &item = g_history[actualIndex];

          // 获取列表项的矩形
          RECT rcItem;
          SendMessageW(hwnd, LB_GETITEMRECT, index, (LPARAM)&rcItem);

          // 计算图标区域（需要与绘制代码保持一致）
          // 只在项索引或位置变化时才创建字体和计算文本宽度，避免每次 MOUSEMOVE
          // 都重算
          static int s_cachedIconIndex = -1;
          static int s_cachedIconTop = -1;
          static RECT s_cachedIconRect = {};
          RECT rcIcon;
          if (index == s_cachedIconIndex && rcItem.top == s_cachedIconTop) {
            rcIcon = s_cachedIconRect;
          } else {
            HDC hdc = GetDC(hwnd);
            HFONT hHeaderFont =
                CreateFontW(MScale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
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

            int iconX = rcItem.left + MScale(10) + textSize.cx + MScale(4);
            int iconY = rcItem.top + MScale(2) + MScale(2);
            int iconSize = MScale(12);
            rcIcon = {iconX, iconY, iconX + iconSize, iconY + iconSize};
            s_cachedIconIndex = index;
            s_cachedIconTop = rcItem.top;
            s_cachedIconRect = rcIcon;
          }

          // 检查鼠标是否在图标区域内
          if (PtInRect(&rcIcon, pt) && !item.sourceAppPath.empty()) {
            iconFound = true;
            g_isHoveringIcon = true;
            g_hoverIconIndex = index;

            // 显示 Tooltip（筛选提示）
            if (g_hwndListBoxTooltip != NULL && !item.sourceApp.empty()) {
              std::wstring tipText = T(STR_TOOLTIP_FILTER_BY_APP);
              // 仅当 tooltip 文本变化时才更新，避免频繁激活导致闪烁
              if (g_listBoxTooltipText != tipText) {
                g_listBoxTooltipText = tipText;
                TOOLINFOW ti = {};
                ti.cbSize = TTTOOLINFOW_V1_SIZE;
                ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
                ti.hwnd = g_hwndListBox;
                ti.uId = 0;
                ti.lpszText = (LPWSTR)g_listBoxTooltipText.c_str();
                SendMessageW(g_hwndListBoxTooltip, TTM_UPDATETIPTEXTW, 0,
                             (LPARAM)&ti);

                POINT ptMouse;
                GetCursorPos(&ptMouse);
                POINT ptScreen = {ptMouse.x - MScale(20),
                                  ptMouse.y - MScale(25)};
                SendMessageW(g_hwndListBoxTooltip, TTM_TRACKPOSITION, 0,
                             MAKELPARAM(ptScreen.x, ptScreen.y));

                SendMessageW(g_hwndListBoxTooltip, TTM_TRACKACTIVATE, TRUE,
                             (LPARAM)&ti);
              } else {
                // 文本未变化，仅更新位置以跟随鼠标
                POINT ptMouse;
                GetCursorPos(&ptMouse);
                POINT ptScreen = {ptMouse.x - MScale(20),
                                  ptMouse.y - MScale(25)};
                SendMessageW(g_hwndListBoxTooltip, TTM_TRACKPOSITION, 0,
                             MAKELPARAM(ptScreen.x, ptScreen.y));
              }
            }
          }

          // 时间戳悬浮检测：鼠标在时间戳文本上时显示手形光标和筛选提示
          if (!iconFound) {
            int contentLeft = rcItem.left + MScale(10);
            int headerTop = rcItem.top + MScale(2);
            // s_cachedIconRect.left = contentLeft + textSize.cx + MScale(4)
            // 时间戳文本右边界 = contentLeft + textSize.cx =
            // s_cachedIconRect.left - MScale(4)
            int tsRight = s_cachedIconRect.left - MScale(4);
            if (tsRight < contentLeft)
              tsRight = contentLeft;
            RECT rcTimestamp = {contentLeft, headerTop, tsRight,
                                headerTop + MScale(18)};

            if (PtInRect(&rcTimestamp, pt) && item.timestamp.length() >= 10) {
              g_isHoveringTimestamp = true;
              g_hoverTimestampIndex = index;
              timestampHoverFound = true;

              // 显示 Tooltip（筛选提示）
              if (g_hwndListBoxTooltip != NULL) {
                std::wstring tipText = T(STR_TOOLTIP_FILTER_BY_DATE);
                if (g_listBoxTooltipText != tipText) {
                  g_listBoxTooltipText = tipText;
                  TOOLINFOW ti = {};
                  ti.cbSize = TTTOOLINFOW_V1_SIZE;
                  ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
                  ti.hwnd = g_hwndListBox;
                  ti.uId = 0;
                  ti.lpszText = (LPWSTR)g_listBoxTooltipText.c_str();
                  SendMessageW(g_hwndListBoxTooltip, TTM_UPDATETIPTEXTW, 0,
                               (LPARAM)&ti);

                  POINT ptMouse;
                  GetCursorPos(&ptMouse);
                  POINT ptScreen = {ptMouse.x - MScale(20),
                                    ptMouse.y - MScale(25)};
                  SendMessageW(g_hwndListBoxTooltip, TTM_TRACKPOSITION, 0,
                               MAKELPARAM(ptScreen.x, ptScreen.y));
                  SendMessageW(g_hwndListBoxTooltip, TTM_TRACKACTIVATE, TRUE,
                               (LPARAM)&ti);
                } else {
                  POINT ptMouse;
                  GetCursorPos(&ptMouse);
                  POINT ptScreen = {ptMouse.x - MScale(20),
                                    ptMouse.y - MScale(25)};
                  SendMessageW(g_hwndListBoxTooltip, TTM_TRACKPOSITION, 0,
                               MAKELPARAM(ptScreen.x, ptScreen.y));
                }
              }
            }
          }

          // 图片项悬浮检测：只要鼠标在 TYPE_IMAGE 项内，标记为悬浮状态
          // 不显示 tooltip（文件名已在列表项中显示），避免 tooltip 闪烁
          if (!iconFound && item.type == TYPE_IMAGE) {
            bool hasValidPath =
                !item.imageFilePath.empty() || !item.imageFileName.empty();
            if (hasValidPath) {
              imageHoverFound = true;
              g_isHoveringImage = true;
              g_hoverImageIndex = index;
            }
          }

          // 文件/文件夹名称悬浮检测（仅检测文字区域）
          // 仅对当前选中项启用悬浮动画：选中文件/文件夹类型后，
          // 鼠标悬浮在该选中项的文字区域上才显示对应动画与手形光标。
          // 同时检查 TYPE_FILE 和 TYPE_TEXT，因为文件路径可能作为文本复制
          int curSelForHover = (int)SendMessageW(hwnd, LB_GETCURSEL, 0, 0);
          if (!iconFound && !g_isHoveringImage && index == curSelForHover &&
              (item.type == TYPE_FILE || item.type == TYPE_TEXT)) {
            // 缓存文件属性结果，避免同一项重复 I/O
            static int s_cachedFileAttrIndex = -1;
            static DWORD s_cachedFileAttrs = INVALID_FILE_ATTRIBUTES;
            // 如果已经在悬浮当前项，保持状态（避免 GetFileAttributesW
            // 瞬态失败导致闪烁）
            if (g_isHoveringFolder && g_hoverFolderIndex == index) {
              // 滞回机制：仅当鼠标仍在该项实际文字区域内时保持悬浮状态。
              // 用户要求文件/文件夹类型只检测实际文字长度，不延伸到整行。
              bool isFolder = (s_cachedFileAttrs != INVALID_FILE_ATTRIBUTES &&
                               (s_cachedFileAttrs & FILE_ATTRIBUTE_DIRECTORY));
              // attrs 无效说明当前悬浮项是网址/IP（文件路径必定 attrs 有效）
              bool hoverIsUrlOrIp =
                  (s_cachedFileAttrs == INVALID_FILE_ATTRIBUTES);
              HoverTextGeom geomApprox =
                  GetHoverTextGeom(item, isFolder, hoverIsUrlOrIp);
              RECT rcTextApprox = CalcFolderTextRect(
                  hwnd, rcItem, geomApprox.text, geomApprox.iconOffset,
                  geomApprox.rightInset);
              if (PtInRect(&rcTextApprox, pt)) {
                folderHoverFound = true;
                SetCursor(LoadCursor(NULL, IDC_HAND));
              }
            } else {
              // 先用垂直范围预筛，避免鼠标在标题行等位置时仍触发磁盘 I/O
              if (pt.y >= rcItem.top + MScale(2) + MScale(20) &&
                  pt.y < rcItem.top + MScale(2) + MScale(20) + MScale(22)) {
                DWORD attrs;
                if (index == s_cachedFileAttrIndex) {
                  attrs = s_cachedFileAttrs;
                } else {
                  attrs = GetFileAttributesW(item.content.c_str());
                  s_cachedFileAttrIndex = index;
                  s_cachedFileAttrs = attrs;
                }
                if (attrs != INVALID_FILE_ATTRIBUTES) {
                  bool isFolder = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
                  // 文字区域：只检测实际文字宽度（文件名），而不是整行
                  HoverTextGeom geom = GetHoverTextGeom(item, isFolder, false);
                  RECT rcFolderText =
                      CalcFolderTextRect(hwnd, rcItem, geom.text,
                                         geom.iconOffset, geom.rightInset);
                  if (PtInRect(&rcFolderText, pt)) {
                    folderHoverFound = true;
                    g_isHoveringFolder = true;
                    g_hoverFolderIndex = index;
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    // 启动下划线动画。
                    // 若刚从其他文件夹项切换过来（wasHoveringFolder 为 true），
                    // 保持当前进度，避免重置为 0 导致视觉后滞；
                    // 仅首次进入悬浮时从 0 开始。
                    if (!g_folderUnderlineAnimating &&
                        g_folderUnderlineProgress < 1.0f) {
                      g_folderUnderlineAnimating = true;
                      if (!wasHoveringFolder) {
                        g_folderUnderlineProgress = 0.0f;
                      }
                      SetTimer(hwnd, ID_FOLDER_UNDERLINE_TIMER, 16, NULL);
                    }
                  }
                } else if (IsUrl(item.content) || IsIPv4Address(item.content)) {
                  // 网址/IP：悬浮动画与文件/文件夹一致
                  HoverTextGeom geomIp = GetHoverTextGeom(item, false, true);
                  RECT rcIpText =
                      CalcFolderTextRect(hwnd, rcItem, geomIp.text,
                                         geomIp.iconOffset, geomIp.rightInset);
                  if (PtInRect(&rcIpText, pt)) {
                    folderHoverFound = true;
                    g_isHoveringFolder = true;
                    g_hoverFolderIndex = index;
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    if (!g_folderUnderlineAnimating &&
                        g_folderUnderlineProgress < 1.0f) {
                      g_folderUnderlineAnimating = true;
                      if (!wasHoveringFolder) {
                        g_folderUnderlineProgress = 0.0f;
                      }
                      SetTimer(hwnd, ID_FOLDER_UNDERLINE_TIMER, 16, NULL);
                    }
                  }
                }
              }
            }
          }
        }
      }

      // 如果本轮未检测到图像悬浮，重置状态并隐藏 tooltip
      if (!imageHoverFound) {
        g_isHoveringImage = false;
        g_hoverImageIndex = -1;
      }

      // 如果没有找到图标悬浮且没有图像悬浮且没有时间戳悬浮，隐藏 tooltip
      if (!iconFound && !g_isHoveringImage && !timestampHoverFound) {
        g_isHoveringIcon = false;
        g_hoverIconIndex = -1;
        HideListBoxTrackingTooltip();
      }
      // 如果图标悬浮消失但时间戳悬浮存在，保持 tooltip（文本会自动切换）
      if (!iconFound && timestampHoverFound) {
        g_isHoveringIcon = false;
        g_hoverIconIndex = -1;
      }

      // 重置时间戳悬浮状态
      if (!timestampHoverFound) {
        g_isHoveringTimestamp = false;
        g_hoverTimestampIndex = -1;
      }
    } else {
      g_isHoveringIcon = false;
      g_hoverIconIndex = -1;
      g_isHoveringImage = false;
      g_hoverImageIndex = -1;
      g_isHoveringTimestamp = false;
      g_hoverTimestampIndex = -1;
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
    g_isHoveringImage = false;
    g_hoverImageIndex = -1;
    g_isHoveringTimestamp = false;
    g_hoverTimestampIndex = -1;

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
    g_dragOccurred = false;  // 重置拖拽标志
    g_dragSubItemIndex = -1; // 重置子行拖拽标志
    // 重置单击粘贴标记（滚动条/图标/时间戳等 return 分支均不触发粘贴）
    g_singleClickPasteIndex = -1;
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

      // ===== 快速筛选：检测点击应用图标或时间戳 =====
      if (index >= 0 && index < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[index];
        if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
          const ClipboardItem &item = g_history[actualIndex];
          RECT rcItem;
          SendMessageW(hwnd, LB_GETITEMRECT, index, (LPARAM)&rcItem);

          // 计算时间戳文本宽度和图标位置（与绘制代码一致）
          HDC hdc = GetDC(hwnd);
          HFONT hHeaderFont =
              CreateFontW(MScale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
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

          int contentLeft = rcItem.left + MScale(10);
          int headerTop = rcItem.top + MScale(2);

          // 时间戳文本区域
          RECT rcTimestamp = {contentLeft, headerTop, contentLeft + textSize.cx,
                              headerTop + MScale(18)};

          // 应用图标区域
          int iconX = contentLeft + textSize.cx + MScale(4);
          int iconY = headerTop + MScale(2);
          int iconSize = MScale(12);
          RECT rcIcon = {iconX, iconY, iconX + iconSize, iconY + iconSize};

          // 点击应用图标 → 按应用筛选
          if (PtInRect(&rcIcon, pt) && !item.sourceApp.empty()) {
            g_quickFilterApp = item.sourceApp;
            RefreshListBox();
            InvalidateRect(GetParent(hwnd), NULL, TRUE);
            InvalidateRect(g_hwndSearchBox, NULL, FALSE);
            return 0;
          }
          // 点击时间戳 → 按日期筛选
          if (PtInRect(&rcTimestamp, pt) && item.timestamp.length() >= 10) {
            g_quickFilterDate = item.timestamp.substr(0, 10);
            RefreshListBox();
            InvalidateRect(GetParent(hwnd), NULL, TRUE);
            InvalidateRect(g_hwndSearchBox, NULL, FALSE);
            return 0;
          }

          // ===== 多文件记录：点击下拉三角形 → 展开/收起 =====
          // 三角形视觉尺寸较小（约 14px），命中区域向左放宽 10px，
          // 便于用户点击（行内其余区域仍走选中/拖拽逻辑）
          RECT rcTri;
          bool hasTri = GetMultiFileTriangleRect(hwnd, index, rcTri);
          if (hasTri) {
            RECT rcHit = rcTri;
            rcHit.left -= MScale(10);
            if (PtInRect(&rcHit, pt)) {
              ToggleMultiFileExpanded(actualIndex);
              // 重建列表触发 WM_MEASUREITEM 重新计算行高（展开 n 行 / 收起 1
              // 行）
              RefreshListBoxPreservePosition();
              // 展开时确保头行可见（以当前行为基准向下展开，不向上滚动）
              if (IsMultiFileExpanded(actualIndex)) {
                EnsureItemFullyVisible(hwnd, index);
              }
              // 展开/收起后显示项数量变化，必须重建快捷键缓存
              ResetShortcutAssignment();
              ShowCustomScrollbar(hwnd);
              RefreshScrollbarIfChanged(hwnd);
              return 0;
            }
          }

          // ===== 文本选中复制：记录按下点与锚点 =====
          // 点击任意处先清除旧选中；点击文本内容区则准备拖选
          {
            if (g_textSelItem >= 0)
              ClearTextSelectionForClick(hwnd);
            if (!g_isBatchEditMode && item.type == TYPE_TEXT &&
                IsTextSelectableContent(item.content)) {
              g_textSelItem = index;
              g_textSelAnchor = TextSelCharFromPoint(hwnd, index, pt);
              g_textSelEnd = g_textSelAnchor;
              g_textSelDragging = false;
              g_textSelDownPt = pt;
              SetCapture(hwnd);
            }
          }
        }
      }

      // 悬浮选中后的单击粘贴记录：按下前该项已处于选中状态
      // （通常由悬浮选中产生），松开时点击位置仍在原记录上则直接粘贴。
      // 展开的多文件记录不触发单击粘贴（否则窗口会被隐藏，无法收起/拖拽子文件）。
      if (index >= 0 && index < (int)g_displayIndexMap.size()) {
        if (g_isHoverSelectEnabled && !g_isBatchEditMode &&
            (int)SendMessageW(hwnd, LB_GETCURSEL, 0, 0) == index) {
          int actIdx = g_displayIndexMap[index];
          bool isExpandedMulti = false;
          if (actIdx >= 0 && actIdx < (int)g_history.size()) {
            const ClipboardItem &it = g_history[actIdx];
            if (it.type == TYPE_FILE &&
                it.content.find(L'\n') != std::wstring::npos &&
                IsMultiFileExpanded(actIdx)) {
              isExpandedMulti = true;
            }
          }
          if (!isExpandedMulti)
            g_singleClickPasteIndex = index;
        }
      }

      // 原有拖拽逻辑
      if (index >= 0 && index < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[index];
        if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
          const ClipboardItem &item = g_history[actualIndex];
          // 文件类型（非文件夹）支持拖拽
          if (item.type == TYPE_FILE) {
            bool isMulti = item.content.find(L'\n') != std::wstring::npos;
            if (isMulti && IsMultiFileExpanded(actualIndex)) {
              // 展开的多文件记录（虚拟子项）：直接用 displaySubIndexMap
              // 确定子文件
              int subIdx = (index < (int)g_displaySubIndexMap.size())
                               ? g_displaySubIndexMap[index]
                               : -1;
              if (subIdx >= 0) {
                std::vector<std::wstring> paths;
                SplitMultiFilePaths(item.content, paths);
                if (subIdx < (int)paths.size()) {
                  DWORD attrs = GetFileAttributesW(paths[subIdx].c_str());
                  if (attrs != INVALID_FILE_ATTRIBUTES) {
                    g_dragStartPoint = pt;
                    g_dragItemIndex = index;
                    g_dragSubItemIndex = subIdx;
                  }
                }
              }
            } else {
              // 多文件记录（收起态）或单文件记录：检查第一个文件是否存在
              size_t nlPos = item.content.find(L'\n');
              std::wstring firstPath = (nlPos != std::wstring::npos)
                                           ? item.content.substr(0, nlPos)
                                           : item.content;
              DWORD attrs = GetFileAttributesW(firstPath.c_str());
              if (attrs != INVALID_FILE_ATTRIBUTES) {
                // 文件/文件夹（含多文件记录），支持整行拖拽
                g_dragStartPoint = pt;
                g_dragItemIndex = index;
                g_dragSubItemIndex = -1;
              }
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
            // 懒加载：尝试按需加载缩略图（启动时未预加载）
            if (!canDrag &&
                (EnsureItemImageLoaded(item) || !item.imageFileName.empty())) {
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

    // 非批量编辑模式下，显式选中被点击的项
    // 确保单击图片/文件也能进入选中状态
    if (!g_isBatchEditMode) {
      // 用 LB_GETITEMRECT 逐项命中测试，避免 LB_ITEMFROMPOINT
      // 对部分可见项的偏差
      int selIndex = -1;
      int topIndex = SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
      int count = SendMessageW(hwnd, LB_GETCOUNT, 0, 0);
      RECT rcClient;
      GetClientRect(hwnd, &rcClient);
      for (int i = topIndex; i < count; i++) {
        RECT rcItem;
        if (SendMessageW(hwnd, LB_GETITEMRECT, i, (LPARAM)&rcItem) != LB_ERR) {
          // 跳过完全在可视区域之下的项，防止命中未绘制的项
          if (rcItem.top >= rcClient.bottom)
            break;
          if (rcItem.bottom <= 0)
            continue;
          if (pt.y >= rcItem.top && pt.y < rcItem.bottom) {
            selIndex = i;
            break;
          }
        }
      }
      if (selIndex >= 0 && selIndex < (int)g_displayIndexMap.size()) {
        int oldTopBeforeSelect = g_listBoxTopIndex;
        // 禁用重绘避免 LB_SETCURSEL 同步绘制选中项（绕过双缓冲导致闪烁）
        SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
        SendMessageW(hwnd, LB_SETCURSEL, selIndex, 0);
        SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
        // LB_SETCURSEL 会将选中项滚动到可见区域，可能导致列表框顶部索引变化。
        int actualTop = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
        if (actualTop < 0)
          actualTop = 0;
        // 无论是否滚动，选中变化都需要刷新快捷键缓存
        g_shortcutCacheDirty = true;
        if (actualTop != oldTopBeforeSelect) {
          g_listBoxTopIndex = actualTop;
          g_currentPage = actualTop / ITEMS_PER_PAGE;
          InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
          InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
          ShowCustomScrollbar(hwnd);
          RefreshScrollbarIfChanged(hwnd);
        }
        // 异步重绘，避免阻塞
        InvalidateRect(hwnd, NULL, FALSE);
        // 通知父窗口选择已改变
        SendMessageW(GetParent(hwnd), WM_COMMAND,
                     MAKEWPARAM(ID_LISTBOX, LBN_SELCHANGE), (LPARAM)hwnd);
      }
    } else if (index >= 0 && index < (int)g_displayIndexMap.size()) {
      // 批量编辑模式：ListBoxProc 拦截了默认选择处理，LBN_SELCHANGE
      // 不会触发，需在此手动应用 Shift/Ctrl 选择（Ctrl+单击反选）
      bool isShiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      bool isCtrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      ApplyBatchSelectionFromDisplayIndex(index, isShiftPressed, isCtrlPressed);
      RedrawBatchSelectionUI();
    }

    // 仅在列表框未拥有焦点时设置焦点
    if (GetFocus() != hwnd) {
      SetFocus(hwnd);
    }
    return 0;
  }

  // 处理双击 - 直接发送 LBN_DBLCLK 给父窗口，确保双击预览能触发
  if (message == WM_LBUTTONDBLCLK) {
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    // 确保选中被双击的项
    int hitIndex =
        SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
    if (HIWORD(hitIndex) == 0) {
      int selIndex = LOWORD(hitIndex);
      if (selIndex >= 0 && selIndex < (int)g_displayIndexMap.size()) {
        // 禁用重绘避免 LB_SETCURSEL 同步绘制选中项（绕过双缓冲导致闪烁）
        SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
        SendMessageW(hwnd, LB_SETCURSEL, selIndex, 0);
        SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
      }
    }
    // 发送 LBN_DBLCLK 给父窗口
    SendMessageW(GetParent(hwnd), WM_COMMAND,
                 MAKEWPARAM(ID_LISTBOX, LBN_DBLCLK), (LPARAM)hwnd);
    return 0;
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
            size_t nlPos = item.content.find(L'\n');
            if (nlPos != std::wstring::npos) {
              // 多文件记录
              if (g_dragSubItemIndex >= 0 && IsMultiFileExpanded(actualIndex)) {
                // 展开态：逐个文件拖拽（单文件数据对象）
                std::vector<std::wstring> paths;
                SplitMultiFilePaths(item.content, paths);
                if (g_dragSubItemIndex < (int)paths.size()) {
                  std::wstring sub = paths[g_dragSubItemIndex];
                  DWORD attrs = GetFileAttributesW(sub.c_str());
                  if (attrs != INVALID_FILE_ATTRIBUTES) {
                    dragFilePath = sub;
                  }
                }
              } else {
                // 收起态：收集所有存在的文件路径用于多文件拖放
                std::vector<std::wstring> dragPaths;
                size_t start = 0;
                while (start <= item.content.size()) {
                  size_t end = item.content.find(L'\n', start);
                  if (end == std::wstring::npos)
                    end = item.content.size();
                  if (end > start) {
                    std::wstring p = item.content.substr(start, end - start);
                    DWORD attrs = GetFileAttributesW(p.c_str());
                    if (attrs != INVALID_FILE_ATTRIBUTES) {
                      dragPaths.push_back(p);
                    }
                  }
                  if (end == item.content.size())
                    break;
                  start = end + 1;
                }

                if (!dragPaths.empty()) {
                  // 创建多文件数据对象
                  IDataObject *pDataObject =
                      CreateMultiFileDataObject(dragPaths);
                  if (pDataObject) {
                    // 设置拖放图像（用第一个文件的图标）
                    SetDragImage(pDataObject, dragPaths[0], pt);
                    CDropSource *pDropSource = new CDropSource();
                    g_dragOccurred = true;
                    DWORD dwEffect = 0;
                    DoDragDrop(pDataObject, pDropSource,
                               DROPEFFECT_COPY | DROPEFFECT_MOVE, &dwEffect);
                    pDropSource->Release();
                    pDataObject->Release();
                  }
                  g_dragItemIndex = -1;
                  g_dragSubItemIndex = -1;
                  return 0;
                }
              }
            } else {
              // 单文件记录（文件或文件夹均可拖拽）
              std::wstring firstPath = item.content;
              DWORD attrs = GetFileAttributesW(firstPath.c_str());
              if (attrs != INVALID_FILE_ATTRIBUTES) {
                dragFilePath = firstPath;
              }
            }
          } else if (item.type == TYPE_IMAGE) {
            // 提取原始图片扩展名（优先从文件名获取，默认 png）
            std::wstring imgExt = L"png";
            if (!item.imageFilePath.empty()) {
              size_t dotPos = item.imageFilePath.rfind(L'.');
              if (dotPos != std::wstring::npos) {
                imgExt = item.imageFilePath.substr(dotPos + 1);
              }
            } else if (!item.imageFileName.empty()) {
              size_t dotPos = item.imageFileName.rfind(L'.');
              if (dotPos != std::wstring::npos) {
                imgExt = item.imageFileName.substr(dotPos + 1);
              }
            }

            // 提取原始文件名（用于临时文件保留原图文件名）
            std::wstring originalFileName;
            if (!item.imageFilePath.empty()) {
              size_t slashPos = item.imageFilePath.find_last_of(L"\\/");
              originalFileName = (slashPos != std::wstring::npos)
                                     ? item.imageFilePath.substr(slashPos + 1)
                                     : item.imageFilePath;
            } else if (!item.imageFileName.empty()) {
              originalFileName = item.imageFileName;
            }

            if (!item.imageFilePath.empty()) {
              dragFilePath = item.imageFilePath;
            } else if (!item.imageFileName.empty()) {
              dragFilePath = GetImagesPath() + L"\\" + item.imageFileName;
            }
            // 如果文件不存在但有内存图像数据，创建临时文件用于拖拽（保留原格式和原文件名）
            // 懒加载：按需加载缩略图数据（启动时未预加载）
            if (!dragFilePath.empty()) {
              DWORD attrs = GetFileAttributesW(dragFilePath.c_str());
              if (attrs == INVALID_FILE_ATTRIBUTES) {
                EnsureItemImageLoaded(item);
                if (!item.imageData.empty()) {
                  std::wstring tempPath = SaveImageToTempFile(
                      item.imageData, item.thumbWidth, item.thumbHeight, imgExt,
                      originalFileName);
                  if (!tempPath.empty()) {
                    dragFilePath = tempPath;
                    isTempFile = true;
                  } else {
                    dragFilePath.clear();
                  }
                }
              }
            } else {
              EnsureItemImageLoaded(item);
              if (!item.imageData.empty()) {
                dragFilePath = SaveImageToTempFile(
                    item.imageData, item.thumbWidth, item.thumbHeight, imgExt,
                    originalFileName);
                if (!dragFilePath.empty()) {
                  isTempFile = true;
                }
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
            g_dragSubItemIndex = -1;
            return 0;
          }
        }
      }
    }
  }

  // ===== 文本拖选：鼠标移动时更新选中范围 =====
  if (message == WM_MOUSEMOVE && (wParam & MK_LBUTTON) && g_textSelItem >= 0) {
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    if (!g_textSelDragging) {
      int dx = pt.x - g_textSelDownPt.x;
      int dy = pt.y - g_textSelDownPt.y;
      if (dx * dx + dy * dy >= 16) { // 4px 阈值后进入拖选
        g_textSelDragging = true;
        if (GetCapture() != hwnd)
          SetCapture(hwnd);
      }
    }
    if (g_textSelDragging) {
      int newEnd = TextSelCharFromPoint(hwnd, g_textSelItem, pt);
      if (newEnd != g_textSelEnd) {
        g_textSelEnd = newEnd;
        InvalidateTextSelItem(hwnd);
      }
    }
  }

  // WM_MOUSEMOVE 已在上方完整处理（悬浮检测 + 拖拽），
  // 不再传递给默认列表框过程：默认过程会在屏幕 DC 上直接重绘项，
  // 绕过 WM_PAINT 的双缓冲，导致选中项在悬浮文件路径项时闪烁
  if (message == WM_MOUSEMOVE) {
    return 0;
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
    // 文本拖选结束
    if (g_textSelItem >= 0) {
      if (g_textSelDragging) {
        // 区分"真正拖选"与"误触拖选"：松开点距按下点 <4px 视为普通单击，
        // 继续走下方单击粘贴逻辑；真正拖动出选中范围才保留文本选中供 Ctrl+C。
        int dxDrag = (GET_X_LPARAM(lParam)) - g_textSelDownPt.x;
        int dyDrag = (GET_Y_LPARAM(lParam)) - g_textSelDownPt.y;
        if (dxDrag * dxDrag + dyDrag * dyDrag >= 16) {
          // 真正拖选：保持选中范围（供 Ctrl+C 复制）
          g_textSelDragging = false;
          if (GetCapture() == hwnd)
            ReleaseCapture();
          return 0;
        }
        // 误触拖选：当作普通单击，清除待定选中后继续
        g_textSelDragging = false;
      }
      // 简单点击（未拖选）：清除待定选中，继续原有逻辑
      // 主动释放捕获时置位标志，避免 WM_CAPTURECHANGED 清掉单击粘贴标记
      g_releasingCaptureForClick = true;
      if (GetCapture() == hwnd)
        ReleaseCapture();
      g_releasingCaptureForClick = false;
      ClearTextSelectionForClick(hwnd);
    }
    g_dragItemIndex = -1;
    g_dragSubItemIndex = -1;

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
            } else if (attrs != INVALID_FILE_ATTRIBUTES) {
              // 文件：打开所在目录并选中
              std::wstring cmd = L"/select,\"" + item.content + L"\"";
              ShellExecuteW(NULL, NULL, L"explorer.exe", cmd.c_str(), NULL,
                            SW_SHOWNORMAL);
            } else if (IsUrl(item.content)) {
              // 网址：用默认浏览器打开
              std::wstring url = item.content;
              // www. 开头需要补上 http:// 前缀
              if (_wcsnicmp(url.c_str(), L"www.", 4) == 0)
                url = L"http://" + url;
              ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL,
                            SW_SHOWNORMAL);
            } else if (IsIPv4Address(item.content)) {
              // IP 地址：用 ping 命令打开
              std::wstring cmd = L"/k ping " + item.content;
              ShellExecuteW(NULL, L"open", L"cmd.exe", cmd.c_str(), NULL,
                            SW_SHOWNORMAL);
            }
          }
        }
      }
    }

    // 悬浮选中后的单击粘贴：松开时点击位置仍在按下时已选中的记录上，
    // 且未发生拖拽。文件夹/文件/网址/IP 点击已在上方打开，此处跳过。
    if (g_singleClickPasteIndex >= 0 &&
        g_singleClickPasteIndex < (int)g_displayIndexMap.size()) {
      int pasteIndex = g_singleClickPasteIndex;
      g_singleClickPasteIndex = -1;
      if (!g_dragOccurred) {
        POINT ptUp = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int upHit =
            SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(ptUp.x, ptUp.y));
        if (HIWORD(upHit) == 0 && LOWORD(upHit) == pasteIndex) {
          // 文件夹/文件/网址/IP：点击文字区域时上方已 ShellExecute 打开，
          // 不重复粘贴；点击非文字区域（g_isHoveringFolder=false）走单击粘贴，
          // 与快捷键行为保持一致。
          bool isOpenedAbove =
              g_isHoveringFolder && g_hoverFolderIndex == pasteIndex;
          if (!isOpenedAbove) {
            int actualIndex = g_displayIndexMap[pasteIndex];
            if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
              const ClipboardItem &item = g_history[actualIndex];
              // 与快捷键行为保持一致：文本/文件/图片均支持单击粘贴
              // （图片双击仍为预览，单击粘贴为图片路径）
              if (item.type == TYPE_TEXT || item.type == TYPE_FILE ||
                  item.type == TYPE_IMAGE) {
                if (SetClipboardFromItem(item)) {
                  HWND hwndMain = GetParent(hwnd);
                  if (!hwndMain || !IsWindow(hwndMain))
                    hwndMain = g_hwndMain;
                  if (hwndMain && IsWindow(hwndMain)) {
                    ExitNoActivateMode(hwndMain);
                  }
                  Sleep(100);
                  RestoreFocusAndPaste(hwndMain);
                  if (g_isNotificationEnabled) {
                    ShowTrayBalloon(hwndMain, T(STR_TRAY_HINT),
                                    T(STR_TRAY_PASTED));
                  }
                }
              }
            }
          }
        }
      }
      return 0;
    }
  }

  if (message == WM_CAPTURECHANGED) {
    if (g_isScrollbarDragging) {
      g_isScrollbarDragging = false;
      StartScrollbarHideTimer(hwnd);
      RefreshScrollbarIfChanged(hwnd);
    }
    // 文本拖选被系统打断时：保留选中范围（供 Ctrl+C），结束拖选状态
    if (g_textSelDragging) {
      g_textSelDragging = false;
    }
    // 捕获丢失（切换窗口/系统弹窗）时清除单击粘贴标记，避免残留误触发。
    // UP 处理中主动 ReleaseCapture 释放时不清除（否则文本项单击粘贴被吞掉）。
    if (!g_releasingCaptureForClick)
      g_singleClickPasteIndex = -1;
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
  g_imgNoExistIcon = LoadImageFromResource(IDB_NOEXIST_ICON);
  g_imgTextIcon = LoadImageFromResource(IDB_TEXT_ICON);
  g_imgNetIcon = LoadImageFromResource(IDB_NET_ICON);
  g_imgMailIcon = LoadImageFromResource(IDB_MAIL_ICON);
  g_imgFileIcon = LoadImageFromResource(IDB_FOLDER_ICON);
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
  if (g_imgNoExistIcon) {
    delete g_imgNoExistIcon;
    g_imgNoExistIcon = NULL;
  }
  if (g_imgTextIcon) {
    delete g_imgTextIcon;
    g_imgTextIcon = NULL;
  }
  if (g_imgNetIcon) {
    delete g_imgNetIcon;
    g_imgNetIcon = NULL;
  }
  if (g_imgMailIcon) {
    delete g_imgMailIcon;
    g_imgMailIcon = NULL;
  }
  if (g_imgFileIcon) {
    delete g_imgFileIcon;
    g_imgFileIcon = NULL;
  }
}

// 搜索框子类化窗口过程 - 处理渐变光标
LRESULT CALLBACK SearchBoxProc(HWND hwnd, UINT message, WPARAM wParam,
                               LPARAM lParam) {
  auto getClearButtonRect = [&](RECT *rcClear) -> bool {
    if (!rcClear)
      return false;
    int textLen = GetWindowTextLengthW(hwnd);
    if (textLen <= 0 && !IsQuickFilterActive())
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

    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

    // 快速筛选激活时：追踪药丸悬浮
    if (IsQuickFilterActive()) {
      int newHover = -1;
      bool newCloseHover = false;
      for (int i = 0; i < g_pillCount; i++) {
        if (PtInRect(&g_pills[i].rect, pt)) {
          newHover = i;
          newCloseHover = PtInRect(&g_pills[i].closeRect, pt) != FALSE;
          break;
        }
      }
      // 同时追踪清除按钮悬浮
      RECT rcClear = {};
      bool clearHover = getClearButtonRect(&rcClear) && PtInRect(&rcClear, pt);
      if (newHover != g_hoveredPill || newCloseHover != g_pillCloseHovered ||
          clearHover != g_isSearchClearBtnHover) {
        g_hoveredPill = newHover;
        g_pillCloseHovered = newCloseHover;
        g_isSearchClearBtnHover = clearHover;
        InvalidateRect(hwnd, NULL, FALSE);
      }
      break;
    }

    // 正常模式：追踪清除按钮悬浮
    RECT rcClear = {};
    bool hover = getClearButtonRect(&rcClear) && PtInRect(&rcClear, pt);
    if (hover != g_isSearchClearBtnHover) {
      g_isSearchClearBtnHover = hover;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    break;
  }
  case WM_MOUSELEAVE:
    if (g_isSearchClearBtnHover || g_hoveredPill >= 0) {
      g_isSearchClearBtnHover = false;
      g_hoveredPill = -1;
      g_pillCloseHovered = false;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    break;
  case WM_SETCURSOR: {
    POINT pt = {};
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    // 药丸关闭按钮悬浮 → 手形光标
    if (IsQuickFilterActive()) {
      for (int i = 0; i < g_pillCount; i++) {
        if (PtInRect(&g_pills[i].closeRect, pt)) {
          SetCursor(LoadCursor(NULL, IDC_HAND));
          return TRUE;
        }
      }
    }
    RECT rcClear = {};
    if (getClearButtonRect(&rcClear) && PtInRect(&rcClear, pt)) {
      SetCursor(LoadCursor(NULL, IDC_HAND));
      return TRUE;
    }
    // 药丸激活时，搜索框其他区域显示默认箭头（非文本光标）
    if (IsQuickFilterActive()) {
      SetCursor(LoadCursor(NULL, IDC_ARROW));
      return TRUE;
    }
    break;
  }
  case WM_LBUTTONDOWN: {
    // 点击搜索框时退出不抢焦点模式，允许正常输入
    if (g_isNoActivateMode) {
      ExitNoActivateMode(g_hwndMain);
      SetForegroundWindow(g_hwndMain);
    }
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

    // 快速筛选激活时：处理药丸关闭按钮和清除按钮
    if (IsQuickFilterActive()) {
      // 检查药丸关闭按钮
      for (int i = 0; i < g_pillCount; i++) {
        if (PtInRect(&g_pills[i].closeRect, pt)) {
          if (g_pills[i].type == 1)
            g_quickFilterDate.clear();
          else if (g_pills[i].type == 2)
            g_quickFilterApp.clear();
          else if (g_pills[i].type == 3)
            g_currentFilterTagId = 0;
          // 清除搜索框文本与关键词，避免关闭药丸后输入框残留
          SetWindowTextW(hwnd, L"");
          g_searchKeyword.clear();
          RefreshListBox();
          g_pillCloseHovered = false;
          g_hoveredPill = -1;
          // 若所有药丸已关闭，恢复光标定时器以显示输入光标
          if (!IsQuickFilterActive()) {
            g_caretVisible = true;
            g_caretShowState = true;
            SetTimer(hwnd, ID_CARET_TIMER, 500, NULL);
          }
          UpdateSearchClearButtonVisibility();
          InvalidateRect(hwnd, NULL, FALSE);
          return 0;
        }
      }
      // 检查清除按钮 → 清除所有（搜索文本 + 快速筛选）
      RECT rcClear = {};
      if (getClearButtonRect(&rcClear) && PtInRect(&rcClear, pt)) {
        ClearQuickFilter();
        SetWindowTextW(hwnd, L"");
        PerformSearch(g_hwndMain);
        SetFocus(g_hwndMain);
        g_isSearchClearBtnHover = false;
        g_hoveredPill = -1;
        g_pillCloseHovered = false;
        UpdateSearchClearButtonVisibility();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
      }
      // 其他点击：不做任何操作（不设光标位置）
      return 0;
    }

    // 正常模式：处理清除按钮
    RECT rcClear = {};
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
    // 快速筛选激活时：ESC 清除筛选，其他键吞掉
    if (IsQuickFilterActive()) {
      if (wParam == VK_ESCAPE) {
        ClearQuickFilter();
        // 清除搜索框文本与关键词，避免输入框残留
        SetWindowTextW(hwnd, L"");
        g_searchKeyword.clear();
        RefreshListBox();
        g_hoveredPill = -1;
        g_pillCloseHovered = false;
        // 所有药丸已关闭，恢复光标定时器
        g_caretVisible = true;
        g_caretShowState = true;
        SetTimer(hwnd, ID_CARET_TIMER, 500, NULL);
        UpdateSearchClearButtonVisibility();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
      }
      return 0;
    }
    if (wParam == VK_ESCAPE) {
      int textLen = GetWindowTextLengthW(hwnd);
      if (textLen > 0) {
        // 有文字时：清空输入文本
        SetWindowTextW(hwnd, L"");
        PerformSearch(g_hwndMain);
        UpdateSearchClearButtonVisibility();
        InvalidateRect(hwnd, NULL, TRUE);
      } else {
        // 无文字时：失焦到主窗口，启用 vim/方向键导航
        SetFocus(g_hwndMain);
      }
      return 0;
    }
    break;
  }
  case WM_LBUTTONDBLCLK: {
    // 快速筛选激活时不处理双击
    if (IsQuickFilterActive())
      return 0;
    // 双击全选文本
    SendMessageW(hwnd, EM_SETSEL, 0, -1);
    return 0;
  }
  case WM_CHAR: {
    // 快速筛选激活时吞掉所有字符输入
    if (IsQuickFilterActive())
      return 0;
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
    if (IsQuickFilterActive()) {
      // 药丸显示时：无光标，不启动定时器
      g_caretVisible = false;
    } else {
      g_caretVisible = true;
      g_caretShowState = true;
      SetTimer(hwnd, ID_CARET_TIMER, 500, NULL);
    }
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
      // 药丸显示时不应有光标定时器，安全清理
      if (IsQuickFilterActive()) {
        KillTimer(hwnd, ID_CARET_TIMER);
        return 0;
      }
      // 仅在光标可见性切换时重绘，避免频繁 InvalidateRect 导致闪烁
      g_caretShowState = !g_caretShowState;
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }
    break;
  }
  case WM_ERASEBKGND: {
    // 药丸模式下由 WM_PAINT 双缓冲完整绘制，阻止默认擦除避免闪烁
    if (IsQuickFilterActive())
      return 1;
    break;
  }
  case WM_PAINT: {
    if (IsQuickFilterActive()) {
      // ===== 快速筛选激活：双缓冲绘制药丸（消除闪烁） =====
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);

      RECT rcClient;
      GetClientRect(hwnd, &rcClient);

      // 内存DC双缓冲
      HDC memDC = CreateCompatibleDC(hdc);
      HBITMAP memBitmap =
          CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
      HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

      // 填充搜索框背景
      COLORREF bgColor = GetWhiteColor();
      HBRUSH hBgBrush = CreateSolidBrush(bgColor);
      FillRect(memDC, &rcClient, hBgBrush);
      DeleteObject(hBgBrush);

      // 计算药丸布局
      UpdateSearchPillLayout(hwnd, memDC);

      // 创建药丸字体
      HFONT hPillFont = CreateFontW(
          MScale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
          DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
      HFONT hPillOldFont = (HFONT)SelectObject(memDC, hPillFont);
      SetBkMode(memDC, TRANSPARENT);

      Gdiplus::Graphics graphics(memDC);
      graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
      graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

      int closeBtnSize = MScale(12);
      int padX = MScale(8);
      int gap = MScale(4);

      for (int i = 0; i < g_pillCount; i++) {
        RECT pr = g_pills[i].rect;
        int pillX = pr.left;
        int pillY = pr.top;
        int pillW = pr.right - pr.left;
        int pillH = pr.bottom - pr.top;
        bool closeHovered = (i == g_hoveredPill && g_pillCloseHovered);

        // 药丸底色：日期/应用用默认蓝；分类药丸（3）用分类色块颜色。
        // 悬浮关闭按钮时各分量 7/8 加深作为交互反馈（保持不透明清晰显示）。
        COLORREF pillBase =
            (g_pills[i].type == 3) ? g_pills[i].color : RGB(90, 156, 235);
        Gdiplus::Color pillColor(255, GetRValue(pillBase), GetGValue(pillBase),
                                 GetBValue(pillBase));
        if (closeHovered) {
          pillColor = Gdiplus::Color(255, GetRValue(pillBase) * 7 / 8,
                                     GetGValue(pillBase) * 7 / 8,
                                     GetBValue(pillBase) * 7 / 8);
        }
        Gdiplus::SolidBrush brush(pillColor);
        int radius = pillH / 2;
        Gdiplus::GraphicsPath path;
        path.AddArc(pillX, pillY, radius * 2, radius * 2, 180, 90);
        path.AddArc(pillX + pillW - radius * 2, pillY, radius * 2, radius * 2,
                    270, 90);
        path.AddArc(pillX + pillW - radius * 2, pillY + pillH - radius * 2,
                    radius * 2, radius * 2, 0, 90);
        path.AddArc(pillX, pillY + pillH - radius * 2, radius * 2, radius * 2,
                    90, 90);
        path.AddLine(pillX, pillY + pillH - radius * 2, pillX,
                     pillY + radius * 2);
        graphics.FillPath(&brush, &path);

        // 绘制药丸文字（白色）
        SetTextColor(memDC, RGB(255, 255, 255));
        RECT rcText = {pillX + padX, pillY,
                       pillX + pillW - padX - closeBtnSize - gap,
                       pillY + pillH};
        DrawTextW(memDC, g_pills[i].text.c_str(), -1, &rcText,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        // 始终显示关闭按钮(高亮)，悬浮时改变样式作为交互反馈
        {
          RECT cr = g_pills[i].closeRect;
          int cx = (cr.left + cr.right) / 2;
          int cy = (cr.top + cr.bottom) / 2;
          int r = closeBtnSize / 2;

          if (closeHovered) {
            // 悬浮：白色圆形背景 + 药丸底色 ×
            Gdiplus::SolidBrush closeBg(Gdiplus::Color(255, 255, 255, 255));
            graphics.FillEllipse(&closeBg, cx - r, cy - r, r * 2, r * 2);
            Gdiplus::Pen xPen(Gdiplus::Color(255, GetRValue(pillBase),
                                             GetGValue(pillBase),
                                             GetBValue(pillBase)),
                              2.0f);
            int xr = closeBtnSize / 3;
            graphics.DrawLine(&xPen, cx - xr, cy - xr, cx + xr, cy + xr);
            graphics.DrawLine(&xPen, cx + xr, cy - xr, cx - xr, cy + xr);
          } else {
            // 默认：半透明白色 × (始终可见的高亮状态)
            Gdiplus::Pen xPen(Gdiplus::Color(220, 255, 255, 255), 1.6f);
            int xr = closeBtnSize / 3;
            graphics.DrawLine(&xPen, cx - xr, cy - xr, cx + xr, cy + xr);
            graphics.DrawLine(&xPen, cx + xr, cy - xr, cx - xr, cy + xr);
          }
        }
      }

      SelectObject(memDC, hPillOldFont);
      DeleteObject(hPillFont);

      // 绘制清除按钮（有文本或快速筛选激活时）
      RECT rcClear = {};
      if (getClearButtonRect(&rcClear)) {
        Gdiplus::Graphics clearGraphics(memDC);
        clearGraphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        COLORREF fill = g_isDarkMode ? RGB(118, 122, 132) : RGB(210, 214, 220);
        if (g_isSearchClearBtnHover) {
          fill = g_isDarkMode ? RGB(148, 152, 162) : RGB(188, 194, 202);
        }
        Gdiplus::SolidBrush fillBrush(
            Gdiplus::Color(g_isDarkMode ? 235 : 210, GetRValue(fill),
                           GetGValue(fill), GetBValue(fill)));
        clearGraphics.FillEllipse(&fillBrush, (INT)rcClear.left,
                                  (INT)rcClear.top,
                                  (INT)(rcClear.right - rcClear.left),
                                  (INT)(rcClear.bottom - rcClear.top));
        Gdiplus::Pen xPen(g_isDarkMode ? Gdiplus::Color(255, 24, 26, 30)
                                       : Gdiplus::Color(220, 88, 96, 108),
                          1.6f);
        int inset = 6;
        clearGraphics.DrawLine(
            &xPen, (INT)(rcClear.left + inset), (INT)(rcClear.top + inset),
            (INT)(rcClear.right - inset), (INT)(rcClear.bottom - inset));
        clearGraphics.DrawLine(
            &xPen, (INT)(rcClear.right - inset), (INT)(rcClear.top + inset),
            (INT)(rcClear.left + inset), (INT)(rcClear.bottom - inset));
      }

      // BitBlt 到屏幕
      BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, memDC, 0, 0, SRCCOPY);

      SelectObject(memDC, oldBitmap);
      DeleteObject(memBitmap);
      DeleteDC(memDC);

      EndPaint(hwnd, &ps);
      return 0;
    }

    // ===== 正常模式：默认绘制 + 自定义叠加 =====
    LRESULT result =
        CallWindowProcW(g_oldSearchBoxProc, hwnd, message, wParam, lParam);

    HDC hdc = GetDC(hwnd);
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

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

      // 计算光标X位置 - 使用 GetTextExtentPoint32W 测量文本宽度
      int caretX = textLeft; // 默认起始位置
      int caretLen = GetWindowTextLengthW(hwnd);

      if (caretLen > 0 && charIndex > 0) {
        HDC hdcTemp = GetDC(hwnd);
        HFONT hFont = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        HFONT hOldFont = (HFONT)SelectObject(hdcTemp, hFont);

        wchar_t text[256] = {0};
        GetWindowTextW(hwnd, text, 256);

        SIZE textSize;
        int len = (charIndex > caretLen) ? caretLen : charIndex;
        GetTextExtentPoint32W(hdcTemp, text, len, &textSize);
        caretX = textLeft + textSize.cx;

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

    // 绘制清除按钮
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

// 设置主窗口任务栏样式：forceTaskbarButton=true 强制显示任务栏按钮，
// 否则按 g_isTaskbarVisible 决定。当不在任务栏显示时使用
// WS_EX_TOOLWINDOW（不在任务栏/Alt+Tab 出现），否则用 WS_EX_APPWINDOW。
static void SetMainWindowTaskbarStyle(HWND hwnd, bool forceTaskbarButton) {
  if (!hwnd)
    return;
  LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  ex &= ~WS_EX_NOACTIVATE;
  if (forceTaskbarButton || g_isTaskbarVisible) {
    ex |= WS_EX_APPWINDOW;
    ex &= ~WS_EX_TOOLWINDOW;
  } else {
    ex |= WS_EX_TOOLWINDOW;
    ex &= ~WS_EX_APPWINDOW;
  }
  SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
  SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                   SWP_NOOWNERZORDER | SWP_NOACTIVATE);
}

// 最小化时的处理：若不需要任务栏按钮，则直接隐藏窗口（SW_HIDE），
// 避免无任务栏按钮的窗口最小化时显示为屏幕左下角的标题栏条。
// 返回 true 表示已处理（调用方应跳过默认最小化），false 表示交给系统。
static bool HideInsteadOfMinimizeWhenNoTaskbar(HWND hwnd) {
  CloseTagPopup();
  ExitNoActivateMode(hwnd);
  if (!g_isTaskbarVisible) {
    SetMainWindowTaskbarStyle(hwnd, false);
    ShowWindow(hwnd, SW_HIDE);
    return true;
  }
  SetMainWindowTaskbarStyle(hwnd, true);
  return false;
}

// ==================== Dropshelf 式拖拽呼出（文件中转站） ====================
// 监测其他进程发起的鼠标拖拽（如资源管理器里拖动文件/文件夹）：
// 按住左键"左右晃动两下"（水平方向反转 2 次，每段至少 30px）时呼出主窗体；
// 且源线程的鼠标捕获窗口与被按下窗口不同根（OLE 拖放的捕获窗口是源进程
// 的隐藏顶层窗口；框选/拖标题栏等普通操作的捕获窗口与被按下窗口同根，
// 据此区分），用户可把拖拽中的文件放到窗体上存入历史，实现"中转站"。
// 拖拽结束未落到窗体上则自动隐藏并还原窗体原位置。
#define ID_DRAG_SHELF_TIMER 212  // 轮询定时器
#define ID_DRAG_SHELF_SETTLE 213 // 松手后等待落地回调的兜底定时器
#define DRAG_SHELF_SHAKE_PX 30   // 晃动每段幅度下限（逻辑像素）
#define DRAG_SHELF_SHAKE_TICK 5  // 方向锁定/反转确认的回退阈值
#define DRAG_SHELF_SHAKE_TIMES 2 // 需完成的晃动段数（左右各一下=2段）

// 非静态：CDropTarget::Drop（drag_drop.cpp）落地时通过消息清除
bool g_dragShelfSummoned = false;
static bool g_dragShelfTracking = false; // 正在跟踪一次按住左键的拖动
static DWORD g_dragShelfSrcThread = 0;   // 按下时窗口所属线程
static HWND g_dragShelfSrcHwnd = NULL;   // 按下时的窗口
static RECT g_dragShelfPrevRect = {};    // 呼出前窗体位置（取消时还原）
static bool g_dragShelfMoved = false;    // 呼出时是否移动过窗体
// 呼出蒙版前窗体是否已经可见：可见时不移动位置、结束时也不隐藏窗体
static bool g_dragShelfWasVisible = false;
// 呼出前窗体处于最小化：IsWindowVisible 对最小化窗口仍返回 TRUE，
// 需单独记录，结束时回到最小化并还原呼出前的位置
static bool g_dragShelfWasIconic = false;
static WINDOWPLACEMENT g_dragShelfPrevPlacement = {};

// 水平晃动手势状态
static int g_dragShelfShakeDir = 0; // 当前段水平方向（-1 左 / +1 右，0 未锁定）
static int g_dragShelfShakeAnchorX = 0; // 当前段起点 X（屏幕坐标）
static int g_dragShelfShakeSegMax = 0;  // 当前段沿方向的最大位移（正数）
static int g_dragShelfShakeCount = 0;   // 已完成的晃动段数

// 蒙版状态下隐藏的子控件（结束时恢复显示）
static std::vector<HWND> g_dragShelfHiddenChildren;

static BOOL CALLBACK DragShelfHideChildProc(HWND hChild, LPARAM lParam) {
  (void)lParam;
  if (IsWindowVisible(hChild)) {
    ShowWindow(hChild, SW_HIDE);
    g_dragShelfHiddenChildren.push_back(hChild);
  }
  return TRUE;
}

// 恢复蒙版状态下隐藏的子控件
static void RestoreDragShelfChildren(HWND hwnd) {
  for (HWND hChild : g_dragShelfHiddenChildren) {
    if (hChild && IsWindow(hChild))
      ShowWindow(hChild, SW_SHOW);
  }
  g_dragShelfHiddenChildren.clear();
  if (hwnd && IsWindow(hwnd))
    InvalidateRect(hwnd, NULL, TRUE);
}

// 拖拽期间强制窗口进入置顶层。后台进程对"已可见窗口"的纯 z-order
// 变更会被系统静默忽略（SetWindowPos 返回成功但 WS_EX_TOPMOST 未置位），
// 必须多级强化：
//   1) 常规 SetWindowPos(HWND_TOPMOST)；
//   2) AttachThreadInput 挂接前台（拖拽源）线程，借用其 z-order 权限；
//   3) 隐藏→以 TOPMOST 重新显示——显示隐藏窗口时允许同时指定 z-order
//      不受后台限制；同一条消息内连续调用，合成器不会呈现中间隐藏
//      状态，无闪烁。
static bool ForceWindowTopmost(HWND hwnd) {
  // 1) 常规调用
  SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST)
    return true;
  // 2) 挂接前台线程再试
  HWND fg = GetForegroundWindow();
  DWORD fgTid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
  DWORD myTid = GetCurrentThreadId();
  bool attached = (fgTid != 0 && fgTid != myTid &&
                   AttachThreadInput(myTid, fgTid, TRUE)) != false;
  SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  if (attached)
    AttachThreadInput(myTid, fgTid, FALSE);
  if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST)
    return true;
  // 3) 隐藏→以 TOPMOST 重新显示
  SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW);
  SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  return (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

// 在光标附近呼出主窗体（不激活、不抢焦点，避免打断进行中的拖拽）
// 窗体已可见时：位置保持不变，仅展示"放置文件"蒙版
static void SummonDragShelf(HWND hwnd, const POINT &pt) {
  g_dragShelfSummoned = true;
  g_dragShelfMoved = false;
  // 最小化(IsIconic)的窗口 IsWindowVisible 仍为 TRUE 但视觉不可见：
  // 按未可见处理，走呼出路径（恢复显示 + 展示蒙版）
  g_dragShelfWasVisible = (IsWindowVisible(hwnd) && !IsIconic(hwnd)) != FALSE;
  g_dragShelfWasIconic = false;
  GetWindowRect(hwnd, &g_dragShelfPrevRect);
  if (IsIconic(hwnd)) {
    g_dragShelfWasIconic = true;
    g_dragShelfPrevPlacement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hwnd, &g_dragShelfPrevPlacement);
    // 恢复显示但不激活（激活会取消进行中的 OLE 拖拽）；
    // 最大化后最小化的窗口会恢复为最大化（后续 IsZoomed 判断准确）
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    // 重取真实尺寸（最小化时 GetWindowRect 返回 -32000 哨兵位置）
    GetWindowRect(hwnd, &g_dragShelfPrevRect);
  }
  if (!g_dragShelfWasVisible) {
    int x = 0, y = 0;
    if (!IsZoomed(hwnd)) {
      const int w = g_dragShelfPrevRect.right - g_dragShelfPrevRect.left;
      const int h = g_dragShelfPrevRect.bottom - g_dragShelfPrevRect.top;
      const int off = 40;
      x = pt.x + off;
      y = pt.y + off;
      HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
      MONITORINFO mi = {};
      mi.cbSize = sizeof(MONITORINFO);
      GetMonitorInfoW(mon, &mi);
      if (x + w > mi.rcWork.right)
        x = pt.x - w - off;
      if (x < mi.rcWork.left)
        x = mi.rcWork.left;
      if (y + h > mi.rcWork.bottom)
        y = pt.y - h - off;
      if (y < mi.rcWork.top)
        y = mi.rcWork.top;
      if (x != g_dragShelfPrevRect.left || y != g_dragShelfPrevRect.top)
        g_dragShelfMoved = true;
    }
    // 拖拽期间临时置顶：拖拽源窗口（如资源管理器）可能全屏/前台，
    // 其他应用（如豆包）的拖拽响应浮层也多为置顶窗口；非置顶呼出
    // 会被压在它们下方，蒙版不可见、看似未触发。结束后按置顶开关
    // 归一化（见 NormalizeDragShelfZOrder），不会残留置顶状态。
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW |
                     (g_dragShelfMoved ? 0 : SWP_NOMOVE));
  } else {
    // 窗体已可见：位置保持不变，仅临时置顶确保蒙版浮在拖拽源之上。
    // 已可见窗口的 z-order 变更受后台限制（见 ForceWindowTopmost 注释），
    // 必须走多级强化而不是裸 SetWindowPos
    ForceWindowTopmost(hwnd);
  }
  // 隐藏子控件，展示"放置文件"蒙版（WM_PAINT 绘制）
  g_dragShelfHiddenChildren.clear();
  EnumChildWindows(hwnd, DragShelfHideChildProc, 0);
  InvalidateRect(hwnd, NULL, TRUE);
}

// 中转站结束后按置顶开关归一化 z-order，摘除拖拽期间的临时置顶；
// 与 ExitNoActivateMode 同一套逻辑：摘除置顶必须用 HWND_NOTOPMOST
// （HWND_TOP 只提到当前层顶部，置顶层窗口仍留在置顶层）
static void NormalizeDragShelfZOrder(HWND hwnd) {
  LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  bool isTopmostNow = (ex & WS_EX_TOPMOST) != 0;
  if (g_isTopmost != isTopmostNow) {
    SetWindowPos(hwnd, g_isTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
  }
}

// 隐藏呼出的窗体，可选还原呼出前位置
// 呼出前窗体已可见：仅撤下蒙版，不隐藏窗体、不还原位置
static void DismissDragShelf(HWND hwnd, bool restorePos) {
  g_dragShelfSummoned = false;
  RestoreDragShelfChildren(hwnd);
  if (!g_dragShelfWasVisible) {
    if (g_dragShelfWasIconic) {
      // 呼出前是最小化：回到最小化（不激活），还原呼出前的位置
      g_dragShelfPrevPlacement.showCmd = SW_SHOWMINNOACTIVE;
      SetWindowPlacement(hwnd, &g_dragShelfPrevPlacement);
    } else {
      ShowWindow(hwnd, SW_HIDE);
      if (restorePos && g_dragShelfMoved) {
        SetWindowPos(hwnd, NULL, g_dragShelfPrevRect.left,
                     g_dragShelfPrevRect.top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
    }
  }
  // 按置顶开关归一化 z-order（摘除拖拽期间的临时置顶）
  NormalizeDragShelfZOrder(hwnd);
  g_dragShelfMoved = false;
  g_dragShelfWasVisible = false;
  g_dragShelfWasIconic = false;
}

// 水平晃动手势跟踪：段 = 一次方向反转到下一次反转之间的移动，
// 每段幅度 >= DRAG_SHELF_SHAKE_PX 才计入；反转需回落 TICK 确认。
// 完成 SHAKE_TIMES 段（左右各晃一下）即视为有呼出意图。
static void TrackDragShelfShake(const POINT &pt) {
  const int segMin = MScale(DRAG_SHELF_SHAKE_PX);
  const int tick = MScale(DRAG_SHELF_SHAKE_TICK);

  if (g_dragShelfShakeDir == 0) {
    // 初始方向锁定：超过小阈值后以当前位置为段起点
    if (pt.x <= g_dragShelfShakeAnchorX - tick) {
      g_dragShelfShakeDir = -1;
      g_dragShelfShakeAnchorX = pt.x;
    } else if (pt.x >= g_dragShelfShakeAnchorX + tick) {
      g_dragShelfShakeDir = +1;
      g_dragShelfShakeAnchorX = pt.x;
    }
    return;
  }

  const int dx = pt.x - g_dragShelfShakeAnchorX;
  const int along = dx * g_dragShelfShakeDir; // 沿当前方向的位移
  if (along > g_dragShelfShakeSegMax)
    g_dragShelfShakeSegMax = along;

  // 反转确认：段幅度达标且已向反方向回落超过 tick
  if (g_dragShelfShakeSegMax >= segMin &&
      along <= g_dragShelfShakeSegMax - tick) {
    g_dragShelfShakeCount++;
    // 新段起点 = 上一段最远点，方向反转
    g_dragShelfShakeAnchorX += g_dragShelfShakeDir * g_dragShelfShakeSegMax;
    g_dragShelfShakeDir = -g_dragShelfShakeDir;
    g_dragShelfShakeSegMax = 0;
  }
}

static void HandleDragShelfPoll(HWND hwnd) {
  const bool lbtnDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

  // 自愈看门狗：呼出蒙版期间（左键仍按住=拖拽进行中），若窗口被
  // 其他代码路径拉出置顶层或隐藏/最小化，立即拉回，保证蒙版始终
  // 浮在拖拽源之上。此时本进程仍是后台，直接 SetWindowPos 会被静默
  // 忽略，必须走 ForceWindowTopmost 多级强化
  if (g_dragShelfSummoned && lbtnDown) {
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_TOPMOST)) {
      ForceWindowTopmost(hwnd);
    }
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
      if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
      SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
      if (!(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST))
        ForceWindowTopmost(hwnd);
    }
  }

  // 窗体被其他路径（关闭/快捷键/托盘）隐藏时立即撤下蒙版：
  // 恢复子控件与 z-order，避免下次显示时控件仍处于隐藏状态
  // （仅在左键已松开时判定——按住期间由上面的看门狗负责拉回）
  if (g_dragShelfSummoned && !lbtnDown && !IsWindowVisible(hwnd)) {
    g_dragShelfTracking = false;
    g_dragShelfSrcThread = 0;
    DismissDragShelf(hwnd, true);
    return;
  }

  if (!lbtnDown) {
    if (g_dragShelfTracking) {
      g_dragShelfTracking = false;
      g_dragShelfSrcThread = 0;
      if (g_dragShelfSummoned) {
        POINT pt = {};
        GetCursorPos(&pt);
        RECT rc = {};
        GetWindowRect(hwnd, &rc);
        bool inWin = PtInRect(&rc, pt) != FALSE;
        if (inWin) {
          // 松手位置在窗体上：等待 CDropTarget::Drop 落地回调；
          // 兜底定时器处理"拖拽在窗体上被 ESC 取消"等无落地的情况
          SetTimer(hwnd, ID_DRAG_SHELF_SETTLE, 400, NULL);
        } else {
          DismissDragShelf(hwnd, true);
        }
      }
    }
    return;
  }

  POINT pt = {};
  GetCursorPos(&pt);
  if (!g_dragShelfTracking) {
    g_dragShelfTracking = true;
    g_dragShelfShakeDir = 0;
    g_dragShelfShakeAnchorX = pt.x;
    g_dragShelfShakeSegMax = 0;
    g_dragShelfShakeCount = 0;
    g_dragShelfSrcHwnd = WindowFromPoint(pt);
    g_dragShelfSrcThread = 0;
    if (g_dragShelfSrcHwnd && IsWindow(g_dragShelfSrcHwnd)) {
      // 忽略从本程序窗口发起的拖拽（如把记录拖出去、拖动窗体本身）
      HWND root = GetAncestor(g_dragShelfSrcHwnd, GA_ROOT);
      DWORD pid = 0;
      GetWindowThreadProcessId(root, &pid);
      if (root != hwnd && pid != GetCurrentProcessId())
        g_dragShelfSrcThread =
            GetWindowThreadProcessId(g_dragShelfSrcHwnd, NULL);
    }
    return;
  }

  // 本次拖拽已处理过则不再触发（窗体已可见时也允许呼出蒙版）
  if (!g_dragShelfSrcThread || g_dragShelfSummoned)
    return;

  // 左右晃动两下（每段 >= 30px）才认为有呼出意图
  TrackDragShelfShake(pt);
  if (g_dragShelfShakeCount < DRAG_SHELF_SHAKE_TIMES)
    return;

  // 晃动达标：源线程有鼠标捕获且捕获窗口与被按下窗口不同根 →
  // OLE 拖放进行中；框选/拖动标题栏/滚动条等普通捕获与被按下窗口同根
  GUITHREADINFO gti = {};
  gti.cbSize = sizeof(GUITHREADINFO);
  if (!GetGUIThreadInfo(g_dragShelfSrcThread, &gti) || !gti.hwndCapture)
    return;
  if (GetAncestor(gti.hwndCapture, GA_ROOT) ==
      GetAncestor(g_dragShelfSrcHwnd, GA_ROOT))
    return;
  SummonDragShelf(hwnd, pt);
}

// 窗口过程
// 前置声明：从嵌入的 PNG 资源加载图标（MSIX 兼容），定义在
// RegisterWindowClass 之前
static HICON LoadAppIconFromResource();
// 前置声明：用户协议对话框（定义在文件末尾），托盘菜单"查看用户协议"使用
static bool ShowAgreementDialog(HINSTANCE hInstance);

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam,
                         LPARAM lParam) {
  switch (message) {
  case WM_USER + 0x1000: {
    // 延迟聚焦搜索框（呼出窗口后）
    SetFocus(g_hwndSearchBox);
    SendMessageW(g_hwndSearchBox, EM_SETSEL, 0, -1);
    return 0;
  }
  case WM_USER + 0x2000: {
    // 文件中转站落地回调（CDropTarget::Drop 发出）
    // wParam: 0=窗体本就可见时的普通拖入，1=拖拽呼出后成功落地，
    //         2=拖拽呼出但数据未被接受（隐藏并还原位置）
    g_dragShelfSummoned = false;
    if (wParam == 1) {
      // 落地成功：恢复子控件并切到文件页展示新记录
      RestoreDragShelfChildren(hwnd);
      // 按置顶开关归一化 z-order（摘除拖拽期间的临时置顶）
      NormalizeDragShelfZOrder(hwnd);
      if (g_currentTab != 3)
        SwitchMainPanel(hwnd, 3, true);
      else
        InvalidateRect(hwnd, NULL, TRUE);
    } else if (wParam == 2) {
      DismissDragShelf(hwnd, true);
    }
    return 0;
  }
  case WM_ACTIVATE: {
    // 窗体失焦时立即隐藏滚动条（借鉴 PRO 的即时状态重置）
    if (LOWORD(wParam) == WA_INACTIVE && g_hwndListBox) {
      KillTimer(g_hwndListBox, ID_SCROLLBAR_HIDE_TIMER);
      g_scrollbarVisible = false;
      g_isScrollbarHovered = false;
      g_isScrollbarDragging = false;
      RefreshScrollbarIfChanged(g_hwndListBox);
    }
    break;
  }
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

      // 标题栏区域
      if (pt.y < MainTitlebarHeight()) {
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
    g_mainUiDpi = GetSmartClipUiDpi(hwnd);

    // 设置窗口图标：按当前 DPI 显式请求大小，避免高 DPI 下 LoadIconW
    // 缩放模糊。
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    // 从嵌入的 PNG 资源加载图标（MSIX 兼容），失败时回退到嵌入式 ICO
    HICON hIconBig = LoadAppIconFromResource();
    if (hIconBig == NULL) {
      hIconBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1),
                                   IMAGE_ICON, MScale(32), MScale(32),
                                   LR_DEFAULTCOLOR | LR_SHARED);
    }
    if (hIconBig) {
      SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    }
    HICON hIconSm = LoadAppIconFromResource();
    if (hIconSm == NULL) {
      hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1),
                                  IMAGE_ICON, MScale(16), MScale(16),
                                  LR_DEFAULTCOLOR | LR_SHARED);
    }
    if (hIconSm) {
      SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);
    }

    // 先加载自定义数据目录配置，确保后续 LoadHotkeySettings/LoadHistory
    // 使用正确的目录路径
    LoadCustomDataDir();

    // 加载快捷键设置（依赖 g_customDataDir，必须在 LoadCustomDataDir 之后）
    LoadHotkeySettings();
    // 加载后立即保存一次，将旧格式（6行）迁移为新格式（14行）
    SaveHotkeySettings();
    if (g_isTopmost) {
      SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
      HWND hTopmostBtn = GetDlgItem(hwnd, ID_TOPMOST_BUTTON);
      if (hTopmostBtn)
        SetWindowTextW(hTopmostBtn, L"取消置顶");
      if (g_hwndTitleTopmost)
        InvalidateRect(g_hwndTitleTopmost, NULL, TRUE);
    }

    // 加载外部语言文件（扫描 lang/ 目录）
    LoadExternalLanguages();

    // 应用任务栏显示设置
    ApplyTaskbarVisibility(hwnd);

    // 加载粘贴次数统计
    LoadPasteCount();

    // 不再创建主菜单

    // 注册快捷键，如果默认快捷键冲突则禁用
    // 托盘快捷键总开关关闭时不注册任何快捷键
    if (g_allHotkeysEnabled) {
      if (!RegisterHotkey(hwnd)) {
        // 首次创建阶段可能因窗口尚未稳定而短暂失败，延迟到首次显示后重试。
        g_hotkeyRegisterPendingRetry = g_isHotkeyEnabled;
      }
      if (g_isQuickPasteEnabled) {
        RegisterQuickPasteHotkeys(hwnd);
      }
      if (g_isFavoriteHotkeyEnabled) {
        RegisterFavoriteHotkeys(hwnd);
      }
    }

    // 创建搜索栏（使用ES_MULTILINE以支持EM_SETRECT垂直居中）
    g_hwndSearchBox = CreateWindowExW(
        0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_MULTILINE,
        0, 0, 0, 0, hwnd, (HMENU)ID_SEARCH_BOX, GetModuleHandleW(NULL), NULL);

    // 占位符文本将在SearchBoxProc中自绘

    // 设置搜索框字体（比UI字体大3px）
    HFONT hSearchFont = CreateFontW(
        MScale(g_fontSize + 3), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_fontName.c_str());
    SendMessageW(g_hwndSearchBox, WM_SETFONT, (WPARAM)hSearchFont, TRUE);

    // 设置搜索框左边距
    SendMessageW(g_hwndSearchBox, EM_SETMARGINS, EC_LEFTMARGIN,
                 MAKELPARAM(1, 0));

    // 设置UI控件字体
    HFONT hUIFont = CreateFontW(
        MScale(g_fontSize), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_fontName.c_str());

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
        MScale(g_fontSize + 4), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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

    // 子类化列表框以处理自绘滚动条
    g_oldListBoxProc = (WNDPROC)SetWindowLongPtrW(g_hwndListBox, GWLP_WNDPROC,
                                                  (LONG_PTR)ListBoxProc);
    HideNativeListBoxScrollbar(g_hwndListBox);

    // 注册主窗口为拖放目标（用于显示拖拽图像）
    g_pDropTarget = new CDropTarget();
    RegisterDragDrop(hwnd, g_pDropTarget);

    // Dropshelf 式拖拽呼出轮询（文件中转站）
    SetTimer(hwnd, ID_DRAG_SHELF_TIMER, 40, NULL);

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
        0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0,
        MScale(46), MainTitlebarHeight(), hwnd, (HMENU)ID_TITLEBAR_TOPMOST,
        GetModuleHandleW(NULL), NULL);
    g_oldTitleTopmostProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndTitleTopmost, GWLP_WNDPROC, (LONG_PTR)TitleTopmostBtnProc);

    g_hwndTitleMinimize = CreateWindowExW(
        0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0,
        MScale(46), MainTitlebarHeight(), hwnd, (HMENU)ID_TITLEBAR_MINIMIZE,
        GetModuleHandleW(NULL), NULL);
    g_oldTitleMinimizeProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndTitleMinimize, GWLP_WNDPROC, (LONG_PTR)TitleMinimizeBtnProc);

    g_hwndTitleMaximize = CreateWindowExW(
        0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0,
        MScale(46), MainTitlebarHeight(), hwnd, (HMENU)ID_TITLEBAR_MAXIMIZE,
        GetModuleHandleW(NULL), NULL);
    g_oldTitleMaximizeProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndTitleMaximize, GWLP_WNDPROC, (LONG_PTR)TitleMaximizeBtnProc);

    g_hwndTitleClose =
        CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                        0, 0, MScale(46), MainTitlebarHeight(), hwnd,
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

    LoadTags(); // 加载标签列表
    LoadHistory();
    UpdateListBox();

    // 强制重新计算所有列表项的高度
    if (g_hwndListBox) {
      int itemCount = SendMessageW(g_hwndListBox, LB_GETCOUNT, 0, 0);
      for (int i = 0; i < itemCount; i++) {
        SendMessageW(g_hwndListBox, LB_SETITEMHEIGHT, i, 0);
      }
      InvalidateRect(g_hwndListBox, NULL, FALSE);
      UpdateWindow(g_hwndListBox);
    }

    AddClipboardFormatListener(hwnd);
    AddTrayIcon(hwnd);

    // 延迟刷新，确保窗口完全创建后再计算高度
    SetTimer(hwnd, 2, 200, NULL); // 增加到200ms

    break;
  }
  case WM_SYSCOMMAND: {
    // 允许系统默认处理（最小化/恢复），任务栏单击即可切换窗口可见性
    // 注意：不要拦截 SC_MINIMIZE 用 SW_HIDE，否则任务栏需要点两下才能恢复
    if ((wParam & 0xFFF0) == SC_MINIMIZE) {
      if (HideInsteadOfMinimizeWhenNoTaskbar(hwnd))
        return 0;
    } else if ((wParam & 0xFFF0) == SC_MAXIMIZE) {
      CloseTagPopup();
      // 最大化前归一化 z-order，确保非置顶状态下最大化后不会意外置顶
      // （清除悬浮置顶残留的 WS_EX_TOPMOST）
      ExitNoActivateMode(hwnd);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
  }
  case WM_SHOWWINDOW: {
    // 窗口显示时刷新列表项高度
    if (wParam == TRUE && g_hwndListBox) {
      int itemCount = SendMessageW(g_hwndListBox, LB_GETCOUNT, 0, 0);
      for (int i = 0; i < itemCount; i++) {
        SendMessageW(g_hwndListBox, LB_SETITEMHEIGHT, i, 0);
      }
      InvalidateRect(g_hwndListBox, NULL, FALSE);
    }
    // 首次显示时确保快捷键已注册
    static bool s_firstShow = true;
    if (s_firstShow) {
      s_firstShow = false;
      if (g_allHotkeysEnabled && g_isHotkeyEnabled) {
        if (!RegisterHotkey(hwnd)) {
          if (g_hotkeyRegisterPendingRetry) {
            // 注册失败只在内存中禁用，不覆盖已保存的设置
            // 这样下次启动还能尝试注册
            g_isHotkeyEnabled = false;
          }
        } else {
          g_hotkeyRegisterPendingRetry = false;
        }
      }
      if (g_allHotkeysEnabled && g_isQuickPasteEnabled) {
        RegisterQuickPasteHotkeys(hwnd);
      }
    }
    // 每次窗口显示时重新注册剪贴板监听与全局热键：
    // 1) AddClipboardFormatListener
    // 在某些场景（被其他程序抢占、睡眠/锁屏恢复）
    //    会偶现失效，重新注册可恢复监听；
    // 2) 若用户设置了快捷键，重新尝试注册可修复偶发的热键失效。
    // RemoveClipboardFormatListener 即便未注册也安全返回，故先移除再添加。
    if (wParam == TRUE) {
      RemoveClipboardFormatListener(hwnd);
      AddClipboardFormatListener(hwnd);
      // RegisterAllHotkeys 内部对已注册的热键会先 RegisterHotKey
      // 失败再 Unregister+Register，不会破坏正在生效的热键
      RegisterAllHotkeys(hwnd);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
  }
  case WM_MOUSEACTIVATE: {
    // 不抢焦点模式下（悬浮选中后已失焦）：
    //  - 点击列表项区域：保持不抢焦点（返回 MA_NOACTIVATE），让
    //    单击记录直接触发粘贴，焦点始终在目标应用；
    //  - 点击其他区域（搜索框/按钮等）：退出不抢焦点模式，激活窗口
    //    以便正常输入与操作。
    if (g_isNoActivateMode) {
      POINT ptM = {};
      GetCursorPos(&ptM);
      if (g_hwndListBox && IsWindow(g_hwndListBox)) {
        ScreenToClient(g_hwndListBox, &ptM);
        LRESULT hit = SendMessageW(g_hwndListBox, LB_ITEMFROMPOINT, 0,
                                   MAKELPARAM(ptM.x, ptM.y));
        if (HIWORD(hit) == 0)
          return MA_NOACTIVATE;
      }
      ExitNoActivateMode(hwnd);
      // 返回 MA_ACTIVATE 以激活窗口并接收键盘输入
      return MA_ACTIVATE;
    }
    // 窗口已是前台时，返回 MA_NOACTIVATE 避免重复激活导致闪烁
    // 但仍处理鼠标点击（MA_NOACTIVATE 不阻止点击传递）
    if (GetForegroundWindow() == hwnd) {
      // 搜索框持有焦点时点击列表项：返回 MA_ACTIVATE 让列表框获得
      // 键盘焦点，使空格预览/方向键等导航键可用（否则焦点会留在
      // 搜索框，空格被输入到搜索框而非触发预览）
      if (g_hwndSearchBox && GetFocus() == g_hwndSearchBox && g_hwndListBox &&
          IsWindow(g_hwndListBox)) {
        POINT ptM = {};
        GetCursorPos(&ptM);
        ScreenToClient(g_hwndListBox, &ptM);
        LRESULT hit = SendMessageW(g_hwndListBox, LB_ITEMFROMPOINT, 0,
                                   MAKELPARAM(ptM.x, ptM.y));
        if (HIWORD(hit) == 0)
          return MA_ACTIVATE;
      }
      return MA_NOACTIVATE;
    }
    return MA_ACTIVATE;
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
    // 窗口恢复时移除临时添加的 WS_CAPTION（最小化时添加的）
    if (wParam == SIZE_RESTORED) {
      LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
      if (style & WS_CAPTION) {
        style &= ~WS_CAPTION;
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOOWNERZORDER);
      }
    }

    // 最小化时跳过布局调整，避免子窗口尺寸异常导致最小化显示问题
    if (wParam == SIZE_MINIMIZED) {
      return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    int clientWidth = clientRect.right - clientRect.left;
    int clientHeight = clientRect.bottom - clientRect.top;

    // 标题栏按钮位置（从右到左）
    g_mainUiDpi = GetSmartClipUiDpi(hwnd);
    const int titleBtnWidth = MScale(46);
    MoveWindow(g_hwndTitleClose, clientWidth - titleBtnWidth, 0, titleBtnWidth,
               MainTitlebarHeight(), TRUE);
    MoveWindow(g_hwndTitleMaximize, clientWidth - titleBtnWidth * 2, 0,
               titleBtnWidth, MainTitlebarHeight(), TRUE);
    MoveWindow(g_hwndTitleMinimize, clientWidth - titleBtnWidth * 3, 0,
               titleBtnWidth, MainTitlebarHeight(), TRUE);
    MoveWindow(g_hwndTitleTopmost, clientWidth - titleBtnWidth * 4, 0,
               titleBtnWidth, MainTitlebarHeight(), TRUE);

    // 边距
    const int margin = MScale(10);
    // 内容区域起始Y（标题栏下方）
    const int contentTop = MainTitlebarHeight();
    // 搜索栏高度（包含边框）
    const int searchHeight = MScale(33);
    // 标签页高度
    const int tabHeight = MScale(30);

    // 调整搜索栏（留出边框空间）
    const int borderPadding = MScale(5);
    const int searchX = margin + borderPadding;
    const int searchY = contentTop + margin + borderPadding;
    const int searchW = clientWidth - margin * 2 - borderPadding * 2;
    const int searchH = searchHeight - borderPadding * 2;
    MoveWindow(g_hwndSearchBox, searchX, searchY, searchW, searchH, TRUE);

    const int clearBtnSize = MScale(18);
    const int clearBtnRightInset = MScale(8);
    const int clearBtnX = searchX + searchW - clearBtnSize - clearBtnRightInset;
    const int clearBtnY = searchY + (searchH - clearBtnSize) / 2;
    MoveWindow(g_hwndSearchClearBtn, clearBtnX, clearBtnY, clearBtnSize,
               clearBtnSize, TRUE);

    // 设置搜索框文本区域以实现垂直居中
    {
      RECT rcEdit;
      GetClientRect(g_hwndSearchBox, &rcEdit);
      int editHeight = rcEdit.bottom - rcEdit.top;
      int fontHeight = MScale(g_fontSize + 3);
      int topMargin = (editHeight - fontHeight) / 2;
      if (topMargin < 0)
        topMargin = 0;
      rcEdit.left = MScale(4); // 与光标初始位置偏移一致
      rcEdit.top = topMargin;
      rcEdit.right -= (clearBtnSize + clearBtnRightInset + MScale(8));
      rcEdit.bottom = rcEdit.top + fontHeight + MScale(4);
      SendMessageW(g_hwndSearchBox, EM_SETRECT, 0, (LPARAM)&rcEdit);
      SendMessageW(
          g_hwndSearchBox, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
          MAKELONG(MScale(4), clearBtnSize + clearBtnRightInset + MScale(8)));
    }
    UpdateSearchClearButtonVisibility();

    // 调整筛选按钮位置（5个按钮，总宽度与列表框对齐）
    const int filterBtnSpacing = MScale(4);
    const int iconBtnSize = MScale(32); // 图标按钮大小
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
      // 重置自定义滚动条状态：窗口尺寸变化后，NeedsCustomScrollbar() 的结果
      // 和滑块位置都可能改变，旧状态会导致点击记录重绘时滚动条被截断。
      // 必须在 InvalidateRect 之前重置，确保重绘使用最新状态。
      KillTimer(g_hwndListBox, ID_SCROLLBAR_HIDE_TIMER);
      g_scrollbarVisible = false;
      g_isScrollbarHovered = false;
      g_isScrollbarDragging = false;
      SetRectEmpty(&g_lastThumbRect);
      g_lastThumbValid = false;
      g_lastThumbVisible = false;
      g_lastThumbHovered = false;
      g_lastThumbDragging = false;

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
    LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
    // 设置窗口最小尺寸
    lpMMI->ptMinTrackSize.x = MScale(600); // 最小宽度
    lpMMI->ptMinTrackSize.y = MScale(694); // 最小高度
    // 最大化时约束到工作区域（排除任务栏），避免遮挡任务栏
    RECT rcWork;
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0)) {
      lpMMI->ptMaxPosition.x = rcWork.left;
      lpMMI->ptMaxPosition.y = rcWork.top;
      lpMMI->ptMaxSize.x = rcWork.right - rcWork.left;
      lpMMI->ptMaxSize.y = rcWork.bottom - rcWork.top;
    }
    return 0;
  }
  case WM_MEASUREITEM: {
    LPMEASUREITEMSTRUCT lpMIS = (LPMEASUREITEMSTRUCT)lParam;
    if (lpMIS->CtlID == ID_LISTBOX) {
      // 动态获取列表框宽度
      RECT rcListBox;
      GetClientRect(g_hwndListBox, &rcListBox);
      int listBoxWidth =
          rcListBox.right - rcListBox.left - MScale(20); // 减去左右边距
      if (listBoxWidth < MScale(100))
        listBoxWidth = MScale(560); // 初始化时的默认值

      // 获取列表项对应的实际数据
      if (lpMIS->itemID != (UINT)-1 &&
          lpMIS->itemID < g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[lpMIS->itemID];
        if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
          const ClipboardItem &item = g_history[actualIndex];

          if (item.type == TYPE_IMAGE) {
            // 检查图片文件是否存在（仅当没有内存图片数据时才检查，避免 I/O
            // 导致高度抖动）
            bool imageFileExists = true;
            if (item.imageData.empty() && !item.imageFilePath.empty()) {
              DWORD attrs = GetFileAttributesW(item.imageFilePath.c_str());
              imageFileExists = (attrs != INVALID_FILE_ATTRIBUTES);
            }

            if (!imageFileExists || g_imagePreviewQuality == PREVIEW_OFF) {
              // 图片文件不存在：使用一行高度
              lpMIS->itemHeight = MScale(57);
            } else {
              // 图片类型：计算缩放后的高度
              int availableWidth = listBoxWidth - MScale(20); // 减去左右边距
              float scale = (float)availableWidth / item.imageWidth;

              // 限制最大显示高度为150像素
              int maxImageHeight = MScale(150);
              if (scale * item.imageHeight > maxImageHeight) {
                scale = (float)maxImageHeight / item.imageHeight;
              }

              int displayHeight = (int)(item.imageHeight * scale);

              // 标题(25) + 图片高度 + 尺寸信息(20) + 底部边距(10)
              lpMIS->itemHeight =
                  MScale(25) + displayHeight + MScale(20) + MScale(10);
            }
          } else if (item.type == TYPE_FILE &&
                     item.content.find(L'\n') != std::wstring::npos &&
                     IsMultiFileExpanded(actualIndex)) {
            // 展开的多文件记录已拆成虚拟子项，每行与普通记录一致（57px）
            lpMIS->itemHeight = MScale(57);
          } else {
            // 文本或文件类型：固定高度，一行显示
            // 顶部边距(2) + 标题(20) + 一行文本(22) + 底部边距(13)
            lpMIS->itemHeight = MScale(57);
          }
        } else {
          lpMIS->itemHeight = MScale(87);
        }
      } else {
        lpMIS->itemHeight = MScale(87);
      }
    }
    return TRUE;
  }
  case WM_DRAWITEM: {
    LPDRAWITEMSTRUCT lpDIS = (LPDRAWITEMSTRUCT)lParam;

    // 处理筛选按钮绘制
    if (lpDIS->CtlID >= ID_FILTER_ALL && lpDIS->CtlID <= ID_FILTER_FAVORITE) {
      // 焦点变化时不重绘（筛选按钮不显示焦点框），避免 SetFocus 导致闪烁
      if (lpDIS->itemAction == ODA_FOCUS)
        return TRUE;

      HDC hdc = lpDIS->hDC;
      RECT rc = lpDIS->rcItem;

      // 判断是否选中
      int filterIndex = lpDIS->CtlID - ID_FILTER_ALL;
      bool isSelected = (filterIndex == g_currentTab);

      COLORREF bgColor =
          isSelected ? GetThemeSurfaceColor() : GetThemeWindowBgColor();
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
          MScale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
          DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");

      // 计算图标和文本的总宽度
      SIZE iconSize, textSize;
      SelectObject(hdc, hIconFont);
      GetTextExtentPoint32W(hdc, icon, 1, &iconSize);

      HFONT hFont = (HFONT)SendMessageW(lpDIS->hwndItem, WM_GETFONT, 0, 0);
      SelectObject(hdc, hFont);
      GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &textSize);

      int totalWidth = iconSize.cx + MScale(4) + textSize.cx; // 图标文字间距
      int startX = rc.left + (rc.right - rc.left - totalWidth) / 2;
      int centerY = rc.top + (rc.bottom - rc.top) / 2;

      // 绘制图标
      SelectObject(hdc, hIconFont);
      TextOutW(hdc, startX, centerY - iconSize.cy / 2, icon, 1);

      // 绘制文本
      SelectObject(hdc, hFont);
      TextOutW(hdc, startX + iconSize.cx + MScale(4), centerY - textSize.cy / 2,
               text, (int)wcslen(text));

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
        HFONT hIconFont =
            CreateFontW(MScale(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
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
          MScale(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
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
          MScale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
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
      int inset = MScale(6);
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

      // 删除滑出动画：偏移内容绘制位置
      bool isDeleteSliding = g_deleteSlideAnimating &&
                             (int)lpDIS->itemID == g_deleteSlideDisplayIndex;

      // 填充背景（排除自定义滚动条区域，避免覆盖滚动条）
      // 注意：滑出动画期间也填充整项背景，以彻底清除旧选中框残留
      // （WM_PAINT 已使用双缓冲，整项填充不会引入闪烁）
      RECT rcFill = rcItem;
      rcFill.right -= GetCustomScrollbarReservedWidth();
      HBRUSH hBrush = CreateSolidBrush(bgColor);
      FillRect(hdc, &rcFill, hBrush);
      DeleteObject(hBrush);

      // 删除滑出动画：偏移内容绘制位置
      int savedDC = 0;
      if (isDeleteSliding) {
        savedDC = SaveDC(hdc);
        HRGN hClipRgn = CreateRectRgnIndirect(&rcFill);
        SelectClipRgn(hdc, hClipRgn);
        DeleteObject(hClipRgn);
        SetViewportOrgEx(hdc, g_deleteSlideOffset, 0, NULL);
      }

      // ===== 虚拟子项（展开态多文件记录的各文件行）单独绘制 =====
      // 每行与普通记录一致：57px 高，头部(20px)+内容(22px)+分隔线，
      // 仅头行显示来源应用图标，末行显示上三角（收起按钮）
      int subIdx = (lpDIS->itemID < g_displaySubIndexMap.size())
                       ? g_displaySubIndexMap[lpDIS->itemID]
                       : -1;
      if (subIdx >= 0 && lpDIS->itemID < g_displayIndexMap.size()) {
        int actIdx = g_displayIndexMap[lpDIS->itemID];
        if (actIdx >= 0 && actIdx < (int)g_history.size()) {
          const ClipboardItem &mfItem = g_history[actIdx];
          std::vector<std::wstring> paths;
          SplitMultiFilePaths(mfItem.content, paths);
          int fileCount = (int)paths.size();

          if (subIdx < fileCount) {
            const std::wstring &p = paths[subIdx];
            size_t lastSep = p.find_last_of(L"\\/");
            std::wstring name =
                (lastSep != std::wstring::npos) ? p.substr(lastSep + 1) : p;

            // 子行选中高亮（浅蓝色阴影）
            bool subSelected = (g_contextSubItemDisplay == (int)lpDIS->itemID);
            if (subSelected) {
              RECT rcSub = rcItem;
              rcSub.right -= GetCustomScrollbarReservedWidth();
              HBRUSH hSubBrush = CreateSolidBrush(RGB(230, 240, 255));
              FillRect(hdc, &rcSub, hSubBrush);
              DeleteObject(hSubBrush);
            }

            // 内容区域（与普通记录完全一致）
            RECT rcContent = rcItem;
            rcContent.left += MScale(10);
            rcContent.right -= MScale(6);
            rcContent.top += MScale(2);
            rcContent.right -= GetCustomScrollbarReservedWidth();
            if (rcContent.right < rcContent.left + MScale(80))
              rcContent.right = rcContent.left + MScale(80);

            // 选中边框（与普通记录一致）
            if (isSelected) {
              int selTop = rcItem.top + MScale(1);
              RECT rcSelection = {rcItem.left + MScale(1), selTop,
                                  rcContent.right + MScale(4),
                                  rcItem.bottom - MScale(5)};
              if (rcSelection.right <= rcSelection.left + MScale(8))
                rcSelection.right = rcSelection.left + MScale(8);
              if (rcSelection.bottom <= rcSelection.top + MScale(8))
                rcSelection.bottom = rcSelection.top + MScale(8);
              HPEN hBorderPen =
                  CreatePen(PS_SOLID, std::max(1, MScale(1)), GetAccentColor());
              HPEN hOldPen = (HPEN)SelectObject(hdc, hBorderPen);
              HBRUSH hOldBrush =
                  (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
              Rectangle(hdc, rcSelection.left, rcSelection.top,
                        rcSelection.right, rcSelection.bottom);
              SelectObject(hdc, hOldPen);
              SelectObject(hdc, hOldBrush);
              DeleteObject(hBorderPen);
            }

            // ===== 头部区域（20px）=====
            RECT rcHeader = rcContent;
            rcHeader.bottom = rcHeader.top + MScale(20);
            SetBkMode(hdc, TRANSPARENT);

            // 头行（subIdx==0）：时间文本 + 来源应用图标（与普通记录一致）
            if (subIdx == 0) {
              std::wstring headerText =
                  GetRelativeTimeString(mfItem.timestamp) + L" -";
              HFONT hOldHeaderF = (HFONT)SelectObject(hdc, GetListHeaderFont());
              SetTextColor(hdc, RGB(148, 149, 148));
              RECT rcHeaderText = rcHeader;
              DrawTextW(hdc, headerText.c_str(), -1, &rcHeaderText,
                        DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
              // 测量时间文本宽度，在其右侧绘制来源应用图标
              SIZE szHeader;
              if (GetTextExtentPoint32W(hdc, headerText.c_str(),
                                        (int)headerText.length(), &szHeader)) {
                HICON hAppIcon = GetAppIcon(mfItem.sourceAppPath);
                if (hAppIcon) {
                  int appIconSize = MScale(12);
                  DrawIconEx(hdc, rcHeader.left + szHeader.cx + MScale(2),
                             rcHeader.top + MScale(2), hAppIcon, appIconSize,
                             appIconSize, 0, NULL, DI_NORMAL);
                }
              }
              SelectObject(hdc, hOldHeaderF);
              SetTextColor(hdc, textColor);
            }

            // 快捷键（所有子行都有，右对齐在头部区域）
            if (g_isQuickPasteEnabled && g_allHotkeysEnabled) {
              int shortcutIndex =
                  GetShortcutIndexForDisplayIndex((int)lpDIS->itemID);
              if (shortcutIndex >= 0 && shortcutIndex < 10) {
                wchar_t keyChar = (shortcutIndex == 9)
                                      ? L'0'
                                      : (wchar_t)(L'1' + shortcutIndex);
                std::wstring shortcutText =
                    GetQuickPasteModifierText() + keyChar;
                HFONT hShortcutFont =
                    (HFONT)SelectObject(hdc, GetListHeaderFont());
                RECT rcShortcut = rcHeader;
                rcShortcut.right -= MScale(4);
                SetTextColor(hdc, RGB(100, 149, 237));
                DrawTextW(hdc, shortcutText.c_str(), -1, &rcShortcut,
                          DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(hdc, hShortcutFont);
                SetTextColor(hdc, textColor);
              }
            }

            // ===== 内容区域（头部下方 22px）=====
            int contentY = rcContent.top + MScale(20);
            int iconSize = MScale(16);
            int iconGap = MScale(4);
            int rowH = MScale(22);
            int iconY = contentY + (rowH - iconSize) / 2;
            int textLeft = rcContent.left + iconSize + iconGap;

            // 文件图标
            DWORD mfAttrs = GetFileAttributesW(p.c_str());
            bool pIsFolder = (mfAttrs != INVALID_FILE_ATTRIBUTES &&
                              (mfAttrs & FILE_ATTRIBUTE_DIRECTORY));
            HICON hIcon =
                pIsFolder ? GetCachedFolderIcon(p) : GetCachedFileIcon(p);
            if (hIcon)
              DrawIconEx(hdc, rcContent.left, iconY, hIcon, iconSize, iconSize,
                         0, NULL, DI_NORMAL);

            // 文件名
            HFONT hOldMain = (HFONT)SelectObject(hdc, GetListMainFont());
            RECT rcName = {textLeft, contentY, rcContent.right - MScale(6),
                           contentY + rowH};
            // 末行右侧预留收缩三角空间
            if (subIdx == fileCount - 1)
              rcName.right -= MScale(16);
            if (!g_searchKeyword.empty()) {
              DrawHighlightedText(hdc, name, g_searchKeyword, rcName,
                                  GetTextColor(), GetAccentColor());
            } else {
              DrawTextW(hdc, name.c_str(), -1, &rcName,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                            DT_NOPREFIX);
            }
            SelectObject(hdc, hOldMain);

            // 末行：绘制上三角（收起按钮）
            if (subIdx == fileCount - 1) {
              int triW = MScale(14);
              RECT rcTri = {rcContent.right - MScale(6) - triW, contentY,
                            rcContent.right - MScale(6), contentY + rowH};
              DrawMultiFileTriangle(hdc, rcTri,
                                    isSelected ? GetAccentColor()
                                               : RGB(150, 150, 150),
                                    false); // pointingUp = true（收起）
            }

            // 底部分隔线（每行都画：中间行之间用灰色点线分隔，
            // 末行兼作与下一条记录的分隔）
            if (!isSelected) {
              int separatorRight =
                  rcItem.right - MScale(10) - GetCustomScrollbarReservedWidth();
              if (separatorRight < rcItem.left + MScale(10))
                separatorRight = rcItem.left + MScale(10);
              HPEN hPen = CreatePen(PS_DOT, 1,
                                    g_isDarkMode ? RGB(96, 96, 102)
                                                 : RGB(200, 200, 200));
              HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
              MoveToEx(hdc, rcItem.left + MScale(10), rcItem.bottom - MScale(5),
                       NULL);
              LineTo(hdc, separatorRight, rcItem.bottom - MScale(5));
              SelectObject(hdc, hOldPen);
              DeleteObject(hPen);
            }

            // 展开态分组虚线框（蓝色虚线包围所有子行，完整矩形）
            {
              HPEN hGroupPen = CreatePen(PS_DASH, 1, GetAccentColor());
              HPEN hOldGPen = (HPEN)SelectObject(hdc, hGroupPen);
              HBRUSH hOldGBrush =
                  (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
              // 左边左移 1px，底边上移 1px（与单行选中框底边对齐）
              int bx = rcItem.left;
              int bx2 = rcContent.right + MScale(4);
              int yBottom = rcItem.bottom - MScale(5) - 1;
              // 左右边：末行止于底边，其余行贯穿到底（拼接成连续边线）
              int yEnd = (subIdx == fileCount - 1) ? yBottom : rcItem.bottom;
              MoveToEx(hdc, bx, rcItem.top, NULL);
              LineTo(hdc, bx, yEnd);
              MoveToEx(hdc, bx2, rcItem.top, NULL);
              LineTo(hdc, bx2, yEnd);
              // 顶边（仅头行）
              if (subIdx == 0) {
                MoveToEx(hdc, bx, rcItem.top + MScale(1), NULL);
                LineTo(hdc, bx2, rcItem.top + MScale(1));
              }
              // 底边（仅末行）
              if (subIdx == fileCount - 1) {
                MoveToEx(hdc, bx, yBottom, NULL);
                LineTo(hdc, bx2, yBottom);
              }
              SelectObject(hdc, hOldGPen);
              SelectObject(hdc, hOldGBrush);
              DeleteObject(hGroupPen);
            }

            // 恢复删除滑出动画的DC状态
            if (isDeleteSliding && savedDC) {
              RestoreDC(hdc, savedDC);
            }
            return TRUE; // 子项已自行绘制，跳过下方统一绘制
          }
        }
      }

      SetTextColor(hdc, textColor);

      // 绘制内容区域
      RECT rcContent = rcItem;
      rcContent.left += MScale(10); // 左边距
      rcContent.right -= MScale(6); // 右边距
      rcContent.top += MScale(2);   // 顶部边距
      rcContent.right -= GetCustomScrollbarReservedWidth();
      if (rcContent.right < rcContent.left + MScale(80))
        rcContent.right = rcContent.left + MScale(80);

      // 如果选中，绘制蓝色边框，但给右侧自定义滚动条和底部分隔线留出空间
      // 选中框严格限制在本项矩形内（selTop = rcItem.top + 1），
      // 避免边框延伸到上一项区域被其重绘覆盖，导致选中项闪烁
      if (isSelected) {
        int selTop = rcItem.top + MScale(1);
        RECT rcSelection = {rcItem.left + MScale(1), selTop,
                            rcContent.right + MScale(4),
                            rcItem.bottom - MScale(5)};
        if (rcSelection.right <= rcSelection.left + MScale(8))
          rcSelection.right = rcSelection.left + MScale(8);
        if (rcSelection.bottom <= rcSelection.top + MScale(8))
          rcSelection.bottom = rcSelection.top + MScale(8);

        HPEN hBorderPen =
            CreatePen(PS_SOLID, std::max(1, MScale(1)), GetAccentColor());
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
          HFONT hFont = GetListMainFont();
          HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
          SetBkMode(hdc, TRANSPARENT);

          // 绘制时间戳和来源应用图标（使用16px字体）
          HFONT hHeaderFont = GetListHeaderFont();
          HFONT hPrevFont = (HFONT)SelectObject(hdc, hHeaderFont);

          // 绘制时间戳
          std::wstring headerText =
              GetRelativeTimeString(item.timestamp) + L" -";
          RECT rcHeader = rcContent;
          rcHeader.bottom = rcHeader.top + MScale(18);
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
            int iconX = rcContent.left + textSize.cx + MScale(4);
            int iconY = rcContent.top + MScale(2);
            int iconSize = MScale(12);

            // 检查是否鼠标悬浮在此图标上
            bool isIconHovered =
                (g_isHoveringIcon && g_hoverIconIndex == (int)lpDIS->itemID);

            // 日间模式始终显示原色图标；夜间模式仅在悬浮时显示原色
            if (isIconHovered || !g_isDarkMode) {
              // 直接绘制原色图标
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
              const float brighten = 0.14f;
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

          if (g_isQuickPasteEnabled && g_allHotkeysEnabled) {
            int shortcutIndex =
                GetShortcutIndexForDisplayIndex((int)lpDIS->itemID);
            if (shortcutIndex >= 0 && shortcutIndex < 10) {
              wchar_t keyChar =
                  (shortcutIndex == 9) ? L'0' : (wchar_t)(L'1' + shortcutIndex);
              std::wstring shortcutText = GetQuickPasteModifierText() + keyChar;
              RECT rcShortcut = rcHeader;
              rcShortcut.right -= MScale(4);
              SetTextColor(hdc, RGB(100, 149, 237));
              DrawTextW(hdc, shortcutText.c_str(), -1, &rcShortcut,
                        DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            }
          }

          // 在快捷键下方绘制分类标签（所有标签页都显示）
          if (!item.tagIds.empty()) {
            HFONT hTagFont = GetListTagFont();
            HFONT hPrevTagFont = (HFONT)SelectObject(hdc, hTagFont);

            // 计算标签区域的中心位置（在快捷键提示和时间文本之间）
            int timeTextWidth = MScale(100); // 时间文本大致宽度
            int shortcutWidth = MScale(64);  // 快捷键提示大致宽度
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
              totalTagWidth += tagTextSize.cx + MScale(10);
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
              int tagPadH = MScale(4); // 水平内边距
              int tagPadV = MScale(1); // 垂直内边距
              int tagWidth = tagTextSize.cx + tagPadH * 2;
              int tagHeight = tagTextSize.cy + tagPadV * 2;
              int tagY = rcHeader.top + (MScale(18) - tagHeight) / 2;

              if (tagX < rcHeader.left + timeTextWidth ||
                  tagX + tagWidth > rcHeader.right - shortcutWidth) {
                break; // 防止超出边界
              }

              // 绘制圆角背景
              HBRUSH hTagBrush = CreateSolidBrush(tag->color);
              HBRUSH hOldTagBrush = (HBRUSH)SelectObject(hdc, hTagBrush);
              HPEN hTagPen =
                  CreatePen(PS_SOLID, std::max(1, MScale(1)), tag->color);
              HPEN hOldTagPen = (HPEN)SelectObject(hdc, hTagPen);
              RoundRect(hdc, tagX, tagY, tagX + tagWidth, tagY + tagHeight,
                        MScale(4), MScale(4));
              SelectObject(hdc, hOldTagBrush);
              SelectObject(hdc, hOldTagPen);
              DeleteObject(hTagBrush);
              DeleteObject(hTagPen);

              // 绘制白色文字
              SetTextColor(hdc, RGB(255, 255, 255));
              RECT rcTagText = {tagX, tagY, tagX + tagWidth, tagY + tagHeight};
              DrawTextW(hdc, tag->name.c_str(), -1, &rcTagText,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

              tagX += tagWidth + MScale(3); // 标签间距
            }

            SelectObject(hdc, hPrevTagFont);
          }

          SelectObject(hdc, hPrevFont);

          // 恢复主体内容颜色（支持暗黑模式）
          SetTextColor(hdc, GetTextColor());

          // 调整内容区域（在标题下方）
          rcContent.top += MScale(20);

          if (item.type == TYPE_IMAGE) {
            // 检查图片文件是否存在（仅当没有内存图片数据时才检查，避免绘制期间
            // I/O 导致闪烁）
            bool imageFileExists = true;
            if (item.imageData.empty() && !item.imageFilePath.empty()) {
              DWORD attrs = GetFileAttributesW(item.imageFilePath.c_str());
              imageFileExists = (attrs != INVALID_FILE_ATTRIBUTES);
            }

            // 图片文件不存在的情况
            if (!imageFileExists) {
              // 显示 noexist.png 图标和浅色文件名
              if (g_imgNoExistIcon) {
                Gdiplus::Graphics graphics(hdc);
                graphics.SetInterpolationMode(
                    Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(g_imgNoExistIcon, rcContent.left,
                                   rcContent.top + MScale(1), MScale(18),
                                   MScale(18));
              }

              // 获取文件名
              std::wstring fileName = item.imageFilePath;
              size_t lastSlash = fileName.find_last_of(L"\\/");
              if (lastSlash != std::wstring::npos) {
                fileName = fileName.substr(lastSlash + 1);
              }

              // 使用浅色字体绘制文件名
              RECT rcText = rcContent;
              rcText.left += MScale(22);
              rcText.bottom = rcText.top + MScale(22);
              SetTextColor(hdc, RGB(180, 180, 180));
              DrawTextW(hdc, fileName.c_str(), -1, &rcText,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                            DT_NOPREFIX);
            }
            // 检查图片预览设置
            else if (g_imagePreviewQuality == PREVIEW_OFF) {
              // 关闭预览模式：只显示文件名和尺寸信息
              HFONT hTextFont = GetListTextFont();
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
            } else if ((item.imageData.empty() &&
                        !EnsureItemImageLoaded(item)) ||
                       item.imageWidth <= 0 || item.imageHeight <= 0) {
              HFONT hTextFont = GetListTextFont();
              HFONT hPrevTextFont = (HFONT)SelectObject(hdc, hTextFont);
              std::wstring displayText = !item.imageFileName.empty()
                                             ? item.imageFileName
                                             : L"图片预览不可用";
              SetTextColor(hdc, RGB(150, 150, 150));
              DrawTextW(hdc, displayText.c_str(), -1, &rcContent,
                        DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX |
                            DT_END_ELLIPSIS);
              SelectObject(hdc, hPrevTextFont);
            } else {
              // 绘制图片（使用缩略图数据）
              int availableWidth = rcContent.right - rcContent.left;
              int availableHeight =
                  rcItem.bottom - rcContent.top - MScale(10); // 留出底部边距

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
              int maxPreviewHeight = MScale(150);
              if (scale * srcHeight > maxPreviewHeight) {
                scale = (float)maxPreviewHeight / srcHeight;
              }

              int displayWidth = (int)(srcWidth * scale);
              int displayHeight = (int)(srcHeight * scale);

              // 居中显示
              int x = rcContent.left + (availableWidth - displayWidth) / 2;
              int y = rcContent.top;
              int imageRadius = MScale(10);

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
              HFONT hSizeFont = GetListSizeFont();
              HFONT hPrevSizeFont = (HFONT)SelectObject(hdc, hSizeFont);

              std::wstring sizeText = L"[" + std::to_wstring(item.imageWidth) +
                                      L"x" + std::to_wstring(item.imageHeight) +
                                      L"]";
              RECT rcSize = rcContent;
              rcSize.top = y + displayHeight + MScale(5);
              SetTextColor(hdc, RGB(128, 128, 128));
              DrawTextW(hdc, sizeText.c_str(), -1, &rcSize,
                        DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

              SelectObject(hdc, hPrevSizeFont);
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
              rcText.bottom = rcText.top + MScale(22);

              // 检查文件是否存在（同时缓存属性，避免重复调用
              // GetFileAttributesW）
              bool fileExists = true;
              bool isFolder = false;
              std::wstring filePath;
              if (item.type == TYPE_FILE) {
                // 多文件记录：取第一个路径做存在性/图标判断
                size_t nlPos = item.content.find(L'\n');
                filePath = (nlPos != std::wstring::npos)
                               ? item.content.substr(0, nlPos)
                               : item.content;
                DWORD attrs = GetFileAttributesW(filePath.c_str());
                fileExists = (attrs != INVALID_FILE_ATTRIBUTES);
                if (fileExists && (attrs & FILE_ATTRIBUTE_DIRECTORY))
                  isFolder = true;
              } else if (item.type == TYPE_TEXT) {
                // 文本类型可能是文件路径，检查是否存在
                if (GetLinkType(item.content) == LINK_FILE_PATH) {
                  filePath = item.content;
                  DWORD attrs = GetFileAttributesW(filePath.c_str());
                  fileExists = (attrs != INVALID_FILE_ATTRIBUTES);
                  if (fileExists && (attrs & FILE_ATTRIBUTE_DIRECTORY))
                    isFolder = true;
                }
              } else if (item.type == TYPE_IMAGE &&
                         !item.imageFilePath.empty()) {
                filePath = item.imageFilePath;
                DWORD attrs = GetFileAttributesW(filePath.c_str());
                fileExists = (attrs != INVALID_FILE_ATTRIBUTES);
              }

              // 多文件记录判断（提前到此处，确保控制流优先进入多文件渲染分支）
              bool isMultiFileRecord =
                  (item.type == TYPE_FILE &&
                   item.content.find(L'\n') != std::wstring::npos);

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
                                     rcText.top + MScale(1), MScale(18),
                                     MScale(18));
                }

                // 调整文本位置（图标后面）
                rcText.left += MScale(22);

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
              } else if (isFolder && !isMultiFileRecord) {
                // 文件夹类型（单文件）：显示系统资源管理器文件夹图标
                HICON hFolderIcon = GetCachedFolderIcon(item.content);
                if (hFolderIcon) {
                  int iconSize = MScale(16);
                  DrawIconEx(hdc, rcText.left, rcText.top + MScale(1),
                             hFolderIcon, iconSize, iconSize, 0, NULL,
                             DI_NORMAL);
                }

                // 调整文本位置（图标后面）
                rcText.left += MScale(20);

                RECT rcPathText = rcText;
                rcPathText.right -= MScale(20);

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
                if (!g_searchKeyword.empty()) {
                  // 搜索激活时：文件夹路径匹配段用主题色高亮
                  DrawHighlightedText(hdc, text, g_searchKeyword, rcPathText,
                                      folderTextColor, GetAccentColor());
                } else {
                  DrawTextW(hdc, text.c_str(), -1, &rcPathText,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                                DT_END_ELLIPSIS | DT_NOPREFIX);
                }
                DrawDetectedColorDot(hdc, rcPathText, text);
              } else {
                // 普通文本或文件
                // 文件类型（非文件夹）悬浮时字体变蓝动画
                // 同时检查 TYPE_FILE 和
                // TYPE_TEXT，因为文件路径可能作为文本复制
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

                // 绘制类型图标（18x18 DPI 缩放）：
                // - TYPE_FILE 多文件记录：每个文件名前内联绘制对应系统图标
                // - TYPE_FILE 单文件且存在：系统文件图标（与资源管理器一致）
                // - TYPE_TEXT 网址：net.png 图标
                // - TYPE_TEXT 其他：text.png 图标
                // 图标占用 22px 宽度（18 图标 + 4 间距），文本右移
                if (isMultiFileRecord) {
                  // ===== 收起态：内联绘制每个文件的图标 + 文件名 =====
                  // 展开态由虚拟子项单独绘制（WM_DRAWITEM 上方已 return TRUE）
                  // 拆分所有路径
                  std::vector<std::wstring> paths;
                  size_t mfStart = 0;
                  while (mfStart <= item.content.size()) {
                    size_t mfEnd = item.content.find(L'\n', mfStart);
                    if (mfEnd == std::wstring::npos)
                      mfEnd = item.content.size();
                    if (mfEnd > mfStart)
                      paths.push_back(
                          item.content.substr(mfStart, mfEnd - mfStart));
                    if (mfEnd == item.content.size())
                      break;
                    mfStart = mfEnd + 1;
                  }

                  int mfIconSize = MScale(16);
                  int mfIconGap = MScale(4); // 图标与文字间距
                  int triW = MScale(14);     // 右侧预留下拉三角形空间
                  int mfX = rcText.left;
                  int mfRight = rcText.right - triW;
                  int mfIconY = rcText.top +
                                (rcText.bottom - rcText.top - mfIconSize) / 2;

                  for (size_t i = 0; i < paths.size(); ++i) {
                    const std::wstring &p = paths[i];
                    size_t lastSep = p.find_last_of(L"\\/");
                    std::wstring name = (lastSep != std::wstring::npos)
                                            ? p.substr(lastSep + 1)
                                            : p;
                    // 段文本：文件名 + "、"（非末项）
                    std::wstring seg = name;
                    if (i + 1 < paths.size())
                      seg += L"\u3001";

                    // 判断该路径是否为文件夹，选择对应图标
                    DWORD mfAttrs = GetFileAttributesW(p.c_str());
                    bool pIsFolder = (mfAttrs != INVALID_FILE_ATTRIBUTES &&
                                      (mfAttrs & FILE_ATTRIBUTE_DIRECTORY));
                    HICON hIcon = pIsFolder ? GetCachedFolderIcon(p)
                                            : GetCachedFileIcon(p);

                    // 测量段文本宽度
                    RECT rcMeasure = {0, 0, 0, 0};
                    DrawTextW(hdc, seg.c_str(), -1, &rcMeasure,
                              DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
                    int segW = rcMeasure.right - rcMeasure.left;
                    int needW = mfIconSize + mfIconGap + segW;

                    if (mfX + needW > mfRight) {
                      // 空间不足：绘制图标 + 截断的文件名 + 下拉三角形
                      // （省略号已按需求改为下拉三角形，文件名直接按
                      // rcSeg 裁剪，不再绘制省略号字符）
                      if (hIcon)
                        DrawIconEx(hdc, mfX, mfIconY, hIcon, mfIconSize,
                                   mfIconSize, 0, NULL, DI_NORMAL);
                      RECT rcSeg = rcText;
                      rcSeg.left = mfX + mfIconSize + mfIconGap;
                      rcSeg.right = mfRight - triW - MScale(2);
                      if (!g_searchKeyword.empty()) {
                        // 搜索激活时：文件名匹配段用主题色高亮
                        DrawHighlightedText(hdc, name, g_searchKeyword, rcSeg,
                                            GetTextColor(), GetAccentColor());
                      } else {
                        DrawTextW(hdc, name.c_str(), -1, &rcSeg,
                                  DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                                      DT_NOPREFIX);
                      }
                      // 绘制下拉三角形（点击可展开/收起）
                      RECT rcTri = rcText;
                      rcTri.left = mfRight;
                      DrawMultiFileTriangle(hdc, rcTri,
                                            isSelected ? GetAccentColor()
                                                       : RGB(150, 150, 150));
                      mfX = mfRight; // 已无空间
                      break;
                    }

                    // 绘制图标
                    if (hIcon)
                      DrawIconEx(hdc, mfX, mfIconY, hIcon, mfIconSize,
                                 mfIconSize, 0, NULL, DI_NORMAL);

                    // 绘制段文本（垂直居中）
                    RECT rcSeg = rcText;
                    rcSeg.left = mfX + mfIconSize + mfIconGap;
                    rcSeg.right = rcSeg.left + segW;
                    if (!g_searchKeyword.empty()) {
                      // 搜索激活时：文件名匹配段用主题色高亮
                      DrawHighlightedText(hdc, seg, g_searchKeyword, rcSeg,
                                          GetTextColor(), GetAccentColor());
                    } else {
                      DrawTextW(hdc, seg.c_str(), -1, &rcSeg,
                                DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                                    DT_NOPREFIX);
                    }

                    mfX += needW;
                  }
                  // 所有文件都画完后仍需绘制下拉三角（始终支持展开）
                  if (mfX < mfRight) {
                    RECT rcTri = rcText;
                    rcTri.left = mfRight;
                    DrawMultiFileTriangle(hdc, rcTri,
                                          isSelected ? GetAccentColor()
                                                     : RGB(150, 150, 150));
                  }
                  // 多文件记录已自行绘制文本，跳过下方统一文本绘制
                } else if (item.type == TYPE_FILE && fileExists) {
                  HICON hFileIcon = GetCachedFileIcon(filePath);
                  if (hFileIcon) {
                    int iconSize = MScale(16);
                    DrawIconEx(hdc, rcText.left, rcText.top + MScale(1),
                               hFileIcon, iconSize, iconSize, 0, NULL,
                               DI_NORMAL);
                    rcText.left += MScale(20);
                  }
                } else if (item.type == TYPE_TEXT &&
                           GetLinkType(item.content) == LINK_URL &&
                           g_imgNetIcon) {
                  // 网址类型：net.png 图标
                  Gdiplus::Graphics graphics(hdc);
                  graphics.SetInterpolationMode(
                      Gdiplus::InterpolationModeHighQualityBicubic);
                  int iconSize = MScale(18);
                  int iconY =
                      rcText.top + (rcText.bottom - rcText.top - iconSize) / 2;
                  graphics.DrawImage(g_imgNetIcon, rcText.left, iconY, iconSize,
                                     iconSize);
                  rcText.left += MScale(22);
                } else if (item.type == TYPE_TEXT &&
                           GetLinkType(item.content) == LINK_EMAIL &&
                           g_imgMailIcon) {
                  // 邮箱类型：mail.png 图标
                  Gdiplus::Graphics graphics(hdc);
                  graphics.SetInterpolationMode(
                      Gdiplus::InterpolationModeHighQualityBicubic);
                  int iconSize = MScale(18);
                  int iconY =
                      rcText.top + (rcText.bottom - rcText.top - iconSize) / 2;
                  graphics.DrawImage(g_imgMailIcon, rcText.left, iconY,
                                     iconSize, iconSize);
                  rcText.left += MScale(22);
                } else if (item.type == TYPE_TEXT && g_imgTextIcon) {
                  Gdiplus::Graphics graphics(hdc);
                  graphics.SetInterpolationMode(
                      Gdiplus::InterpolationModeHighQualityBicubic);
                  int iconSize = MScale(18);
                  int iconY =
                      rcText.top + (rcText.bottom - rcText.top - iconSize) / 2;
                  graphics.DrawImage(g_imgTextIcon, rcText.left, iconY,
                                     iconSize, iconSize);
                  rcText.left += MScale(22);
                }

                // 普通文本绘制（多文件记录已在上方自行绘制，跳过）：
                // 含 emoji 时走 DirectWrite 彩色渲染路径，
                // 否则用 GDI DrawTextW 保持原性能与外观。
                // 通过参考 GetListMainFont 的 GDI TextMetrics 换算出真实
                // em size，保证文字大小与 GDI 行完全一致；emoji 字号单独
                // 缩到 0.9 倍，避免视觉上撑满行高。
                if (!isMultiFileRecord) {
                  COLORREF mainTextColor = GetTextColor();
                  // 文本选中复制：该文本项是否有非空选中范围
                  int selStart = -1, selEnd = -1;
                  bool hasTextSel = (item.type == TYPE_TEXT &&
                                     (int)lpDIS->itemID == g_textSelItem &&
                                     GetTextSelRange(&selStart, &selEnd));

                  // 已移除长文本展开：超长文本统一以省略号截断
                  RECT rcDraw = rcText;
                  if (hasTextSel && IsTextSelectableContent(item.content)) {
                    // 选中高亮绘制（单行截断）
                    DrawTextSelectionContent(
                        hdc, text, g_searchKeyword, rcDraw,
                        /*multiLine=*/false, /*verticalCenter=*/true, selStart,
                        selEnd, mainTextColor, GetAccentColor());
                  } else if (TextContainsEmoji(text.c_str(),
                                               (int)text.length())) {
                    // 含 emoji：走 DirectWrite 彩色渲染路径，末尾省略号
                    DrawTextWithColorEmoji(
                        hdc, text.c_str(), (int)text.length(), rcDraw,
                        GetListMainFont(), L"Microsoft YaHei",
                        /*fontWeight=*/400, mainTextColor,
                        /*align=*/0, /*verticalCenter=*/true,
                        /*endEllipsis=*/true, /*emojiScale=*/0.9f);
                  } else if (!g_searchKeyword.empty()) {
                    // 搜索激活时：匹配关键词用金黄色高亮
                    DrawHighlightedText(hdc, text, g_searchKeyword, rcDraw,
                                        mainTextColor, GetAccentColor());
                  } else {
                    // 普通文本：超长显示省略号
                    DrawTextW(hdc, text.c_str(), -1, &rcDraw,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                                  DT_NOPREFIX | DT_END_ELLIPSIS);
                  }
                  DrawDetectedColorDot(hdc, rcText, text);
                }
              }
            }
          }

          SelectObject(hdc, hOldFont);
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

      // 恢复删除滑出动画的DC状态
      if (isDeleteSliding && savedDC) {
        RestoreDC(hdc, savedDC);
      }

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
      g_lastSearchInputTick = GetTickCount();
      // 保存光标位置，PerformSearch 内部可能重置光标
      DWORD selStart = 0;
      DWORD selEnd = 0;
      SendMessageW(g_hwndSearchBox, EM_GETSEL, (WPARAM)&selStart,
                   (LPARAM)&selEnd);
      PerformSearch(hwnd);
      // 搜索/筛选后自动选中首项：确保空格预览、方向键导航有可用选中项。
      // UpdateListBox 内 LB_RESETCONTENT 已清空选中，此处补回首项选中。
      if (g_hwndListBox && !g_displayIndexMap.empty()) {
        int curSel = (int)SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0);
        if (curSel == LB_ERR) {
          SendMessageW(g_hwndListBox, LB_SETCURSEL, 0, 0);
        }
      }
      UpdateSearchClearButtonVisibility();
      // 恢复光标位置（clamp 到文本长度内）
      if (g_hwndSearchBox && IsWindow(g_hwndSearchBox) &&
          GetFocus() == g_hwndSearchBox) {
        int textLen = GetWindowTextLengthW(g_hwndSearchBox);
        int restoreStart = std::max(0, std::min((int)selStart, textLen));
        int restoreEnd = std::max(0, std::min((int)selEnd, textLen));
        SendMessageW(g_hwndSearchBox, EM_SETSEL, restoreStart, restoreEnd);
      }
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
      bool showFavTooltip = false; // 标记：首次切到收藏页时需显示 tooltip
      int newTab = g_currentTab;
      if (wID == ID_FILTER_ALL) {
        newTab = 0;
        filterChanged = true;
      } else if (wID == ID_FILTER_TEXT) {
        newTab = 1;
        filterChanged = true;
      } else if (wID == ID_FILTER_IMAGE) {
        newTab = 2;
        filterChanged = true;
      } else if (wID == ID_FILTER_FILE) {
        newTab = 3;
        filterChanged = true;
      } else if (wID == ID_FILTER_FAVORITE) {
        KillTimer(hwnd, ID_FAVORITE_TOOLTIP_TIMER);
        HideFavoriteFilterTooltip();
        if (g_currentTab == 4) {
          // 已在收藏页，再次单击 → 弹出分类下拉菜单（筛选模式，不可编辑）
          RECT btnRect;
          GetWindowRect(g_hwndFilterFavorite, &btnRect);
          ShowTagPopup(hwnd, btnRect.left, btnRect.bottom,
                       btnRect.right - btnRect.left, true);
        } else {
          // 首次单击 → 切换到收藏页，显示全部
          newTab = 4;
          g_currentFilterTagId = 0;
          filterChanged = true;
          // tooltip 在面板切换之后再显示，避免被重绘隐藏
          showFavTooltip = true;
        }
      }

      if (filterChanged) {
        // 重绘所有筛选按钮以更新选中状态
        InvalidateRect(g_hwndFilterAll, NULL, FALSE);
        InvalidateRect(g_hwndFilterText, NULL, FALSE);
        InvalidateRect(g_hwndFilterImage, NULL, FALSE);
        InvalidateRect(g_hwndFilterFile, NULL, FALSE);
        InvalidateRect(g_hwndFilterFavorite, NULL, FALSE);
        // 使用 SwitchMainPanel 以保存/恢复滚动位置并清除快捷键缓存
        SwitchMainPanel(hwnd, newTab, false);
        // 标题栏药丸需随标签页名称变化而重绘
        if (IsQuickFilterActive())
          InvalidateRect(hwnd, NULL, TRUE);
      }

      // tooltip 必须在 SwitchMainPanel 重绘之后显示，否则会被覆盖
      if (showFavTooltip) {
        ShowFavoriteFilterTooltip(hwnd);
        SetTimer(hwnd, ID_FAVORITE_TOOLTIP_TIMER, 1400, NULL);
      }
    }

    // 处理列表框双击事件
    if (wID == ID_LISTBOX && wNotifyCode == LBN_DBLCLK) {
      int index = SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0);
      if (index != LB_ERR && index < (int)g_displayIndexMap.size()) {
        // 批量编辑模式下，双击切换选择状态
        if (g_isBatchEditMode) {
          // 批量选中多条文件时，双击=粘贴未反选的文件后自动退出批量模式
          if (g_selectedItems.size() >= 2) {
            bool allFile = true;
            for (int ai : g_selectedItems) {
              if (ai < 0 || ai >= (int)g_history.size() ||
                  g_history[ai].type != TYPE_FILE) {
                allFile = false;
                break;
              }
            }
            if (allFile && PasteSelectedFilesBatch(hwnd))
              return 0;
          }
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
          if (!g_isTopmost) {
            CloseTagPopup();
            ExitNoActivateMode(hwnd);
          }

          if (SetClipboardFromItem(item)) {
            RememberPasteTarget(hwnd);
            RestoreFocusAndPaste(hwnd);
            IncrementPasteCount();
            if (g_isNotificationEnabled) {
              ShowTrayBalloon(hwnd, T(STR_TRAY_HINT), T(STR_TRAY_PASTED));
            }
          }
        } else if (item.type == TYPE_FILE) {
          // 文件类型：复制文件路径到剪贴板
          // 多文件记录（content 含 L'\n'）：构建 CF_HDROP 支持多文件粘贴；
          // 单文件：保持原 CF_UNICODETEXT 行为（兼容性）。
          size_t nlPos = item.content.find(L'\n');
          if (nlPos != std::wstring::npos) {
            // 多文件：拆分路径构建 CF_HDROP
            std::vector<std::wstring> paths;
            size_t start = 0;
            while (start <= item.content.size()) {
              size_t end = item.content.find(L'\n', start);
              if (end == std::wstring::npos)
                end = item.content.size();
              if (end > start) {
                paths.push_back(item.content.substr(start, end - start));
              }
              if (end == item.content.size())
                break;
              start = end + 1;
            }

            if (OpenClipboard(NULL)) {
              EmptyClipboard();
              // 计算 DROPFILES 总大小
              size_t totalLen = 0;
              for (const auto &p : paths)
                totalLen += (p.size() + 1) * sizeof(wchar_t);
              totalLen += sizeof(wchar_t); // 结尾双 \0

              HGLOBAL hGlobal =
                  GlobalAlloc(GMEM_MOVEABLE, sizeof(DROPFILES) + totalLen);
              if (hGlobal != NULL) {
                DROPFILES *pDrop = (DROPFILES *)GlobalLock(hGlobal);
                if (pDrop != NULL) {
                  pDrop->pFiles = sizeof(DROPFILES);
                  pDrop->pt.x = 0;
                  pDrop->pt.y = 0;
                  pDrop->fNC = FALSE;
                  pDrop->fWide = TRUE;
                  wchar_t *pStr =
                      (wchar_t *)((BYTE *)pDrop + sizeof(DROPFILES));
                  for (size_t i = 0; i < paths.size(); ++i) {
                    wcscpy_s(pStr, paths[i].size() + 1, paths[i].c_str());
                    pStr += paths[i].size() + 1;
                  }
                  *pStr = L'\0'; // 结尾双 \0
                  GlobalUnlock(hGlobal);
                  SetClipboardData(CF_HDROP, hGlobal);

                  g_isRestoringClipboard = true;
                  if (g_isNotificationEnabled) {
                    ShowTrayBalloon(hwnd, T(STR_TRAY_HINT),
                                    T(STR_TRAY_FILE_PATH_COPIED));
                  }
                }
              }
              CloseClipboard();
              SetTimer(hwnd, 1, 100, NULL);
            }
          } else if (OpenClipboard(NULL)) {
            // 单文件：保持原 CF_UNICODETEXT 行为
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
                  ShowTrayBalloon(hwnd, T(STR_TRAY_HINT),
                                  T(STR_TRAY_FILE_PATH_COPIED));
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
        ShowTrayBalloon(hwnd, T(STR_TRAY_HINT),
                        g_isBatchEditMode ? L"批量编辑模式已开启"
                                          : L"批量编辑模式已关闭");
      }
      // 重绘列表框
      InvalidateRect(g_hwndListBox, NULL, FALSE);
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
          ShowTrayBalloon(hwnd, T(STR_TRAY_HINT), T(STR_TRAY_PINNED));
        }
      } else {
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetWindowTextW(GetDlgItem(hwnd, ID_TOPMOST_BUTTON), L"置顶");
        if (g_isNotificationEnabled) {
          ShowTrayBalloon(hwnd, T(STR_TRAY_HINT), T(STR_TRAY_UNPINNED));
        }
        SaveHotkeySettings();
      }
    } else if (wID == ID_DARKMODE_BUTTON && wNotifyCode == BN_CLICKED) {
      // 切换暗黑模式
      g_isDarkMode = !g_isDarkMode;
      g_themeMode = g_isDarkMode ? THEME_DARK : THEME_LIGHT;

      if (g_isNotificationEnabled) {
        ShowTrayBalloon(hwnd, T(STR_TRAY_HINT),
                        g_isDarkMode ? L"已切换到暗黑模式"
                                     : L"已切换到明亮模式");
      }
      SaveHotkeySettings();

      // ApplyTheme 内部已使用 RedrawWindow + RDW_ALLCHILDREN 递归重绘
      // 主窗口及所有子控件（筛选按钮、标题栏按钮、翻页按钮等）。
      ApplyTheme();
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
          // 非平滑翻页：不清空快捷键分配记录，CollectVisibleShortcutDisplayIndices
          // 会自动跳过已分配过的项，只给新出现的项编号
          ApplyListBoxTopIndex(g_hwndListBox, topIndex);
          ShowCustomScrollbar(g_hwndListBox);
          RefreshScrollbarIfChanged(g_hwndListBox);
        }
        // 更新按钮状态
        InvalidateRect(g_hwndPageUpBtn, NULL, TRUE);
        InvalidateRect(g_hwndPageDownBtn, NULL, TRUE);
      }
    } else if (wID == ID_PAGE_DOWN_BTN && wNotifyCode == BN_CLICKED) {
      // 下一页 - 使用
      // CalculateNextPageIndex：未完整显示的项会成为下一页首项，
      // 确保它在下一页完整显示并获得快捷键
      int expectedNextTop = CalculateNextPageIndex(g_listBoxTopIndex);
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
          // 非平滑翻页：不清空快捷键分配记录，CollectVisibleShortcutDisplayIndices
          // 会自动跳过已分配过的项，只给新出现的项编号（包括最后一页）
          ApplyListBoxTopIndex(g_hwndListBox, topIndex);
          ShowCustomScrollbar(g_hwndListBox);
          RefreshScrollbarIfChanged(g_hwndListBox);
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
          ShowTrayBalloon(hwnd, T(STR_TRAY_HINT), T(STR_TRAY_PINNED));
        }
      } else {
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        if (g_isNotificationEnabled) {
          ShowTrayBalloon(hwnd, T(STR_TRAY_HINT), T(STR_TRAY_UNPINNED));
        }
      }
      SaveHotkeySettings();
      InvalidateRect(g_hwndTitleTopmost, NULL, TRUE);
    } else if (wID == ID_TITLEBAR_MINIMIZE && wNotifyCode == BN_CLICKED) {
      // 防御性检查：确认光标确实在最小化按钮上，避免误触发的最小化
      // （用户反馈点击收藏按钮偶现最小化窗体，此处加保护以排除任何路由异常）
      POINT ptCursor = {};
      GetCursorPos(&ptCursor);
      RECT rcMinimize = {};
      bool isCursorOnMinimize = false;
      if (g_hwndTitleMinimize &&
          GetWindowRect(g_hwndTitleMinimize, &rcMinimize)) {
        isCursorOnMinimize = PtInRect(&rcMinimize, ptCursor) != FALSE;
      }
      if (isCursorOnMinimize) {
        // 与 WM_SYSCOMMAND/SC_MINIMIZE 处理保持一致：
        // 不需要任务栏按钮时直接隐藏（SW_HIDE），避免最小化时显示为
        // 屏幕左下角的标题栏条；需要任务栏按钮时交给系统正常最小化。
        if (HideInsteadOfMinimizeWhenNoTaskbar(hwnd))
          ; // 已隐藏，无需 SW_MINIMIZE
        else
          ShowWindow(hwnd, SW_MINIMIZE);
      }
    } else if (wID == ID_TITLEBAR_MAXIMIZE && wNotifyCode == BN_CLICKED) {
      // 标题栏最大化/还原按钮
      if (IsZoomed(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
      } else {
        // 最大化前归一化 z-order，确保非置顶状态下最大化后不会意外置顶
        ExitNoActivateMode(hwnd);
        ShowWindow(hwnd, SW_MAXIMIZE);
      }
      InvalidateRect(g_hwndTitleMaximize, NULL, TRUE);
    } else if (wID == ID_TITLEBAR_CLOSE && wNotifyCode == BN_CLICKED) {
      // 标题栏关闭按钮 - 隐藏窗口而不是退出
      CloseTagPopup();
      // 清除不抢焦点模式
      ExitNoActivateMode(hwnd);
      ShowWindow(hwnd, SW_HIDE);
    } else if (wID == IDM_EXIT) {
      DestroyWindow(hwnd);
    } else if (wID == IDM_RESTART) {
      // 重启应用：启动新实例后退出当前实例
      // 获取当前可执行文件路径
      wchar_t exePath[MAX_PATH] = {0};
      GetModuleFileNameW(NULL, exePath, MAX_PATH);
      // 启动新实例，附加 -restart 参数：新实例据此重试获取互斥量，
      // 等待当前实例退出释放 Global\SmartClipMutex，避免误判为"已运行"
      std::wstring cmd = L"\"" + std::wstring(exePath) + L"\" -restart";
      STARTUPINFOW si = {};
      si.cb = sizeof(si);
      PROCESS_INFORMATION pi = {};
      // 使用 CREATE_NEW_PROCESS_GROUP 避免新实例继承当前控制台
      if (CreateProcessW(exePath, &cmd[0], NULL, NULL, FALSE,
                         CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        // 新实例已启动，退出当前实例（直接销毁窗口，避免 WM_CLOSE
        // 仅隐藏窗口）
        DestroyWindow(hwnd);
      }
    } else if (wID == IDM_SETTINGS) {
      // 显示模态设置对话框
      ShowSettingsDialog(hwnd);
    } else if (wID == IDM_VIEW_AGREEMENT) {
      // 手动查看用户协议（仅查看，不影响已接受的记录）
      HWND existingDlg = FindWindowW(L"SmartClipAgreementDlg", NULL);
      if (!existingDlg || !IsWindow(existingDlg))
        ShowAgreementDialog(GetModuleHandleW(NULL));
    } else if (wID == IDM_COPY) {
      // 右键菜单：复制
      if (g_contextMenuIndex >= 0 &&
          g_contextMenuIndex < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[g_contextMenuIndex]; // 获取实际索引
        const ClipboardItem &item = g_history[actualIndex];
        if (OpenClipboard(NULL)) {
          EmptyClipboard();

          if (item.type == TYPE_TEXT) {
            // 文本类型：复制文本内容
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
          } else if (item.type == TYPE_FILE) {
            // 文件类型：多文件记录构建 CF_HDROP，单文件用 CF_UNICODETEXT
            size_t nlPos = item.content.find(L'\n');
            if (nlPos != std::wstring::npos) {
              // 多文件：拆分路径构建 CF_HDROP
              std::vector<std::wstring> paths;
              size_t start = 0;
              while (start <= item.content.size()) {
                size_t end = item.content.find(L'\n', start);
                if (end == std::wstring::npos)
                  end = item.content.size();
                if (end > start) {
                  paths.push_back(item.content.substr(start, end - start));
                }
                if (end == item.content.size())
                  break;
                start = end + 1;
              }
              size_t totalLen = 0;
              for (const auto &p : paths)
                totalLen += (p.size() + 1) * sizeof(wchar_t);
              totalLen += sizeof(wchar_t);
              HGLOBAL hGlobal =
                  GlobalAlloc(GMEM_MOVEABLE, sizeof(DROPFILES) + totalLen);
              if (hGlobal != NULL) {
                DROPFILES *pDrop = (DROPFILES *)GlobalLock(hGlobal);
                if (pDrop != NULL) {
                  pDrop->pFiles = sizeof(DROPFILES);
                  pDrop->pt.x = 0;
                  pDrop->pt.y = 0;
                  pDrop->fNC = FALSE;
                  pDrop->fWide = TRUE;
                  wchar_t *pStr =
                      (wchar_t *)((BYTE *)pDrop + sizeof(DROPFILES));
                  for (size_t i = 0; i < paths.size(); ++i) {
                    wcscpy_s(pStr, paths[i].size() + 1, paths[i].c_str());
                    pStr += paths[i].size() + 1;
                  }
                  *pStr = L'\0';
                  GlobalUnlock(hGlobal);
                  SetClipboardData(CF_HDROP, hGlobal);
                }
              }
            } else {
              // 单文件：CF_UNICODETEXT
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
              // 懒加载：启动时未预加载缩略图，按需从文件加载
              EnsureItemImageLoaded(item);
              if (item.imageData.empty())
                return false;
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
            ShowTrayBalloon(hwnd, T(STR_TRAY_HINT), T(STR_TRAY_COPIED));
          }
        }
      }
    } else if (wID == IDM_PASTE) {
      // 批量模式：粘贴选中的文件（排除反选）后自动退出批量模式
      if (g_isBatchEditMode && g_selectedItems.size() >= 2) {
        PasteSelectedFilesBatch(hwnd);
        return 0;
      }
      // 右键菜单：执行粘贴（模拟Ctrl+V）
      if (g_contextMenuIndex >= 0 &&
          g_contextMenuIndex < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[g_contextMenuIndex];
        const ClipboardItem &item = g_history[actualIndex];
        if (SetClipboardFromItem(item)) {
          ExitNoActivateMode(hwnd);
          RememberPasteTarget(hwnd);
          RestoreFocusAndPaste(hwnd);
          if (g_isNotificationEnabled) {
            ShowTrayBalloon(hwnd, T(STR_TRAY_HINT), T(STR_TRAY_PASTED));
          }
        }
      }
    } else if (wID == IDM_EDIT) {
      // 编辑文本记录：打开文本编辑弹窗（由原记录位置浮出）
      if (g_contextMenuIndex >= 0 &&
          g_contextMenuIndex < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[g_contextMenuIndex];
        if (actualIndex >= 0 && actualIndex < (int)g_history.size() &&
            g_history[actualIndex].type == TYPE_TEXT) {
          RECT rcItem;
          if (SendMessageW(g_hwndListBox, LB_GETITEMRECT, g_contextMenuIndex,
                           (LPARAM)&rcItem) != LB_ERR) {
            POINT tl = {rcItem.left, rcItem.top};
            POINT br = {rcItem.right, rcItem.bottom};
            ClientToScreen(g_hwndListBox, &tl);
            ClientToScreen(g_hwndListBox, &br);
            RECT rcScreen = {tl.x, tl.y, br.x, br.y};
            ShowTextEditorPopup(hwnd, actualIndex, rcScreen);
          }
        }
      }
    } else if (wID == IDM_FAVORITE) {
      // 收藏/取消收藏
      if (g_contextMenuIndex >= 0 &&
          g_contextMenuIndex < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[g_contextMenuIndex];
        int savedSelDisplay = g_contextMenuIndex;
        int savedTopIndex = g_listBoxTopIndex;
        g_history[actualIndex].isFavorite = !g_history[actualIndex].isFavorite;
        SaveHistory();

        if (g_currentTab == 4) {
          // 收藏筛选页：取消收藏后该项从列表移除，需要重建列表
          UpdateListBox();
          // 恢复滚动位置
          if (savedTopIndex > 0 && g_hwndListBox) {
            int maxTop = GetListBoxMaxTopIndex();
            if (savedTopIndex > maxTop)
              savedTopIndex = maxTop;
            ApplyListBoxTopIndex(g_hwndListBox, savedTopIndex);
          }
          ResetShortcutAssignment();
          // 选中相邻项（该项已移除，选中原来位置或最后一项）
          int fallback = savedSelDisplay;
          if (fallback >= (int)g_displayIndexMap.size())
            fallback = (int)g_displayIndexMap.size() - 1;
          if (fallback >= 0)
            SelectListDisplayIndex(fallback);
        } else {
          // 非收藏筛选页：项位置不变，仅重绘该项，避免闪烁
          ResetShortcutAssignment();
          if (g_hwndListBox) {
            RECT rcItem;
            if (SendMessageW(g_hwndListBox, LB_GETITEMRECT, savedSelDisplay,
                             (LPARAM)&rcItem) != LB_ERR) {
              InvalidateRect(g_hwndListBox, &rcItem, FALSE);
            }
          }
        }
        if (g_isNotificationEnabled) {
          ShowTrayBalloon(hwnd, T(STR_TRAY_HINT),
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
              ShowTrayBalloon(hwnd, T(STR_TRAY_HINT),
                              (L"已移除标签: " + tag->name).c_str());
            }
          }
        } else {
          // 检查标签数量限制（最多5个）
          if ((int)g_history[actualIndex].tagIds.size() >= 5) {
            ThemedConfirmDialogConfig dialog = {
                L"标签数量限制",
                L"每条记录最多添加5个标签",
                L"已达到标签数量上限",
                L"请先移除部分标签后再添加新标签。",
                L"知道了",
                L"取消",
                424,
                220,
                {14, 68, 410, 160},
                false,
                false,
                true};
            ShowThemedConfirmDialog(hwnd, dialog);
            break;
          }
          AddTagToItem(actualIndex, tagId);
          if (g_isNotificationEnabled) {
            Tag *tag = GetTagById(tagId);
            if (tag) {
              ShowTrayBalloon(hwnd, T(STR_TRAY_HINT),
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
          // 多文件记录：取第一个路径
          size_t nlPos = item.content.find(L'\n');
          filePath = (nlPos != std::wstring::npos)
                         ? item.content.substr(0, nlPos)
                         : item.content;
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
    } else if (wID == IDM_SELECT_IN_EXPLORER) {
      // 右键菜单：在资源管理器中选中多文件
      if (g_contextMenuIndex >= 0 &&
          g_contextMenuIndex < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[g_contextMenuIndex];
        const ClipboardItem &item = g_history[actualIndex];

        // 仅对多文件记录生效
        if (item.type != TYPE_FILE ||
            item.content.find(L'\n') == std::wstring::npos) {
          break;
        }

        // 拆分多文件路径
        std::vector<std::wstring> paths;
        size_t start = 0;
        while (start <= item.content.size()) {
          size_t end = item.content.find(L'\n', start);
          if (end == std::wstring::npos)
            end = item.content.size();
          if (end > start) {
            std::wstring p = item.content.substr(start, end - start);
            // 仅保留实际存在的文件，避免 SHOpenFolderAndSelectItems 失败
            DWORD attrs = GetFileAttributesW(p.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES) {
              paths.push_back(p);
            }
          }
          if (end == item.content.size())
            break;
          start = end + 1;
        }

        if (paths.empty()) {
          break;
        }

        // 取公共父目录作为文件夹 PIDL
        std::wstring folderPath;
        size_t lastSep = paths[0].find_last_of(L"\\/");
        if (lastSep != std::wstring::npos) {
          folderPath = paths[0].substr(0, lastSep);
        }
        if (folderPath.empty()) {
          break;
        }

        PIDLIST_ABSOLUTE pidlFolder = ILCreateFromPathW(folderPath.c_str());
        if (!pidlFolder) {
          break;
        }

        // 为每个文件创建子项 PIDL（相对于文件夹）
        std::vector<PIDLIST_RELATIVE> childPidls;
        childPidls.reserve(paths.size());
        bool pidlOk = true;
        for (const auto &p : paths) {
          PIDLIST_ABSOLUTE pidlFull = ILCreateFromPathW(p.c_str());
          if (!pidlFull) {
            pidlOk = false;
            break;
          }
          PIDLIST_RELATIVE pidlChild = ILFindChild(pidlFolder, pidlFull);
          if (!pidlChild) {
            ILFree(pidlFull);
            pidlOk = false;
            break;
          }
          // ILFindChild 返回的是 pidlFull 内部的指针，需要克隆一份独立副本
          childPidls.push_back(
              (PIDLIST_RELATIVE)ILClone((LPCITEMIDLIST)pidlChild));
          ILFree(pidlFull);
        }

        if (pidlOk && !childPidls.empty()) {
          std::vector<LPCITEMIDLIST> pidlPtrs;
          pidlPtrs.reserve(childPidls.size());
          for (auto &c : childPidls) {
            pidlPtrs.push_back((LPCITEMIDLIST)c);
          }
          // 选中多个文件（0 表示默认行为）
          SHOpenFolderAndSelectItems(pidlFolder, (UINT)pidlPtrs.size(),
                                     pidlPtrs.data(), 0);
        }

        // 释放资源
        for (auto &c : childPidls) {
          ILFree((PIDLIST_ABSOLUTE)c);
        }
        ILFree(pidlFolder);
      }
    } else if (wID == IDM_DELETE_SUBITEM) {
      // 删除多文件记录中的单个子行（仅移除该文件路径，保留其余）
      int dispIdx = g_contextSubItemDisplay;
      g_contextSubItemDisplay = -1; // 立即清除高亮

      if (dispIdx >= 0 && dispIdx < (int)g_displayIndexMap.size() &&
          dispIdx < (int)g_displaySubIndexMap.size()) {
        int actIdx = g_displayIndexMap[dispIdx];
        int subIdx = g_displaySubIndexMap[dispIdx];

        if (actIdx >= 0 && actIdx < (int)g_history.size() && subIdx >= 0) {
          ClipboardItem &mfItem = g_history[actIdx];
          std::vector<std::wstring> paths;
          SplitMultiFilePaths(mfItem.content, paths);

          if (subIdx < (int)paths.size()) {
            paths.erase(paths.begin() + subIdx);

            if (paths.empty()) {
              // 没有剩余文件：删除整条记录
              g_history.erase(g_history.begin() + actIdx);
              g_expandedItems.erase(actIdx);
            } else {
              // 用 \n 重新连接剩余路径
              std::wstring newContent;
              for (size_t i = 0; i < paths.size(); ++i) {
                if (i > 0)
                  newContent += L'\n';
                newContent += paths[i];
              }
              mfItem.content = newContent;
              // 只剩1个文件时不再是多文件记录，收起展开状态
              if (paths.size() <= 1)
                g_expandedItems[actIdx] = false;
            }

            SaveHistory();
            RefreshListBoxPreservePosition();
            ResetShortcutAssignment();

            if (g_isNotificationEnabled) {
              ShowTrayBalloon(hwnd, T(STR_TRAY_HINT), T(STR_TRAY_DELETED));
            }
          }
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
          // 保存滚动位置
          int savedTopIndex = g_listBoxTopIndex;
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
          // 恢复滚动位置
          if (savedTopIndex > 0 && g_hwndListBox) {
            int maxTop = GetListBoxMaxTopIndex();
            if (savedTopIndex > maxTop)
              savedTopIndex = maxTop;
            ApplyListBoxTopIndex(g_hwndListBox, savedTopIndex);
          }
          ResetShortcutAssignment();
          if (g_isNotificationEnabled) {
            ShowTrayBalloon(hwnd, T(STR_TRAY_HINT), T(STR_TRAY_BATCH_DELETED));
          }
        }
      } else if (g_contextMenuIndex >= 0 &&
                 g_contextMenuIndex < (int)g_displayIndexMap.size()) {
        int actualIndex = g_displayIndexMap[g_contextMenuIndex]; // 获取实际索引
        bool shouldDelete = true;

        if (g_history[actualIndex].isFavorite) {
          ThemedConfirmDialogConfig dialog = {T(STR_DLG_DELETE_FAV_TITLE),
                                              T(STR_DLG_DELETE_FAV_SUBTITLE),
                                              T(STR_DLG_DELETE_FAV_BODY1),
                                              T(STR_DLG_DELETE_FAV_BODY2),
                                              T(STR_DLG_DELETE_FAV_CONFIRM),
                                              T(STR_DLG_CONFIRM_CANCEL),
                                              424,
                                              246,
                                              {14, 78, 410, 180},
                                              true,
                                              false,
                                              true};
          shouldDelete = ShowThemedConfirmDialog(hwnd, dialog);
        }

        if (shouldDelete) {
          // 启动删除滑出动画，动画结束后在定时器中执行实际删除
          g_deleteSlideAnimating = true;
          g_deleteSlideDisplayIndex = g_contextMenuIndex;
          g_deleteSlideActualIndex = actualIndex;
          g_deleteSlideOffset = 0;
          // 计算目标宽度（列表框可见宽度）
          RECT rcListBox;
          GetClientRect(g_hwndListBox, &rcListBox);
          g_deleteSlideTargetWidth = rcListBox.right - rcListBox.left -
                                     GetCustomScrollbarReservedWidth();
          // 立即失效当前项矩形，触发滑出动画的首帧重绘
          // （选中框已限制在本项矩形内，无需额外向上扩展无效区域）
          RECT rcClear;
          if (g_hwndListBox && SendMessageW(g_hwndListBox, LB_GETITEMRECT,
                                            g_deleteSlideDisplayIndex,
                                            (LPARAM)&rcClear) != LB_ERR) {
            InvalidateRect(g_hwndListBox, &rcClear, FALSE);
          }
          SetTimer(g_hwndListBox, ID_DELETE_SLIDE_TIMER, 16, NULL);
        }
      }
    } else if (wID >= IDM_BATCH_ADD_TAG) {
      // 批量编辑模式：批量加入标签（处理二级菜单选择）
      if (g_isBatchEditMode && !g_selectedItems.empty()) {
        int tagId = wID - IDM_BATCH_ADD_TAG;
        std::vector<int> selectionOrder = g_selectedItems;
        // 为所有选中的项目添加标签（跳过已达5个标签上限的项）
        for (int actualIndex : selectionOrder) {
          if (actualIndex >= 0 && actualIndex < (int)g_history.size()) {
            if ((int)g_history[actualIndex].tagIds.size() < 5) {
              g_history[actualIndex].tagIds.insert(tagId);
              g_history[actualIndex].isFavorite = true;
            }
          }
        }
        PromoteHistoryItemsToFrontInOrder(selectionOrder);
        SaveHistory();
        g_selectedItems.clear();
        g_batchSelectionAnchorDisplayIndex = LB_ERR;
        UpdateListBox();
        if (g_isNotificationEnabled) {
          ShowTrayBalloon(hwnd, T(STR_TRAY_HINT), T(STR_TRAY_BATCH_TAGGED));
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
    } else if (wID == IDM_PAUSE_RESUME) {
      // 托盘菜单：暂停/恢复剪贴板监听
      g_isClipboardPaused = !g_isClipboardPaused;
      RefreshTrayTooltip();
      if (g_isNotificationEnabled) {
        ShowTrayBalloon(hwnd, T(STR_TRAY_HINT),
                        g_isClipboardPaused ? T(STR_TRAY_PAUSED)
                                            : T(STR_TRAY_RESUMED));
      }
    } else if (wID == IDM_QUICK_PASTE_TOGGLE) {
      // 托盘菜单：启用/关闭快捷键（本 app 所有）
      g_allHotkeysEnabled = !g_allHotkeysEnabled;
      ResetShortcutAssignment();
      if (g_hwndMain) {
        if (g_allHotkeysEnabled) {
          RegisterAllHotkeys(g_hwndMain);
        } else {
          UnregisterAllHotkeys(g_hwndMain);
        }
        // 主窗体快捷键标签显示依赖该标志，重绘以反映变化
        InvalidateRect(g_hwndListBox, NULL, FALSE);
      }
      SaveHotkeySettings();
      if (g_isNotificationEnabled) {
        ShowTrayBalloon(hwnd, T(STR_TRAY_HINT),
                        g_allHotkeysEnabled ? T(STR_TRAY_QUICK_PASTE_ENABLED)
                                            : T(STR_TRAY_QUICK_PASTE_DISABLED));
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
    } else if (wParam == ID_DRAG_SHELF_TIMER) {
      // Dropshelf 式拖拽呼出轮询（文件中转站）
      HandleDragShelfPoll(hwnd);
      // 悬浮不抢焦点模式：鼠标离开主窗体后立即退出并按置顶开关归一化
      // z-order，避免未开启置顶时窗口残留 TOPMOST 一直浮在所有窗口之上
      // （拖拽蒙版呼出期间不判定——鼠标此刻必然在窗外拖拽，退出会立即
      // 把蒙版降级压到拖拽源下方，表现为蒙版一闪即逝）
      if (g_isNoActivateMode && !g_dragShelfSummoned) {
        POINT ptCur = {};
        GetCursorPos(&ptCur);
        RECT rcMain = {};
        GetWindowRect(hwnd, &rcMain);
        if (!PtInRect(&rcMain, ptCur))
          ExitNoActivateMode(hwnd);
      }
    } else if (wParam == ID_DRAG_SHELF_SETTLE) {
      KillTimer(hwnd, ID_DRAG_SHELF_SETTLE);
      // 松手后 400ms 仍未收到落地回调：拖拽在窗体上被取消，隐藏并还原
      if (g_dragShelfSummoned)
        DismissDragShelf(hwnd, true);
    } else if (wParam == ID_RESTORE_FOCUS_FOR_PASTE) {
      KillTimer(hwnd, ID_RESTORE_FOCUS_FOR_PASTE);
      if (g_previousActiveWindow && IsWindow(g_previousActiveWindow))
        RestoreForegroundWindow(g_previousActiveWindow);
      SetTimer(hwnd, ID_SEND_DEFERRED_PASTE, 20, NULL);
    } else if (wParam == ID_SEND_DEFERRED_PASTE) {
      KillTimer(hwnd, ID_SEND_DEFERRED_PASTE);
      if (g_deferredPasteWaitForModifierRelease && AreModifierKeysDown()) {
        SetTimer(hwnd, ID_SEND_DEFERRED_PASTE, 10, NULL);
        break;
      }
      if (g_deferredPasteSimulate) {
        ReleaseAllModifierKeys();
        SendCtrlVInput();
      }
      g_deferredPasteSimulate = false;
      g_deferredPasteWaitForModifierRelease = false;
      IncrementPasteCount();
      // 双击粘贴后保持主窗体隐藏；只有快捷粘贴前窗体原本可见时才恢复它。
      if (g_restoreMainWindowAfterPaste && !g_deferredPasteKeepsTopmostVisible)
        SetTimer(hwnd, ID_RESTORE_TOPMOST_AFTER_PASTE, 180, NULL);
      if (g_deferredPasteKeepsTopmostVisible) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
      }
      g_deferredPasteKeepsTopmostVisible = false;
    } else if (wParam == ID_RESTORE_TOPMOST_AFTER_PASTE) {
      KillTimer(hwnd, ID_RESTORE_TOPMOST_AFTER_PASTE);
      ShowWindow(hwnd, SW_SHOWNOACTIVATE);
      if (g_isTopmost) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
      } else {
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
      }
    } else if (wParam == 2) {
      // 延迟刷新列表项高度
      if (g_hwndListBox) {
        int itemCount = SendMessageW(g_hwndListBox, LB_GETCOUNT, 0, 0);
        for (int i = 0; i < itemCount; i++) {
          SendMessageW(g_hwndListBox, LB_SETITEMHEIGHT, i, 0);
        }
        InvalidateRect(g_hwndListBox, NULL, FALSE);
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
    } else if (wParam == ID_CLIPBOARD_DEBOUNCE_TIMER) {
      // 剪贴板文本防抖：300ms 到期，提交挂起的文本到历史记录
      KillTimer(hwnd, ID_CLIPBOARD_DEBOUNCE_TIMER);
      FlushPendingClipboardText();
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
                InvalidateRect(g_hwndListBox, NULL, FALSE);
              }
            }
          } else {
            // 禁用重绘避免 LB_SETCURSEL 同步绘制选中项（绕过双缓冲导致闪烁）
            SendMessageW(g_hwndListBox, WM_SETREDRAW, FALSE, 0);
            SendMessageW(g_hwndListBox, LB_SETCURSEL, g_contextMenuIndex, 0);
            SendMessageW(g_hwndListBox, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(g_hwndListBox, NULL, FALSE);
          }
        } else {
          return 0; // 不在项上，不显示菜单
        }
      }

      // ===== 子行右键：展开态多文件记录的子行仅显示"删除此行"菜单 =====
      int contextSubIdx =
          (g_contextMenuIndex >= 0 &&
           g_contextMenuIndex < (int)g_displaySubIndexMap.size())
              ? g_displaySubIndexMap[g_contextMenuIndex]
              : -1;
      if (contextSubIdx >= 0) {
        g_contextSubItemDisplay = g_contextMenuIndex;
        InvalidateRect(g_hwndListBox, NULL, FALSE);

        HMENU hSubMenu = CreatePopupMenu();
        HBITMAP hSubDeleteIcon =
            CreateMenuIconBitmap(L"\uE74D", RGB(200, 60, 60));
        MENUITEMINFOW subMii = {};
        subMii.cbSize = sizeof(MENUITEMINFOW);
        subMii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;
        subMii.wID = IDM_DELETE_SUBITEM;
        subMii.dwTypeData = (LPWSTR)T(STR_CTX_DELETE);
        subMii.hbmpItem = hSubDeleteIcon;
        InsertMenuItemW(hSubMenu, GetMenuItemCount(hSubMenu), TRUE, &subMii);

        SetForegroundWindow(hwnd);
        TrackPopupMenu(hSubMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);

        DeleteObject(hSubDeleteIcon);
        DestroyMenu(hSubMenu);
        // 菜单关闭后清除子行高亮（若已执行删除则已被清除，此处兜底Dismiss场景）
        g_contextSubItemDisplay = -1;
        InvalidateRect(g_hwndListBox, NULL, FALSE);
        break;
      }

      // 创建右键菜单（统一使用 AppendMenuW + InsertMenuItemW 末尾追加，
      // 避免硬编码位置导致菜单项错位）
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
          AppendMenuW(hTagSubMenu, MF_GRAYED, 0, T(STR_CTX_NO_TAGS));
        }
        mii.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_BITMAP;
        mii.hSubMenu = hTagSubMenu;
        mii.dwTypeData = (LPWSTR)T(STR_CTX_BATCH_ADD_TAG);
        mii.hbmpItem = hTagIcon;
        InsertMenuItemW(hMenu, GetMenuItemCount(hMenu), TRUE, &mii);

        // 分隔符
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

        // 批量删除
        mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;
        mii.hSubMenu = NULL;
        mii.wID = IDM_DELETE;
        mii.dwTypeData = (LPWSTR)T(STR_CTX_BATCH_DELETE);
        mii.hbmpItem = hDeleteIcon;
        InsertMenuItemW(hMenu, GetMenuItemCount(hMenu), TRUE, &mii);

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
        HBITMAP hEditIcon = CreateMenuIconBitmap(L"\uE70F");  // Edit
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
        mii.dwTypeData = (LPWSTR)T(STR_CTX_COPY);
        mii.hbmpItem = hCopyIcon;
        InsertMenuItemW(hMenu, GetMenuItemCount(hMenu), TRUE, &mii);

        // 执行粘贴
        mii.wID = IDM_PASTE;
        mii.dwTypeData = (LPWSTR)T(STR_CTX_PASTE);
        mii.hbmpItem = hPasteIcon;
        InsertMenuItemW(hMenu, GetMenuItemCount(hMenu), TRUE, &mii);

        // 编辑（仅文本类型）
        if (item.type == TYPE_TEXT) {
          mii.wID = IDM_EDIT;
          mii.dwTypeData = (LPWSTR)T(STR_CTX_EDIT);
          mii.hbmpItem = hEditIcon;
          InsertMenuItemW(hMenu, GetMenuItemCount(hMenu), TRUE, &mii);
        }

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
        mii.dwTypeData = (LPWSTR)T(STR_CTX_TAG);
        mii.hbmpItem = hFavoriteIcon;
        InsertMenuItemW(hMenu, GetMenuItemCount(hMenu), TRUE, &mii);

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
        HBITMAP hSelectIcon =
            NULL; // “在资源管理器中选中”菜单图标（多文件时创建）
        // 多文件记录：用“在资源管理器中选中”替换“打开所在位置”
        // （多文件场景下选中全部文件比仅打开第一个文件所在目录更有用）
        bool isMultiFileItem = (item.type == TYPE_FILE &&
                                item.content.find(L'\n') != std::wstring::npos);
        if (hasFilePath) {
          // 分隔符
          AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

          if (isMultiFileItem) {
            // 多文件记录：显示“在资源管理器中选中”
            hSelectIcon =
                CreateMenuIconBitmap(L"\uE8B7"); // MultiSelectMirrored
            mii.wID = IDM_SELECT_IN_EXPLORER;
            mii.dwTypeData = (LPWSTR)T(STR_CTX_SELECT_IN_EXPLORER);
            mii.hbmpItem = hSelectIcon;
            InsertMenuItemW(hMenu, GetMenuItemCount(hMenu), TRUE, &mii);
          } else {
            // 其他有文件路径的记录：显示“打开所在位置”
            mii.wID = IDM_OPEN_LOCATION;
            mii.dwTypeData = (LPWSTR)T(STR_CTX_OPEN_LOCATION);
            mii.hbmpItem = hOpenLocationIcon;
            InsertMenuItemW(hMenu, GetMenuItemCount(hMenu), TRUE, &mii);
          }
        }

        // 分隔符
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

        // 删除（始终追加到末尾，避免位置计算错误）
        mii.wID = IDM_DELETE;
        mii.dwTypeData = (LPWSTR)T(STR_CTX_DELETE);
        mii.hbmpItem = hDeleteIcon;
        InsertMenuItemW(hMenu, GetMenuItemCount(hMenu), TRUE, &mii);

        SetForegroundWindow(hwnd);
        TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);

        // 释放位图资源
        DeleteObject(hCopyIcon);
        DeleteObject(hPasteIcon);
        DeleteObject(hEditIcon);
        DeleteObject(hFavoriteIcon);
        DeleteObject(hDeleteIcon);
        DeleteObject(hOpenLocationIcon);
        if (hSelectIcon) {
          DeleteObject(hSelectIcon);
        }
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
  case WM_ERASEBKGND: {
    // 不在屏幕 DC 上填充背景，
    // WM_PAINT 的双缓冲（FillRect 内存 DC + BitBlt）会完整覆盖更新区域。
    // 若在此处填充屏幕 DC，会在 BitBlt 前短暂擦除内容，造成闪烁。
    return 1;
  }
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdcScreen = BeginPaint(hwnd, &ps);

    // 双缓冲：先绘制到内存DC，再一次性 BitBlt 到屏幕，消除闪烁
    int paintW = ps.rcPaint.right - ps.rcPaint.left;
    int paintH = ps.rcPaint.bottom - ps.rcPaint.top;
    HDC memDC = CreateCompatibleDC(hdcScreen);
    HBITMAP memBmp = CreateCompatibleBitmap(hdcScreen, paintW, paintH);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
    SetWindowOrgEx(memDC, ps.rcPaint.left, ps.rcPaint.top, NULL);

    // 后续绘制全部使用 memDC
    HDC hdc = memDC;

    // 获取客户区大小
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int clientWidth = clientRect.right - clientRect.left;

    // 填充背景
    HBRUSH hBgBrush = CreateSolidBrush(GetBgColor());
    FillRect(hdc, &clientRect, hBgBrush);
    DeleteObject(hBgBrush);

    // 绘制标题栏背景
    RECT rcTitle = {0, 0, clientWidth, MainTitlebarHeight()};
    HBRUSH hTitleBrush = CreateSolidBrush(GetBgColor());
    FillRect(hdc, &rcTitle, hTitleBrush);
    DeleteObject(hTitleBrush);

    // 绘制窗口标题栏图标
    HICON hIcon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
    if (!hIcon) {
      hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_ICON1));
    }
    if (hIcon) {
      int iconSize = MScale(16);
      DrawIconEx(hdc, MScale(10), (MainTitlebarHeight() - iconSize) / 2, hIcon,
                 iconSize, iconSize, 0, NULL, DI_NORMAL);
    }

    // 绘制标题文字
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetTextColor());
    HFONT hTitleFont = CreateFontW(
        MScale(17), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hTitleFont);
    RECT rcTitleText = {MScale(32), 0, clientWidth - MScale(46) * 4,
                        MainTitlebarHeight()};
    DrawTextW(hdc, L"Smart Clip", -1, &rcTitleText, DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, hOldFont);
    DeleteObject(hTitleFont);

    // 绘制搜索框边框
    if (g_hwndSearchBox) {
      RECT searchRect;
      GetWindowRect(g_hwndSearchBox, &searchRect);
      MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&searchRect, 2);

      // 扩展矩形以绘制边框
      RECT borderRect = {
          searchRect.left - MScale(3), searchRect.top - MScale(3),
          searchRect.right + MScale(3), searchRect.bottom + MScale(3)};

      Graphics graphics(hdc);
      graphics.SetSmoothingMode(SmoothingModeAntiAlias);

      // 绘制圆角矩形边框
      int radius = MScale(8);
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

    // 文件中转站蒙版：半透明遮罩 + 中央虚线框（file.png 图标 + 提示文字），
    // 仅在拖拽呼出（g_dragShelfSummoned）期间显示，此时子控件已隐藏
    if (g_dragShelfSummoned) {
      Graphics gMask(hdc);
      gMask.SetSmoothingMode(SmoothingModeAntiAlias);

      // 半透明蒙版覆盖整个客户区
      SolidBrush maskBrush(Color(150, 0, 0, 0));
      gMask.FillRectangle(&maskBrush, 0, 0, clientWidth,
                          clientRect.bottom - clientRect.top);

      // 中央虚线圆角框
      const int boxW = std::min(clientWidth - MScale(40), MScale(380));
      const int boxH = MScale(180);
      const int cx = clientWidth / 2;
      const int cy = (clientRect.bottom - clientRect.top) / 2;
      const RECT box = {cx - boxW / 2, cy - boxH / 2, cx + boxW / 2,
                        cy + boxH / 2};

      const int radius = MScale(10);
      GraphicsPath boxPath;
      boxPath.AddArc(box.left, box.top, radius * 2, radius * 2, 180, 90);
      boxPath.AddArc(box.right - radius * 2, box.top, radius * 2, radius * 2,
                     270, 90);
      boxPath.AddArc(box.right - radius * 2, box.bottom - radius * 2,
                     radius * 2, radius * 2, 0, 90);
      boxPath.AddArc(box.left, box.bottom - radius * 2, radius * 2, radius * 2,
                     90, 90);
      boxPath.CloseFigure();
      Pen dashPen(Color(255, 0x00, 0x90, 0xFE), (REAL)MScale(2));
      dashPen.SetDashStyle(DashStyleDash);
      gMask.DrawPath(&dashPen, &boxPath);

      // 框内居中：file.png 图标在上，提示文字在下。
      // 提示文字按当前语言实测高度：中文单行；外文较长时在框宽内
      // 自动换行（GDI+ DrawString 依 RectF 自动 wrap），整体仍垂直居中
      const int iconSize = MScale(52);
      const int textGap = MScale(14);
      Font fontMask(L"Microsoft YaHei", (REAL)MScale(14));
      StringFormat sfMask;
      sfMask.SetAlignment(StringAlignmentCenter);
      sfMask.SetLineAlignment(StringAlignmentCenter);
      RectF layoutAll((REAL)box.left, (REAL)box.top, (REAL)boxW, (REAL)boxH);
      RectF measured = {};
      gMask.MeasureString(T(STR_DRAG_SHELF_HINT), -1, &fontMask, layoutAll,
                          &sfMask, &measured);
      // 文字高度上限：框高扣除图标与间距，防止极端长文本溢出
      REAL textH = measured.Height + (REAL)MScale(4);
      const REAL maxTextH = (REAL)(boxH - iconSize - textGap - MScale(12));
      if (textH > maxTextH)
        textH = maxTextH;
      const int totalH = iconSize + textGap + (int)textH;
      const int iconY = box.top + (boxH - totalH) / 2;
      if (g_imgFileIcon) {
        gMask.DrawImage(g_imgFileIcon, cx - iconSize / 2, iconY, iconSize,
                        iconSize);
      }

      SolidBrush textBrush(Color(255, 255, 255, 255));
      gMask.DrawString(T(STR_DRAG_SHELF_HINT), -1, &fontMask,
                       RectF((REAL)box.left, (REAL)(iconY + iconSize + textGap),
                             (REAL)boxW, textH),
                       &sfMask, &textBrush);
    }

    // 将内存DC一次性复制到屏幕
    BitBlt(hdcScreen, ps.rcPaint.left, ps.rcPaint.top, paintW, paintH, memDC,
           ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);

    EndPaint(hwnd, &ps);
    break;
  }
  case WM_CLOSE: {
    KillTimer(hwnd, ID_FAVORITE_TOOLTIP_TIMER);
    HideFavoriteFilterTooltip();
    CloseTagPopup();
    // 清除不抢焦点模式
    ExitNoActivateMode(hwnd);
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
    KillTimer(hwnd, ID_DRAG_SHELF_TIMER);
    KillTimer(hwnd, ID_DRAG_SHELF_SETTLE);
    // 退出前提交挂起的剪贴板文本（避免丢失最后一次复制）
    if (g_clipboardTextPending) {
      KillTimer(hwnd, ID_CLIPBOARD_DEBOUNCE_TIMER);
      FlushPendingClipboardText();
    }
    RemoveClipboardFormatListener(hwnd);
    RemoveTrayIcon();
    UnregisterHotkey(hwnd);
    UnregisterQuickPasteHotkeys(hwnd);
    UnregisterFavoriteHotkeys(hwnd);
    SaveHistory();
    SaveHotkeySettings(); // 退出时保存快捷键设置，防止丢失
    // 清理图标缓存
    ClearIconCache();
    // 释放文件类型图标缓存
    FreeFileIconCache();
    // 释放按钮图片资源
    FreeButtonImages();
    // 释放列表字体缓存
    CleanupListFonts();
    // 确保重置剪贴板恢复标志，避免下次启动时无法录入
    g_isRestoringClipboard = false;
    PostQuitMessage(0);
    break;
  }

  case WM_CLIPBOARDUPDATE: {
    // 暂停监听期间不录入新剪贴板内容
    if (g_isClipboardPaused) {
      break;
    }
    if (!g_isRestoringClipboard && OpenClipboard(NULL)) {
      // 优先处理文件路径
      if (IsClipboardFormatAvailable(CF_HDROP)) {
        // 非文本格式到来时，立即提交挂起的文本（避免丢失）
        FlushPendingClipboardText();
        HGLOBAL hGlobal = GetClipboardData(CF_HDROP);
        if (hGlobal != NULL) {
          HDROP hDrop = (HDROP)GlobalLock(hGlobal);
          if (hDrop != NULL) {
            UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
            if (fileCount == 1) {
              // 单文件：保持原逻辑（区分文件夹/图片/普通文件）
              WCHAR filePath[MAX_PATH];
              if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH) > 0) {
                // 检查是否为文件夹
                DWORD attrs = GetFileAttributesW(filePath);
                if (attrs != INVALID_FILE_ATTRIBUTES &&
                    (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                  // 是文件夹，保存路径
                  AddFileToHistory(filePath);
                }
                // 图片文件和其他文件统一存为 TYPE_FILE
                else {
                  AddFileToHistory(filePath);
                }
              }
            } else if (fileCount > 1) {
              // 多文件：所有文件统一用 L'\n' 连接后存为 TYPE_FILE 记录
              std::wstring joinedPaths;
              joinedPaths.reserve(fileCount * MAX_PATH);
              for (UINT i = 0; i < fileCount; ++i) {
                WCHAR filePath[MAX_PATH];
                if (DragQueryFileW(hDrop, i, filePath, MAX_PATH) > 0) {
                  if (!joinedPaths.empty())
                    joinedPaths.push_back(L'\n');
                  joinedPaths += filePath;
                }
              }
              if (!joinedPaths.empty()) {
                AddFilesToHistory(joinedPaths, nullptr);
              }
            }
            GlobalUnlock(hGlobal);
          }
        }
      }
      // 处理位图图像
      else if (IsClipboardFormatAvailable(CF_DIB)) {
        // 非文本格式到来时，立即提交挂起的文本（避免丢失）
        FlushPendingClipboardText();
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
      // 处理Unicode文本格式（300ms 防抖，合并多步写入，优先保留 md 格式）
      else if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HGLOBAL hGlobal = GetClipboardData(CF_UNICODETEXT);
        if (hGlobal != NULL) {
          wchar_t *pData = (wchar_t *)GlobalLock(hGlobal);
          if (pData != NULL) {
            std::wstring content(pData);
            ScheduleDebouncedTextCapture(hwnd, content);
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
              ScheduleDebouncedTextCapture(hwnd, content);
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
        // 清除不抢焦点模式
        ExitNoActivateMode(hwnd);
        ShowWindow(hwnd, SW_HIDE);
      } else {
        // 托盘点击使用正常激活模式
        ExitNoActivateMode(hwnd);
        SetMainWindowTaskbarStyle(hwnd, false);
        // 保持隐藏前的最大化/普通状态；最小化时恢复到最小化前状态
        if (g_startupMaximized && !IsZoomed(hwnd)) {
          g_startupMaximized = false; // 一次性消费：恢复上次退出时的最大化
          ShowWindow(hwnd, SW_MAXIMIZE);
        } else {
          ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
        }
        // 确保窗口显示在最前
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
      }
    } else if (lParam == WM_RBUTTONUP) {
      // 创建独立的托盘菜单
      HMENU hTrayMenu = CreatePopupMenu();

      // 创建菜单图标
      HBITMAP hSettingsIcon =
          CreateMenuIconBitmap(L"\uE713", RGB(60, 60, 60), 3); // Settings
      HBITMAP hNotificationIcon = CreateMenuIconBitmap(
          g_isNotificationEnabled ? L"\uEA8F" : L"\uE7ED", RGB(60, 60, 60),
          3); // Ringer/RingerOff
      HBITMAP hPauseIcon = CreateMenuIconBitmap(
          g_isClipboardPaused ? L"\uE768" : L"\uE769", RGB(60, 60, 60),
          3); // Play/Pause
      // 快捷键总开关图标：启用时显示键盘高亮，关闭时显示禁用键盘
      HBITMAP hQuickPasteIcon = CreateMenuIconBitmap(
          g_allHotkeysEnabled ? L"\uE765" : L"\uE8D8", RGB(60, 60, 60),
          3); // Keyboard/KeyboardClassic
      HBITMAP hLightModeIcon = CreateMenuIconBitmap(L"\uE706", RGB(60, 60, 60),
                                                    3); // Brightness (太阳)
      HBITMAP hDarkModeIcon =
          CreateMenuIconBitmap(L"\uE708", RGB(60, 60, 60), 3); // Moon (月亮)
      HBITMAP hExitIcon = CreateMenuIconBitmap(L"\uE7E8", RGB(200, 60, 60),
                                               3); // Power (红色)
      HBITMAP hRestartIcon = CreateMenuIconBitmap(L"\uE72C", RGB(60, 60, 60),
                                                  3); // Refresh (重启)

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

      // 暂停/恢复剪贴板监听
      mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;
      mii.fState = 0;
      mii.wID = IDM_PAUSE_RESUME;
      mii.dwTypeData = (LPWSTR)(g_isClipboardPaused ? T(STR_TRAY_MENU_RESUME)
                                                    : T(STR_TRAY_MENU_PAUSE));
      mii.hbmpItem = hPauseIcon;
      InsertMenuItemW(hTrayMenu, 2, TRUE, &mii);

      // 启用/关闭快捷键（带勾选状态）
      mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP | MIIM_STATE;
      mii.wID = IDM_QUICK_PASTE_TOGGLE;
      mii.dwTypeData =
          (LPWSTR)(g_allHotkeysEnabled ? T(STR_TRAY_MENU_QUICK_PASTE_DISABLE)
                                       : T(STR_TRAY_MENU_QUICK_PASTE_ENABLE));
      mii.hbmpItem = hQuickPasteIcon;
      mii.fState = g_allHotkeysEnabled ? MFS_CHECKED : MFS_UNCHECKED;
      InsertMenuItemW(hTrayMenu, 3, TRUE, &mii);

      int insertIndex = 4;

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
      mii.wID = IDM_RESTART;
      mii.dwTypeData = (LPWSTR)T(STR_TRAY_MENU_RESTART);
      mii.hbmpItem = hRestartIcon;
      InsertMenuItemW(hTrayMenu, insertIndex++, TRUE, &mii);

      mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;
      mii.fState = 0;
      mii.wID = IDM_EXIT;
      mii.dwTypeData = (LPWSTR)T(STR_TRAY_MENU_EXIT);
      mii.hbmpItem = hExitIcon;
      InsertMenuItemW(hTrayMenu, insertIndex, TRUE, &mii);

      // 显示托盘菜单（使用 TPM_RETURNCMD 确保菜单完全关闭后再处理命令，
      // 避免在菜单模态循环中创建设置对话框导致偶现打不开）
      POINT pt;
      GetCursorPos(&pt);
      SetForegroundWindow(hwnd);
      int cmd = TrackPopupMenu(hTrayMenu,
                               TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_RETURNCMD,
                               pt.x, pt.y, 0, hwnd, NULL);
      PostMessage(hwnd, WM_NULL, 0, 0);
      if (cmd) {
        // 使用 PostMessageW 异步派发，确保 WM_NULL 先被处理以释放菜单前台锁，
        // 避免 ShowSettingsDialog 中 SetForegroundWindow 失败导致窗体打不开
        PostMessageW(hwnd, WM_COMMAND, cmd, 0);
      }

      // 释放菜单资源和位图
      DestroyMenu(hTrayMenu);
      DeleteObject(hSettingsIcon);
      DeleteObject(hNotificationIcon);
      DeleteObject(hPauseIcon);
      DeleteObject(hQuickPasteIcon);
      DeleteObject(hLightModeIcon);
      DeleteObject(hDarkModeIcon);
      DeleteObject(hExitIcon);
      DeleteObject(hRestartIcon);
    }
    break;
  }
  case WM_HOTKEY: {
    // 托盘快捷键总开关关闭时不响应任何快捷键
    if (!g_allHotkeysEnabled)
      break;
    // 处理快捷键按下事件
    if (wParam == ID_HOTKEY_TOGGLE) {
      // 切换窗口可见性
      if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
        CloseTagPopup();
        // 隐藏时清除不抢焦点模式
        ExitNoActivateMode(hwnd);
        ShowWindow(hwnd, SW_HIDE);
      } else {
        // 记录当前活动窗口（呼出剪贴板前的窗口）
        g_previousActiveWindow = GetForegroundWindow();
        // 记录原窗口中拥有焦点的子窗口（如资源管理器地址栏编辑框）
        // 使用 AttachThreadInput 跨线程获取 GetFocus
        g_previousFocusWindow = NULL;
        if (g_previousActiveWindow != NULL) {
          DWORD targetThread =
              GetWindowThreadProcessId(g_previousActiveWindow, NULL);
          DWORD currentThread = GetCurrentThreadId();
          if (targetThread != currentThread) {
            AttachThreadInput(currentThread, targetThread, TRUE);
            g_previousFocusWindow = GetFocus();
            AttachThreadInput(currentThread, targetThread, FALSE);
          } else {
            g_previousFocusWindow = GetFocus();
          }
        }
        // 正常激活模式：SmartClip 获取焦点，搜索框可直接输入
        g_isNoActivateMode = false;
        SetMainWindowTaskbarStyle(hwnd, false);
        // 显示并激活窗口：SW_SHOW 保持隐藏前的最大化/普通状态
        // （SW_SHOWNORMAL 会把最大化窗口还原成普通尺寸）；
        // 最小化时用 SW_RESTORE 恢复到最小化前的状态
        if (g_startupMaximized && !IsZoomed(hwnd)) {
          g_startupMaximized = false; // 一次性消费：恢复上次退出时的最大化
          ShowWindow(hwnd, SW_MAXIMIZE);
        } else {
          ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
        }
        // 根据置顶状态设置 z-order，避免 TOPMOST→NOTOPMOST 技巧导致
        // 最大化后窗口意外保持置顶
        if (g_isTopmost) {
          SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        } else {
          SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        }
        SetForegroundWindow(hwnd);
        // 延迟聚焦搜索框，确保窗口操作全部完成后再设置焦点，
        // 避免输入过程中光标被重置
        PostMessageW(g_hwndMain, WM_USER + 0x1000, 0, 0);
      }
    } else if (wParam >= ID_HOTKEY_PASTE_1 && wParam <= ID_HOTKEY_PASTE_10) {
      int pasteOffset = (int)(wParam - ID_HOTKEY_PASTE_1);
      int visibleIds[10] = {};
      int visibleCount = CollectVisibleShortcutDisplayIndices(visibleIds, 10);
      if (pasteOffset >= 0 && pasteOffset < visibleCount) {
        if (PasteHistoryItemByDisplayIndex(hwnd, visibleIds[pasteOffset]) &&
            g_isNotificationEnabled) {
          ShowTrayBalloon(hwnd, T(STR_TRAY_QUICK_PASTE_TITLE),
                          T(STR_TRAY_PASTED));
        }
      }
    } else if (wParam >= ID_HOTKEY_FAVORITE_1 &&
               wParam <= ID_HOTKEY_FAVORITE_9) {
      int favOffset = (int)(wParam - ID_HOTKEY_FAVORITE_1);
      // 收藏快捷键对应收藏下拉列表中的标签（前9个）
      if (favOffset >= 0 && favOffset < (int)g_tags.size()) {
        int tagId = g_tags[favOffset].id;
        // 切换到收藏页并按对应标签筛选
        if (g_currentTab != 4) {
          SwitchMainPanel(hwnd, 4, true);
        }
        g_currentFilterTagId = tagId;
        UpdateListBox();
        ResetShortcutAssignment();
        InvalidateRect(g_hwndFilterFavorite, NULL, FALSE);
      }
    }
    break;
  }
  default:
    return DefWindowProcW(hwnd, message, wParam, lParam);
  }
  return 0;
}

// 从嵌入的 RCDATA PNG 资源加载 HICON（完全绕过 ICO 资源和文件路径，MSIX
// 兼容）
static HICON LoadAppIconFromResource() {
  HMODULE hModule = GetModuleHandle(NULL);
  HRSRC hRes = FindResource(hModule, MAKEINTRESOURCE(IDB_APP_ICON), RT_RCDATA);
  if (!hRes)
    return NULL;
  DWORD dataSize = SizeofResource(hModule, hRes);
  if (dataSize == 0)
    return NULL;
  HGLOBAL hGlobal = LoadResource(hModule, hRes);
  if (!hGlobal)
    return NULL;
  void *pData = LockResource(hGlobal);
  if (!pData)
    return NULL;

  HGLOBAL hBuf = GlobalAlloc(GMEM_MOVEABLE, dataSize);
  if (!hBuf)
    return NULL;
  void *pBuf = GlobalLock(hBuf);
  if (!pBuf) {
    GlobalFree(hBuf);
    return NULL;
  }
  memcpy(pBuf, pData, dataSize);
  GlobalUnlock(hBuf);

  IStream *pStream = NULL;
  if (CreateStreamOnHGlobal(hBuf, TRUE, &pStream) != S_OK) {
    GlobalFree(hBuf);
    return NULL;
  }
  Gdiplus::Bitmap *pBitmap = Gdiplus::Bitmap::FromStream(pStream);
  pStream->Release();
  if (!pBitmap || pBitmap->GetLastStatus() != Gdiplus::Ok) {
    delete pBitmap;
    return NULL;
  }
  HICON hIcon = NULL;
  pBitmap->GetHICON(&hIcon);
  delete pBitmap;
  return hIcon;
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

  // 从嵌入的 PNG 资源加载图标（MSIX 兼容），失败时回退到嵌入式 ICO
  wcex.hIcon = LoadAppIconFromResource();
  if (wcex.hIcon == NULL) {
    wcex.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON1));
  }
  if (wcex.hIcon == NULL) {
    wcex.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
  }

  wcex.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
  wcex.hbrBackground = CreateSolidBrush(RGB(245, 245, 245)); // 自定义背景色
  wcex.lpszMenuName = NULL;
  wcex.lpszClassName = L"SmartClip";

  wcex.hIconSm = LoadAppIconFromResource();
  if (wcex.hIconSm == NULL) {
    wcex.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON1));
  }
  if (wcex.hIconSm == NULL) {
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
  g_mainUiDpi = GetSmartClipUiDpi(NULL);
  int screenWidth = GetSystemMetrics(SM_CXSCREEN);
  int screenHeight = GetSystemMetrics(SM_CYSCREEN);
  int windowWidth = MScale(600);
  int windowHeight = MScale(694);
  int x = (screenWidth - windowWidth) / 2;
  int y = (screenHeight - windowHeight) / 2;

  // 创建时默认使用 WS_EX_TOOLWINDOW 防止 MSIX 环境下窗口还没应用设置就被
  // Shell 抓取到任务栏 WM_CREATE 中会调用 ApplyTaskbarVisibility
  // 根据用户设置更新为正确的样式
  g_hwndMain =
      CreateWindowExW(WS_EX_TOOLWINDOW, L"SmartClip", L"Smart Clip",
                      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, windowWidth,
                      windowHeight, NULL, NULL, hInstance, NULL);

  if (!g_hwndMain) {
    return FALSE;
  }

  // 修改窗口样式：移除系统标题栏但保留边框
  // 注意：保留 WS_SYSMENU 和 WS_MINIMIZEBOX，否则任务栏图标点击无响应
  LONG_PTR style = GetWindowLongPtrW(g_hwndMain, GWL_STYLE);
  style &= ~(WS_CAPTION | WS_THICKFRAME);
  style |= WS_THICKFRAME;  // 重新添加可调整大小的边框
  style |= WS_MAXIMIZEBOX; // 允许最大化
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

  // 启动时窗口显示策略：
  // - 桌面版开机自启动（wWinMain 中将 nCmdShow 改为
  // SW_SHOWMINNOACTIVE）：保持隐藏，
  //   用户从托盘唤起，避免开机即弹窗打扰用户。
  // - MSIX 开机自启动：MSIX StartupTask 启动时不传 -minimized 参数，无法通过
  //   命令行区分。通过检测系统启动后 2 分钟内启动来推断开机自启，隐藏窗口。
  // - 其他场景（第一次运行、退出后重新启动、托盘"重启应用"）：显示主窗口，
  //   让用户能立即看到应用界面。
  bool isAutostart = (nCmdShow == SW_SHOWMINNOACTIVE);
  if (!isAutostart) {
    // MSIX 环境下检测开机自启：路径包含 WindowsApps + 系统启动后 2 分钟内
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring pathStr(exePath);
    if (pathStr.find(L"WindowsApps") != std::wstring::npos &&
        GetTickCount64() < 2 * 60 * 1000) {
      isAutostart = true;
    }
  }
  int startupShowCmd = isAutostart ? SW_HIDE : SW_SHOWNORMAL;
  if (!isAutostart && g_startupMaximized) {
    // 非自启场景直接按上次退出时的最大化状态显示
    g_startupMaximized = false;
    startupShowCmd = SW_MAXIMIZE;
  }
  ShowWindow(g_hwndMain, startupShowCmd);
  UpdateWindow(g_hwndMain);

  return TRUE;
}

// 运行应用程序
int RunApplication() {
  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0)) {
    if (HandleMainNavigationKey(msg))
      continue;

    // 处理应用内搜索框快捷键（受总开关 g_allHotkeysEnabled 控制）
    if (msg.message == WM_KEYDOWN && g_isSearchHotkeyEnabled &&
        g_allHotkeysEnabled && IsWindowVisible(g_hwndMain)) {
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

// ==================== 首次运行用户协议 ====================

// 用户协议文本版本：修改协议文案时递增，已接受过旧版本的用户会被
// 要求重新确认一次；未修改协议则保持相同版本，不再反复弹出。
static const int kAgreementVersion = 1;

static std::wstring GetAgreementFilePath() {
  return GetSmartClipDataDir() + L"\\agreement_accepted";
}

static bool IsAgreementAccepted() {
  // 优先检查数据库 settings 表（新方案），同时兼容旧的文件标记
  if (IsAgreementAcceptedInDb())
    return true;
  std::wstring path = GetAgreementFilePath();
  DWORD attr = GetFileAttributesW(path.c_str());
  return (attr != INVALID_FILE_ATTRIBUTES &&
          !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

// 已接受的协议版本：读取数据库记录；旧数据未记录版本时视为已接受
// 当前版本，避免升级到带版本机制后误弹一次。
static int GetAgreementAcceptedVersion() {
  if (!IsAgreementAccepted())
    return 0;
  int v = DbGetSettingInt("agreement_version", 0);
  return v > 0 ? v : kAgreementVersion;
}

static void SaveAgreementAccepted() {
  // 写入数据库 settings 表，并附带时间戳的事件记录
  RecordAgreementAction(L"accepted");
  // 记录当前接受的协议版本，协议文案更新后据此重新确认
  DbSetSettingInt("agreement_version", kAgreementVersion);
  // 同时写入文件标记作为备份：当数据库暂时不可用（如 WAL 恢复期间）
  // IsAgreementAcceptedInDb 可能失败，文件标记确保不会误弹协议窗口
  std::wstring path = GetAgreementFilePath();
  HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(hFile, "1", 1, &written, NULL);
    CloseHandle(hFile);
  }
}

static UINT g_agreementDpi = 96;
static HFONT g_hAgreementTitleFont = NULL;    // 主标题
static HFONT g_hAgreementSubtitleFont = NULL; // 副标题
static HFONT g_hAgreementSectionFont = NULL;  // 卡片小标题
static HFONT g_hAgreementBodyFont = NULL;     // 正文
static HFONT g_hAgreementBtnFont = NULL;      // 按钮
static HFONT g_hAgreementVersionFont = NULL;  // 版本号
static HFONT g_hAgreementRemindFont = NULL;   // 底部提示
static bool g_agreementAccepted = false;
static bool g_agreementDone = false;

// 滚动状态
static int g_agreeScrollOffset = 0;
static int g_agreeContentHeight = 0;
static bool g_agreeScrolledToBottom =
    false; // 滚动位置标记（仅用于显示提示，不再限制同意按钮）
static bool g_agreeScrollbarHover = false;
static bool g_agreeScrollbarDrag = false;
static int g_agreeDragStartY = 0;
static int g_agreeDragStartOffset = 0;

// 按钮状态
static bool g_agreeAcceptHover = false;
static bool g_agreeAcceptPressed = false;
static bool g_agreeDeclineHover = false;
static bool g_agreeDeclinePressed = false;

// 右上角关闭按钮状态
static bool g_agreeCloseHover = false;
static bool g_agreeClosePressed = false;

// 获取关闭按钮矩形（右上角，借鉴主窗体风格）
static RECT AgreementGetCloseBtnRect(HWND hwnd) {
  int dpiVal = (int)g_agreementDpi;
  int btnW = MulDiv(40, dpiVal, 96);
  int btnH = MulDiv(32, dpiVal, 96);
  RECT rcClient;
  GetClientRect(hwnd, &rcClient);
  RECT rc = {rcClient.right - btnW, 0, rcClient.right, btnH};
  return rc;
}

// 绘制关闭按钮（借鉴主窗体标题栏关闭按钮风格）
static void AgreementDrawCloseButton(Graphics &g, HDC hdc, HWND hwnd) {
  RECT rc = AgreementGetCloseBtnRect(hwnd);
  int w = rc.right - rc.left;
  int h = rc.bottom - rc.top;

  // 背景：hover 时红色，否则透明
  COLORREF bgClr;
  if (g_agreeCloseHover) {
    bgClr = RGB(232, 17, 35); // 关闭按钮悬浮红色（与主窗体一致）
  } else {
    bgClr = GetThemeWindowBgColor();
  }
  SolidBrush bgBrush(
      Color(255, GetRValue(bgClr), GetGValue(bgClr), GetBValue(bgClr)));
  g.FillRectangle(&bgBrush, rc.left, rc.top, w, h);

  // 图标：使用 Segoe MDL2 Assets ChromeClose \uE8BB
  HFONT hIconFont = CreateFontW(
      MulDiv(11, (int)g_agreementDpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
      FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
  HFONT oldFont = (HFONT)SelectObject(hdc, hIconFont);
  SetBkMode(hdc, TRANSPARENT);
  // hover 时白色，否则使用主题文字色
  COLORREF iconClr =
      g_agreeCloseHover ? RGB(255, 255, 255) : GetThemeTextPrimaryColor();
  SetTextColor(hdc, iconClr);
  const wchar_t *icon = L"\uE8BB"; // ChromeClose
  DrawTextW(hdc, icon, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(hdc, oldFont);
  DeleteObject(hIconFont);
}

// 布局常量（设计像素）— Web 风格更紧凑
enum {
  kAgreeDlgW = 720,
  kAgreeDlgH = 560,
  kAgreeHeaderH = 80,
  kAgreeFooterH = 76,
  kAgreePadX = 36,
  kAgreeScrollbarW = 8,
  kAgreeScrollbarRightPad = 8,
  kAgreeCardRadius = 12,
  kAgreeCardSpacing = 16,
  kAgreeCardPad = 24,
  kAgreeBtnW = 138,
  kAgreeBtnH = 48,
  kAgreeBtnGap = 14,
};

// 计算多行文本高度
static int AgreementCalcTextHeight(HDC hdc, const wchar_t *text, int width,
                                   HFONT font) {
  if (!text || !hdc || !font)
    return 0;
  HFONT oldFont = (HFONT)SelectObject(hdc, font);
  RECT rc = {0, 0, width, 0};
  DrawTextW(hdc, text, -1, &rc, DT_CALCRECT | DT_WORDBREAK);
  SelectObject(hdc, oldFont);
  return rc.bottom - rc.top;
}

// 计算内容总高度（卡片1 + 卡片2 + 间距）
static int AgreementMeasureContent(HWND hwnd) {
  int dpiVal = (int)g_agreementDpi;
  auto scale = [dpiVal](int v) { return MulDiv(v, dpiVal, 96); };

  int cardWidth = MulDiv(kAgreeDlgW - kAgreePadX * 2, dpiVal, 96);
  int textWidth = cardWidth - scale(kAgreeCardPad) * 2;

  HDC hdc = GetDC(hwnd);
  int eulaBodyH = AgreementCalcTextHeight(hdc, T(STR_EULA_BODY), textWidth,
                                          g_hAgreementBodyFont);
  int privacyBodyH = AgreementCalcTextHeight(hdc, T(STR_PRIVACY_BODY),
                                             textWidth, g_hAgreementBodyFont);
  ReleaseDC(hwnd, hdc);

  int card1H = scale(kAgreeCardPad) + scale(22) + scale(8) + eulaBodyH +
               scale(kAgreeCardPad);
  int card2H = scale(kAgreeCardPad) + scale(22) + scale(8) + privacyBodyH +
               scale(kAgreeCardPad);
  return card1H + scale(kAgreeCardSpacing) + card2H;
}

// 获取内容区可见矩形（不含顶部标题区和底部按钮区）
static void AgreementGetContentViewport(HWND hwnd, RECT *rc) {
  if (!rc)
    return;
  int dpiVal = (int)g_agreementDpi;
  auto scale = [dpiVal](int v) { return MulDiv(v, dpiVal, 96); };
  RECT rcClient;
  GetClientRect(hwnd, &rcClient);
  rc->left = scale(kAgreePadX);
  rc->top = scale(kAgreeHeaderH);
  rc->right = rcClient.right - scale(kAgreePadX);
  rc->bottom = rcClient.bottom - scale(kAgreeFooterH);
}

// 获取滚动条 thumb 矩形
static bool AgreementGetScrollbarThumb(HWND hwnd, RECT *rcThumb) {
  if (!rcThumb)
    return false;
  RECT rcView;
  AgreementGetContentViewport(hwnd, &rcView);
  int viewH = rcView.bottom - rcView.top;
  if (g_agreeContentHeight <= viewH)
    return false;

  int trackH = viewH;
  int thumbH = (viewH * viewH) / g_agreeContentHeight;
  if (thumbH < 30)
    thumbH = 30;
  if (thumbH > trackH)
    thumbH = trackH;

  int travel = trackH - thumbH;
  int maxScroll = g_agreeContentHeight - viewH;
  int thumbY = 0;
  if (maxScroll > 0)
    thumbY = (g_agreeScrollOffset * travel + maxScroll / 2) / maxScroll;

  int dpiVal = (int)g_agreementDpi;
  int sbW = MulDiv(kAgreeScrollbarW, dpiVal, 96);
  int sbRightPad = MulDiv(kAgreeScrollbarRightPad, dpiVal, 96);

  rcThumb->left = rcView.right - sbRightPad - sbW;
  rcThumb->top = rcView.top + thumbY;
  rcThumb->right = rcView.right - sbRightPad;
  rcThumb->bottom = rcThumb->top + thumbH;
  return true;
}

// 限制滚动偏移到合法范围
static void AgreementClampScrollOffset(HWND hwnd) {
  RECT rcView;
  AgreementGetContentViewport(hwnd, &rcView);
  int viewH = rcView.bottom - rcView.top;
  int maxScroll = g_agreeContentHeight - viewH;
  if (maxScroll < 0)
    maxScroll = 0;
  if (g_agreeScrollOffset < 0)
    g_agreeScrollOffset = 0;
  if (g_agreeScrollOffset > maxScroll)
    g_agreeScrollOffset = maxScroll;
  // 更新"已滚动到底"标志：允许 1px 容差
  g_agreeScrolledToBottom = (g_agreeScrollOffset >= maxScroll);
}

// 按行滚动（鼠标滚轮）
static void AgreementScrollBy(HWND hwnd, int delta) {
  int oldOffset = g_agreeScrollOffset;
  int lineH = MulDiv(28, (int)g_agreementDpi, 96);
  g_agreeScrollOffset -= delta * lineH / WHEEL_DELTA;
  AgreementClampScrollOffset(hwnd);
  if (g_agreeScrollOffset != oldOffset) {
    InvalidateRect(hwnd, NULL, FALSE);
  }
}

// 绘制圆角矩形填充
static void AgreementFillRoundRect(Graphics &g, int x, int y, int w, int h,
                                   int r, COLORREF fill) {
  GraphicsPath path;
  CreateRoundRectPath(&path, x, y, w, h, r);
  SolidBrush brush(
      Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
  g.FillPath(&brush, &path);
}

// 绘制圆角矩形边框
static void AgreementDrawRoundBorder(Graphics &g, int x, int y, int w, int h,
                                     int r, COLORREF border, float penWidth) {
  GraphicsPath path;
  CreateRoundRectPath(&path, x, y, w, h, r);
  Pen pen(Color(255, GetRValue(border), GetGValue(border), GetBValue(border)),
          penWidth);
  g.DrawPath(&pen, &path);
}

// ==================== Web 风格辅助函数 ====================

// GDI+ 字符串格式化（垂直居中、水平居中）
static StringFormat *AgreementLeftFormat() {
  static StringFormat fmt;
  fmt.SetAlignment(StringAlignmentNear);
  fmt.SetLineAlignment(StringAlignmentCenter);
  fmt.SetFormatFlags(StringFormatFlagsNoWrap);
  return &fmt;
}

// 现代化按钮：图标 + 文字（Web 风格）
static void AgreementDrawIconButton(Graphics &g, HDC hdc, const RECT &rc,
                                    const wchar_t *icon, const wchar_t *text,
                                    bool isPrimary, bool hovered, bool pressed,
                                    bool disabled = false) {
  int dpiVal = (int)g_agreementDpi;
  int radius = MulDiv(10, dpiVal, 96);
  int w = rc.right - rc.left;
  int h = rc.bottom - rc.top;

  // 配色
  COLORREF fill, border;
  Gdiplus::Color gdiFill;
  if (isPrimary) {
    COLORREF accent = GetThemeAccentColor();
    COLORREF accentStrong = GetThemeAccentStrongColor();
    if (disabled) {
      // 禁用：浅色背景
      fill = g_isDarkMode ? RGB(60, 63, 70) : RGB(200, 203, 208);
    } else if (pressed) {
      fill = accentStrong;
    } else if (hovered) {
      // hover: 加深 10%
      int r = GetRValue(accent) * 9 / 10;
      int gC = GetGValue(accent) * 9 / 10;
      int b = GetBValue(accent) * 9 / 10;
      fill = RGB(std::min(r, 255), std::min(gC, 255), std::min(b, 255));
    } else {
      fill = accent;
    }
    border = fill;
    gdiFill =
        Gdiplus::Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill));
  } else {
    if (pressed) {
      fill = g_isDarkMode ? RGB(46, 49, 56) : RGB(224, 227, 232);
    } else if (hovered) {
      fill = g_isDarkMode ? RGB(40, 43, 48) : RGB(232, 235, 240);
    } else {
      fill = g_isDarkMode ? RGB(34, 36, 40) : RGB(246, 245, 243);
    }
    border = g_isDarkMode ? RGB(62, 64, 70) : RGB(210, 214, 220);
    gdiFill =
        Gdiplus::Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill));
  }

  // 绘制背景
  GraphicsPath path;
  CreateRoundRectPath(&path, rc.left, rc.top, w, h, radius);
  SolidBrush bgBrush(gdiFill);
  g.FillPath(&bgBrush, &path);

  // 边框（仅 secondary）
  if (!isPrimary) {
    Pen borderPen(
        Color(255, GetRValue(border), GetGValue(border), GetBValue(border)),
        1.0f);
    g.DrawPath(&borderPen, &path);
  }

  // 不再绘制按钮左侧图标，文字水平居中（适配高分辨率）
  (void)icon;
  Gdiplus::Font gdiFont(hdc, g_hAgreementBtnFont);
  Gdiplus::RectF measureBox(0, 0, (REAL)w, (REAL)h);
  Gdiplus::RectF textBounds;
  g.MeasureString(text, -1, &gdiFont, measureBox, AgreementLeftFormat(),
                  &textBounds);
  int textW = (int)textBounds.Width;
  int startX = rc.left + (w - textW) / 2;

  // 绘制文字（GDI+ 抗锯齿）
  COLORREF textClr;
  if (disabled) {
    textClr = g_isDarkMode ? RGB(120, 120, 120) : RGB(140, 140, 140);
  } else {
    textClr = isPrimary ? RGB(255, 255, 255) : GetThemeTextPrimaryColor();
  }
  SolidBrush textBrush(
      Color(255, GetRValue(textClr), GetGValue(textClr), GetBValue(textClr)));
  RectF textRect((REAL)startX, (REAL)rc.top, (REAL)textW + 4, (REAL)h);
  g.DrawString(text, -1, &gdiFont, textRect, AgreementLeftFormat(), &textBrush);
}

static LRESULT CALLBACK AgreementDlgProc(HWND hwnd, UINT message, WPARAM wParam,
                                         LPARAM lParam) {
  switch (message) {
  case WM_CREATE: {
    g_agreementDpi = GetSmartClipUiDpi(hwnd);
    if (g_agreementDpi == 0)
      g_agreementDpi = 96;
    int dpiVal = (int)g_agreementDpi;
    auto scale = [dpiVal](int v) { return MulDiv(v, dpiVal, 96); };

    // 创建字体（统一使用微软雅黑）
    g_hAgreementTitleFont = CreateFontW(
        scale(24), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hAgreementSubtitleFont = CreateFontW(
        scale(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hAgreementSectionFont = CreateFontW(
        scale(18), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hAgreementBodyFont = CreateFontW(
        scale(19), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hAgreementBtnFont = CreateFontW(
        scale(18), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hAgreementVersionFont = CreateFontW(
        scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hAgreementRemindFont = CreateFontW(
        scale(19), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");

    // 禁用 DWM 非客户区渲染，避免 WS_POPUP 窗口左上出现黑色边框
    {
      BOOL bEnable = FALSE;
      DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &bEnable,
                            sizeof(bEnable));
    }

    // 重置状态
    g_agreeScrollOffset = 0;
    g_agreeScrollbarHover = false;
    g_agreeScrollbarDrag = false;
    g_agreeAcceptHover = false;
    g_agreeAcceptPressed = false;
    g_agreeDeclineHover = false;
    g_agreeDeclinePressed = false;

    // 计算内容高度
    g_agreeContentHeight = AgreementMeasureContent(hwnd);
    // 初始判断是否已滚动到底（内容不足时直接视为到底）
    {
      RECT rcView;
      AgreementGetContentViewport(hwnd, &rcView);
      int viewH = rcView.bottom - rcView.top;
      int maxScroll = g_agreeContentHeight - viewH;
      g_agreeScrolledToBottom = (maxScroll <= 0);
    }

    // 设置窗口尺寸（WS_POPUP 无系统标题栏，不调用 AdjustWindowRectEx
    // 避免产生边框偏移）
    int dlgW = scale(kAgreeDlgW);
    int dlgH = scale(kAgreeDlgH);
    SetWindowPos(hwnd, NULL, 0, 0, dlgW, dlgH, SWP_NOMOVE | SWP_NOZORDER);

    // 圆角区域
    int cornerR = scale(14);
    HRGN hRgn = CreateRoundRectRgn(0, 0, dlgW + 1, dlgH + 1, cornerR, cornerR);
    SetWindowRgn(hwnd, hRgn, TRUE);

    // 居中
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hwnd, NULL, (sw - dlgW) / 2, (sh - dlgH) / 2, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER);
    return 0;
  }
  case WM_ERASEBKGND:
    return 1; // 避免闪烁
  case WM_NCCALCSIZE:
    // 完全去除非客户区，避免左、上出现黑色边框
    if (wParam)
      return 0;
    break;
  case WM_NCACTIVATE:
    return TRUE; // 不绘制非客户区激活状态
  case WM_NCPAINT:
    return 0; // 不绘制非客户区，避免黑色边框
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    if (!hdc) {
      EndPaint(hwnd, &ps);
      return 0;
    }

    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    int clientW = rcClient.right - rcClient.left;
    int clientH = rcClient.bottom - rcClient.top;

    // 双缓冲
    HDC memDc = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, clientW, clientH);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDc, memBmp);

    Graphics g(memDc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    int dpiVal = (int)g_agreementDpi;
    auto scale = [dpiVal](int v) { return MulDiv(v, dpiVal, 96); };

    // 主背景
    COLORREF bg = GetThemeWindowBgColor();
    SolidBrush bgBrush(Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
    g.FillRectangle(&bgBrush, 0, 0, clientW, clientH);

    int padX = scale(kAgreePadX);
    HFONT oldFont = (HFONT)SelectObject(memDc, g_hAgreementBodyFont);

    // 2. 内容区（可滚动）
    RECT rcView;
    AgreementGetContentViewport(hwnd, &rcView);
    int viewW = rcView.right - rcView.left;
    int cardWidth = viewW;
    int textWidth = cardWidth - scale(kAgreeCardPad) * 2;

    // 设置剪裁区域，避免内容溢出内容区（同时裁剪 GDI+ 和 GDI 绘制）
    HRGN hClipRgn =
        CreateRectRgn(rcView.left, rcView.top, rcView.right, rcView.bottom);
    g.SetClip(hClipRgn);
    SelectClipRgn(memDc, hClipRgn);
    DeleteObject(hClipRgn);

    // 卡片1: EULA
    int card1Y = rcView.top - g_agreeScrollOffset;
    int card1H = 0;
    {
      HDC measureDc = GetDC(hwnd);
      int eulaBodyH = AgreementCalcTextHeight(measureDc, T(STR_EULA_BODY),
                                              textWidth, g_hAgreementBodyFont);
      ReleaseDC(hwnd, measureDc);
      card1H = scale(kAgreeCardPad) + scale(22) + scale(8) + eulaBodyH +
               scale(kAgreeCardPad);

      COLORREF cardBg = GetThemeDialogCardBgColor();
      COLORREF cardBorder = g_isDarkMode ? RGB(62, 64, 70) : RGB(218, 222, 228);

      // Web 风格：卡片底部 2px 阴影
      int shadowOff = scale(2);
      Gdiplus::Color shadowClr = g_isDarkMode ? Gdiplus::Color(40, 0, 0, 0)
                                              : Gdiplus::Color(12, 0, 0, 0);
      GraphicsPath shadowPath;
      CreateRoundRectPath(&shadowPath, rcView.left, card1Y + shadowOff,
                          cardWidth, card1H, scale(kAgreeCardRadius));
      SolidBrush shadowBrush(shadowClr);
      g.FillPath(&shadowBrush, &shadowPath);

      AgreementFillRoundRect(g, rcView.left, card1Y, cardWidth, card1H,
                             scale(kAgreeCardRadius), cardBg);
      AgreementDrawRoundBorder(g, rcView.left, card1Y, cardWidth, card1H,
                               scale(kAgreeCardRadius), cardBorder, 1.0f);

      SetBkMode(memDc, TRANSPARENT);
      SetTextColor(memDc, GetThemeTextPrimaryColor());
      SelectObject(memDc, g_hAgreementSectionFont);
      RECT rcSection = {rcView.left + scale(kAgreeCardPad),
                        card1Y + scale(kAgreeCardPad),
                        rcView.left + cardWidth - scale(kAgreeCardPad),
                        card1Y + scale(kAgreeCardPad) + scale(22)};
      DrawTextW(memDc, T(STR_EULA_TITLE), -1, &rcSection,
                DT_LEFT | DT_SINGLELINE);

      SetTextColor(memDc, GetThemeTextPrimaryColor());
      SelectObject(memDc, g_hAgreementBodyFont);
      RECT rcBody = {rcView.left + scale(kAgreeCardPad),
                     card1Y + scale(kAgreeCardPad) + scale(22) + scale(8),
                     rcView.left + cardWidth - scale(kAgreeCardPad),
                     card1Y + card1H - scale(kAgreeCardPad)};
      DrawTextW(memDc, T(STR_EULA_BODY), -1, &rcBody,
                DT_LEFT | DT_TOP | DT_WORDBREAK);
      SelectObject(memDc, oldFont);
    }

    // 卡片2: Privacy Policy
    int card2Y = card1Y + card1H + scale(kAgreeCardSpacing);
    {
      HDC measureDc = GetDC(hwnd);
      int privacyBodyH = AgreementCalcTextHeight(
          measureDc, T(STR_PRIVACY_BODY), textWidth, g_hAgreementBodyFont);
      ReleaseDC(hwnd, measureDc);
      int card2H = scale(kAgreeCardPad) + scale(22) + scale(8) + privacyBodyH +
                   scale(kAgreeCardPad);

      COLORREF cardBg = GetThemeDialogCardBgColor();
      COLORREF cardBorder = g_isDarkMode ? RGB(62, 64, 70) : RGB(218, 222, 228);

      // Web 风格：卡片底部 2px 阴影
      int shadowOff2 = scale(2);
      Gdiplus::Color shadowClr2 = g_isDarkMode ? Gdiplus::Color(40, 0, 0, 0)
                                               : Gdiplus::Color(12, 0, 0, 0);
      GraphicsPath shadowPath2;
      CreateRoundRectPath(&shadowPath2, rcView.left, card2Y + shadowOff2,
                          cardWidth, card2H, scale(kAgreeCardRadius));
      SolidBrush shadowBrush2(shadowClr2);
      g.FillPath(&shadowBrush2, &shadowPath2);

      AgreementFillRoundRect(g, rcView.left, card2Y, cardWidth, card2H,
                             scale(kAgreeCardRadius), cardBg);
      AgreementDrawRoundBorder(g, rcView.left, card2Y, cardWidth, card2H,
                               scale(kAgreeCardRadius), cardBorder, 1.0f);

      SetBkMode(memDc, TRANSPARENT);
      SetTextColor(memDc, GetThemeTextPrimaryColor());
      SelectObject(memDc, g_hAgreementSectionFont);
      RECT rcSection = {rcView.left + scale(kAgreeCardPad),
                        card2Y + scale(kAgreeCardPad),
                        rcView.left + cardWidth - scale(kAgreeCardPad),
                        card2Y + scale(kAgreeCardPad) + scale(22)};
      DrawTextW(memDc, T(STR_PRIVACY_TITLE), -1, &rcSection,
                DT_LEFT | DT_SINGLELINE);

      SetTextColor(memDc, GetThemeTextPrimaryColor());
      SelectObject(memDc, g_hAgreementBodyFont);
      RECT rcBody = {rcView.left + scale(kAgreeCardPad),
                     card2Y + scale(kAgreeCardPad) + scale(22) + scale(8),
                     rcView.left + cardWidth - scale(kAgreeCardPad),
                     card2Y + card2H - scale(kAgreeCardPad)};
      DrawTextW(memDc, T(STR_PRIVACY_BODY), -1, &rcBody,
                DT_LEFT | DT_TOP | DT_WORDBREAK);
      SelectObject(memDc, oldFont);
    }

    // 取消剪裁，绘制滚动条
    g.ResetClip();
    SelectClipRgn(memDc, NULL);

    // 滚动条（仅显示 thumb，不显示轨道）
    RECT rcThumb;
    if (AgreementGetScrollbarThumb(hwnd, &rcThumb)) {
      COLORREF thumbColor;
      if (g_agreeScrollbarDrag) {
        thumbColor = GetThemeAccentStrongColor();
      } else if (g_agreeScrollbarHover) {
        thumbColor = g_isDarkMode ? RGB(142, 148, 158) : RGB(155, 155, 160);
      } else {
        thumbColor = g_isDarkMode ? RGB(102, 108, 118) : RGB(180, 180, 180);
      }
      int thumbW = rcThumb.right - rcThumb.left;
      int thumbH = rcThumb.bottom - rcThumb.top;
      int thumbR = thumbW / 2;
      AgreementFillRoundRect(g, rcThumb.left, rcThumb.top, thumbW, thumbH,
                             thumbR, thumbColor);
    }

    // 3. 底部按钮区（绘制不透明背景遮盖内容区溢出的文字）
    int footerY = clientH - scale(kAgreeFooterH);
    // 不透明背景
    SolidBrush footerBgBrush(
        Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
    g.FillRectangle(&footerBgBrush, 0, footerY, clientW, scale(kAgreeFooterH));

    // 分隔线（Web 风格：顶部 1px 分隔线）
    COLORREF sepColor = GetThemeSeparatorColor();
    Pen sepPen(Color(255, GetRValue(sepColor), GetGValue(sepColor),
                     GetBValue(sepColor)),
               1.0f);
    g.DrawLine(&sepPen, padX, footerY, clientW - padX, footerY);

    SetBkMode(memDc, TRANSPARENT);
    // 底部提示文字：纯黑色（暗色模式使用白色），Web 风格更大更醒目
    SetTextColor(memDc, g_isDarkMode ? RGB(255, 255, 255) : RGB(0, 0, 0));
    SelectObject(memDc, g_hAgreementRemindFont);
    RECT rcRemind = {padX, footerY + scale(18), clientW - padX,
                     footerY + scale(18) + scale(24)};
    DrawTextW(memDc, T(STR_AGREEMENT_DIALOG_REMIND), -1, &rcRemind,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(memDc, oldFont);

    int btnY = footerY + (scale(kAgreeFooterH) - scale(kAgreeBtnH)) / 2;
    int declineX = clientW - padX - scale(kAgreeBtnW);
    int acceptX = declineX - scale(kAgreeBtnGap) - scale(kAgreeBtnW);

    RECT rcAccept = {acceptX, btnY, acceptX + scale(kAgreeBtnW),
                     btnY + scale(kAgreeBtnH)};
    RECT rcDecline = {declineX, btnY, declineX + scale(kAgreeBtnW),
                      btnY + scale(kAgreeBtnH)};
    // Web 风格图标按钮：Accept = 对勾（\uE8FB），Decline = 叉（\uE711）
    // 首次安装时同意按钮始终可用（不再要求滚动到底）
    AgreementDrawIconButton(g, memDc, rcAccept, L"\uE8FB",
                            T(STR_AGREEMENT_DIALOG_ACCEPT), true,
                            g_agreeAcceptHover, g_agreeAcceptPressed, false);
    AgreementDrawIconButton(g, memDc, rcDecline, L"\uE711",
                            T(STR_AGREEMENT_DIALOG_DECLINE), false,
                            g_agreeDeclineHover, g_agreeDeclinePressed);

    // 4. 顶部标题区（Web 风格：更简洁的排版）
    int headerH = scale(kAgreeHeaderH);
    g.FillRectangle(&footerBgBrush, 0, 0, clientW, headerH);

    SetBkMode(memDc, TRANSPARENT);
    // SmartClip 应用图标（左上角）
    // 使用 LoadImageW 加载大尺寸 ICO（256x256 优先），再用 GDI+ 高质量
    // 插值缩放绘制，避免 LoadIconW 默认 32x32 拉伸导致的模糊
    int iconSize = scale(32);
    int iconX = padX;
    int iconY = scale(24);
    // 加载 ICO 中最大的 256x256 变体，再高质量下采样到 iconSize，确保清晰
    int loadSize = 256;
    HICON hAppIcon =
        (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_ICON1),
                          IMAGE_ICON, loadSize, loadSize, LR_DEFAULTCOLOR);
    if (!hAppIcon) {
      hAppIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_ICON1));
    }
    if (hAppIcon) {
      // 直接从 HICON 创建 GDI+ 位图（保留原生 alpha 通道）
      Bitmap *pBmp = Bitmap::FromHICON(hAppIcon);
      if (pBmp && pBmp->GetWidth() > 0) {
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        Rect dstRect(iconX, iconY, iconSize, iconSize);
        g.DrawImage(pBmp, dstRect, 0, 0, (INT)pBmp->GetWidth(),
                    (INT)pBmp->GetHeight(), UnitPixel);
      }
      if (pBmp)
        delete pBmp;
      DestroyIcon(hAppIcon);
    }

    SetTextColor(memDc, GetThemeTextPrimaryColor());
    SelectObject(memDc, g_hAgreementTitleFont);
    int titleY = scale(30);
    int titleX = iconX + iconSize + scale(10); // 图标右侧
    RECT rcTitle = {titleX, titleY, clientW - padX, titleY + scale(30)};
    DrawTextW(memDc, T(STR_AGREEMENT_DIALOG_TITLE), -1, &rcTitle,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(memDc, g_hAgreementSubtitleFont);
    SetTextColor(memDc, GetThemeTextSecondaryColor());
    int subtitleY = titleY + scale(30);
    // 副标题左对齐到 padX（与图标左边缘对齐）
    RECT rcSubtitle = {padX, subtitleY, clientW - padX, subtitleY + scale(22)};
    DrawTextW(memDc, T(STR_AGREEMENT_DIALOG_SUBTITLE), -1, &rcSubtitle,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    // 版本号（避开关闭按钮区域）
    SelectObject(memDc, g_hAgreementVersionFont);
    SetTextColor(memDc, GetThemeTextSecondaryColor());
    std::wstring versionText = L"v" + std::wstring(APP_VERSION_STRING);
    int closeBtnW = MulDiv(40, (int)g_agreementDpi, 96);
    RECT rcVer = {clientW - closeBtnW - padX - scale(120), titleY + scale(4),
                  clientW - closeBtnW - padX, titleY + scale(4) + scale(20)};
    DrawTextW(memDc, versionText.c_str(), -1, &rcVer, DT_RIGHT | DT_SINGLELINE);
    SelectObject(memDc, oldFont);

    // 5. 关闭按钮（借鉴主窗体标题栏关闭按钮风格）
    AgreementDrawCloseButton(g, memDc, hwnd);

    BitBlt(hdc, 0, 0, clientW, clientH, memDc, 0, 0, SRCCOPY);
    SelectObject(memDc, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDc);

    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_NCHITTEST: {
    // 始终返回 HTCLIENT，确保 WM_MOUSEMOVE 能收到（修复关闭按钮 hover
    // 不恢复） 窗口拖拽在 WM_LBUTTONDOWN 中手动处理
    return DefWindowProcW(hwnd, message, wParam, lParam);
  }
  case WM_MOUSEWHEEL: {
    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    AgreementScrollBy(hwnd, delta);
    return 0;
  }
  case WM_LBUTTONDOWN: {
    int dpiVal = (int)g_agreementDpi;
    auto scale = [dpiVal](int v) { return MulDiv(v, dpiVal, 96); };
    int x = GET_X_LPARAM(lParam);
    int y = GET_Y_LPARAM(lParam);

    // 检测关闭按钮
    RECT rcClose = AgreementGetCloseBtnRect(hwnd);
    if (PtInRect(&rcClose, {x, y})) {
      g_agreeClosePressed = true;
      SetCapture(hwnd);
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }

    // 标题区拖拽窗口（排除关闭按钮和内容区）
    int headerH = scale(kAgreeHeaderH);
    if (y < headerH) {
      ReleaseCapture();
      SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, lParam);
      return 0;
    }

    // 检测滚动条
    RECT rcThumb;
    if (AgreementGetScrollbarThumb(hwnd, &rcThumb)) {
      RECT rcView;
      AgreementGetContentViewport(hwnd, &rcView);
      int trackX = rcView.right - scale(kAgreeScrollbarRightPad) -
                   scale(kAgreeScrollbarW);
      RECT rcTrack = {trackX, rcView.top, trackX + scale(kAgreeScrollbarW),
                      rcView.bottom};
      if (PtInRect(&rcTrack, {x, y})) {
        if (PtInRect(&rcThumb, {x, y})) {
          g_agreeScrollbarDrag = true;
          g_agreeDragStartY = y;
          // 记录拖拽起始时 thumb 的 Y 位置（相对轨道顶部）
          g_agreeDragStartOffset = rcThumb.top - rcView.top;
          SetCapture(hwnd);
        } else {
          int viewH = rcView.bottom - rcView.top;
          int delta = (y < rcThumb.top) ? -viewH : viewH;
          g_agreeScrollOffset += delta;
          AgreementClampScrollOffset(hwnd);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
      }
    }

    // 检测按钮
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    int padX = scale(kAgreePadX);
    int footerY = rcClient.bottom - scale(kAgreeFooterH);
    int btnY = footerY + (scale(kAgreeFooterH) - scale(kAgreeBtnH)) / 2;
    int declineX = rcClient.right - padX - scale(kAgreeBtnW);
    int acceptX = declineX - scale(kAgreeBtnGap) - scale(kAgreeBtnW);
    RECT rcAccept = {acceptX, btnY, acceptX + scale(kAgreeBtnW),
                     btnY + scale(kAgreeBtnH)};
    RECT rcDecline = {declineX, btnY, declineX + scale(kAgreeBtnW),
                      btnY + scale(kAgreeBtnH)};

    if (PtInRect(&rcAccept, {x, y})) {
      g_agreeAcceptPressed = true;
      InvalidateRect(hwnd, NULL, FALSE);
    } else if (PtInRect(&rcDecline, {x, y})) {
      g_agreeDeclinePressed = true;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  }
  case WM_MOUSEMOVE: {
    int dpiVal = (int)g_agreementDpi;
    auto scale = [dpiVal](int v) { return MulDiv(v, dpiVal, 96); };
    int x = GET_X_LPARAM(lParam);
    int y = GET_Y_LPARAM(lParam);
    bool needRedraw = false;

    if (g_agreeScrollbarDrag) {
      RECT rcView;
      AgreementGetContentViewport(hwnd, &rcView);
      int viewH = rcView.bottom - rcView.top;
      int maxScroll = g_agreeContentHeight - viewH;
      if (maxScroll > 0) {
        int deltaY = y - g_agreeDragStartY;
        int thumbH = (viewH * viewH) / g_agreeContentHeight;
        if (thumbH < 30)
          thumbH = 30;
        int travel = viewH - thumbH;
        // g_agreeDragStartOffset 是起始 thumb Y（相对轨道顶部），
        // 加上鼠标位移得到新 thumb Y
        int newThumbY = g_agreeDragStartOffset + deltaY;
        if (newThumbY < 0)
          newThumbY = 0;
        if (newThumbY > travel)
          newThumbY = travel;
        int oldOffset = g_agreeScrollOffset;
        // thumb Y → scroll offset
        g_agreeScrollOffset = (newThumbY * maxScroll + travel / 2) / travel;
        AgreementClampScrollOffset(hwnd);
        if (g_agreeScrollOffset != oldOffset)
          needRedraw = true;
      }
    }

    RECT rcThumb;
    bool hasScrollbar = AgreementGetScrollbarThumb(hwnd, &rcThumb);
    bool newHover = hasScrollbar && PtInRect(&rcThumb, {x, y});
    if (newHover != g_agreeScrollbarHover) {
      g_agreeScrollbarHover = newHover;
      needRedraw = true;
    }

    // 关闭按钮 hover
    RECT rcClose = AgreementGetCloseBtnRect(hwnd);
    bool newCloseHover = PtInRect(&rcClose, {x, y});
    if (newCloseHover != g_agreeCloseHover) {
      g_agreeCloseHover = newCloseHover;
      needRedraw = true;
    }

    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    int padX = scale(kAgreePadX);
    int footerY = rcClient.bottom - scale(kAgreeFooterH);
    int btnY = footerY + (scale(kAgreeFooterH) - scale(kAgreeBtnH)) / 2;
    int declineX = rcClient.right - padX - scale(kAgreeBtnW);
    int acceptX = declineX - scale(kAgreeBtnGap) - scale(kAgreeBtnW);
    RECT rcAccept = {acceptX, btnY, acceptX + scale(kAgreeBtnW),
                     btnY + scale(kAgreeBtnH)};
    RECT rcDecline = {declineX, btnY, declineX + scale(kAgreeBtnW),
                      btnY + scale(kAgreeBtnH)};

    // 同意按钮始终响应 hover（首次安装无需滚动到底）
    bool newAcceptHover = PtInRect(&rcAccept, {x, y});
    if (newAcceptHover != g_agreeAcceptHover) {
      g_agreeAcceptHover = newAcceptHover;
      needRedraw = true;
    }
    bool newDeclineHover = PtInRect(&rcDecline, {x, y});
    if (newDeclineHover != g_agreeDeclineHover) {
      g_agreeDeclineHover = newDeclineHover;
      needRedraw = true;
    }

    if (needRedraw)
      InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  }
  case WM_LBUTTONUP: {
    int dpiVal = (int)g_agreementDpi;
    auto scale = [dpiVal](int v) { return MulDiv(v, dpiVal, 96); };
    int x = GET_X_LPARAM(lParam);
    int y = GET_Y_LPARAM(lParam);

    // 关闭按钮释放
    if (g_agreeClosePressed) {
      g_agreeClosePressed = false;
      ReleaseCapture();
      RECT rcClose = AgreementGetCloseBtnRect(hwnd);
      if (PtInRect(&rcClose, {x, y})) {
        // 触发关闭（等同拒绝）
        g_agreementAccepted = false;
        g_agreementDone = true;
        RecordAgreementAction(L"declined");
        DeleteFileW(GetAgreementFilePath().c_str());
        DestroyWindow(hwnd);
        return 0;
      }
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }

    if (g_agreeScrollbarDrag) {
      g_agreeScrollbarDrag = false;
      ReleaseCapture();
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }

    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    int padX = scale(kAgreePadX);
    int footerY = rcClient.bottom - scale(kAgreeFooterH);
    int btnY = footerY + (scale(kAgreeFooterH) - scale(kAgreeBtnH)) / 2;
    int declineX = rcClient.right - padX - scale(kAgreeBtnW);
    int acceptX = declineX - scale(kAgreeBtnGap) - scale(kAgreeBtnW);
    RECT rcAccept = {acceptX, btnY, acceptX + scale(kAgreeBtnW),
                     btnY + scale(kAgreeBtnH)};
    RECT rcDecline = {declineX, btnY, declineX + scale(kAgreeBtnW),
                      btnY + scale(kAgreeBtnH)};

    if (g_agreeAcceptPressed && PtInRect(&rcAccept, {x, y})) {
      // 首次安装时同意按钮始终可用（不再要求滚动到底）
      g_agreementAccepted = true;
      g_agreementDone = true;
      // 接受事件由 SaveAgreementAccepted() 统一记录（含时间戳）
      DestroyWindow(hwnd);
      return 0;
    }
    if (g_agreeDeclinePressed && PtInRect(&rcDecline, {x, y})) {
      g_agreementAccepted = false;
      g_agreementDone = true;
      // 记录拒绝事件
      RecordAgreementAction(L"declined");
      DeleteFileW(GetAgreementFilePath().c_str());
      DestroyWindow(hwnd);
      return 0;
    }
    g_agreeAcceptPressed = false;
    g_agreeDeclinePressed = false;
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  }
  case WM_KEYDOWN: {
    if (wParam == VK_ESCAPE) {
      g_agreementAccepted = false;
      g_agreementDone = true;
      RecordAgreementAction(L"declined");
      DeleteFileW(GetAgreementFilePath().c_str());
      DestroyWindow(hwnd);
      return 0;
    }
    if (wParam == VK_RETURN) {
      // 首次安装时同意按钮始终可用（不再要求滚动到底）
      g_agreementAccepted = true;
      g_agreementDone = true;
      // 接受事件由 SaveAgreementAccepted() 统一记录（含时间戳）
      DestroyWindow(hwnd);
      return 0;
    }
    if (wParam == VK_PRIOR) {
      RECT rcView;
      AgreementGetContentViewport(hwnd, &rcView);
      int viewH = rcView.bottom - rcView.top;
      g_agreeScrollOffset -= viewH;
      AgreementClampScrollOffset(hwnd);
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }
    if (wParam == VK_NEXT) {
      RECT rcView;
      AgreementGetContentViewport(hwnd, &rcView);
      int viewH = rcView.bottom - rcView.top;
      g_agreeScrollOffset += viewH;
      AgreementClampScrollOffset(hwnd);
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }
    if (wParam == VK_HOME) {
      g_agreeScrollOffset = 0;
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }
    if (wParam == VK_END) {
      RECT rcView;
      AgreementGetContentViewport(hwnd, &rcView);
      int viewH = rcView.bottom - rcView.top;
      g_agreeScrollOffset = g_agreeContentHeight - viewH;
      AgreementClampScrollOffset(hwnd);
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }
    break;
  }
  case WM_CLOSE:
    g_agreementAccepted = false;
    g_agreementDone = true;
    RecordAgreementAction(L"declined");
    DeleteFileW(GetAgreementFilePath().c_str());
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    // 窗口销毁时退出消息循环（不使用 PostQuitMessage 避免污染主消息循环）
    g_agreementDone = true;
    return 0;
  }
  return DefWindowProcW(hwnd, message, wParam, lParam);
}

// 显示用户协议对话框。返回 true 表示用户同意。
static bool ShowAgreementDialog(HINSTANCE hInstance) {
  // 重置弹窗状态，支持再次打开（托盘"查看用户协议"）
  g_agreementDone = false;
  g_agreementAccepted = false;
  g_agreeScrollOffset = 0;

  // 注册窗口类（hbrBackground 设为 NULL，避免系统在非客户区刷出黑色边框）
  WNDCLASSW wc = {};
  wc.lpfnWndProc = AgreementDlgProc;
  wc.hInstance = hInstance;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = NULL;
  wc.lpszClassName = L"SmartClipAgreementDlg";
  RegisterClassW(&wc);

  // 协议窗体不置顶：置顶开关只作用于主窗体，小窗体始终为普通层级
  HWND hDlg = CreateWindowExW(
      0, L"SmartClipAgreementDlg", T(STR_AGREEMENT_DIALOG_TITLE),
      WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CW_USEDEFAULT,
      CW_USEDEFAULT, 720, 560, NULL, NULL, hInstance, NULL);
  if (!hDlg)
    return false;

  ShowWindow(hDlg, SW_SHOW);
  UpdateWindow(hDlg);
  SetForegroundWindow(hDlg);

  // 消息循环：只处理弹窗自己的消息，避免污染主线程消息队列
  MSG msg;
  while (!g_agreementDone && GetMessageW(&msg, NULL, 0, 0) > 0) {
    // 只 dispatch 属于弹窗或其子窗口的消息
    if (msg.hwnd == NULL || IsChild(hDlg, msg.hwnd) || msg.hwnd == hDlg) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    } else {
      // 其他窗口的消息：原样分发（系统级）
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  // 清理字体
  if (g_hAgreementTitleFont) {
    DeleteObject(g_hAgreementTitleFont);
    g_hAgreementTitleFont = NULL;
  }
  if (g_hAgreementSubtitleFont) {
    DeleteObject(g_hAgreementSubtitleFont);
    g_hAgreementSubtitleFont = NULL;
  }
  if (g_hAgreementSectionFont) {
    DeleteObject(g_hAgreementSectionFont);
    g_hAgreementSectionFont = NULL;
  }
  if (g_hAgreementBodyFont) {
    DeleteObject(g_hAgreementBodyFont);
    g_hAgreementBodyFont = NULL;
  }
  if (g_hAgreementBtnFont) {
    DeleteObject(g_hAgreementBtnFont);
    g_hAgreementBtnFont = NULL;
  }
  if (g_hAgreementVersionFont) {
    DeleteObject(g_hAgreementVersionFont);
    g_hAgreementVersionFont = NULL;
  }
  if (g_hAgreementRemindFont) {
    DeleteObject(g_hAgreementRemindFont);
    g_hAgreementRemindFont = NULL;
  }
  DestroyWindow(hDlg);
  UnregisterClassW(L"SmartClipAgreementDlg", hInstance);
  return g_agreementAccepted;
}

// 入口函数
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                    LPWSTR lpCmdLine, int nCmdShow) {
  // 检测 -minimized 命令行参数：开机自启动时通过该参数指示主窗体最小化启动，
  // 不抢焦点、不显示在屏幕上，但任务栏按钮可见，用户点击任务栏即可恢复窗口。
  bool startMinimized = (wcsstr(lpCmdLine, L"-minimized") != NULL ||
                         wcsstr(lpCmdLine, L"/minimized") != NULL);
  if (startMinimized) {
    nCmdShow = SW_SHOWMINNOACTIVE;
  }
  // 检测 -restart 命令行参数：托盘"重启应用"时新实例带此参数启动。
  // 此时旧实例尚未完全退出、互斥量仍被占用，因此需要重试一段时间，
  // 等待旧实例释放 Global\SmartClipMutex，避免误判为"已运行"而退出。
  bool isRestart = (wcsstr(lpCmdLine, L"-restart") != NULL ||
                    wcsstr(lpCmdLine, L"/restart") != NULL);

  // 创建命名互斥量，检测是否已有实例在运行
  HANDLE hMutex = NULL;
  const int kMaxRestartRetries = 50; // 最多重试 50 次，每次 100ms，共 5 秒
  for (int i = 0; i < kMaxRestartRetries; ++i) {
    hMutex = CreateMutexW(NULL, TRUE, L"Global\\SmartClipMutex");
    if (hMutex == NULL) {
      // 创建互斥量失败，退出程序
      return 1;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
      break; // 成功获取互斥量
    }
    // 互斥量仍被占用：释放本次句柄，根据是否重启决定行为
    CloseHandle(hMutex);
    hMutex = NULL;
    if (!isRestart) {
      // 非重启场景：已有实例在运行，提示并激活现有窗口后退出
      MessageBoxW(NULL, L"本程序已在运行", L"Smart Clip",
                  MB_OK | MB_ICONINFORMATION);
      HWND hExistingWindow = FindWindowW(L"SmartClip", L"Smart Clip");
      if (hExistingWindow != NULL) {
        // 显示窗口（如果隐藏）并设置为前台窗口
        ShowWindow(hExistingWindow, SW_SHOW);
        SetForegroundWindow(hExistingWindow);
      }
      return 0;
    }
    // 重启场景：等待旧实例退出后重试
    Sleep(100);
  }
  if (hMutex == NULL) {
    // 重启重试超时：旧实例迟迟未释放互斥量，提示用户手动启动
    MessageBoxW(NULL, L"重启超时，请手动启动应用", L"Smart Clip",
                MB_OK | MB_ICONWARNING);
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

  // 首次运行/协议更新：加载语言并检查用户协议
  // - LoadCustomDataDir: 加载数据目录配置
  // - LoadExternalLanguages: 加载外部语言文件（让首次协议弹窗能匹配系统语言）
  // - LoadHotkeySettings: 首次运行时根据系统区域设置 g_appLanguage
  // - GetAgreementAcceptedVersion: 仅在从未接受或协议文案版本更新时弹出
  LoadCustomDataDir();
  LoadExternalLanguages();
  LoadHotkeySettings();
  ApplyLanguage();
  if (GetAgreementAcceptedVersion() < kAgreementVersion) {
    // 首次运行时跟随系统主题，使协议弹窗外观与系统一致
    g_isDarkMode = IsSystemDarkMode();
    if (!ShowAgreementDialog(hInstance)) {
      // 用户不同意协议，直接退出
      OleUninitialize();
      GdiplusShutdown(gdiplusToken);
      CloseHandle(hMutex);
      return 0;
    }
    SaveAgreementAccepted();
  }

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

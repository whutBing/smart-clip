#include "text_editor.h"
#include "custom_scrollbar.h"
#include "graphics_utils.h"
#include "history.h"
#include "hotkey.h"
#include "theme.h"
#include <cmath>
#include <string>
#include <windows.h>
#include <windowsx.h>

// ==================== 常量 ====================
static int GetTextEditorHeight() {
  return ScaleForDpi(300, GetSmartClipUiDpi(NULL));
}
static const int kTextEditorAnimSteps = 6;
static const UINT_PTR kTextEditorOpenTimer = 0x7001;
static const UINT_PTR kTextEditorCloseTimer = 0x7002;
static const UINT_PTR kTextEditorScrollbarHideTimer = 0x7003;

// ==================== 全局状态（本模块私有） ====================
static HWND g_hwndTextEditor = NULL;
static HWND g_hwndTextEditorEdit = NULL;
static HWND g_hwndTextEditorBackdrop = NULL;
static int g_textEditorActualIndex = -1;
static RECT g_textEditorOriginRect = {};
static int g_textEditorTargetX = 0;
static int g_textEditorTargetY = 0;
static int g_textEditorTargetW = 500;
static int g_textEditorAnimStep = 0;
static bool g_textEditorClosing = false;
static bool g_textEditorSaveOnClose = false;
static bool g_textEditorClassRegistered = false;
static WNDPROC g_oldTextEditProc = NULL;
static std::wstring g_textEditorPendingText;
static bool g_textEditorPendingSave = false;
static bool g_textEditorReadyToClose = false;
static CustomScrollbar g_textEditorScrollbar;
static int g_textEditLineHeight = 0;

// ==================== 辅助函数 ====================

static LRESULT CALLBACK TextEditorBackdropProc(HWND hwnd, UINT msg,
                                               WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    COLORREF fill = g_isDarkMode ? RGB(18, 20, 24) : RGB(238, 243, 249);
    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);

    HPEN pen = CreatePen(PS_SOLID, 1,
                         g_isDarkMode ? RGB(42, 46, 54) : RGB(220, 228, 238));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    for (int y = 12; y < rc.bottom; y += 18) {
      MoveToEx(hdc, 0, y, NULL);
      LineTo(hdc, rc.right, y + 7);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_LBUTTONDOWN:
  case WM_RBUTTONDOWN:
  case WM_MBUTTONDOWN:
    if (g_hwndTextEditor && IsWindow(g_hwndTextEditor)) {
      g_textEditorSaveOnClose = true;
      PostMessageW(g_hwndTextEditor, WM_CLOSE, 0, 0);
    }
    return 0;
  case WM_ERASEBKGND:
    return 1;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void DestroyTextEditorBackdrop() {
  if (g_hwndTextEditorBackdrop && IsWindow(g_hwndTextEditorBackdrop))
    DestroyWindow(g_hwndTextEditorBackdrop);
  g_hwndTextEditorBackdrop = NULL;
}

static void ShowTextEditorBackdrop(HWND hwndParent) {
  static bool s_backdropClassRegistered = false;
  if (!hwndParent || !IsWindow(hwndParent))
    return;
  if (!s_backdropClassRegistered) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = TextEditorBackdropProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"SmartClipTextEditorBackdrop";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    RegisterClassW(&wc);
    s_backdropClassRegistered = true;
  }

  DestroyTextEditorBackdrop();

  RECT rcParent = {};
  GetWindowRect(hwndParent, &rcParent);
  int w = rcParent.right - rcParent.left;
  int h = rcParent.bottom - rcParent.top;
  if (w <= 0 || h <= 0)
    return;

  g_hwndTextEditorBackdrop = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      L"SmartClipTextEditorBackdrop", L"", WS_POPUP, rcParent.left,
      rcParent.top, w, h, hwndParent, NULL, GetModuleHandleW(NULL), NULL);
  if (!g_hwndTextEditorBackdrop)
    return;

  SetLayeredWindowAttributes(g_hwndTextEditorBackdrop, 0,
                             g_isDarkMode ? 150 : 125, LWA_ALPHA);
  ShowWindow(g_hwndTextEditorBackdrop, SW_SHOWNOACTIVATE);
}

static int GetEditTextTotalHeight(HWND hwndTarget) {
  if (!hwndTarget)
    return 0;
  int lineCount = (int)SendMessageW(hwndTarget, EM_GETLINECOUNT, 0, 0);
  if (g_textEditLineHeight <= 0)
    g_textEditLineHeight = ScaleForDpi(16, GetSmartClipUiDpi(NULL));
  return lineCount * g_textEditLineHeight;
}

static int GetEditTextVisibleHeight(HWND hwndTarget) {
  if (!hwndTarget)
    return 0;
  RECT rc;
  GetClientRect(hwndTarget, &rc);
  return rc.bottom - rc.top;
}

static int GetEditTextScrollTop(HWND hwndTarget) {
  if (!hwndTarget)
    return 0;
  int firstLine = (int)SendMessageW(hwndTarget, EM_GETFIRSTVISIBLELINE, 0, 0);
  if (g_textEditLineHeight <= 0)
    g_textEditLineHeight = ScaleForDpi(16, GetSmartClipUiDpi(NULL));
  return firstLine * g_textEditLineHeight;
}

static void SetEditTextScrollTop(HWND hwndTarget, int scrollTop) {
  if (!hwndTarget || g_textEditLineHeight <= 0)
    return;
  int targetFirst = scrollTop / g_textEditLineHeight;
  int currentFirst =
      (int)SendMessageW(hwndTarget, EM_GETFIRSTVISIBLELINE, 0, 0);
  int delta = targetFirst - currentFirst;
  if (delta != 0)
    SendMessageW(hwndTarget, EM_LINESCROLL, 0, delta);
}

static void RefreshEditLineHeight(HWND hwndEdit) {
  if (!hwndEdit)
    return;
  HDC hdc = GetDC(hwndEdit);
  if (!hdc)
    return;
  HFONT hFont = (HFONT)SendMessageW(hwndEdit, WM_GETFONT, 0, 0);
  HFONT hOld = (HFONT)SelectObject(hdc, hFont);
  TEXTMETRICW tm;
  GetTextMetricsW(hdc, &tm);
  g_textEditLineHeight = tm.tmHeight;
  SelectObject(hdc, hOld);
  ReleaseDC(hwndEdit, hdc);
  if (g_textEditLineHeight <= 0)
    g_textEditLineHeight = ScaleForDpi(16, GetSmartClipUiDpi(NULL));
}

static LRESULT CALLBACK TextEditorEditSubclass(HWND hwnd, UINT msg,
                                               WPARAM wParam, LPARAM lParam) {
  if (msg == WM_KEYDOWN) {
    if (wParam == VK_ESCAPE) {
      g_textEditorSaveOnClose = true;
      PostMessageW(GetParent(hwnd), WM_CLOSE, 0, 0);
      return 0;
    }
    if (wParam == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000)) {
      g_textEditorSaveOnClose = true;
      PostMessageW(GetParent(hwnd), WM_CLOSE, 0, 0);
      return 0;
    }
  }
  LRESULT result =
      CallWindowProcW(g_oldTextEditProc, hwnd, msg, wParam, lParam);
  if (msg == WM_MOUSEWHEEL || msg == WM_VSCROLL || msg == WM_KEYUP ||
      msg == WM_LBUTTONUP) {
    HWND hwndParent = GetParent(hwnd);
    if (hwndParent && g_textEditorScrollbar.hwndOwner == hwndParent) {
      CSRefresh(&g_textEditorScrollbar, GetEditTextTotalHeight,
                GetEditTextVisibleHeight, GetEditTextScrollTop);
    }
  }
  return result;
}

static void UpdateTextEditorLayout(int w, int h) {
  if (g_hwndTextEditorEdit) {
    const int inset = ScaleForDpi(2, GetSmartClipUiDpi(NULL));
    int reserved = CSReservedWidth(&g_textEditorScrollbar);
    int editW = std::max(1, w - inset * 2 - reserved);
    int editH = std::max(1, h - inset * 2);
    SetWindowPos(g_hwndTextEditorEdit, NULL, inset, inset, editW, editH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    // 动画每帧都会重排 EDIT，长文本下 EM_GETLINECOUNT 为 O(行数)；
    // 滚动条刷新统一延后到动画结束（WM_TIMER 完成分支）执行一次，
    // 避免打开/关闭动画期间重复计算拖慢展开。
  }
}

// ==================== 弹窗窗口过程 ====================

static LRESULT CALLBACK TextEditorPopupProc(HWND hwnd, UINT msg, WPARAM wParam,
                                            LPARAM lParam) {
  switch (msg) {
  case WM_CREATE: {
    CREATESTRUCTW *cs = (CREATESTRUCTW *)lParam;
    UINT dpi = GetSmartClipUiDpi(NULL);
    const int inset = ScaleForDpi(2, dpi);
    int sbReserved = ScaleForDpi(CS_DEFAULT_TRACK_WIDTH + 2, dpi);
    g_hwndTextEditorEdit = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        inset, inset, g_textEditorTargetW - inset * 2 - sbReserved,
        GetTextEditorHeight() - inset * 2, hwnd, NULL, cs->hInstance, NULL);
    g_oldTextEditProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndTextEditorEdit, GWLP_WNDPROC, (LONG_PTR)TextEditorEditSubclass);
    // 缓存编辑字体，避免每次打开都 CreateFontW（长文本预览打开更快）
    static HFONT s_hEditFont = NULL;
    if (!s_hEditFont)
      s_hEditFont = CreateFontW(ScaleForDpi(20, dpi), 0, 0, 0, FW_NORMAL, FALSE,
                                FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    SendMessageW(g_hwndTextEditorEdit, WM_SETFONT, (WPARAM)s_hEditFont, TRUE);
    RefreshEditLineHeight(g_hwndTextEditorEdit);
    // 长文本优化：设置文本期间挂起重绘，避免 EDIT 逐行排版时反复触发
    // WM_PAINT/WM_ERASEBKGND；文本就绪后统一失效重绘一次。
    SendMessageW(g_hwndTextEditorEdit, WM_SETREDRAW, FALSE, 0);
    if (g_textEditorActualIndex >= 0 &&
        g_textEditorActualIndex < (int)g_history.size()) {
      SetWindowTextW(g_hwndTextEditorEdit,
                     g_history[g_textEditorActualIndex].content.c_str());
    }
    // 清空 SetWindowTextW 生成的 Undo 缓冲：长文本下缓冲与文本同量级，
    // 打开即释放，避免占用翻倍内存并拖慢后续编辑。
    SendMessageW(g_hwndTextEditorEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(g_hwndTextEditorEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hwndTextEditorEdit, NULL, FALSE);
    CSInit(&g_textEditorScrollbar, hwnd, g_hwndTextEditorEdit,
           kTextEditorScrollbarHideTimer);
    return 0;
  }

  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HBRUSH hBg = CreateSolidBrush(GetThemeSurfaceColor());
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);
    CSPaint(&g_textEditorScrollbar, hdc, GetEditTextTotalHeight,
            GetEditTextVisibleHeight, GetEditTextScrollTop);
    COLORREF borderColor = GetThemeSeparatorColor();
    HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    MoveToEx(hdc, 0, 0, NULL);
    LineTo(hdc, rc.right, 0);
    MoveToEx(hdc, 0, 0, NULL);
    LineTo(hdc, 0, rc.bottom);
    MoveToEx(hdc, 0, rc.bottom - 1, NULL);
    LineTo(hdc, rc.right - 1, rc.bottom - 1);
    MoveToEx(hdc, rc.right - 1, 0, NULL);
    LineTo(hdc, rc.right - 1, rc.bottom - 1);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    EndPaint(hwnd, &ps);
    return 0;
  }

  case WM_CTLCOLOREDIT: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, GetThemeSurfaceColor());
    SetTextColor(hdc, GetThemeTextPrimaryColor());
    static HBRUSH hEditBrush = NULL;
    if (hEditBrush)
      DeleteObject(hEditBrush);
    hEditBrush = CreateSolidBrush(GetThemeSurfaceColor());
    return (LRESULT)hEditBrush;
  }

  case WM_ERASEBKGND:
    return 1;

  case WM_ACTIVATE:
    if (LOWORD(wParam) == WA_INACTIVE && !g_textEditorClosing &&
        g_textEditorReadyToClose) {
      g_textEditorSaveOnClose = true;
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    return 0;

  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) {
      g_textEditorSaveOnClose = true;
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
      return 0;
    }
    break;

  case WM_LBUTTONDOWN:
  case WM_LBUTTONDBLCLK:
    if (CSOnLButtonDown(&g_textEditorScrollbar, GET_X_LPARAM(lParam),
                        GET_Y_LPARAM(lParam), GetEditTextTotalHeight,
                        GetEditTextVisibleHeight, GetEditTextScrollTop,
                        SetEditTextScrollTop))
      return 0;
    break;

  case WM_MOUSEMOVE:
    if (CSOnMouseMove(&g_textEditorScrollbar, GET_X_LPARAM(lParam),
                      GET_Y_LPARAM(lParam), GetEditTextTotalHeight,
                      GetEditTextVisibleHeight, GetEditTextScrollTop,
                      SetEditTextScrollTop))
      return 0;
    break;

  case WM_LBUTTONUP:
    if (CSOnLButtonUp(&g_textEditorScrollbar))
      return 0;
    break;

  case WM_MOUSELEAVE:
    CSOnMouseLeave(&g_textEditorScrollbar);
    break;

  case WM_CLOSE:
    if (!g_textEditorClosing) {
      if (g_textEditorSaveOnClose && g_hwndTextEditorEdit &&
          IsWindow(g_hwndTextEditorEdit)) {
        int len = GetWindowTextLengthW(g_hwndTextEditorEdit);
        g_textEditorPendingText.assign(len, L'\0');
        GetWindowTextW(g_hwndTextEditorEdit, &g_textEditorPendingText[0],
                       len + 1);
        g_textEditorPendingSave = true;
      } else {
        g_textEditorPendingSave = false;
      }
      g_textEditorClosing = true;
      g_textEditorAnimStep = 0;
      if (g_textEditorScrollbar.hideTimerId)
        KillTimer(hwnd, g_textEditorScrollbar.hideTimerId);
      SetTimer(hwnd, kTextEditorCloseTimer, 16, NULL);
    }
    return 0;

  case WM_TIMER:
    if (wParam == kTextEditorOpenTimer) {
      g_textEditorAnimStep++;
      double t = (double)g_textEditorAnimStep / kTextEditorAnimSteps;
      double ease = 1.0 - pow(1.0 - t, 3.0);
      int ox = g_textEditorOriginRect.left;
      int oy = g_textEditorOriginRect.top;
      int ow = g_textEditorOriginRect.right - g_textEditorOriginRect.left;
      int oh = g_textEditorOriginRect.bottom - g_textEditorOriginRect.top;
      int x = (int)(ox + (g_textEditorTargetX - ox) * ease);
      int y = (int)(oy + (g_textEditorTargetY - oy) * ease);
      int w = (int)(ow + (g_textEditorTargetW - ow) * ease);
      int h = (int)(oh + (GetTextEditorHeight() - oh) * ease);
      SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
      // 长文本优化：动画期间不跟随每帧 resize EDIT。EDIT 创建时已按最终
      // 尺寸排版，父窗口由小变大时超出部分由父窗口裁剪即可，避免长文本
      // 在 6 帧动画里反复重排全部行（O(行数)×帧数）。最终布局在动画
      // 完成分支一次性设置。
      if (g_textEditorAnimStep >= kTextEditorAnimSteps) {
        KillTimer(hwnd, kTextEditorOpenTimer);
        UpdateTextEditorLayout(g_textEditorTargetW, GetTextEditorHeight());
        SetFocus(g_hwndTextEditorEdit);
        int len = GetWindowTextLengthW(g_hwndTextEditorEdit);
        SendMessageW(g_hwndTextEditorEdit, EM_SETSEL, len, len);
        CSRefresh(&g_textEditorScrollbar, GetEditTextTotalHeight,
                  GetEditTextVisibleHeight, GetEditTextScrollTop);
        g_textEditorReadyToClose = true;
      }
    } else if (wParam == kTextEditorCloseTimer) {
      g_textEditorAnimStep++;
      double t = (double)g_textEditorAnimStep / kTextEditorAnimSteps;
      double ease = t * t;
      int ox = g_textEditorOriginRect.left;
      int oy = g_textEditorOriginRect.top;
      int ow = g_textEditorOriginRect.right - g_textEditorOriginRect.left;
      int oh = g_textEditorOriginRect.bottom - g_textEditorOriginRect.top;
      int x = (int)(g_textEditorTargetX + (ox - g_textEditorTargetX) * ease);
      int y = (int)(g_textEditorTargetY + (oy - g_textEditorTargetY) * ease);
      int w = (int)(g_textEditorTargetW + (ow - g_textEditorTargetW) * ease);
      int h =
          (int)(GetTextEditorHeight() + (oh - GetTextEditorHeight()) * ease);
      SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
      // 关闭动画同理：EDIT 保持最终尺寸，随父窗口缩小被裁剪即可，
      // 无需每帧重排长文本。
      if (g_textEditorAnimStep >= kTextEditorAnimSteps) {
        KillTimer(hwnd, kTextEditorCloseTimer);
        DestroyTextEditorBackdrop();
        DestroyWindow(hwnd);
      }
    } else if (wParam == kTextEditorScrollbarHideTimer) {
      CSOnTimer(&g_textEditorScrollbar);
    }
    return 0;

  case WM_DESTROY:
    if (g_textEditorPendingSave && g_textEditorActualIndex >= 0 &&
        g_textEditorActualIndex < (int)g_history.size()) {
      if (g_history[g_textEditorActualIndex].content !=
          g_textEditorPendingText) {
        g_history[g_textEditorActualIndex].content = g_textEditorPendingText;
        SaveHistory();
        UpdateListBox();
      }
    }
    g_hwndTextEditor = NULL;
    g_hwndTextEditorEdit = NULL;
    g_textEditorClosing = false;
    g_textEditorSaveOnClose = false;
    g_textEditorPendingSave = false;
    g_textEditorPendingText.clear();
    g_textEditorActualIndex = -1;
    g_textEditLineHeight = 0;
    g_textEditorReadyToClose = false;
    DestroyTextEditorBackdrop();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ==================== 对外接口 ====================

void ShowTextEditorPopup(HWND hwndParent, int actualIndex,
                         const RECT &itemRectScreen) {
  if (g_hwndTextEditor)
    return;
  if (!g_textEditorClassRegistered) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = TextEditorPopupProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"SmartClipTextEditorPopup";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    RegisterClassW(&wc);
    g_textEditorClassRegistered = true;
  }

  g_textEditorActualIndex = actualIndex;
  g_textEditorOriginRect = itemRectScreen;
  g_textEditorClosing = false;
  g_textEditorSaveOnClose = false;
  g_textEditorAnimStep = 0;
  g_textEditorReadyToClose = false;

  // 编辑弹窗宽度与所属窗口保持一致（高效模式 = 主窗体宽度），
  // 左右边缘与所属窗口客户区左边缘对齐。
  int cardW = itemRectScreen.right - itemRectScreen.left;
  int cardH = itemRectScreen.bottom - itemRectScreen.top;
  RECT rcParentClient = {};
  GetClientRect(hwndParent, &rcParentClient);
  int parentW = rcParentClient.right - rcParentClient.left;
  g_textEditorTargetW = parentW > 0 ? parentW : cardW;
  int targetH = GetTextEditorHeight();

  // 目标位置：左右与所属窗口客户区对齐，垂直方向以卡片中心为锚点居中
  POINT ptParentOrigin = {0, 0};
  ClientToScreen(hwndParent, &ptParentOrigin);
  int targetX = ptParentOrigin.x;
  int cardCenterY = itemRectScreen.top + cardH / 2;
  int targetY = cardCenterY - targetH / 2;

  RECT rcWork = {};
  if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0)) {
    int workW = rcWork.right - rcWork.left;
    int workH = rcWork.bottom - rcWork.top;
    if (g_textEditorTargetW > workW) {
      g_textEditorTargetW = workW;
      targetX = rcWork.left;
    } else {
      if (targetX < rcWork.left)
        targetX = rcWork.left;
      if (targetX + g_textEditorTargetW > rcWork.right)
        targetX = rcWork.right - g_textEditorTargetW;
    }
    if (targetH > workH) {
      targetY = rcWork.top;
    } else {
      if (targetY < rcWork.top)
        targetY = rcWork.top;
      if (targetY + targetH > rcWork.bottom)
        targetY = rcWork.bottom - targetH;
    }
  }
  g_textEditorTargetX = targetX;
  g_textEditorTargetY = targetY;

  int initW = itemRectScreen.right - itemRectScreen.left;
  int initH = itemRectScreen.bottom - itemRectScreen.top;

  ShowTextEditorBackdrop(hwndParent);

  g_hwndTextEditor =
      CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
                      L"SmartClipTextEditorPopup", L"", WS_POPUP,
                      itemRectScreen.left, itemRectScreen.top, initW, initH,
                      hwndParent, NULL, GetModuleHandleW(NULL), NULL);
  if (!g_hwndTextEditor) {
    DestroyTextEditorBackdrop();
    return;
  }
  SetLayeredWindowAttributes(g_hwndTextEditor, 0, 255, LWA_ALPHA);
  ShowWindow(g_hwndTextEditor, SW_SHOW);
  SetForegroundWindow(g_hwndTextEditor);

  SetTimer(g_hwndTextEditor, kTextEditorOpenTimer, 16, NULL);
}

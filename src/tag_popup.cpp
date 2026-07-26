#include "tag_popup.h"

#include "graphics_utils.h"
#include "history.h"
#include "themed_dialog.h"
#include "theme.h"
#include <commctrl.h>
#include <string>
#include <windows.h>

extern HWND g_hwndFilterAll;
extern HWND g_hwndFilterText;
extern HWND g_hwndFilterImage;
extern HWND g_hwndFilterFile;
extern HWND g_hwndFilterFavorite;
extern HWND g_hwndMain;
extern int g_currentTab;

#define IDC_TAG_POPUP_LIST 4010
#define IDC_TAG_POPUP_ADD 4011
#define IDC_TAG_POPUP_EDIT 4012
#define IDC_TAG_POPUP_NAME 4013
#define IDC_TAG_POPUP_COLORS 4014

static HWND g_hwndTagPopup = NULL;
static HWND g_hwndTagPopupTooltip = NULL;
static int g_tagPopupHoverIndex = -1;
static int g_tagPopupEditIndex = -1;
static HWND g_hwndTagPopupEdit = NULL;
static COLORREF g_tagPopupEditColor = RGB(66, 133, 244);
static COLORREF g_tagPopupOriginalColor = RGB(66, 133, 244);
static int g_tagPopupColorPickerIndex = -1;
static int g_tagPopupArrowHeight = 10;
static int g_tagPopupArrowWidth = 16;
static bool g_tagPopupFilterMode = false;
static UINT g_tagPopupDpi = 96;

static const COLORREF g_commonColors[] = {
    RGB(244, 67, 54),  RGB(233, 30, 99),  RGB(156, 39, 176),
    RGB(103, 58, 183), RGB(63, 81, 181),  RGB(33, 150, 243),
    RGB(0, 188, 212),  RGB(0, 150, 136),  RGB(76, 175, 80),
    RGB(139, 195, 74), RGB(255, 193, 7),  RGB(255, 152, 0),
};
static const int g_commonColorsCount =
    sizeof(g_commonColors) / sizeof(g_commonColors[0]);
static HBRUSH g_tagPopupEditBrush = NULL;

static COLORREF GetTagPopupBgColor() { return GetThemeSurfaceColor(); }
static COLORREF GetTagPopupBorderColor() { return GetThemeSeparatorColor(); }
static COLORREF GetTagPopupHoverColor() { return GetThemeDropdownHoverColor(); }
static COLORREF GetTagPopupTextColor() { return GetThemeTextPrimaryColor(); }
static COLORREF GetTagPopupMutedTextColor() {
  return GetThemeTextSecondaryColor();
}
static COLORREF GetTagPopupAccentColor() { return GetThemeAccentColor(); }
static COLORREF GetTagPopupInputBgColor() { return GetThemeInputBgColor(); }
static COLORREF GetTagPopupTooltipBgColor() { return GetThemeDialogCardBgColor(); }
static COLORREF GetTagPopupTooltipTextColor() {
  return GetThemeTextPrimaryColor();
}

static int TDpi(int value) { return ScaleForDpi(value, g_tagPopupDpi); }
static int TagPopupArrowHeight() { return TDpi(g_tagPopupArrowHeight); }
static int TagPopupArrowWidth() { return TDpi(g_tagPopupArrowWidth); }
static int TagPopupItemHeight() { return TDpi(32); }
static int TagPopupColorBoxSize() { return TDpi(16); }
static int TagPopupPadding() { return TDpi(8); }
static int TagPopupCornerRadius() { return TDpi(8); }

static void RefreshTagPopupTooltipTheme() {
  if (!g_hwndTagPopupTooltip)
    return;
  SendMessageW(g_hwndTagPopupTooltip, TTM_SETTIPBKCOLOR,
               (WPARAM)GetTagPopupTooltipBgColor(), 0);
  SendMessageW(g_hwndTagPopupTooltip, TTM_SETTIPTEXTCOLOR,
               (WPARAM)GetTagPopupTooltipTextColor(), 0);
}

static void ResizeTagPopupForCurrentState(HWND hwnd, int arrowHeight, int padding,
                                          int itemHeight) {
  int newHeight = arrowHeight + padding + itemHeight +
                  g_tags.size() * itemHeight + padding;
  if (g_tagPopupEditIndex >= (int)g_tags.size() && g_hwndTagPopupEdit) {
    newHeight += itemHeight;
  }
  if (g_tagPopupColorPickerIndex >= 0) {
    int colorRows = (g_commonColorsCount + 5) / 6;
    int colorAreaHeight = TDpi(20) + colorRows * TDpi(22) + TDpi(8);
    newHeight += colorAreaHeight;
  }

  RECT rcWindow;
  GetWindowRect(hwnd, &rcWindow);
  SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left, newHeight,
               SWP_NOMOVE | SWP_NOZORDER);
}

static void CloseTagPopupEditControls() {
  if (g_hwndTagPopupEdit) {
    DestroyWindow(g_hwndTagPopupEdit);
    g_hwndTagPopupEdit = NULL;
  }
  g_tagPopupEditIndex = -1;
  g_tagPopupColorPickerIndex = -1;
}

static bool CommitTagPopupEdit(HWND hwnd, int arrowHeight, int padding,
                               int itemHeight) {
  if (!g_hwndTagPopupEdit)
    return false;

  wchar_t name[256] = {0};
  GetWindowTextW(g_hwndTagPopupEdit, name, 256);

  if (wcslen(name) > 0) {
    bool isDuplicate = false;
    for (int i = 0; i < (int)g_tags.size(); i++) {
      if (i == g_tagPopupEditIndex)
        continue;
      if (g_tags[i].name == name) {
        isDuplicate = true;
        break;
      }
    }

    if (isDuplicate) {
      MessageBoxW(hwnd, L"标签名称已存在，请使用其他名称", L"提示",
                  MB_OK | MB_ICONWARNING);
      SetFocus(g_hwndTagPopupEdit);
      return false;
    }

    if (g_tagPopupEditIndex >= (int)g_tags.size()) {
      AddTag(name, g_tagPopupEditColor);
    } else {
      g_tags[g_tagPopupEditIndex].name = name;
      g_tags[g_tagPopupEditIndex].color = g_tagPopupEditColor;
    }
    SaveTags();
  }

  CloseTagPopupEditControls();
  ResizeTagPopupForCurrentState(hwnd, arrowHeight, padding, itemHeight);
  InvalidateRect(hwnd, NULL, TRUE);
  return true;
}

static void DeleteTagPopupEditWithConfirm(HWND hwnd, int arrowHeight, int padding,
                                          int itemHeight) {
  if (g_tagPopupEditIndex < 0 || g_tagPopupEditIndex >= (int)g_tags.size())
    return;

  int tagId = g_tags[g_tagPopupEditIndex].id;
  std::wstring tagName = g_tags[g_tagPopupEditIndex].name;

  // 默认标签（ID 1-3）不允许删除
  if (tagId >= 1 && tagId <= 3) {
    std::wstring defaultBody =
        L"默认分类「" + tagName + L"」是系统内置分类，不支持删除操作。";
    ThemedConfirmDialogConfig dialog = {
        L"无法删除",
        L"默认分类不可删除",
        L"该分类为系统默认分类，无法删除",
        defaultBody.c_str(),
        L"知道了",
        L"取消",
        424,
        220,
        {14, 68, 410, 160},
        false,
        false,
        true};
    ShowThemedConfirmDialog(hwnd, dialog);
    return;
  }

  std::wstring body =
      L"将删除分类「" + tagName + L"」。\n该分类下的记录不会被删除。";
  ThemedConfirmDialogConfig dialog = {
      L"删除收藏分类",
      L"删除当前分类",
      L"分类会被移除，记录本身会保留",
      body.c_str(),
      L"删除分类",
      L"取消",
      424,
      252,
      {14, 78, 410, 186},
      true,
      false,
      true};
  if (!ShowThemedConfirmDialog(hwnd, dialog))
    return;

  CloseTagPopupEditControls();
  RemoveTag(tagId);
  SaveTags();
  SaveHistory();
  ResizeTagPopupForCurrentState(hwnd, arrowHeight, padding, itemHeight);
  InvalidateRect(hwnd, NULL, TRUE);

  if (g_currentFilterTagId == tagId) {
    g_currentFilterTagId = 0;
    UpdateListBox();
  }
}

static LRESULT CALLBACK TagPopupProc(HWND hwnd, UINT message, WPARAM wParam,
                                     LPARAM lParam) {
  int itemHeight = TagPopupItemHeight();
  int colorBoxSize = TagPopupColorBoxSize();
  int padding = TagPopupPadding();
  int cornerRadius = TagPopupCornerRadius();
  int arrowHeight = TagPopupArrowHeight();
  int arrowWidth = TagPopupArrowWidth();

  switch (message) {
  case WM_CREATE: {
    g_hwndTagPopupTooltip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, hwnd, NULL, GetModuleHandle(NULL), NULL);
    RefreshTagPopupTooltipTheme();

    RECT rc;
    GetWindowRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    POINT arrowPoints[3] = {{width / 2 - arrowWidth / 2, arrowHeight},
                            {width / 2, 0},
                            {width / 2 + arrowWidth / 2, arrowHeight}};
    HRGN hArrowRgn = CreatePolygonRgn(arrowPoints, 3, WINDING);
    HRGN hBodyRgn = CreateRoundRectRgn(0, arrowHeight, width + 1, height + 1,
                                       cornerRadius, cornerRadius);
    CombineRgn(hBodyRgn, hBodyRgn, hArrowRgn, RGN_OR);
    SetWindowRgn(hwnd, hBodyRgn, TRUE);
    DeleteObject(hArrowRgn);

    return 0;
  }

  case WM_SIZE: {
    int width = LOWORD(lParam);
    int height = HIWORD(lParam);

    POINT arrowPoints[3] = {{width / 2 - arrowWidth / 2, arrowHeight},
                            {width / 2, 0},
                            {width / 2 + arrowWidth / 2, arrowHeight}};
    HRGN hArrowRgn = CreatePolygonRgn(arrowPoints, 3, WINDING);
    HRGN hBodyRgn = CreateRoundRectRgn(0, arrowHeight, width + 1, height + 1,
                                       cornerRadius, cornerRadius);
    CombineRgn(hBodyRgn, hBodyRgn, hArrowRgn, RGN_OR);
    SetWindowRgn(hwnd, hBodyRgn, TRUE);
    DeleteObject(hArrowRgn);
    return 0;
  }

  case WM_ERASEBKGND:
    // 抑制背景擦除，避免悬浮时闪烁（WM_PAINT 中已绘制整个背景）
    return 1;

  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdcReal = BeginPaint(hwnd, &ps);

    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

    // 双缓冲：在内存 DC 上绘制，避免悬浮时闪烁
    HDC hdc = CreateCompatibleDC(hdcReal);
    HBITMAP hMemBmp =
        CreateCompatibleBitmap(hdcReal, rcClient.right, rcClient.bottom);
    HBITMAP hOldMemBmp = (HBITMAP)SelectObject(hdc, hMemBmp);

    HBRUSH hBgBrush = CreateSolidBrush(GetTagPopupBgColor());
    FillRect(hdc, &rcClient, hBgBrush);
    DeleteObject(hBgBrush);

    HPEN hBorderPen =
        CreatePen(PS_SOLID, std::max(1, TDpi(1)), GetTagPopupBorderColor());
    HPEN hOldPen = (HPEN)SelectObject(hdc, hBorderPen);

    POINT arrowPoints[3] = {{rcClient.right / 2 - arrowWidth / 2, arrowHeight},
                            {rcClient.right / 2, 0},
                            {rcClient.right / 2 + arrowWidth / 2, arrowHeight}};
    HBRUSH hWhiteBrush = CreateSolidBrush(GetTagPopupBgColor());
    HBRUSH hOldBrush2 = (HBRUSH)SelectObject(hdc, hWhiteBrush);
    Polygon(hdc, arrowPoints, 3);
    SelectObject(hdc, hOldBrush2);
    DeleteObject(hWhiteBrush);

    HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hNullBrush);
    RoundRect(hdc, 0, arrowHeight, rcClient.right, rcClient.bottom, cornerRadius,
              cornerRadius);
    SelectObject(hdc, hOldBrush);

    HPEN hSeamPen =
        CreatePen(PS_SOLID, std::max(1, TDpi(2)), GetTagPopupBgColor());
    HPEN hOldSeamPen = (HPEN)SelectObject(hdc, hSeamPen);
    MoveToEx(hdc, rcClient.right / 2 - arrowWidth / 2 + TDpi(1), arrowHeight,
             NULL);
    LineTo(hdc, rcClient.right / 2 + arrowWidth / 2, arrowHeight);
    SelectObject(hdc, hOldSeamPen);
    DeleteObject(hSeamPen);

    SelectObject(hdc, hOldPen);
    DeleteObject(hBorderPen);

    HFONT hFont = CreateFontW(TDpi(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                              FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    int y = arrowHeight + padding;

    HFONT hTitleFont = CreateFontW(
        TDpi(20), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    SelectObject(hdc, hTitleFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetTagPopupAccentColor());
    RECT rcTitle = {padding, y, rcClient.right - padding - TDpi(30),
                    y + itemHeight};
    DrawTextW(hdc, L"全部分类", -1, &rcTitle,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(hTitleFont);

    int addBtnSize = TDpi(24);
    int addBtnX = rcClient.right - padding - addBtnSize;
    int addBtnY = y + (itemHeight - addBtnSize) / 2;
    RECT rcAddBtn = {addBtnX, addBtnY, addBtnX + addBtnSize,
                     addBtnY + addBtnSize};

    bool isAddingNew = (g_tagPopupEditIndex >= (int)g_tags.size() &&
                        g_hwndTagPopupEdit != NULL);

    if (!g_tagPopupFilterMode) {
      if (g_tagPopupHoverIndex == -2 && !isAddingNew) {
        HBRUSH hHoverBrush = CreateSolidBrush(GetTagPopupHoverColor());
        SelectObject(hdc, GetStockObject(NULL_PEN));
        RoundRect(hdc, rcAddBtn.left - TDpi(2), rcAddBtn.top - TDpi(2),
                  rcAddBtn.right + TDpi(2), rcAddBtn.bottom + TDpi(2),
                  TDpi(6), TDpi(6));
        DeleteObject(hHoverBrush);
      }

      HFONT hAddFont = CreateFontW(
          TDpi(20), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
          DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
      SelectObject(hdc, hAddFont);
      SetBkMode(hdc, TRANSPARENT);
      if (isAddingNew) {
        SetTextColor(hdc, GetTagPopupMutedTextColor());
      } else {
        SetTextColor(hdc, GetTagPopupAccentColor());
      }
      DrawTextW(hdc, L"+", -1, &rcAddBtn,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      SelectObject(hdc, hFont);
      DeleteObject(hAddFont);
    }

    y += itemHeight;

    for (int i = 0; i < (int)g_tags.size(); i++) {
      const Tag &tag = g_tags[i];
      RECT rcTag = {padding, y, rcClient.right - padding, y + itemHeight};

      if (g_tagPopupEditIndex == i) {
        RECT rcEditColor = {padding + padding, y + (itemHeight - colorBoxSize) / 2,
                            padding + padding + colorBoxSize,
                            y + (itemHeight + colorBoxSize) / 2};
        HBRUSH hEditColorBrush = CreateSolidBrush(g_tagPopupEditColor);
        FillRect(hdc, &rcEditColor, hEditColorBrush);
        DeleteObject(hEditColorBrush);

        int btnSize = TDpi(20);
        int btnY = y + (itemHeight - btnSize) / 2;
        int confirmX = rcClient.right - padding - btnSize * 2 - TDpi(4);
        int cancelX = rcClient.right - padding - btnSize;

        HFONT hSymFont = CreateFontW(
            TDpi(16), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
        SelectObject(hdc, hSymFont);
        SetTextColor(hdc, RGB(76, 175, 80));
        RECT rcConfirm = {confirmX, btnY, confirmX + btnSize, btnY + btnSize};
        DrawTextW(hdc, L"✓", -1, &rcConfirm,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SetTextColor(hdc, RGB(244, 67, 54));
        RECT rcCancel = {cancelX, btnY, cancelX + btnSize, btnY + btnSize};
        DrawTextW(hdc, L"✕", -1, &rcCancel,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, hFont);
        DeleteObject(hSymFont);

        y += itemHeight;
        continue;
      }

      if (g_tagPopupHoverIndex == i && g_tagPopupColorPickerIndex < 0) {
        HBRUSH hHoverBrush = CreateSolidBrush(GetTagPopupHoverColor());
        FillRect(hdc, &rcTag, hHoverBrush);
        DeleteObject(hHoverBrush);
      }

      COLORREF displayColor = tag.color;
      if (g_tagPopupColorPickerIndex == i) {
        displayColor = g_tagPopupEditColor;
      }
      RECT rcColor = {rcTag.left + padding, y + (itemHeight - colorBoxSize) / 2,
                      rcTag.left + padding + colorBoxSize,
                      y + (itemHeight + colorBoxSize) / 2};
      HBRUSH hColorBrush = CreateSolidBrush(displayColor);
      FillRect(hdc, &rcColor, hColorBrush);
      DeleteObject(hColorBrush);

      SetTextColor(hdc, GetTagPopupTextColor());
      RECT rcName = rcTag;
      rcName.left = rcColor.right + padding;
      rcName.right = rcClient.right - padding;
      DrawTextW(hdc, tag.name.c_str(), -1, &rcName,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      y += itemHeight;
    }

    if (g_tagPopupColorPickerIndex >= 0) {
      int currentRowTop;
      if (g_tagPopupColorPickerIndex >= (int)g_tags.size()) {
        currentRowTop =
            arrowHeight + padding + itemHeight + g_tags.size() * itemHeight;
      } else {
        currentRowTop = arrowHeight + padding + itemHeight +
                        g_tagPopupColorPickerIndex * itemHeight;
      }
      int currentRowBottom = currentRowTop + itemHeight;

      int colorBoxLeft = padding + padding;
      int colorBoxRight = colorBoxLeft + colorBoxSize;
      int colorBoxTop = currentRowTop + (itemHeight - colorBoxSize) / 2;
      int colorBoxBottom = colorBoxTop + colorBoxSize;

      int colorY = currentRowBottom + TDpi(4);
      int colorBtnSize = TDpi(18);
      int colorSpacing = TDpi(4);
      int colorsPerRow = 6;
      int colorRows = (g_commonColorsCount + colorsPerRow - 1) / colorsPerRow;
      int colorAreaHeight = TDpi(20) + colorRows * (colorBtnSize + colorSpacing);

      RECT rcOverlay = {0, 0, rcClient.right, rcClient.bottom};
      HDC hdcMem = CreateCompatibleDC(hdc);
      HBITMAP hBmpMem =
          CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
      SelectObject(hdcMem, hBmpMem);
      HBRUSH hOverlayBrush = CreateSolidBrush(GetTagPopupBgColor());
      FillRect(hdcMem, &rcOverlay, hOverlayBrush);
      BLENDFUNCTION bf = {AC_SRC_OVER, 0, 200, 0};
      AlphaBlend(hdc, 0, 0, rcClient.right, rcClient.bottom, hdcMem, 0, 0,
                 rcClient.right, rcClient.bottom, bf);
      DeleteDC(hdcMem);
      DeleteObject(hBmpMem);
      DeleteObject(hOverlayBrush);

      RECT rcColorBox = {colorBoxLeft, colorBoxTop, colorBoxRight,
                         colorBoxBottom};
      HBRUSH hColorBoxBrush = CreateSolidBrush(g_tagPopupEditColor);
      FillRect(hdc, &rcColorBox, hColorBoxBrush);
      DeleteObject(hColorBoxBrush);

      RECT rcColorArea = {padding - TDpi(4), colorY - TDpi(4),
                          rcClient.right - padding + TDpi(4),
                          colorY + colorAreaHeight + TDpi(4)};
      HBRUSH hColorAreaBg = CreateSolidBrush(GetTagPopupBgColor());
      FillRect(hdc, &rcColorArea, hColorAreaBg);
      DeleteObject(hColorAreaBg);

      SetTextColor(hdc, GetTagPopupMutedTextColor());
      RECT rcColorLabel = {padding, colorY, rcClient.right - padding,
                           colorY + TDpi(16)};
      DrawTextW(hdc, L"选择颜色:", -1, &rcColorLabel,
                DT_LEFT | DT_TOP | DT_SINGLELINE);
      colorY += TDpi(20);

      for (int c = 0; c < g_commonColorsCount; c++) {
        int col = c % 6;
        int row = c / 6;
        int cx = padding + col * (colorBtnSize + colorSpacing);
        int cy = colorY + row * (colorBtnSize + colorSpacing);

        RECT rcColorBtn = {cx, cy, cx + colorBtnSize, cy + colorBtnSize};
        HBRUSH hColorBtnBrush = CreateSolidBrush(g_commonColors[c]);
        FillRect(hdc, &rcColorBtn, hColorBtnBrush);
        DeleteObject(hColorBtnBrush);

        if (g_commonColors[c] == g_tagPopupEditColor) {
          HPEN hSelPen = CreatePen(PS_SOLID, std::max(1, TDpi(2)),
                                   RGB(0, 0, 0));
          HPEN hOldSelPen = (HPEN)SelectObject(hdc, hSelPen);
          SelectObject(hdc, GetStockObject(NULL_BRUSH));
          Rectangle(hdc, cx - TDpi(1), cy - TDpi(1),
                    cx + colorBtnSize + TDpi(1),
                    cy + colorBtnSize + TDpi(1));
          SelectObject(hdc, hOldSelPen);
          DeleteObject(hSelPen);
        }
      }
    }

    if (g_tagPopupEditIndex >= (int)g_tags.size() && g_hwndTagPopupEdit) {
      int newRowY =
          arrowHeight + padding + itemHeight + g_tags.size() * itemHeight;

      RECT rcNewColor = {padding + padding,
                         newRowY + (itemHeight - colorBoxSize) / 2,
                         padding + padding + colorBoxSize,
                         newRowY + (itemHeight + colorBoxSize) / 2};
      HBRUSH hNewColorBrush = CreateSolidBrush(g_tagPopupEditColor);
      FillRect(hdc, &rcNewColor, hNewColorBrush);
      DeleteObject(hNewColorBrush);

      int btnSize = TDpi(20);
      int btnY = newRowY + (itemHeight - btnSize) / 2;
      int confirmX = rcClient.right - padding - btnSize * 2 - TDpi(8);
      int cancelX = rcClient.right - padding - btnSize;

      HFONT hBtnFont = CreateFontW(
          TDpi(16), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
      SelectObject(hdc, hBtnFont);
      SetTextColor(hdc, RGB(76, 175, 80));
      RECT rcConfirm = {confirmX, btnY, confirmX + btnSize, btnY + btnSize};
      DrawTextW(hdc, L"✓", -1, &rcConfirm,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);

      SetTextColor(hdc, RGB(244, 67, 54));
      RECT rcCancel = {cancelX, btnY, cancelX + btnSize, btnY + btnSize};
      DrawTextW(hdc, L"✕", -1, &rcCancel,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);

      SelectObject(hdc, hFont);
      DeleteObject(hBtnFont);
    }

    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);

    // 将内存 DC 复制到屏幕
    BitBlt(hdcReal, 0, 0, rcClient.right, rcClient.bottom, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, hOldMemBmp);
    DeleteObject(hMemBmp);
    DeleteDC(hdc);
    EndPaint(hwnd, &ps);
    return 0;
  }

  case WM_CTLCOLOREDIT: {
    HDC hdcEdit = (HDC)wParam;
    SetBkColor(hdcEdit, GetTagPopupInputBgColor());
    SetTextColor(hdcEdit, GetTagPopupTextColor());
    if (!g_tagPopupEditBrush)
      g_tagPopupEditBrush = CreateSolidBrush(GetTagPopupInputBgColor());
    return (INT_PTR)g_tagPopupEditBrush;
  }

  case WM_THEMECHANGED:
    if (g_tagPopupEditBrush) {
      DeleteObject(g_tagPopupEditBrush);
      g_tagPopupEditBrush = NULL;
    }
    RefreshTagPopupTooltipTheme();
    if (g_hwndTagPopupEdit)
      InvalidateRect(g_hwndTagPopupEdit, NULL, TRUE);
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;

  case WM_MOUSEMOVE: {
    int x = LOWORD(lParam);
    int y = HIWORD(lParam);
    int newHoverIndex = -1;

    if (!g_tagPopupFilterMode && g_tagPopupEditIndex < 0 &&
        g_tagPopupColorPickerIndex < 0) {
      int addBtnSize = TDpi(24);
      int addBtnX = TDpi(200) - padding - addBtnSize;
      int addBtnY = arrowHeight + padding + (itemHeight - addBtnSize) / 2;
      bool isAddingNew = (g_tagPopupEditIndex >= (int)g_tags.size() &&
                          g_hwndTagPopupEdit != NULL);
      if (!isAddingNew && x >= addBtnX - 2 && x < addBtnX + addBtnSize + 2 &&
          y >= addBtnY - 2 && y < addBtnY + addBtnSize + 2) {
        newHoverIndex = -2;
      }
    }

    int itemY = arrowHeight + padding + itemHeight;
    for (int i = 0; i < (int)g_tags.size(); i++) {
      if (y >= itemY && y < itemY + itemHeight) {
        newHoverIndex = i;
        break;
      }
      itemY += itemHeight;
      if (g_tagPopupColorPickerIndex == i) {
        int colorRows = (g_commonColorsCount + 5) / 6;
        int colorAreaHeight = TDpi(20) + colorRows * TDpi(22) + TDpi(8);
        itemY += colorAreaHeight;
      }
    }

    if (newHoverIndex != g_tagPopupHoverIndex) {
      g_tagPopupHoverIndex = newHoverIndex;
      InvalidateRect(hwnd, NULL, TRUE);

      TOOLINFOW ti = {};
      ti.cbSize = sizeof(TOOLINFOW);
      ti.uFlags = TTF_SUBCLASS;
      ti.hwnd = hwnd;
      // 删除所有可能的旧工具（uId=1 和 uId=2）
      ti.uId = 1;
      SendMessageW(g_hwndTagPopupTooltip, TTM_DELTOOLW, 0, (LPARAM)&ti);
      ti.uId = 2;
      SendMessageW(g_hwndTagPopupTooltip, TTM_DELTOOLW, 0, (LPARAM)&ti);

      RECT rcClient;
      GetClientRect(hwnd, &rcClient);

      if (newHoverIndex == -2) {
        int addBtnSize = TDpi(24);
        int addBtnX = TDpi(200) - padding - addBtnSize;
        int addBtnY = arrowHeight + padding + (itemHeight - addBtnSize) / 2;
        ti.rect.left = addBtnX - 2;
        ti.rect.top = addBtnY - 2;
        ti.rect.right = addBtnX + addBtnSize + 2;
        ti.rect.bottom = addBtnY + addBtnSize + 2;
        ti.uId = 1;
        ti.lpszText = (LPWSTR)L"新增分类";
        SendMessageW(g_hwndTagPopupTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
      } else if (newHoverIndex >= 0) {
        // 计算 hover 项的矩形（需考虑颜色选择器展开的额外高度）
        int itemY = arrowHeight + padding + itemHeight;
        for (int j = 0; j < newHoverIndex; j++) {
          itemY += itemHeight;
          if (g_tagPopupColorPickerIndex == j) {
            int colorRows = (g_commonColorsCount + 5) / 6;
            int colorAreaHeight = TDpi(20) + colorRows * TDpi(22) + TDpi(8);
            itemY += colorAreaHeight;
          }
        }
        ti.rect.left = 0;
        ti.rect.top = itemY;
        ti.rect.right = rcClient.right;
        ti.rect.bottom = itemY + itemHeight;
        ti.uId = 2;
        // 在标签名称后添加收藏快捷键提示（前9个标签）
        extern bool g_isFavoriteHotkeyEnabled;
        extern UINT g_favoriteHotkeyModifiers;
        std::wstring tipText = g_tags[newHoverIndex].name;
        if (g_isFavoriteHotkeyEnabled && newHoverIndex < 9) {
          std::wstring modText;
          if (g_favoriteHotkeyModifiers & MOD_CONTROL) modText += L"Ctrl+";
          if (g_favoriteHotkeyModifiers & MOD_ALT) modText += L"Alt+";
          if (g_favoriteHotkeyModifiers & MOD_SHIFT) modText += L"Shift+";
          if (g_favoriteHotkeyModifiers & MOD_WIN) modText += L"Win+";
          modText += (wchar_t)(L'1' + newHoverIndex);
          tipText += L" (" + modText + L")";
        }
        ti.lpszText = (LPWSTR)tipText.c_str();
        SendMessageW(g_hwndTagPopupTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
      }
    }
    return 0;
  }

  case WM_MOUSELEAVE:
    g_tagPopupHoverIndex = -100;
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;

  case WM_LBUTTONDOWN: {
    int x = LOWORD(lParam);
    int y = HIWORD(lParam);

    if (g_tagPopupEditIndex >= 0 && g_hwndTagPopupEdit) {
      int editY;
      if (g_tagPopupEditIndex >= (int)g_tags.size()) {
        editY = arrowHeight + padding + itemHeight +
                g_tags.size() * itemHeight;
      } else {
        editY = arrowHeight + padding + itemHeight +
                g_tagPopupEditIndex * itemHeight;
      }

      RECT rcClient;
      GetClientRect(hwnd, &rcClient);

      int btnSize = TDpi(20);
      int btnY = editY + (itemHeight - btnSize) / 2;
      int confirmX = rcClient.right - padding - btnSize * 2 - TDpi(4);
      int cancelX = rcClient.right - padding - btnSize;

      if (x >= confirmX && x < confirmX + btnSize && y >= btnY &&
          y < btnY + btnSize) {
        CommitTagPopupEdit(hwnd, arrowHeight, padding, itemHeight);
        return 0;
      }

      if (x >= cancelX && x < cancelX + btnSize && y >= btnY &&
          y < btnY + btnSize) {
        if (g_tagPopupEditIndex < (int)g_tags.size()) {
          DeleteTagPopupEditWithConfirm(hwnd, arrowHeight, padding, itemHeight);
          return 0;
        }
        CloseTagPopupEditControls();
        ResizeTagPopupForCurrentState(hwnd, arrowHeight, padding, itemHeight);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
      }
    }

    int addBtnSize = TDpi(24);
    int addBtnX = TDpi(200) - padding - addBtnSize;
    int addBtnY = arrowHeight + padding + (itemHeight - addBtnSize) / 2;
    bool isAddingNew = (g_tagPopupEditIndex >= (int)g_tags.size() &&
                        g_hwndTagPopupEdit != NULL);
    if (!g_tagPopupFilterMode && !isAddingNew && x >= addBtnX - 2 &&
        x < addBtnX + addBtnSize + 2 && y >= addBtnY - 2 &&
        y < addBtnY + addBtnSize + 2) {
      g_tagPopupEditIndex = (int)g_tags.size();
      g_tagPopupEditColor = RGB(244, 67, 54);
      g_tagPopupColorPickerIndex = -1;

      if (g_hwndTagPopupEdit) {
        DestroyWindow(g_hwndTagPopupEdit);
      }
      int newY = arrowHeight + padding + itemHeight + g_tags.size() * itemHeight;
      int editHeight = TDpi(22);
      int editYOffset = (itemHeight - editHeight) / 2;
      g_hwndTagPopupEdit = CreateWindowExW(
          0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
          padding + padding + colorBoxSize + padding, newY + editYOffset, TDpi(80),
          editHeight, hwnd, (HMENU)IDC_TAG_POPUP_NAME, GetModuleHandle(NULL),
          NULL);

      HFONT hFont = CreateFontW(TDpi(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE,
                                L"Microsoft YaHei");
      SendMessageW(g_hwndTagPopupEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
      SetFocus(g_hwndTagPopupEdit);

      int newHeight = arrowHeight + padding + itemHeight +
                      g_tags.size() * itemHeight + itemHeight + padding;
      RECT rcWindow;
      GetWindowRect(hwnd, &rcWindow);
      SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left, newHeight,
                   SWP_NOMOVE | SWP_NOZORDER);
      InvalidateRect(hwnd, NULL, TRUE);
      return 0;
    }

    int itemY = arrowHeight + padding + itemHeight;

    if (y >= arrowHeight + padding && y < arrowHeight + padding + itemHeight) {
      if (g_tagPopupEditIndex >= 0) {
        if (g_tagPopupEditIndex == 0) {
          return 0;
        }
      }
      if (g_tagPopupColorPickerIndex >= 0) {
        if (g_tagPopupColorPickerIndex == 0) {
          g_tagPopupColorPickerIndex = -1;
          int newHeight =
              arrowHeight + padding + itemHeight + g_tags.size() * itemHeight + padding;
          RECT rcWindow;
          GetWindowRect(hwnd, &rcWindow);
          SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left, newHeight,
                       SWP_NOMOVE | SWP_NOZORDER);
          InvalidateRect(hwnd, NULL, TRUE);
          return 0;
        }
      }

      g_currentFilterTagId = 0;
      g_currentTab = 4;
      InvalidateRect(g_hwndFilterAll, NULL, FALSE);
      InvalidateRect(g_hwndFilterText, NULL, FALSE);
      InvalidateRect(g_hwndFilterImage, NULL, FALSE);
      InvalidateRect(g_hwndFilterFile, NULL, FALSE);
      InvalidateRect(g_hwndFilterFavorite, NULL, FALSE);
      UpdateListBox();
      DestroyWindow(hwnd);
      // 销毁弹窗后确保主窗体保持前台
      if (g_hwndMain && IsWindow(g_hwndMain)) {
        SetForegroundWindow(g_hwndMain);
      }
      return 0;
    }

    for (int i = 0; i < (int)g_tags.size(); i++) {
      if (y >= itemY && y < itemY + itemHeight) {
        int colorBoxLeft = padding + padding;
        int colorBoxRight = colorBoxLeft + colorBoxSize;
        int nameLeft = colorBoxRight + padding;
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        int nameRight = rcClient.right - padding;

        if (g_tagPopupFilterMode) {
          g_currentFilterTagId = g_tags[i].id;
          g_currentTab = 4;
          InvalidateRect(g_hwndFilterAll, NULL, FALSE);
          InvalidateRect(g_hwndFilterText, NULL, FALSE);
          InvalidateRect(g_hwndFilterImage, NULL, FALSE);
          InvalidateRect(g_hwndFilterFile, NULL, FALSE);
          InvalidateRect(g_hwndFilterFavorite, NULL, FALSE);
          UpdateListBox();
          DestroyWindow(hwnd);
          // 销毁弹窗后确保主窗体保持前台，避免被其他窗口覆盖
          if (g_hwndMain && IsWindow(g_hwndMain)) {
            SetForegroundWindow(g_hwndMain);
          }
          return 0;
        }

        if (x >= colorBoxLeft && x < colorBoxRight) {
          g_tagPopupColorPickerIndex = i;
          g_tagPopupEditColor = g_tags[i].color;
          g_tagPopupOriginalColor = g_tags[i].color;

          int colorRows = (g_commonColorsCount + 5) / 6;
          int colorAreaHeight = TDpi(20) + colorRows * TDpi(22) + TDpi(8);
          int newHeight = padding + itemHeight + (i + 1) * itemHeight +
                          colorAreaHeight + (g_tags.size() - i - 1) * itemHeight +
                          padding;
          RECT rcWindow;
          GetWindowRect(hwnd, &rcWindow);
          SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left, newHeight,
                       SWP_NOMOVE | SWP_NOZORDER);
          InvalidateRect(hwnd, NULL, TRUE);
          return 0;
        }

        if (x >= nameLeft && x < nameRight) {
          g_tagPopupEditIndex = i;
          g_tagPopupEditColor = g_tags[i].color;
          g_tagPopupColorPickerIndex = -1;

          if (g_hwndTagPopupEdit) {
            DestroyWindow(g_hwndTagPopupEdit);
          }
          int editHeight = TDpi(22);
          int editYOffset = (itemHeight - editHeight) / 2;
          g_hwndTagPopupEdit = CreateWindowExW(
              0, L"EDIT", g_tags[i].name.c_str(),
              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
              padding + padding + colorBoxSize + padding, itemY + editYOffset, TDpi(80),
              editHeight, hwnd, (HMENU)IDC_TAG_POPUP_NAME, GetModuleHandle(NULL),
              NULL);

          HFONT hFont = CreateFontW(TDpi(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE,
                                    L"Microsoft YaHei");
          SendMessageW(g_hwndTagPopupEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
          SendMessageW(g_hwndTagPopupEdit, EM_SETSEL, 0, -1);
          SetFocus(g_hwndTagPopupEdit);

          InvalidateRect(hwnd, NULL, TRUE);
          return 0;
        }
      }
      itemY += itemHeight;
      if (g_tagPopupColorPickerIndex == i) {
        int colorRows = (g_commonColorsCount + 5) / 6;
        int colorAreaHeight = TDpi(20) + colorRows * TDpi(22) + TDpi(8);
        itemY += colorAreaHeight;
      }
    }

    if (g_tagPopupEditIndex >= 0 && g_hwndTagPopupEdit) {
      int editY;
      if (g_tagPopupEditIndex >= (int)g_tags.size()) {
        editY = arrowHeight + padding + itemHeight + g_tags.size() * itemHeight;
      } else {
        editY = arrowHeight + padding + itemHeight + g_tagPopupEditIndex * itemHeight;
      }

      int colorBoxLeft = padding + padding;
      int colorBoxRight = colorBoxLeft + colorBoxSize;
      int colorBoxTop = editY + (itemHeight - colorBoxSize) / 2;
      int colorBoxBottom = colorBoxTop + colorBoxSize;
      if (x >= colorBoxLeft && x < colorBoxRight && y >= colorBoxTop &&
          y < colorBoxBottom) {
        if (g_tagPopupColorPickerIndex == g_tagPopupEditIndex) {
          g_tagPopupColorPickerIndex = -1;
          int newHeight = arrowHeight + padding + itemHeight +
                          g_tags.size() * itemHeight + itemHeight + padding;
          RECT rcWindow;
          GetWindowRect(hwnd, &rcWindow);
          SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left, newHeight,
                       SWP_NOMOVE | SWP_NOZORDER);
        } else {
          g_tagPopupColorPickerIndex = g_tagPopupEditIndex;
          g_tagPopupOriginalColor = g_tagPopupEditColor;

          int colorRows = (g_commonColorsCount + 5) / 6;
          int colorAreaHeight = TDpi(20) + colorRows * TDpi(22) + TDpi(8);
          int newHeight = arrowHeight + padding + itemHeight +
                          g_tags.size() * itemHeight + itemHeight + colorAreaHeight +
                          padding;
          RECT rcWindow;
          GetWindowRect(hwnd, &rcWindow);
          SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left, newHeight,
                       SWP_NOMOVE | SWP_NOZORDER);
        }
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
      }
    }

    if (g_tagPopupColorPickerIndex >= 0) {
      int colorPickerY;
      if (g_tagPopupColorPickerIndex >= (int)g_tags.size()) {
        colorPickerY = arrowHeight + padding + itemHeight +
                       g_tags.size() * itemHeight + itemHeight + 4;
      } else {
        colorPickerY = arrowHeight + padding + itemHeight +
                       (g_tagPopupColorPickerIndex + 1) * itemHeight + 4;
      }
      int colorY = colorPickerY + 20;
      int colorBtnSize = 18;
      int colorSpacing = 4;

      for (int c = 0; c < g_commonColorsCount; c++) {
        int col = c % 6;
        int row = c / 6;
        int cx = padding + col * (colorBtnSize + colorSpacing);
        int cy = colorY + row * (colorBtnSize + colorSpacing);

        if (x >= cx && x < cx + colorBtnSize && y >= cy &&
            y < cy + colorBtnSize) {
          g_tagPopupEditColor = g_commonColors[c];

          if (g_tagPopupColorPickerIndex < (int)g_tags.size()) {
            g_tags[g_tagPopupColorPickerIndex].color = g_commonColors[c];
            SaveTags();
            g_tagPopupColorPickerIndex = -1;

            int newHeight = arrowHeight + padding + itemHeight +
                            g_tags.size() * itemHeight + padding;
            RECT rcWindow;
            GetWindowRect(hwnd, &rcWindow);
            SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left,
                         newHeight, SWP_NOMOVE | SWP_NOZORDER);
          } else {
            g_tagPopupColorPickerIndex = -1;

            int newHeight = arrowHeight + padding + itemHeight +
                            g_tags.size() * itemHeight + itemHeight + padding;
            RECT rcWindow;
            GetWindowRect(hwnd, &rcWindow);
            SetWindowPos(hwnd, NULL, 0, 0, rcWindow.right - rcWindow.left,
                         newHeight, SWP_NOMOVE | SWP_NOZORDER);
          }
          InvalidateRect(hwnd, NULL, TRUE);
          return 0;
        }
      }
    }
    return 0;
  }

  case WM_LBUTTONDBLCLK: {
    if (g_tagPopupFilterMode)
      return 0;

    int x = LOWORD(lParam);
    int y = HIWORD(lParam);
    int itemY = arrowHeight + padding + itemHeight;

    for (int i = 0; i < (int)g_tags.size(); i++) {
      if (y >= itemY && y < itemY + itemHeight) {
        if (g_tagPopupEditIndex >= 0 || g_tagPopupColorPickerIndex >= 0) {
          return 0;
        }

        int colorBoxLeft = padding + padding;
        int colorBoxRight = colorBoxLeft + colorBoxSize;
        int nameLeft = colorBoxRight + padding;
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        int nameRight = rcClient.right - padding;

        if (x >= nameLeft && x < nameRight) {
          g_tagPopupEditIndex = i;
          g_tagPopupEditColor = g_tags[i].color;
          g_tagPopupColorPickerIndex = -1;

          if (g_hwndTagPopupEdit) {
            DestroyWindow(g_hwndTagPopupEdit);
          }
          int editHeight = TDpi(22);
          int editYOffset = (itemHeight - editHeight) / 2;
          g_hwndTagPopupEdit = CreateWindowExW(
              0, L"EDIT", g_tags[i].name.c_str(),
              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
              padding + padding + colorBoxSize + padding, itemY + editYOffset, TDpi(80),
              editHeight, hwnd, (HMENU)IDC_TAG_POPUP_NAME, GetModuleHandle(NULL),
              NULL);

          HFONT hFont = CreateFontW(TDpi(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE,
                                    L"Microsoft YaHei");
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
    if (g_tagPopupFilterMode)
      return 0;

    int y = HIWORD(lParam);
    int itemY = arrowHeight + padding + itemHeight;

    for (int i = 0; i < (int)g_tags.size(); i++) {
      if (y >= itemY && y < itemY + itemHeight) {
        g_tagPopupEditIndex = i;
        g_tagPopupEditColor = g_tags[i].color;
        g_tagPopupColorPickerIndex = -1;

        if (g_hwndTagPopupEdit) {
          DestroyWindow(g_hwndTagPopupEdit);
        }
        int editHeight = TDpi(22);
        int editYOffset = (itemHeight - editHeight) / 2;
        g_hwndTagPopupEdit = CreateWindowExW(
            0, L"EDIT", g_tags[i].name.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
            padding + padding + colorBoxSize + padding, itemY + editYOffset, TDpi(80),
            editHeight, hwnd, (HMENU)IDC_TAG_POPUP_NAME, GetModuleHandle(NULL),
            NULL);

        HFONT hFont = CreateFontW(TDpi(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE,
                                  L"Microsoft YaHei");
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
    }
    return 0;
  }

  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) {
      DestroyWindow(hwnd);
      return 0;
    }
    if (wParam == VK_RETURN && g_hwndTagPopupEdit) {
      CommitTagPopupEdit(hwnd, arrowHeight, padding, itemHeight);
      return 0;
    }
    break;

  case WM_ACTIVATE:
    if (LOWORD(wParam) == WA_INACTIVE) {
      HWND hwndFocus = GetFocus();
      if (hwndFocus != g_hwndTagPopupEdit) {
        DestroyWindow(hwnd);
      }
    }
    return 0;

  case WM_DESTROY:
    g_hwndTagPopup = NULL;
    g_tagPopupEditIndex = -1;
    if (g_tagPopupEditBrush) {
      DeleteObject(g_tagPopupEditBrush);
      g_tagPopupEditBrush = NULL;
    }
    if (g_hwndTagPopupEdit) {
      DestroyWindow(g_hwndTagPopupEdit);
      g_hwndTagPopupEdit = NULL;
    }
    return 0;
  }
  return DefWindowProcW(hwnd, message, wParam, lParam);
}

void ShowTagPopup(HWND hwndParent, int x, int y, int btnWidth, bool filterMode) {
  g_tagPopupFilterMode = filterMode;
  g_tagPopupDpi = GetSmartClipUiDpi(hwndParent);

  if (g_hwndTagPopup) {
    DestroyWindow(g_hwndTagPopup);
  }

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

  int itemHeight = TDpi(32);
  int padding = TDpi(8);
  int width = TDpi(200);
  int arrowH = TDpi(g_tagPopupArrowHeight);
  int height = arrowH + padding + itemHeight +
               g_tags.size() * itemHeight + padding;

  int popupX = x + btnWidth / 2 - width / 2;
  int popupY = y;

  g_hwndTagPopup = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"TagPopupWindow", NULL, WS_POPUP, popupX, popupY,
      width, height, hwndParent, NULL, GetModuleHandle(NULL), NULL);

  ShowWindow(g_hwndTagPopup, SW_SHOW);
  UpdateWindow(g_hwndTagPopup);
}

void CloseTagPopup() {
  if (g_hwndTagPopup) {
    DestroyWindow(g_hwndTagPopup);
    g_hwndTagPopup = NULL;
  }
}

bool IsTagPopupVisible() {
  return g_hwndTagPopup && IsWindowVisible(g_hwndTagPopup);
}

HWND GetTagPopupWindow() { return g_hwndTagPopup; }

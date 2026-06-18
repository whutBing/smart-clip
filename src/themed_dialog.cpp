#include "themed_dialog.h"

#include "graphics_utils.h"
#include "settings.h"
#include "theme.h"
#include <algorithm>
#include <commctrl.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <vector>
#include <windows.h>
#include <windowsx.h>

using namespace Gdiplus;

HWND g_hwndActiveThemedDialog = NULL;

static HWND g_hwndThemedDialogCloseBtn = NULL;
static WNDPROC g_oldThemedDialogCloseProc = NULL;
static bool g_themedDialogCloseHover = false;
static bool g_themedDialogClosePressed = false;
static WNDPROC g_oldDialogPasswordToggleProc = NULL;
static WNDPROC g_oldDialogEditProc = NULL;

#define IDC_THEMED_DIALOG_TITLE 42001
#define IDC_THEMED_DIALOG_SUBTITLE 42002
#define IDC_THEMED_DIALOG_BODY 42003

static const wchar_t *GetPasswordVisibilityIcon(bool visible) {
  return visible ? L"\uED1A" : L"\uE7B3";
}

static void SyncDialogEditTextRect(HWND hwnd) {
  RECT rcClient = {};
  GetClientRect(hwnd, &rcClient);
  if (rcClient.right <= rcClient.left || rcClient.bottom <= rcClient.top) {
    return;
  }

  HDC hdc = GetDC(hwnd);
  if (!hdc) {
    return;
  }

  HFONT hFont = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
  HFONT hOldFont = NULL;
  if (hFont) {
    hOldFont = (HFONT)SelectObject(hdc, hFont);
  }

  TEXTMETRICW tm = {};
  GetTextMetricsW(hdc, &tm);
  LOGFONTW lf = {};
  if (hFont) {
    GetObjectW(hFont, sizeof(lf), &lf);
  }
  if (hOldFont) {
    SelectObject(hdc, hOldFont);
  }
  ReleaseDC(hwnd, hdc);

  const int horizontalPadding = 12;
  int fontHeight = lf.lfHeight != 0 ? abs(lf.lfHeight) : 0;
  if (fontHeight <= 0) {
    fontHeight = (int)tm.tmHeight;
  }
  const int textHeight = std::max(1, fontHeight + (int)tm.tmExternalLeading);
  const int availableHeight = rcClient.bottom - rcClient.top;
  int topPadding = (availableHeight - textHeight) / 2;
  if (topPadding < 3)
    topPadding = 3;
  topPadding += 1;
  int bottomPadding = availableHeight - textHeight - topPadding;
  if (bottomPadding < 3) {
    bottomPadding = 3;
    topPadding = std::max(3, availableHeight - textHeight - bottomPadding);
  }
  RECT rcText = {horizontalPadding,
                 topPadding,
                 std::max(horizontalPadding + 1,
                          (int)rcClient.right - horizontalPadding),
                 std::max(topPadding + textHeight + 1,
                          (int)rcClient.bottom - bottomPadding)};
  SendMessageW(hwnd, EM_SETRECT, 0, (LPARAM)&rcText);
}

static LRESULT CALLBACK DialogEditProc(HWND hwnd, UINT message, WPARAM wParam,
                                       LPARAM lParam) {
  switch (message) {
  case WM_GETDLGCODE: {
    LRESULT code =
        CallWindowProcW(g_oldDialogEditProc, hwnd, message, wParam, lParam);
    return code & ~(DLGC_WANTALLKEYS | DLGC_WANTMESSAGE);
  }
  case WM_SETFONT: {
    LRESULT result =
        CallWindowProcW(g_oldDialogEditProc, hwnd, message, wParam, lParam);
    SyncDialogEditTextRect(hwnd);
    InvalidateRect(hwnd, NULL, TRUE);
    return result;
  }
  case WM_CHAR:
    if (wParam == VK_RETURN) {
      return 0;
    }
    break;
  case WM_SIZE: {
    LRESULT result =
        CallWindowProcW(g_oldDialogEditProc, hwnd, message, wParam, lParam);
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    if (rc.right > rc.left && rc.bottom > rc.top) {
      HRGN hRgn = CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, 10, 10);
      SetWindowRgn(hwnd, hRgn, TRUE);
    }
    SyncDialogEditTextRect(hwnd);
    return result;
  }
  case WM_NCCALCSIZE:
    return CallWindowProcW(g_oldDialogEditProc, hwnd, message, wParam, lParam);
  case WM_NCPAINT: {
    HDC hdc = GetWindowDC(hwnd);
    if (hdc) {
      RECT rcWin;
      GetWindowRect(hwnd, &rcWin);
      OffsetRect(&rcWin, -rcWin.left, -rcWin.top);
      HBRUSH hBr = CreateSolidBrush(GetThemeDialogEditBgColor());
      FillRect(hdc, &rcWin, hBr);
      DeleteObject(hBr);
      ReleaseDC(hwnd, hdc);
    }
    return 0;
  }
  case WM_ERASEBKGND: {
    HDC hdc = (HDC)wParam;
    RECT rcClient = {};
    GetClientRect(hwnd, &rcClient);
    HBRUSH hBrush = CreateSolidBrush(GetThemeDialogEditBgColor());
    FillRect(hdc, &rcClient, hBrush);
    DeleteObject(hBrush);
    return 1;
  }
  case WM_PAINT: {
    LRESULT result =
        CallWindowProcW(g_oldDialogEditProc, hwnd, message, wParam, lParam);

    HWND hParent = GetParent(hwnd);
    HDC hdc = GetDC(hParent);
    if (hdc) {
      RECT rcEdit;
      GetWindowRect(hwnd, &rcEdit);
      MapWindowPoints(HWND_DESKTOP, hParent, (LPPOINT)&rcEdit, 2);

      RECT rcBorder = {rcEdit.left - 1, rcEdit.top - 1, rcEdit.right + 1,
                       rcEdit.bottom + 1};
      int w = rcBorder.right - rcBorder.left;
      int h = rcBorder.bottom - rcBorder.top;

      Graphics g(hdc);
      g.SetSmoothingMode(SmoothingModeAntiAlias);
      g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

      COLORREF borderColor = g_isDarkMode ? RGB(86, 90, 98)
                                          : RGB(210, 214, 220);
      GraphicsPath path;
      CreateRoundRectPath(&path, rcBorder.left, rcBorder.top, w, h, 6);
      Pen pen(Color(255, GetRValue(borderColor), GetGValue(borderColor),
                    GetBValue(borderColor)),
              1.2f);
      g.DrawPath(&pen, &path);
      ReleaseDC(hParent, hdc);
    }
    return result;
  }
  }
  return CallWindowProcW(g_oldDialogEditProc, hwnd, message, wParam, lParam);
}

static BOOL CALLBACK StyleDialogEditChildren(HWND hwnd, LPARAM) {
  wchar_t className[32] = {};
  GetClassNameW(hwnd, className, _countof(className));
  if (wcscmp(className, L"Edit") != 0)
    return TRUE;

  SetWindowTheme(hwnd, L"", L"");
  LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  style |= ES_MULTILINE;
  style &= ~WS_BORDER;
  SetWindowLongPtrW(hwnd, GWL_STYLE, style);
  LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if ((exStyle & WS_EX_CLIENTEDGE) != 0) {
    exStyle &= ~WS_EX_CLIENTEDGE;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
  }
  SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
               MAKELONG(12, 12));
  RECT rc = {};
  GetClientRect(hwnd, &rc);
  if (rc.right > rc.left && rc.bottom > rc.top) {
    HRGN hRgn = CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, 10, 10);
    SetWindowRgn(hwnd, hRgn, TRUE);
  }

  g_oldDialogEditProc =
      (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)DialogEditProc);
  SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
               MAKELONG(0, 0));
  SyncDialogEditTextRect(hwnd);
  return TRUE;
}

static bool IsThemedDialogFieldLabel(const ThemedDialogConfig *config,
                                     int controlId) {
  if (!config || !config->fieldLabelIds) {
    return false;
  }
  for (int i = 0; i < config->fieldLabelCount; ++i) {
    if (config->fieldLabelIds[i] == controlId) {
      return true;
    }
  }
  return false;
}

static HBRUSH GetThemedDialogBackgroundBrush() {
  static HBRUSH s_darkBrush = NULL;
  static HBRUSH s_lightBrush = NULL;
  if (g_isDarkMode) {
    if (!s_darkBrush) {
      s_darkBrush = CreateSolidBrush(GetThemeDialogBgColor());
    }
    return s_darkBrush;
  }
  if (!s_lightBrush) {
    s_lightBrush = CreateSolidBrush(GetThemeDialogBgColor());
  }
  return s_lightBrush;
}

static HBRUSH GetThemedDialogCardBrush() {
  static HBRUSH s_darkBrush = NULL;
  static HBRUSH s_lightBrush = NULL;
  if (g_isDarkMode) {
    if (!s_darkBrush) {
      s_darkBrush = CreateSolidBrush(GetThemeDialogCardBgColor());
    }
    return s_darkBrush;
  }
  if (!s_lightBrush) {
    s_lightBrush = CreateSolidBrush(GetThemeDialogCardBgColor());
  }
  return s_lightBrush;
}

static HBRUSH GetThemedDialogEditBrush() {
  static HBRUSH s_darkBrush = NULL;
  static HBRUSH s_lightBrush = NULL;
  if (g_isDarkMode) {
    if (!s_darkBrush) {
      s_darkBrush = CreateSolidBrush(GetThemeDialogEditBgColor());
    }
    return s_darkBrush;
  }
  if (!s_lightBrush) {
    s_lightBrush = CreateSolidBrush(GetThemeDialogEditBgColor());
  }
  return s_lightBrush;
}

static void CenterDialogToParent(HWND hDlg, HWND hwndParent) {
  RECT rcParent = {};
  RECT rcDlg = {};
  GetWindowRect(hwndParent, &rcParent);
  GetWindowRect(hDlg, &rcDlg);
  int x = rcParent.left +
          ((rcParent.right - rcParent.left) - (rcDlg.right - rcDlg.left)) / 2;
  int y = rcParent.top +
          ((rcParent.bottom - rcParent.top) - (rcDlg.bottom - rcDlg.top)) / 2;
  SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

static void InitThemedDialogFonts(HWND hDlg, ThemedDialogConfig *config) {
  config->hFont = CreateFontW(
      g_fontSize + (config ? config->bodyFontDelta : 4), 0, 0, 0, FW_NORMAL,
      FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
      g_fontName.c_str());
  EnumChildWindows(
      hDlg,
      [](HWND h, LPARAM lp) -> BOOL {
        SendMessageW(h, WM_SETFONT, (WPARAM)lp, TRUE);
        return TRUE;
      },
      (LPARAM)config->hFont);
  EnumChildWindows(hDlg, StyleDialogEditChildren, 0);

  config->hTitleFont = CreateFontW(
      g_fontSize + 8 + (config ? config->titleFontDelta : 0), 0, 0, 0,
      FW_SEMIBOLD, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_fontName.c_str());
  HWND hTitle = GetDlgItem(hDlg, IDC_THEMED_DIALOG_TITLE);
  if (hTitle) {
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)config->hTitleFont, TRUE);
  }

  config->hCloseFont = CreateFontW(
      g_fontSize - 3, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
  HWND hClose = GetDlgItem(hDlg, config->closeBtnId);
  if (hClose) {
    SendMessageW(hClose, WM_SETFONT, (WPARAM)config->hCloseFont, TRUE);
  }
}

static void CleanupThemedDialogFonts(ThemedDialogConfig *config) {
  if (!config) {
    return;
  }
  if (config->hFont) {
    DeleteObject(config->hFont);
    config->hFont = NULL;
  }
  if (config->hTitleFont) {
    DeleteObject(config->hTitleFont);
    config->hTitleFont = NULL;
  }
  if (config->hCloseFont) {
    DeleteObject(config->hCloseFont);
    config->hCloseFont = NULL;
  }
}

void ApplyDialogPasswordMask(HWND hEdit, bool revealed) {
  if (!hEdit)
    return;
  SendMessageW(hEdit, EM_SETPASSWORDCHAR, revealed ? 0 : (WPARAM)L'*', 0);
  InvalidateRect(hEdit, NULL, TRUE);
  UpdateWindow(hEdit);
  RedrawWindow(hEdit, NULL, NULL,
               RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
}

static PasswordToggleBinding *FindDialogPasswordToggleBinding(
    ThemedDialogConfig *config, int controlId) {
  if (!config || !config->userData)
    return NULL;

  auto *bindings =
      reinterpret_cast<std::vector<PasswordToggleBinding> *>(config->userData);
  for (auto &binding : *bindings) {
    if (binding.buttonId == controlId)
      return &binding;
  }
  return NULL;
}

static PasswordToggleBinding *FindDialogPasswordToggleBindingByButton(
    HWND hButton, HWND *outDialog = NULL) {
  HWND hDlg = GetParent(hButton);
  if (outDialog)
    *outDialog = hDlg;
  if (!hDlg)
    return NULL;

  ThemedDialogConfig *config = reinterpret_cast<ThemedDialogConfig *>(
      GetWindowLongPtrW(hDlg, GWLP_USERDATA));
  return FindDialogPasswordToggleBinding(config, GetDlgCtrlID(hButton));
}

static void DrawDialogPasswordEye(HDC hdc, const RECT &rc, bool revealed,
                                  bool pressed) {
  COLORREF bg = GetThemeDialogEditBgColor();
  HBRUSH hBrush = CreateSolidBrush(bg);
  FillRect(hdc, &rc, hBrush);
  DeleteObject(hBrush);

  HFONT hEye = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE,
                           L"Segoe MDL2 Assets");
  HFONT hOldFont = (HFONT)SelectObject(hdc, hEye);
  int oldBkMode = SetBkMode(hdc, TRANSPARENT);
  COLORREF oldTextColor = GetTextColor(hdc);
  COLORREF iconColor =
      pressed ? (g_isDarkMode ? RGB(226, 230, 238) : RGB(50, 56, 66))
              : (g_isDarkMode ? RGB(150, 150, 156) : RGB(136, 136, 142));
  SetTextColor(hdc, iconColor);
  const wchar_t *eyeIcon = GetPasswordVisibilityIcon(revealed);
  DrawTextW(hdc, eyeIcon, 1, const_cast<RECT *>(&rc),
            DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  SetTextColor(hdc, oldTextColor);
  SetBkMode(hdc, oldBkMode);
  SelectObject(hdc, hOldFont);
  DeleteObject(hEye);
}

static LRESULT CALLBACK DialogPasswordToggleProc(HWND hwnd, UINT message,
                                                 WPARAM wParam,
                                                 LPARAM lParam) {
  static bool s_pressed = false;

  switch (message) {
  case WM_PAINT: {
    PAINTSTRUCT ps = {};
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    PasswordToggleBinding *binding = FindDialogPasswordToggleBindingByButton(hwnd);
    DrawDialogPasswordEye(hdc, rc, binding && binding->revealed, s_pressed);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_ERASEBKGND:
    return 1;
  case WM_LBUTTONDOWN:
    s_pressed = true;
    SetCapture(hwnd);
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  case WM_LBUTTONUP: {
    bool wasPressed = s_pressed;
    s_pressed = false;
    if (GetCapture() == hwnd)
      ReleaseCapture();

    RECT rc = {};
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    GetClientRect(hwnd, &rc);
    if (wasPressed && PtInRect(&rc, pt)) {
      HWND hDlg = NULL;
      PasswordToggleBinding *binding =
          FindDialogPasswordToggleBindingByButton(hwnd, &hDlg);
      if (binding && hDlg) {
        binding->revealed = !binding->revealed;
        HWND hEdit = GetDlgItem(hDlg, binding->editId);
        ApplyDialogPasswordMask(hEdit, binding->revealed);
      }
    }
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  }
  case WM_CAPTURECHANGED:
    s_pressed = false;
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  case WM_SETCURSOR:
    SetCursor(LoadCursor(NULL, IDC_HAND));
    return TRUE;
  }

  return CallWindowProcW(g_oldDialogPasswordToggleProc, hwnd, message, wParam,
                         lParam);
}

HWND CreateDialogPasswordToggleButton(HWND hDlg, HINSTANCE hInst, int x, int y,
                                      int buttonId) {
  HWND hButton =
      CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_NOTIFY, x,
                      y, 28, 32, hDlg, (HMENU)(INT_PTR)buttonId, hInst, NULL);
  WNDPROC oldProc = (WNDPROC)SetWindowLongPtrW(
      hButton, GWLP_WNDPROC, (LONG_PTR)DialogPasswordToggleProc);
  if (!g_oldDialogPasswordToggleProc)
    g_oldDialogPasswordToggleProc = oldProc;
  return hButton;
}

int GetDialogPasswordEditWidth(int fullWidth) {
  const int kEyeButtonWidth = 28;
  const int kEyeGap = 8;
  return fullWidth - kEyeButtonWidth - kEyeGap;
}

int GetDialogPasswordToggleX(int editX, int fullWidth) {
  const int kEyeGap = 8;
  return editX + GetDialogPasswordEditWidth(fullWidth) + kEyeGap;
}

static LRESULT CALLBACK ThemedDialogCloseBtnProc(HWND hwnd, UINT message,
                                                 WPARAM wParam,
                                                 LPARAM lParam) {
  switch (message) {
  case WM_MOUSEMOVE: {
    if (!g_themedDialogCloseHover) {
      g_themedDialogCloseHover = true;
      InvalidateRect(hwnd, NULL, TRUE);
    }
    TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    return 0;
  }
  case WM_MOUSELEAVE:
    g_themedDialogCloseHover = false;
    g_themedDialogClosePressed = false;
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  case WM_LBUTTONDOWN:
    g_themedDialogClosePressed = true;
    SetCapture(hwnd);
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  case WM_LBUTTONUP: {
    bool wasPressed = g_themedDialogClosePressed;
    g_themedDialogClosePressed = false;
    if (GetCapture() == hwnd)
      ReleaseCapture();
    InvalidateRect(hwnd, NULL, TRUE);

    RECT rc = {};
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    GetClientRect(hwnd, &rc);
    if (wasPressed && PtInRect(&rc, pt)) {
      SendMessageW(GetParent(hwnd), WM_COMMAND,
                   MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
    }
    return 0;
  }
  case WM_CAPTURECHANGED:
    if (g_themedDialogClosePressed) {
      g_themedDialogClosePressed = false;
      InvalidateRect(hwnd, NULL, TRUE);
    }
    return 0;
  case WM_SETCURSOR:
    SetCursor(LoadCursorW(NULL, IDC_ARROW));
    return TRUE;
  }

  return CallWindowProcW(g_oldThemedDialogCloseProc, hwnd, message, wParam,
                         lParam);
}

static LRESULT CALLBACK ThemedDialogProc(HWND hwnd, UINT message, WPARAM wParam,
                                         LPARAM lParam) {
  ThemedDialogConfig *config =
      reinterpret_cast<ThemedDialogConfig *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (config && config->onMessage) {
    LRESULT handledResult = 0;
    if (config->onMessage(hwnd, message, wParam, lParam, config->userData,
                          &handledResult)) {
      return handledResult;
    }
  }
  switch (message) {
  case WM_THEMECHANGED:
    InvalidateRect(hwnd, NULL, TRUE);
    EnumChildWindows(
        hwnd,
        [](HWND h, LPARAM) -> BOOL {
          InvalidateRect(h, NULL, TRUE);
          return TRUE;
        },
        0);
    RedrawWindow(hwnd, NULL, NULL,
                 RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    return 0;
  case WM_CTLCOLORSTATIC: {
    HDC hdc = (HDC)wParam;
    HWND hCtl = (HWND)lParam;
    int controlId = GetDlgCtrlID(hCtl);
    SetBkMode(hdc, TRANSPARENT);
    if (controlId == IDC_THEMED_DIALOG_TITLE) {
      SetTextColor(hdc, GetThemeTextPrimaryColor());
    } else if (controlId == IDC_THEMED_DIALOG_SUBTITLE) {
      SetTextColor(hdc, GetThemeTextSecondaryColor());
    } else if (controlId == IDC_THEMED_DIALOG_BODY) {
      SetTextColor(hdc, g_isDarkMode ? RGB(206, 210, 218) : RGB(72, 78, 88));
    } else {
      SetTextColor(hdc, g_isDarkMode ? RGB(222, 225, 230) : RGB(62, 68, 78));
    }
    return (LRESULT)(IsThemedDialogFieldLabel(config, controlId)
                         ? GetThemedDialogCardBrush()
                         : GetThemedDialogBackgroundBrush());
  }
  case WM_CTLCOLOREDIT: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, GetThemeDialogEditBgColor());
    SetTextColor(hdc, g_isDarkMode ? RGB(236, 238, 242) : RGB(34, 38, 44));
    return (LRESULT)GetThemedDialogEditBrush();
  }
  case WM_CTLCOLORDLG:
    return (LRESULT)GetThemedDialogBackgroundBrush();
  case WM_ERASEBKGND: {
    HDC hdc = (HDC)wParam;
    RECT rcClient = {};
    GetClientRect(hwnd, &rcClient);
    FillRect(hdc, &rcClient, GetThemedDialogBackgroundBrush());
    return 1;
  }
  case WM_DRAWITEM: {
    if (!config) {
      return FALSE;
    }
    LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
    if (!dis) {
      return FALSE;
    }
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    if ((int)dis->CtlID == config->closeBtnId) {
      bool hover = g_themedDialogCloseHover;
      bool pressed = g_themedDialogClosePressed;
      FillRect(hdc, &rc, GetThemedDialogBackgroundBrush());

      Graphics g(hdc);
      g.SetSmoothingMode(SmoothingModeAntiAlias);
      if (hover || pressed) {
        COLORREF fill = pressed ? RGB(200, 28, 46) : RGB(232, 17, 35);
        GraphicsPath closePath;
        CreateRoundRectPath(&closePath, rc.left + 1, rc.top + 1,
                            rc.right - rc.left - 2, rc.bottom - rc.top - 2, 8);
        SolidBrush closeBrush(
            Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
        g.FillPath(&closeBrush, &closePath);
      }

      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc,
                   (hover || pressed) ? RGB(255, 255, 255)
                                      : (g_isDarkMode ? RGB(220, 223, 228)
                                                      : RGB(90, 96, 108)));
      HFONT hOldFont =
          (HFONT)SelectObject(hdc, (HFONT)SendMessageW((HWND)dis->hwndItem,
                                                       WM_GETFONT, 0, 0));
      DrawTextW(hdc, L"\uE8BB", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      SelectObject(hdc, hOldFont);
      return TRUE;
    }

    if (dis->CtlID != IDOK && dis->CtlID != IDCANCEL) {
      return FALSE;
    }

    bool isPrimary = (dis->CtlID == IDOK);
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool focused = (dis->itemState & ODS_FOCUS) != 0;
    FillRect(hdc, &rc, GetThemedDialogBackgroundBrush());

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    GraphicsPath path;
    CreateRoundRectPath(&path, rc.left + 1, rc.top + 1,
                        rc.right - rc.left - 2, rc.bottom - rc.top - 2, 10);

    COLORREF fill;
    COLORREF border;
    if (isPrimary && config->primaryButtonDanger) {
      fill = pressed ? (g_isDarkMode ? RGB(156, 52, 56) : RGB(190, 49, 53))
                     : (g_isDarkMode ? RGB(176, 62, 68) : RGB(214, 64, 71));
      border = pressed ? (g_isDarkMode ? RGB(170, 70, 74) : RGB(202, 58, 64))
                       : (g_isDarkMode ? RGB(194, 82, 88) : RGB(228, 84, 92));
    } else {
      fill = isPrimary ? (pressed ? (g_isDarkMode ? RGB(86, 118, 168)
                                                  : RGB(0, 94, 184))
                                  : GetThemeAccentColor())
                       : (g_isDarkMode ? RGB(38, 40, 46)
                                       : RGB(247, 248, 250));
      border = isPrimary ? (pressed ? (g_isDarkMode ? RGB(78, 108, 154)
                                                    : RGB(0, 88, 172))
                                    : GetThemeAccentColor())
                         : (g_isDarkMode ? RGB(74, 76, 82)
                                         : RGB(208, 212, 220));
    }
    SolidBrush brush(
        Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
    Pen pen(Color(255, GetRValue(border), GetGValue(border), GetBValue(border)),
            focused ? 1.6f : 1.0f);
    g.FillPath(&brush, &path);
    g.DrawPath(&pen, &path);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, isPrimary ? RGB(255, 255, 255)
                                : (g_isDarkMode ? RGB(226, 228, 232)
                                                : RGB(52, 58, 66)));
    HFONT hBtnFont = CreateFontW(
        g_fontSize + 1, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_fontName.c_str());
    HFONT hOldFont = (HFONT)SelectObject(hdc, hBtnFont);
    const wchar_t *buttonText =
        isPrimary ? (config->primaryButtonText ? config->primaryButtonText
                                               : L"确定")
                  : (config->secondaryButtonText ? config->secondaryButtonText
                                                 : L"取消");
    DrawTextW(hdc, buttonText, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);
    DeleteObject(hBtnFont);
    return TRUE;
  }
  case WM_PAINT: {
    PAINTSTRUCT ps = {};
    HDC hdc = BeginPaint(hwnd, &ps);
    if (hdc && config) {
      RECT rcClient = {};
      GetClientRect(hwnd, &rcClient);
      FillRect(hdc, &rcClient, GetThemedDialogBackgroundBrush());

      Graphics g(hdc);
      g.SetSmoothingMode(SmoothingModeAntiAlias);

      GraphicsPath outerPath;
      CreateRoundRectPath(&outerPath, 0, 0, config->dlgW - 1, config->dlgH - 1, 18);
      COLORREF outerBorder = g_isDarkMode ? RGB(62, 64, 70) : RGB(218, 222, 228);
      Pen outerPen(Color(255, GetRValue(outerBorder), GetGValue(outerBorder),
                         GetBValue(outerBorder)),
                   1.0f);
      g.DrawPath(&outerPen, &outerPath);

      GraphicsPath cardPath;
      CreateRoundRectPath(&cardPath, config->cardRect.left, config->cardRect.top,
                          config->cardRect.right - config->cardRect.left,
                          config->cardRect.bottom - config->cardRect.top, 18);
      COLORREF fill = GetThemeDialogCardBgColor();
      COLORREF border = g_isDarkMode ? RGB(62, 64, 70) : RGB(222, 225, 230);
      if (config->drawCardBackground) {
        SolidBrush brush(
            Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
        g.FillPath(&brush, &cardPath);
      }
      if (config->drawCardBorder) {
        Pen pen(Color(255, GetRValue(border), GetGValue(border),
                      GetBValue(border)),
                1.0f);
        g.DrawPath(&pen, &cardPath);
      }
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_COMMAND:
    if (LOWORD(wParam) == IDOK) {
      if (!config || !config->onOk || config->onOk(hwnd, config->userData)) {
        if (config && config->doneFlag) {
          *config->doneFlag = true;
        }
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    if (config &&
        (LOWORD(wParam) == IDCANCEL || LOWORD(wParam) == config->closeBtnId)) {
      if (config->doneFlag) {
        *config->doneFlag = true;
      }
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
      return 0;
    }
    break;
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) {
      if (config && config->doneFlag) {
        *config->doneFlag = true;
      }
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
      return 0;
    }
    break;
  case WM_CLOSE:
    if (config && config->doneFlag) {
      *config->doneFlag = true;
    }
    return 0;
  case WM_NCACTIVATE:
    return TRUE;
  case WM_NCPAINT:
    return 0;
  }
  return DefDlgProcW(hwnd, message, wParam, lParam);
}

HWND CreateThemedDialog(HWND hwndParent, HINSTANCE hInst,
                        ThemedDialogConfig *config) {
  HWND hDlg =
      CreateWindowExW(0, L"#32770", config->windowTitle,
                      WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                      CW_USEDEFAULT, CW_USEDEFAULT, config->dlgW, config->dlgH,
                      hwndParent, NULL, hInst, NULL);
  if (!hDlg) {
    return NULL;
  }

  HRGN hRgn =
      CreateRoundRectRgn(0, 0, config->dlgW + 1, config->dlgH + 1, 22, 22);
  SetWindowRgn(hDlg, hRgn, TRUE);

  CreateWindowExW(0, L"STATIC", config->title, WS_CHILD | WS_VISIBLE, 22, 16,
                  260, 28, hDlg, (HMENU)IDC_THEMED_DIALOG_TITLE, hInst, NULL);
  CreateWindowExW(0, L"STATIC", config->subtitle, WS_CHILD | WS_VISIBLE, 22, 46,
                  320, 18, hDlg, (HMENU)IDC_THEMED_DIALOG_SUBTITLE, hInst, NULL);
  if (config->bodyText && config->bodyText[0] != L'\0') {
    CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", config->bodyText,
                    WS_CHILD | WS_VISIBLE, config->cardRect.left + 20,
                    config->cardRect.top + 24,
                    config->cardRect.right - config->cardRect.left - 40, 52, hDlg,
                    (HMENU)IDC_THEMED_DIALOG_BODY, hInst, NULL);
  }
  g_themedDialogCloseHover = false;
  g_themedDialogClosePressed = false;
  g_hwndThemedDialogCloseBtn =
      CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                      config->dlgW - 44, 4, 40, 24, hDlg,
                      (HMENU)(INT_PTR)config->closeBtnId, hInst, NULL);
  if (g_hwndThemedDialogCloseBtn) {
    g_oldThemedDialogCloseProc = (WNDPROC)SetWindowLongPtrW(
        g_hwndThemedDialogCloseBtn, GWLP_WNDPROC,
        (LONG_PTR)ThemedDialogCloseBtnProc);
  }

  SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)config);
  SetWindowLongPtrW(hDlg, GWLP_WNDPROC, (LONG_PTR)ThemedDialogProc);
  return hDlg;
}

void ShowThemedDialog(HWND hwndParent, HWND hDlg, ThemedDialogConfig *config) {
  InitThemedDialogFonts(hDlg, config);
  CenterDialogToParent(hDlg, hwndParent);
  ShowWindow(hDlg, SW_SHOW);
  EnableWindow(hwndParent, FALSE);
  if (config->initialFocus) {
    SetFocus(config->initialFocus);
  }
  g_hwndActiveThemedDialog = hDlg;
}

void RunThemedDialogLoop(HWND hDlg, bool *doneFlag) {
  MSG msg = {};
  while (doneFlag && !*doneFlag && GetMessageW(&msg, NULL, 0, 0)) {
    if (!IsDialogMessageW(hDlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
}

void CloseThemedDialog(HWND hwndParent, HWND hDlg, ThemedDialogConfig *config) {
  EnableWindow(hwndParent, TRUE);
  SetForegroundWindow(hwndParent);
  if (g_hwndThemedDialogCloseBtn &&
      GetParent(g_hwndThemedDialogCloseBtn) == hDlg) {
    g_hwndThemedDialogCloseBtn = NULL;
    g_oldThemedDialogCloseProc = NULL;
    g_themedDialogCloseHover = false;
    g_themedDialogClosePressed = false;
  }
  DestroyWindow(hDlg);
  if (g_hwndActiveThemedDialog == hDlg) {
    g_hwndActiveThemedDialog = NULL;
  }
  CleanupThemedDialogFonts(config);
}

bool ShowThemedConfirmDialog(HWND hwndParent,
                             const ThemedConfirmDialogConfig &dialog) {
  HINSTANCE hInst = GetModuleHandleW(NULL);
  const int closeBtnId = 4301;
  static bool s_confirmDone = false;
  static bool s_confirmAccepted = false;

  s_confirmDone = false;
  s_confirmAccepted = false;

  ThemedDialogConfig config = {};
  config.windowTitle = dialog.windowTitle;
  config.title = dialog.title;
  config.subtitle = dialog.subtitle;
  config.bodyText = dialog.bodyText;
  config.primaryButtonText =
      dialog.confirmText ? dialog.confirmText : L"确定";
  config.secondaryButtonText =
      dialog.cancelText ? dialog.cancelText : L"取消";
  config.dlgW = dialog.dlgW > 0 ? dialog.dlgW : 424;
  config.dlgH = dialog.dlgH > 0 ? dialog.dlgH : 246;
  config.closeBtnId = closeBtnId;
  config.bodyFontDelta = 0;
  config.titleFontDelta = 0;
  if (dialog.cardRect.right > dialog.cardRect.left &&
      dialog.cardRect.bottom > dialog.cardRect.top) {
    config.cardRect = dialog.cardRect;
  } else {
    config.cardRect = {14, 78, 410, 180};
  }
  config.doneFlag = &s_confirmDone;
  config.primaryButtonDanger = dialog.danger;
  config.drawCardBackground = dialog.drawCardBackground;

  HWND hDlg = CreateThemedDialog(hwndParent, hInst, &config);
  if (!hDlg)
    return false;

  const int btnW = 78;
  const int btnH = 27;
  const int btnGap = 10;
  const int btnY = config.dlgH - 42;
  const int cancelBtnX = config.dlgW - 24 - btnW;
  const int okBtnX = cancelBtnX - btnGap - btnW;
  CreateWindowExW(0, L"BUTTON", config.primaryButtonText,
                  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                  okBtnX, btnY, btnW, btnH, hDlg, (HMENU)IDOK, hInst, NULL);
  CreateWindowExW(0, L"BUTTON", config.secondaryButtonText,
                  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                  cancelBtnX, btnY, btnW, btnH, hDlg, (HMENU)IDCANCEL, hInst,
                  NULL);

  config.initialFocus = GetDlgItem(hDlg, IDCANCEL);
  config.onOk = +[](HWND, void *) -> bool {
    s_confirmAccepted = true;
    return true;
  };

  ShowThemedDialog(hwndParent, hDlg, &config);
  RunThemedDialogLoop(hDlg, &s_confirmDone);
  CloseThemedDialog(hwndParent, hDlg, &config);
  return s_confirmAccepted;
}

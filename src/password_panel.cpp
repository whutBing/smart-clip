#include "password_panel.h"

#include "graphics_utils.h"
#include "history.h"
#include "password_vault.h"
#include "themed_dialog.h"
#include "theme.h"
#include "tray.h"
#include <commctrl.h>
#include <cstdlib>
#include <gdiplus.h>
#include <set>
#include <string>
#include <uxtheme.h>
#include <vector>
#include <windows.h>

extern bool g_isTopmost;
using namespace Gdiplus;

static bool CopyPasswordTextToClipboard(const std::wstring &text) {
  if (!OpenClipboard(NULL))
    return false;
  EmptyClipboard();
  HGLOBAL hGlobal =
      GlobalAlloc(GMEM_MOVEABLE, (text.length() + 1) * sizeof(wchar_t));
  if (!hGlobal) {
    CloseClipboard();
    return false;
  }
  wchar_t *pData = (wchar_t *)GlobalLock(hGlobal);
  if (!pData) {
    GlobalFree(hGlobal);
    CloseClipboard();
    return false;
  }
  wcscpy_s(pData, text.length() + 1, text.c_str());
  GlobalUnlock(hGlobal);
  SetClipboardData(CF_UNICODETEXT, hGlobal);
  CloseClipboard();
  return true;
}

static wchar_t RandomCharsetChar(const std::wstring &charset) {
  if (charset.empty())
    return L'\0';
  return charset[rand() % charset.size()];
}

static std::wstring BuildRandomPassword(bool includeDigits, bool includeLower,
                                        bool includeUpper, bool includeSymbols,
                                        int length) {
  std::wstring digits = L"0123456789";
  std::wstring lower = L"abcdefghijklmnopqrstuvwxyz";
  std::wstring upper = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  std::wstring symbols = L"!@#$%^&*()_+-=,.<>?/\\|[]{}:;~";
  std::wstring charset;
  std::wstring result;

  if (includeDigits) {
    charset += digits;
    result += RandomCharsetChar(digits);
  }
  if (includeLower) {
    charset += lower;
    result += RandomCharsetChar(lower);
  }
  if (includeUpper) {
    charset += upper;
    result += RandomCharsetChar(upper);
  }
  if (includeSymbols) {
    charset += symbols;
    result += RandomCharsetChar(symbols);
  }

  if (charset.empty()) {
    charset = digits + lower + upper;
    result.clear();
  }

  while ((int)result.size() < length) {
    result += RandomCharsetChar(charset);
  }

  for (int i = (int)result.size() - 1; i > 0; --i) {
    int j = rand() % (i + 1);
    std::swap(result[i], result[j]);
  }

  if ((int)result.size() > length) {
    result.resize(length);
  }
  return result;
}

bool QuickGenerateConfiguredPassword(HWND hwndNotify) {
  std::wstring password = BuildRandomPassword(
      g_passwordGeneratorIncludeDigits, g_passwordGeneratorIncludeLower,
      g_passwordGeneratorIncludeUpper, g_passwordGeneratorIncludeSymbols,
      g_passwordGeneratorLength);
  if (password.empty()) {
    return false;
  }
  if (!CopyPasswordTextToClipboard(password)) {
    return false;
  }
  if (hwndNotify) {
    ShowTrayBalloon(hwndNotify, L"随机密码", L"已按当前规则复制随机密码");
  }
  return true;
}

enum {
  IDC_GEN_DIGITS = 6101,
  IDC_GEN_LOWER = 6102,
  IDC_GEN_UPPER = 6103,
  IDC_GEN_SYMBOLS = 6104,
  IDC_GEN_LENGTH_SLIDER = 6105,
  IDC_GEN_LENGTH_EDIT = 6106,
  IDC_GEN_PASSWORD_EDIT = 6107,
  IDC_GEN_REFRESH = 6108,
};

struct GeneratorState {
  bool done;
  bool includeDigits;
  bool includeLower;
  bool includeUpper;
  bool includeSymbols;
  int length;
  std::wstring password;
  HFONT hResultFont;
  HFONT hIconFont;
  HFONT hRefreshFont;
};

static int ClampPasswordGeneratorLength(int value) {
  if (value < 6)
    return 6;
  if (value > 64)
    return 64;
  return value;
}

static void PersistPasswordGeneratorSettings(const GeneratorState *state) {
  g_passwordGeneratorIncludeDigits = state->includeDigits;
  g_passwordGeneratorIncludeLower = state->includeLower;
  g_passwordGeneratorIncludeUpper = state->includeUpper;
  g_passwordGeneratorIncludeSymbols = state->includeSymbols;
  g_passwordGeneratorLength = state->length;
  SaveVaultSettings();
}

static void DestroyPasswordGeneratorFonts(GeneratorState *state) {
  if (state->hResultFont) {
    DeleteObject(state->hResultFont);
    state->hResultFont = NULL;
  }
  if (state->hIconFont) {
    DeleteObject(state->hIconFont);
    state->hIconFont = NULL;
  }
  if (state->hRefreshFont) {
    DeleteObject(state->hRefreshFont);
    state->hRefreshFont = NULL;
  }
}

static void EnsurePasswordGeneratorHasCharset(HWND hDlg, GeneratorState *state,
                                              int fallbackId) {
  if (state->includeDigits || state->includeLower || state->includeUpper ||
      state->includeSymbols) {
    return;
  }

  state->includeDigits = (fallbackId == IDC_GEN_DIGITS);
  state->includeLower = (fallbackId == IDC_GEN_LOWER);
  state->includeUpper = (fallbackId == IDC_GEN_UPPER);
  state->includeSymbols = (fallbackId == IDC_GEN_SYMBOLS);
  if (!state->includeDigits && !state->includeLower && !state->includeUpper &&
      !state->includeSymbols) {
    state->includeDigits = true;
  }

  SendDlgItemMessageW(hDlg, IDC_GEN_DIGITS, BM_SETCHECK,
                      state->includeDigits ? BST_CHECKED : BST_UNCHECKED, 0);
  SendDlgItemMessageW(hDlg, IDC_GEN_LOWER, BM_SETCHECK,
                      state->includeLower ? BST_CHECKED : BST_UNCHECKED, 0);
  SendDlgItemMessageW(hDlg, IDC_GEN_UPPER, BM_SETCHECK,
                      state->includeUpper ? BST_CHECKED : BST_UNCHECKED, 0);
  SendDlgItemMessageW(hDlg, IDC_GEN_SYMBOLS, BM_SETCHECK,
                      state->includeSymbols ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void SyncPasswordGeneratorFlags(HWND hDlg, GeneratorState *state) {
  state->includeDigits =
      SendDlgItemMessageW(hDlg, IDC_GEN_DIGITS, BM_GETCHECK, 0, 0) == BST_CHECKED;
  state->includeLower =
      SendDlgItemMessageW(hDlg, IDC_GEN_LOWER, BM_GETCHECK, 0, 0) == BST_CHECKED;
  state->includeUpper =
      SendDlgItemMessageW(hDlg, IDC_GEN_UPPER, BM_GETCHECK, 0, 0) == BST_CHECKED;
  state->includeSymbols =
      SendDlgItemMessageW(hDlg, IDC_GEN_SYMBOLS, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void RefreshPasswordGenerator(HWND hDlg, GeneratorState *state) {
  state->password = BuildRandomPassword(
      state->includeDigits, state->includeLower, state->includeUpper,
      state->includeSymbols, state->length);
  SetDlgItemTextW(hDlg, IDC_GEN_PASSWORD_EDIT, state->password.c_str());
  wchar_t lenBuf[16];
  _snwprintf_s(lenBuf, _countof(lenBuf), L"%d", state->length);
  SetDlgItemTextW(hDlg, IDC_GEN_LENGTH_EDIT, lenBuf);
  SendDlgItemMessageW(hDlg, IDC_GEN_LENGTH_SLIDER, TBM_SETPOS, TRUE,
                      state->length);
  HWND hPasswordEdit = GetDlgItem(hDlg, IDC_GEN_PASSWORD_EDIT);
  if (hPasswordEdit)
    InvalidateRect(hPasswordEdit, NULL, TRUE);
  PersistPasswordGeneratorSettings(state);
}

static void TriggerPasswordGeneratorRefresh(HWND hDlg, GeneratorState *state) {
  SyncPasswordGeneratorFlags(hDlg, state);
  EnsurePasswordGeneratorHasCharset(hDlg, state, IDC_GEN_DIGITS);
  RefreshPasswordGenerator(hDlg, state);
}

static bool HandlePasswordGeneratorMessage(HWND hw, UINT message, WPARAM wParam,
                                           LPARAM lParam, void *userData,
                                           LRESULT *result) {
  GeneratorState *state = static_cast<GeneratorState *>(userData);
  if (!state) {
    return false;
  }

  switch (message) {
  case WM_COMMAND: {
    WORD id = LOWORD(wParam);
    WORD notify = HIWORD(wParam);
    if ((id == IDC_GEN_DIGITS || id == IDC_GEN_LOWER || id == IDC_GEN_UPPER ||
         id == IDC_GEN_SYMBOLS) &&
        notify == BN_CLICKED) {
      SyncPasswordGeneratorFlags(hw, state);
      EnsurePasswordGeneratorHasCharset(hw, state, id);
      RefreshPasswordGenerator(hw, state);
      *result = 0;
      return true;
    }
    if (id == IDC_GEN_REFRESH && notify == BN_CLICKED) {
      TriggerPasswordGeneratorRefresh(hw, state);
      *result = 0;
      return true;
    }
    if (id == IDC_GEN_LENGTH_EDIT && notify == EN_CHANGE) {
      wchar_t buf[16] = {};
      GetDlgItemTextW(hw, IDC_GEN_LENGTH_EDIT, buf, _countof(buf));
      if (buf[0] == L'\0') {
        return false;
      }
      int value = ClampPasswordGeneratorLength(_wtoi(buf));
      if (value != state->length) {
        state->length = value;
        RefreshPasswordGenerator(hw, state);
      }
    }
    return false;
  }
  case WM_HSCROLL:
    if ((HWND)lParam == GetDlgItem(hw, IDC_GEN_LENGTH_SLIDER)) {
      state->length = (int)SendDlgItemMessageW(hw, IDC_GEN_LENGTH_SLIDER,
                                               TBM_GETPOS, 0, 0);
      RefreshPasswordGenerator(hw, state);
      *result = 0;
      return true;
    }
    return false;
  case WM_KEYDOWN:
    if (wParam == VK_F5 ||
        (wParam == 'R' && (GetKeyState(VK_CONTROL) & 0x8000))) {
      TriggerPasswordGeneratorRefresh(hw, state);
      *result = 0;
      return true;
    }
    return false;
  case WM_NOTIFY: {
    LPNMHDR hdr = reinterpret_cast<LPNMHDR>(lParam);
    if (!hdr || hdr->idFrom != IDC_GEN_LENGTH_SLIDER ||
        hdr->code != NM_CUSTOMDRAW) {
      return false;
    }

    LPNMCUSTOMDRAW cd = reinterpret_cast<LPNMCUSTOMDRAW>(lParam);
    if (cd->dwDrawStage != CDDS_PREPAINT) {
      return false;
    }

    HDC hdc = cd->hdc;
    RECT rc = cd->rc;
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    SolidBrush bgBrush(Color(255, GetRValue(GetThemeDialogBgColor()),
                             GetGValue(GetThemeDialogBgColor()),
                             GetBValue(GetThemeDialogBgColor())));
    g.FillRectangle(&bgBrush, (INT)rc.left, (INT)rc.top,
                    (INT)(rc.right - rc.left), (INT)(rc.bottom - rc.top));

    const int minValue =
        (int)SendMessageW(hdr->hwndFrom, TBM_GETRANGEMIN, 0, 0);
    const int maxValue =
        (int)SendMessageW(hdr->hwndFrom, TBM_GETRANGEMAX, 0, 0);
    const int currentValue =
        (int)SendMessageW(hdr->hwndFrom, TBM_GETPOS, 0, 0);

    const int thumbSize = 12;
    const int trackInset = thumbSize / 2 + 2;
    const int trackHeight = 4;
    const int centerY = (rc.top + rc.bottom) / 2;
    const int trackLeft = rc.left + trackInset;
    const int trackRight = rc.right - trackInset;
    const int trackWidth = std::max(1, trackRight - trackLeft);
    const double ratio =
        (maxValue > minValue)
            ? (double)(currentValue - minValue) / (double)(maxValue - minValue)
            : 0.0;
    const int thumbCenterX = trackLeft + (int)(ratio * trackWidth + 0.5);

    COLORREF inactiveTrackColor =
        g_isDarkMode ? RGB(70, 74, 82) : RGB(232, 235, 240);
    COLORREF activeTrackColor = RGB(120, 196, 255);
    Pen inactivePen(Color(255, GetRValue(inactiveTrackColor),
                          GetGValue(inactiveTrackColor),
                          GetBValue(inactiveTrackColor)),
                    (REAL)trackHeight);
    inactivePen.SetStartCap(LineCapRound);
    inactivePen.SetEndCap(LineCapRound);
    Pen activePen(Color(255, GetRValue(activeTrackColor),
                        GetGValue(activeTrackColor),
                        GetBValue(activeTrackColor)),
                  (REAL)trackHeight);
    activePen.SetStartCap(LineCapRound);
    activePen.SetEndCap(LineCapRound);
    g.DrawLine(&inactivePen, trackLeft, centerY, trackRight, centerY);
    g.DrawLine(&activePen, trackLeft, centerY, thumbCenterX, centerY);

    COLORREF thumbFillColor = RGB(255, 255, 255);
    Pen thumbBorderPen(Color(255, GetRValue(activeTrackColor),
                             GetGValue(activeTrackColor),
                             GetBValue(activeTrackColor)),
                       1.8f);
    SolidBrush thumbFillBrush(Color(255, GetRValue(thumbFillColor),
                                    GetGValue(thumbFillColor),
                                    GetBValue(thumbFillColor)));
    const int thumbX = thumbCenterX - thumbSize / 2;
    const int thumbY = centerY - thumbSize / 2;
    g.FillEllipse(&thumbFillBrush, thumbX, thumbY, thumbSize, thumbSize);
    g.DrawEllipse(&thumbBorderPen, thumbX, thumbY, thumbSize, thumbSize);

    *result = CDRF_SKIPDEFAULT;
    return true;
  }
  case WM_DRAWITEM: {
    LPDRAWITEMSTRUCT dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
    if (!dis) {
      return false;
    }
    if (dis->CtlID == IDC_GEN_REFRESH) {
      HDC hdc = dis->hDC;
      RECT rc = dis->rcItem;
      HBRUSH bgBrush = CreateSolidBrush(GetThemeDialogBgColor());
      FillRect(hdc, &rc, bgBrush);
      DeleteObject(bgBrush);

      Graphics g(hdc);
      g.SetSmoothingMode(SmoothingModeAntiAlias);
      bool pressed = (dis->itemState & ODS_SELECTED) != 0;
      bool focused = (dis->itemState & ODS_FOCUS) != 0;
      bool hover = (dis->itemState & ODS_HOTLIGHT) != 0;
      COLORREF fill = GetThemeAccentColor();
      if (pressed) {
        fill = g_isDarkMode ? RGB(52, 93, 146) : RGB(0, 94, 184);
      } else if (hover) {
        fill = g_isDarkMode ? RGB(92, 132, 196) : RGB(28, 120, 214);
      }
      COLORREF border = pressed ? fill
                                : (focused ? RGB(255, 255, 255) : fill);

      GraphicsPath path;
      CreateRoundRectPath(&path, rc.left + 1, rc.top + 1,
                          rc.right - rc.left - 2, rc.bottom - rc.top - 2, 10);
      SolidBrush brush(
          Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
      Pen pen(
          Color(255, GetRValue(border), GetGValue(border), GetBValue(border)),
          focused ? 1.6f : 1.0f);
      g.FillPath(&brush, &path);
      g.DrawPath(&pen, &path);

      HFONT oldFont = (HFONT)SelectObject(hdc, state->hRefreshFont);
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, RGB(255, 255, 255));
      DrawTextW(hdc, L"刷新", -1, &rc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      SelectObject(hdc, oldFont);
      *result = TRUE;
      return true;
    }
    return false;
  }
  case WM_CLOSE:
    PersistPasswordGeneratorSettings(state);
    DestroyPasswordGeneratorFonts(state);
    return false;
  default:
    return false;
  }
}

// 密码库菜单ID
#define IDM_PW_ADD 3500
#define IDM_PW_EDIT 3501
#define IDM_PW_DELETE 3502
#define IDM_PW_COPY 3503

void UpdatePasswordListBox() {
  SendMessageW(g_hwndListBox, LB_RESETCONTENT, 0, 0);
  g_displayIndexMap.clear();
  g_pwVisibleSet.clear();

  if (!g_vaultUnlocked)
    return;

  SendMessageW(g_hwndListBox, LB_ADDSTRING, 0, (LPARAM)L"");
  g_displayIndexMap.push_back(-2);

  std::wstring keyword = g_searchKeyword;
  for (int i = 0; i < (int)g_passwords.size(); i++) {
    if (!keyword.empty()) {
      std::wstring nameLower = g_passwords[i].name;
      std::wstring kwLower = keyword;
      for (auto &c : nameLower)
        c = towlower(c);
      for (auto &c : kwLower)
        c = towlower(c);
      if (nameLower.find(kwLower) == std::wstring::npos)
        continue;
    }
    SendMessageW(g_hwndListBox, LB_ADDSTRING, 0,
                 (LPARAM)g_passwords[i].name.c_str());
    g_displayIndexMap.push_back(i);
  }
  SendMessageW(g_hwndListBox, LB_ADDSTRING, 0, (LPARAM)L"+ 新增密码");
  g_displayIndexMap.push_back(-1);
}

void ShowSetMasterPasswordDialog(HWND hwndParent) {
  HINSTANCE hInst = GetModuleHandleW(NULL);
  const int closeBtnId = 2003;
  const int togglePw1Id = 2011;
  const int togglePw2Id = 2012;
  static bool s_setMasterDone = false;
  static std::vector<PasswordToggleBinding> s_passwordToggles;
  s_setMasterDone = false;
  s_passwordToggles = {{2001, togglePw1Id, false}, {2002, togglePw2Id, false}};
  ThemedDialogConfig config = {};
  config.windowTitle = L"设置主密码";
  config.title = L"设置主密码";
  config.subtitle = L"为密码库设置一个新的主密码";
  config.dlgW = 424;
  config.dlgH = 304;
  config.closeBtnId = closeBtnId;
  config.cardRect = {14, 78, 410, 238};
  static const int fieldLabelIds[] = {20011, 20012};
  config.fieldLabelIds = fieldLabelIds;
  config.fieldLabelCount = _countof(fieldLabelIds);
  config.doneFlag = &s_setMasterDone;
  config.userData = &s_passwordToggles;

  HWND hDlg = CreateThemedDialog(hwndParent, hInst, &config);
  if (!hDlg) {
    return;
  }

  const int labelX = 34;
  const int editX = 34;
  const int editW = 356;
  const int pwEditW = GetDialogPasswordEditWidth(editW);
  const int firstY = 92;
  const int blockGap = 62;
  CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"主密码（至少 6 个字符）",
                  WS_CHILD | WS_VISIBLE, labelX, firstY, 210, 20, hDlg,
                  (HMENU)20011, hInst, NULL);
  HWND hPw1 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | ES_PASSWORD |
                                  ES_AUTOHSCROLL | ES_MULTILINE,
                              editX, firstY + 24, pwEditW, 32, hDlg, (HMENU)2001,
                              hInst, NULL);
  ApplyDialogPasswordMask(hPw1, false);
  CreateDialogPasswordToggleButton(hDlg, hInst,
                                   GetDialogPasswordToggleX(editX, editW),
                                   firstY + 24, togglePw1Id);
  CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"确认密码",
                  WS_CHILD | WS_VISIBLE, labelX, firstY + blockGap, 160, 20,
                  hDlg, (HMENU)20012, hInst, NULL);
  HWND hPw2 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | ES_PASSWORD |
                                  ES_AUTOHSCROLL | ES_MULTILINE,
                              editX, firstY + blockGap + 24, pwEditW, 32, hDlg,
                              (HMENU)2002, hInst, NULL);
  ApplyDialogPasswordMask(hPw2, false);
  CreateDialogPasswordToggleButton(hDlg, hInst,
                                   GetDialogPasswordToggleX(editX, editW),
                                   firstY + blockGap + 24, togglePw2Id);
  config.initialFocus = hPw1;

  const int btnW = 65;
  const int btnH = 25;
  const int btnGap = 10;
  const int btnY = config.dlgH - 42;
  const int cancelBtnX = config.dlgW - 24 - btnW;
  const int okBtnX = cancelBtnX - btnGap - btnW;
  CreateWindowExW(0, L"BUTTON", L"确定",
                  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                  okBtnX, btnY, btnW, btnH, hDlg, (HMENU)IDOK, hInst, NULL);
  CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                  cancelBtnX, btnY, btnW, btnH, hDlg, (HMENU)IDCANCEL, hInst,
                  NULL);

  config.onOk = +[](HWND hw, void *) -> bool {
    wchar_t pw1[256] = {}, pw2[256] = {};
    GetDlgItemTextW(hw, 2001, pw1, 256);
    GetDlgItemTextW(hw, 2002, pw2, 256);
    if (wcslen(pw1) < 6) {
      MessageBoxW(hw, L"密码至少需要6个字符", L"提示", MB_OK);
      return false;
    }
    if (wcscmp(pw1, pw2) != 0) {
      MessageBoxW(hw, L"两次输入的密码不一致", L"提示", MB_OK);
      return false;
    }
    if (!SetMasterPassword(pw1)) {
      MessageBoxW(hw, L"设置密码失败", L"错误", MB_OK | MB_ICONERROR);
      return false;
    }
    return true;
  };

  ShowThemedDialog(hwndParent, hDlg, &config);
  RunThemedDialogLoop(hDlg, &s_setMasterDone);
  CloseThemedDialog(hwndParent, hDlg, &config);
}

void ShowVerifyMasterPasswordDialog(HWND hwndParent) {
  HINSTANCE hInst = GetModuleHandleW(NULL);
  const int closeBtnId = 2002;
  const int togglePwId = 2021;
  static bool s_verifyDone = false;
  static std::vector<PasswordToggleBinding> s_passwordToggles;
  s_verifyDone = false;
  s_passwordToggles = {{2001, togglePwId, false}};
  ThemedDialogConfig config = {};
  config.windowTitle = L"验证密码";
  config.title = L"验证密码";
  config.subtitle = L"请输入主密码以解锁密码库";
  config.dlgW = 424;
  config.dlgH = 248;
  config.closeBtnId = closeBtnId;
  config.bodyFontDelta = -2;
  config.cardRect = {14, 78, 410, 182};
  static const int fieldLabelIds[] = {20010};
  config.fieldLabelIds = fieldLabelIds;
  config.fieldLabelCount = _countof(fieldLabelIds);
  config.doneFlag = &s_verifyDone;
  config.userData = &s_passwordToggles;

  HWND hDlg = CreateThemedDialog(hwndParent, hInst, &config);
  if (!hDlg) {
    return;
  }

  CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"主密码", WS_CHILD | WS_VISIBLE,
                  34, 96, 120, 20, hDlg, (HMENU)20010, hInst, NULL);
  const int editX = 34;
  const int editW = 356;
  const int pwEditW = GetDialogPasswordEditWidth(editW);
  HWND hPw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | ES_PASSWORD |
                                 ES_AUTOHSCROLL | ES_MULTILINE,
                             editX, 120, pwEditW, 32, hDlg, (HMENU)2001, hInst,
                             NULL);
  ApplyDialogPasswordMask(hPw, false);
  CreateDialogPasswordToggleButton(hDlg, hInst,
                                   GetDialogPasswordToggleX(editX, editW), 120,
                                   togglePwId);
  config.initialFocus = hPw;

  const int btnW = 65;
  const int btnH = 25;
  const int btnGap = 10;
  const int btnY = config.dlgH - 42;
  const int cancelBtnX = config.dlgW - 24 - btnW;
  const int okBtnX = cancelBtnX - btnGap - btnW;
  CreateWindowExW(0, L"BUTTON", L"确定",
                  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                  okBtnX, btnY, btnW, btnH, hDlg, (HMENU)IDOK, hInst, NULL);
  CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                  cancelBtnX, btnY, btnW, btnH, hDlg, (HMENU)IDCANCEL, hInst,
                  NULL);

  config.onOk = +[](HWND hw, void *) -> bool {
    wchar_t pw[256] = {};
    GetDlgItemTextW(hw, 2001, pw, 256);
    if (!VerifyMasterPassword(pw)) {
      MessageBoxW(hw, L"密码错误", L"提示", MB_OK | MB_ICONWARNING);
      return false;
    }
    g_vaultUnlocked = true;
    return true;
  };

  ShowThemedDialog(hwndParent, hDlg, &config);
  RunThemedDialogLoop(hDlg, &s_verifyDone);
  CloseThemedDialog(hwndParent, hDlg, &config);
}

bool AuthenticateVaultAccess(HWND hwndParent) {
  if (!IsMasterPasswordSet()) {
    ShowSetMasterPasswordDialog(hwndParent);
    if (!g_masterPasswordSet)
      return false;
  }

  g_vaultUnlocked = false;
  g_passwords.clear();

  if (g_vaultAuthMethod == 1) {
    if (TryWindowsHelloAuth(hwndParent)) {
      g_vaultUnlocked = true;
      return true;
    }
  }

  ShowVerifyMasterPasswordDialog(hwndParent);
  return g_vaultUnlocked;
}

void ShowPasswordEntryDialog(HWND hwndParent, int editId) {
  wchar_t initName[256] = {}, initTitle[256] = {}, initAccount[256] = {},
          initPassword[256] = {};
  bool confirmed = false;

  if (editId >= 0) {
    for (const auto &e : g_passwords) {
      if (e.id == editId) {
        wcsncpy_s(initName, e.name.c_str(), 255);
        wcsncpy_s(initTitle, e.title.c_str(), 255);
        wcsncpy_s(initAccount, e.account.c_str(), 255);
        wcsncpy_s(initPassword, e.password.c_str(), 255);
        break;
      }
    }
  }

  HINSTANCE hInst = GetModuleHandleW(NULL);
  const wchar_t *dlgTitle = (editId >= 0) ? L"编辑密码" : L"新增密码";
  const int closeBtnId = 3004;
  const int toggleEntryPwId = 3013;
  static bool s_entryDone = false;
  static bool s_entryOk = false;
  static wchar_t s_entryName[256], s_entryTitle[256], s_entryAccount[256],
      s_entryPassword[256];
  static std::vector<PasswordToggleBinding> s_passwordToggles;
  s_entryDone = false;
  s_entryOk = false;
  s_passwordToggles = {{3003, toggleEntryPwId, false}};

  ThemedDialogConfig config = {};
  config.windowTitle = dlgTitle;
  config.title = dlgTitle;
  config.subtitle =
      (editId >= 0) ? L"更新站点、账号和密码信息" : L"保存新的账号和密码条目";
  config.dlgW = 424;
  config.dlgH = 396;
  config.closeBtnId = closeBtnId;
  config.bodyFontDelta = (editId >= 0) ? -2 : 0;
  config.titleFontDelta = (editId >= 0) ? 0 : -1;
  config.cardRect = {14, 78, 410, 342};
  static const int fieldLabelIds[] = {30010, 30011, 30012, 30013};
  config.fieldLabelIds = fieldLabelIds;
  config.fieldLabelCount = _countof(fieldLabelIds);
  config.doneFlag = &s_entryDone;
  config.userData = &s_passwordToggles;

  HWND hDlg = CreateThemedDialog(hwndParent, hInst, &config);
  if (!hDlg)
    return;

  const int labelX = 34;
  const int editX = 34;
  const int editW = 356;
  const int pwEditW = GetDialogPasswordEditWidth(editW);
  const int firstY = 92;
  const int blockGap = 62;

  CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"名称",
                  WS_CHILD | WS_VISIBLE, labelX, firstY, 100, 20, hDlg,
                  (HMENU)30010, hInst, NULL);
  CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initName,
                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_MULTILINE, editX,
                  firstY + 24, editW, 32, hDlg, (HMENU)3000, hInst, NULL);
  CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"网址 / 应用",
                  WS_CHILD | WS_VISIBLE, labelX, firstY + blockGap, 120, 20,
                  hDlg, (HMENU)30011, hInst, NULL);
  CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initTitle,
                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_MULTILINE, editX,
                  firstY + blockGap + 24, editW, 32, hDlg, (HMENU)3001, hInst,
                  NULL);
  CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"账号",
                  WS_CHILD | WS_VISIBLE, labelX, firstY + blockGap * 2, 100, 20,
                  hDlg, (HMENU)30012, hInst, NULL);
  CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initAccount,
                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_MULTILINE, editX,
                  firstY + blockGap * 2 + 24, editW, 32, hDlg, (HMENU)3002,
                  hInst, NULL);
  CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"密码",
                  WS_CHILD | WS_VISIBLE, labelX, firstY + blockGap * 3, 100, 20,
                  hDlg, (HMENU)30013, hInst, NULL);
  HWND hEntryPw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initPassword,
                                  WS_CHILD | WS_VISIBLE | ES_PASSWORD |
                                      ES_AUTOHSCROLL | ES_MULTILINE,
                                  editX, firstY + blockGap * 3 + 24, pwEditW, 32,
                                  hDlg, (HMENU)3003, hInst, NULL);
  ApplyDialogPasswordMask(hEntryPw, false);
  CreateDialogPasswordToggleButton(hDlg, hInst,
                                   GetDialogPasswordToggleX(editX, editW),
                                   firstY + blockGap * 3 + 24, toggleEntryPwId);
  const int btnW = 65;
  const int btnH = 25;
  const int btnGap = 10;
  const int btnY = config.dlgH - 42;
  const int cancelBtnX = config.dlgW - 24 - btnW;
  const int okBtnX = cancelBtnX - btnGap - btnW;
  CreateWindowExW(0, L"BUTTON", L"确定",
                  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                  okBtnX, btnY, btnW, btnH, hDlg, (HMENU)IDOK, hInst, NULL);
  CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                  cancelBtnX, btnY, btnW, btnH, hDlg, (HMENU)IDCANCEL, hInst,
                  NULL);

  config.initialFocus = GetDlgItem(hDlg, 3000);
  config.onOk = +[](HWND hw, void *) -> bool {
    wchar_t n[256] = {};
    GetDlgItemTextW(hw, 3000, n, 256);
    if (wcslen(n) == 0) {
      MessageBoxW(hw, L"名称不能为空", L"提示", MB_OK);
      return false;
    }
    GetDlgItemTextW(hw, 3000, s_entryName, 256);
    GetDlgItemTextW(hw, 3001, s_entryTitle, 256);
    GetDlgItemTextW(hw, 3002, s_entryAccount, 256);
    GetDlgItemTextW(hw, 3003, s_entryPassword, 256);
    s_entryOk = true;
    return true;
  };

  ShowThemedDialog(hwndParent, hDlg, &config);
  RunThemedDialogLoop(hDlg, &s_entryDone);

  confirmed = s_entryOk;
  if (confirmed) {
    wcsncpy_s(initName, s_entryName, 255);
    wcsncpy_s(initTitle, s_entryTitle, 255);
    wcsncpy_s(initAccount, s_entryAccount, 255);
    wcsncpy_s(initPassword, s_entryPassword, 255);
  }

  CloseThemedDialog(hwndParent, hDlg, &config);

  if (confirmed) {
    if (editId >= 0) {
      UpdatePasswordEntry(editId, initName, initTitle, initAccount, initPassword);
    } else {
      AddPasswordEntry(initName, initTitle, initAccount, initPassword);
    }
    UpdatePasswordListBox();
  }
}

void ShowResetMasterPasswordDialog(HWND hwndParent) {
  HINSTANCE hInst = GetModuleHandleW(NULL);
  const int closeBtnId = 2004;
  const int toggleOldPwId = 2031;
  const int toggleNewPwId = 2032;
  const int toggleConfirmPwId = 2033;
  static bool s_resetDone = false;
  static std::vector<PasswordToggleBinding> s_passwordToggles;
  s_resetDone = false;
  s_passwordToggles = {{2001, toggleOldPwId, false},
                       {2002, toggleNewPwId, false},
                       {2003, toggleConfirmPwId, false}};
  ThemedDialogConfig config = {};
  config.windowTitle = L"重置主密码";
  config.title = L"重置主密码";
  config.subtitle = L"验证旧密码后，设置新的主密码";
  config.dlgW = 424;
  config.dlgH = 370;
  config.closeBtnId = closeBtnId;
  config.bodyFontDelta = -2;
  config.cardRect = {14, 78, 410, 304};
  static const int fieldLabelIds[] = {20021, 20022, 20023};
  config.fieldLabelIds = fieldLabelIds;
  config.fieldLabelCount = _countof(fieldLabelIds);
  config.doneFlag = &s_resetDone;
  config.userData = &s_passwordToggles;

  HWND hDlg = CreateThemedDialog(hwndParent, hInst, &config);
  if (!hDlg) {
    return;
  }

  const int labelX = 34;
  const int editX = 34;
  const int editW = 356;
  const int pwEditW = GetDialogPasswordEditWidth(editW);
  const int firstY = 92;
  const int blockGap = 62;
  CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"旧密码",
                  WS_CHILD | WS_VISIBLE, labelX, firstY, 120, 20, hDlg,
                  (HMENU)20021, hInst, NULL);
  HWND hOldPw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | ES_PASSWORD |
                                    ES_AUTOHSCROLL | ES_MULTILINE,
                                editX, firstY + 24, pwEditW, 32, hDlg,
                                (HMENU)2001, hInst, NULL);
  ApplyDialogPasswordMask(hOldPw, false);
  CreateDialogPasswordToggleButton(hDlg, hInst,
                                   GetDialogPasswordToggleX(editX, editW),
                                   firstY + 24, toggleOldPwId);
  CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"新密码（至少 6 个字符）",
                  WS_CHILD | WS_VISIBLE, labelX, firstY + blockGap, 190, 20,
                  hDlg, (HMENU)20022, hInst, NULL);
  HWND hNewPw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | ES_PASSWORD |
                                    ES_AUTOHSCROLL | ES_MULTILINE,
                                editX, firstY + blockGap + 24, pwEditW, 32, hDlg,
                                (HMENU)2002, hInst, NULL);
  ApplyDialogPasswordMask(hNewPw, false);
  CreateDialogPasswordToggleButton(hDlg, hInst,
                                   GetDialogPasswordToggleX(editX, editW),
                                   firstY + blockGap + 24, toggleNewPwId);
  CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"确认新密码",
                  WS_CHILD | WS_VISIBLE, labelX, firstY + blockGap * 2, 120, 20,
                  hDlg, (HMENU)20023, hInst, NULL);
  HWND hConfirmPw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | ES_PASSWORD |
                                        ES_AUTOHSCROLL | ES_MULTILINE,
                                    editX, firstY + blockGap * 2 + 24, pwEditW,
                                    32, hDlg, (HMENU)2003, hInst, NULL);
  ApplyDialogPasswordMask(hConfirmPw, false);
  CreateDialogPasswordToggleButton(hDlg, hInst,
                                   GetDialogPasswordToggleX(editX, editW),
                                   firstY + blockGap * 2 + 24,
                                   toggleConfirmPwId);
  config.initialFocus = hOldPw;

  const int btnW = 65;
  const int btnH = 25;
  const int btnGap = 10;
  const int btnY = config.dlgH - 42;
  const int cancelBtnX = config.dlgW - 24 - btnW;
  const int okBtnX = cancelBtnX - btnGap - btnW;
  CreateWindowExW(0, L"BUTTON", L"确定",
                  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                  okBtnX, btnY, btnW, btnH, hDlg, (HMENU)IDOK, hInst, NULL);
  CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                  cancelBtnX, btnY, btnW, btnH, hDlg, (HMENU)IDCANCEL, hInst,
                  NULL);

  config.onOk = +[](HWND hw, void *) -> bool {
    wchar_t oldPw[256] = {}, newPw[256] = {}, confirmPw[256] = {};
    GetDlgItemTextW(hw, 2001, oldPw, 256);
    GetDlgItemTextW(hw, 2002, newPw, 256);
    GetDlgItemTextW(hw, 2003, confirmPw, 256);
    if (wcslen(newPw) < 6) {
      MessageBoxW(hw, L"新密码至少需要6个字符", L"提示", MB_OK);
      return false;
    }
    if (wcscmp(newPw, confirmPw) != 0) {
      MessageBoxW(hw, L"两次输入的新密码不一致", L"提示", MB_OK);
      return false;
    }
    if (!ResetMasterPassword(oldPw, newPw)) {
      MessageBoxW(hw, L"旧密码错误", L"提示", MB_OK | MB_ICONWARNING);
      return false;
    }
    MessageBoxW(hw, L"主密码已重置", L"成功", MB_OK | MB_ICONINFORMATION);
    return true;
  };

  ShowThemedDialog(hwndParent, hDlg, &config);
  RunThemedDialogLoop(hDlg, &s_resetDone);
  CloseThemedDialog(hwndParent, hDlg, &config);
}

void ShowPasswordContextMenu(HWND hwnd, int index, POINT pt) {
  if (index < 0 || index >= (int)g_passwords.size())
    return;

  HMENU hMenu = CreatePopupMenu();
  HBITMAP hCopyIcon = CreateMenuIconBitmap(L"\uE8C8");
  HBITMAP hEditIcon = CreateMenuIconBitmap(L"\uE70F");
  HBITMAP hDeleteIcon = CreateMenuIconBitmap(L"\uE74D", RGB(200, 60, 60));

  MENUITEMINFOW mii = {};
  mii.cbSize = sizeof(MENUITEMINFOW);
  mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;

  mii.wID = IDM_PW_COPY;
  mii.dwTypeData = (LPWSTR)L"复制账号密码";
  mii.hbmpItem = hCopyIcon;
  InsertMenuItemW(hMenu, 0, TRUE, &mii);

  mii.wID = IDM_PW_EDIT;
  mii.dwTypeData = (LPWSTR)L"编辑";
  mii.hbmpItem = hEditIcon;
  InsertMenuItemW(hMenu, 1, TRUE, &mii);

  AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

  mii.wID = IDM_PW_DELETE;
  mii.dwTypeData = (LPWSTR)L"删除";
  mii.hbmpItem = hDeleteIcon;
  InsertMenuItemW(hMenu, 3, TRUE, &mii);

  ClientToScreen(hwnd, &pt);
  int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0,
                           hwnd, NULL);
  DestroyMenu(hMenu);
  DeleteObject(hCopyIcon);
  DeleteObject(hEditIcon);
  DeleteObject(hDeleteIcon);

  if (cmd == IDM_PW_COPY) {
    StartPasswordBatchCopy(index, g_hwndMain);
    if (!g_isTopmost) {
      ShowWindow(g_hwndMain, SW_HIDE);
    }
  } else if (cmd == IDM_PW_EDIT) {
    ShowPasswordEntryDialog(hwnd, g_passwords[index].id);
  } else if (cmd == IDM_PW_DELETE) {
    wchar_t msg[512];
    _snwprintf_s(msg, 512, L"确定删除 \"%s\" 的密码记录？",
                 g_passwords[index].title.c_str());
    ThemedConfirmDialogConfig dialog = {L"删除密码记录",
                                        L"删除当前密码记录",
                                        L"该操作不可撤销",
                                        msg,
                                        L"删除",
                                        L"取消",
                                        424,
                                        246,
                                        {14, 78, 410, 180},
                                        true};
    if (ShowThemedConfirmDialog(hwnd, dialog)) {
      DeletePasswordEntry(g_passwords[index].id);
      UpdatePasswordListBox();
    }
  }
}

void ShowRandomPasswordGeneratorDialog(HWND hwndParent) {
  GeneratorState state = {};
  state.done = false;
  state.includeDigits = g_passwordGeneratorIncludeDigits;
  state.includeLower = g_passwordGeneratorIncludeLower;
  state.includeUpper = g_passwordGeneratorIncludeUpper;
  state.includeSymbols = g_passwordGeneratorIncludeSymbols;
  state.length = ClampPasswordGeneratorLength(g_passwordGeneratorLength);
  state.hResultFont = NULL;
  state.hIconFont = NULL;
  state.hRefreshFont = NULL;
  srand((unsigned int)GetTickCount());

  HINSTANCE hInst = GetModuleHandleW(NULL);
  ThemedDialogConfig config = {};
  config.windowTitle = L"随机密码";
  config.title = L"随机密码";
  config.subtitle = L"按当前规则生成，并一键复制到剪贴板";
  config.primaryButtonText = L"复制密码";
  config.secondaryButtonText = L"关闭";
  config.dlgW = 484;
  config.dlgH = 344;
  config.closeBtnId = 6110;
  config.bodyFontDelta = -2;
  config.cardRect = {14, 78, 470, 276};
  static const int fieldLabelIds[] = {6201, 6202, 6203};
  config.fieldLabelIds = fieldLabelIds;
  config.fieldLabelCount = _countof(fieldLabelIds);
  config.doneFlag = &state.done;
  config.userData = &state;

  HWND hDlg = CreateThemedDialog(hwndParent, hInst, &config);
  if (!hDlg)
    return;

  CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"字符类型",
                  WS_CHILD | WS_VISIBLE, 34, 94, 120, 20, hDlg,
                  (HMENU)6201, hInst, NULL);
  HWND hDigits = CreateWindowExW(0, L"BUTTON", L"数字 0-9",
                                 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                 34, 122, 86, 22, hDlg,
                                 (HMENU)IDC_GEN_DIGITS, hInst, NULL);
  HWND hLower = CreateWindowExW(0, L"BUTTON", L"小写 a-z",
                                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                128, 122, 86, 22, hDlg,
                                (HMENU)IDC_GEN_LOWER, hInst, NULL);
  HWND hUpper = CreateWindowExW(0, L"BUTTON", L"大写 A-Z",
                                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                222, 122, 92, 22, hDlg,
                                (HMENU)IDC_GEN_UPPER, hInst, NULL);
  HWND hSymbols = CreateWindowExW(
      0, L"BUTTON", L"符号 !@#$%",
      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 322, 122, 116, 22, hDlg,
      (HMENU)IDC_GEN_SYMBOLS, hInst, NULL);
  SendMessageW(hDigits, BM_SETCHECK,
               state.includeDigits ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(hLower, BM_SETCHECK,
               state.includeLower ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(hUpper, BM_SETCHECK,
               state.includeUpper ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(hSymbols, BM_SETCHECK,
               state.includeSymbols ? BST_CHECKED : BST_UNCHECKED, 0);

  CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"密码长度",
                  WS_CHILD | WS_VISIBLE, 34, 164, 120, 20, hDlg,
                  (HMENU)6202, hInst, NULL);
  HWND hSlider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                 WS_CHILD | WS_VISIBLE | TBS_NOTICKS, 34, 187,
                                 316, 24, hDlg,
                                 (HMENU)IDC_GEN_LENGTH_SLIDER, hInst, NULL);
  SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(6, 64));
  SendMessageW(hSlider, TBM_SETPAGESIZE, 0, 2);
  SetWindowTheme(hSlider, L"", L"");
  CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                  WS_CHILD | WS_VISIBLE | ES_CENTER | ES_NUMBER |
                      ES_AUTOHSCROLL | ES_MULTILINE,
                  360, 182, 78, 32, hDlg, (HMENU)IDC_GEN_LENGTH_EDIT, hInst,
                  NULL);

  CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"随机密码结果",
                  WS_CHILD | WS_VISIBLE, 34, 224, 180, 20, hDlg,
                  (HMENU)6203, hInst, NULL);
  HWND hPasswordEdit = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY | ES_MULTILINE, 34,
      248, 316, 32, hDlg, (HMENU)IDC_GEN_PASSWORD_EDIT, hInst, NULL);
  HWND hRefresh = CreateWindowExW(0, L"BUTTON", L"",
                                  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 362, 248,
                                  76, 32, hDlg, (HMENU)IDC_GEN_REFRESH, hInst,
                                  NULL);

  state.hResultFont = CreateFontW(17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE,
                                  L"Consolas");
  state.hIconFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE,
                                L"Segoe MDL2 Assets");
  state.hRefreshFont = CreateFontW(13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                                   FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE,
                                   L"Microsoft YaHei");
  SendMessageW(hPasswordEdit, WM_SETFONT, (WPARAM)state.hResultFont, TRUE);
  SendMessageW(hRefresh, WM_SETFONT, (WPARAM)state.hRefreshFont, TRUE);
  SendMessageW(hSlider, TBM_SETPOS, TRUE, state.length);

  config.initialFocus = hRefresh;
  RefreshPasswordGenerator(hDlg, &state);

  const int btnW = 90;
  const int btnH = 27;
  const int btnGap = 10;
  const int btnY = config.dlgH - 42;
  const int cancelBtnX = config.dlgW - 24 - btnW;
  const int okBtnX = cancelBtnX - btnGap - 120;
  CreateWindowExW(0, L"BUTTON", L"复制密码",
                  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                  okBtnX, btnY, 120, btnH, hDlg, (HMENU)IDOK, hInst, NULL);
  CreateWindowExW(0, L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                  cancelBtnX, btnY, btnW, btnH, hDlg, (HMENU)IDCANCEL, hInst,
                  NULL);

  config.onOk = +[](HWND hw, void *) -> bool {
    wchar_t pw[512] = {};
    GetDlgItemTextW(hw, IDC_GEN_PASSWORD_EDIT, pw, _countof(pw));
    return CopyPasswordTextToClipboard(pw);
  };
  config.onMessage = HandlePasswordGeneratorMessage;

  ShowThemedDialog(hwndParent, hDlg, &config);
  RunThemedDialogLoop(hDlg, &state.done);
  DestroyPasswordGeneratorFonts(&state);
  CloseThemedDialog(hwndParent, hDlg, &config);
}

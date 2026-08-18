#include "machine_info.h"

#include "graphics_utils.h"
#include "i18n.h"
#include "theme.h"
#include <wincrypt.h>
#include <windowsx.h>
#include <cstring>
#include <string>

using namespace Gdiplus;

// ==================== 硬件信息采集 ====================

struct MachineInfo {
  std::wstring machineCode;
  std::wstring computerName;
  std::wstring osVersion;
  std::wstring cpu;
  std::wstring ram;
  std::wstring diskSerial;
};

static std::wstring TrimW(const std::wstring &s) {
  size_t begin = s.find_first_not_of(L" \t\r\n");
  if (begin == std::wstring::npos)
    return L"";
  size_t end = s.find_last_not_of(L" \t\r\n");
  return s.substr(begin, end - begin + 1);
}

// 读取注册表字符串值
static std::wstring ReadRegString(HKEY root, const wchar_t *subKey,
                                  const wchar_t *name) {
  std::wstring result;
  HKEY hKey = NULL;
  if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
    wchar_t buf[512] = {};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    if (RegQueryValueExW(hKey, name, NULL, &type, (LPBYTE)buf, &size) ==
            ERROR_SUCCESS &&
        type == REG_SZ) {
      result = buf;
    }
    RegCloseKey(hKey);
  }
  return TrimW(result);
}

// Windows 版本：RtlGetVersion 不受 GetVersionExW 兼容性遮蔽影响
static std::wstring GetOsVersionText() {
  typedef LONG(WINAPI *RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
  RtlGetVersionFn pRtlGetVersion =
      (RtlGetVersionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                      "RtlGetVersion");
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  RTL_OSVERSIONINFOW ovi = {};
  if (pRtlGetVersion) {
    ovi.dwOSVersionInfoSize = sizeof(ovi);
    pRtlGetVersion(&ovi);
  }

  std::wstring product = ReadRegString(
      HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
      L"ProductName");
  std::wstring display = ReadRegString(
      HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
      L"DisplayVersion");
  if (!product.empty()) {
    std::wstring text = product;
    if (!display.empty())
      text += L" (" + display + L")";
    if (ovi.dwBuildNumber) {
      wchar_t buf[32];
      wsprintfW(buf, L", Build %lu", (unsigned long)ovi.dwBuildNumber);
      text += buf;
    }
    return text;
  }
  wchar_t buf[96];
  wsprintfW(buf, L"Windows %lu.%lu (Build %lu)",
            (unsigned long)ovi.dwMajorVersion, (unsigned long)ovi.dwMinorVersion,
            (unsigned long)ovi.dwBuildNumber);
  return buf;
}

static std::wstring GetRamText() {
  MEMORYSTATUSEX ms = {};
  ms.dwLength = sizeof(ms);
  if (GlobalMemoryStatusEx(&ms)) {
    double gb = (double)ms.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    wchar_t buf[32];
    swprintf(buf, 32, L"%.1f GB", gb);
    return buf;
  }
  return L"";
}

static std::wstring GetDiskSerialText() {
  DWORD serial = 0;
  if (GetVolumeInformationW(L"C:\\", NULL, 0, &serial, NULL, NULL, NULL, 0)) {
    wchar_t buf[16];
    wsprintfW(buf, L"%04X-%04X", (serial >> 16) & 0xFFFF, serial & 0xFFFF);
    return buf;
  }
  return L"";
}

// 机器码指纹：系统盘卷序列号 + 计算机名 + CPU 型号
static std::wstring GetFingerprintString() {
  DWORD serial = 0;
  GetVolumeInformationW(L"C:\\", NULL, 0, &serial, NULL, NULL, NULL, 0);

  wchar_t compName[MAX_COMPUTERNAME_LENGTH + 1] = {};
  DWORD compLen = _countof(compName);
  GetComputerNameW(compName, &compLen);

  std::wstring cpu = ReadRegString(
      HKEY_LOCAL_MACHINE,
      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
      L"ProcessorNameString");

  wchar_t buf[1024];
  wsprintfW(buf, L"%08X|%s|%s", serial, compName, cpu.c_str());
  return buf;
}

// FNV-1a 64 位哈希（纯 C++ 实现，无系统 API 依赖，输出稳定且不会全零）
static unsigned long long Fnv1a64(const void *data, size_t len,
                                  unsigned long long seed) {
  unsigned long long h = 14695981039346656037ULL;
  h ^= seed;
  const unsigned char *p = (const unsigned char *)data;
  for (size_t i = 0; i < len; ++i) {
    h ^= p[i];
    h *= 1099511628211ULL;
  }
  return h;
}

// 对指纹做双 FNV-1a 哈希（128 位）并格式化为 32 位十六进制机器码
// 格式：XXXX-XXXX-XXXX-XXXX-XXXX-XXXX-XXXX-XXXX
static std::wstring HashToMachineCode(const std::wstring &fingerprint) {
  const void *data = fingerprint.c_str();
  size_t len = fingerprint.size() * sizeof(wchar_t);
  unsigned long long a = Fnv1a64(data, len, 0x9E3779B97F4A7C15ULL);
  unsigned long long b = Fnv1a64(data, len, 0xC2B2AE3D27D4EB4FULL);
  unsigned char bytes[16];
  memcpy(bytes, &a, 8);
  memcpy(bytes + 8, &b, 8);

  // 16 字节 = 32 位十六进制 + 7 个短横线 + 结尾符 = 40
  static const wchar_t kHex[] = L"0123456789ABCDEF";
  wchar_t code[40];
  int pos = 0;
  for (int i = 0; i < 16; ++i) {
    if (i > 0 && i % 2 == 0)
      code[pos++] = L'-';
    code[pos++] = kHex[bytes[i] >> 4];
    code[pos++] = kHex[bytes[i] & 0xF];
  }
  code[pos] = L'\0';
  return std::wstring(code);
}

static const MachineInfo &GetMachineInfo() {
  static MachineInfo info;
  static bool initialized = false;
  if (!initialized) {
    wchar_t compName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD compLen = _countof(compName);
    if (GetComputerNameW(compName, &compLen))
      info.computerName = compName;
    info.osVersion = GetOsVersionText();
    info.cpu = ReadRegString(
        HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"ProcessorNameString");
    info.ram = GetRamText();
    info.diskSerial = GetDiskSerialText();
    info.machineCode = HashToMachineCode(GetFingerprintString());
    initialized = true;
  }
  return info;
}

std::wstring GetMachineCode() { return GetMachineInfo().machineCode; }

// ==================== 本机信息弹窗 ====================

#define IDC_MACHINE_INFO_CLOSE 4501
#define IDC_MACHINE_INFO_COPY 4502
#define ID_MACHINE_INFO_COPIED_TIMER 4503

static const int kMInfoTitlebarH = 36;
static const int kMInfoPad = 24;
static const int kMInfoCodeBoxH = 44;
static const int kMInfoRowH = 34;
static const int kMInfoRowLabelW = 108;

static HWND g_hwndMachineInfo = NULL;
static bool g_machineInfoDone = false;
static bool g_machineInfoClassRegistered = false;

static UINT g_machineInfoDpi = 96;
static HFONT g_mInfoHeaderFont = NULL;  // 标题
static HFONT g_mInfoSectionFont = NULL; // 分区标签/行标签
static HFONT g_mInfoCodeFont = NULL;    // 机器码
static HFONT g_mInfoValueFont = NULL;   // 硬件值
static HFONT g_mInfoBtnFont = NULL;     // 按钮文字
static HFONT g_mInfoCloseFont = NULL;   // 关闭图标

static HWND g_hwndMachineInfoClose = NULL;
static WNDPROC g_oldMachineInfoCloseProc = NULL;
static bool g_machineInfoCloseHover = false;
static bool g_machineInfoClosePressed = false;

static HWND g_hwndMachineInfoCopy = NULL;
static WNDPROC g_oldMachineInfoCopyProc = NULL;
static bool g_machineInfoCopyHover = false;
static bool g_machineInfoCopyPressed = false;
static bool g_machineInfoCopied = false;

static int MIDpi(int v) { return ScaleForDpi(v, g_machineInfoDpi); }
static int MInfoTitlebarH() { return MIDpi(kMInfoTitlebarH); }
static int MInfoPad() { return MIDpi(kMInfoPad); }
static int MInfoCodeBoxH() { return MIDpi(kMInfoCodeBoxH); }
static int MInfoRowH() { return MIDpi(kMInfoRowH); }
static int MInfoRowLabelW() { return MIDpi(kMInfoRowLabelW); }

// 机器码标题 / 代码框 / 硬件标题 / 硬件行起始 Y
static int MInfoCodeLabelY() { return MInfoTitlebarH() + MIDpi(16); }
static int MInfoCodeBoxY() {
  return MInfoCodeLabelY() + MIDpi(20) + MIDpi(8);
}
static int MInfoHwLabelY() {
  return MInfoCodeBoxY() + MInfoCodeBoxH() + MIDpi(14);
}
static int MInfoRowsY() { return MInfoHwLabelY() + MIDpi(20) + MIDpi(6); }

static int MInfoWindowW() { return MIDpi(520); }

static int MInfoWindowH() {
  return MInfoRowsY() + 5 * MInfoRowH() + MIDpi(16);
}

static RECT MInfoCopyBtnRect() {
  int btnW = MIDpi(110);
  int btnH = MIDpi(30);
  int btnX = MInfoWindowW() - MInfoPad() - btnW;
  int btnY = MInfoCodeBoxY() + (MInfoCodeBoxH() - btnH) / 2;
  return {btnX, btnY, btnX + btnW, btnY + btnH};
}

static void CopyMachineCodeToClipboard() {
  std::wstring text = GetMachineCode();
  if (text.empty())
    return;
  // 使用对话框作为剪贴板 owner，避免 OpenClipboard(NULL) 导致系统
  // 将前台窗口切换为未知 owner，从而抢走对话框的激活状态。
  if (!OpenClipboard(g_hwndMachineInfo))
    return;
  EmptyClipboard();
  HGLOBAL hMem =
      GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
  if (hMem) {
    wchar_t *dst = (wchar_t *)GlobalLock(hMem);
    if (dst) {
      memcpy(dst, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
      GlobalUnlock(hMem);
      SetClipboardData(CF_UNICODETEXT, hMem);
    }
  }
  CloseClipboard();
}

static LRESULT CALLBACK MachineInfoCloseBtnProc(HWND hwnd, UINT msg,
                                                WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_MOUSEMOVE: {
    if (!g_machineInfoCloseHover) {
      g_machineInfoCloseHover = true;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    break;
  }
  case WM_MOUSELEAVE:
    if (g_machineInfoCloseHover) {
      g_machineInfoCloseHover = false;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    break;
  case WM_LBUTTONDOWN:
    g_machineInfoClosePressed = true;
    InvalidateRect(hwnd, NULL, FALSE);
    break;
  case WM_LBUTTONUP:
    g_machineInfoClosePressed = false;
    InvalidateRect(hwnd, NULL, FALSE);
    PostMessageW(GetParent(hwnd), WM_CLOSE, 0, 0);
    return 0;
  }
  return CallWindowProcW(g_oldMachineInfoCloseProc, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK MachineInfoCopyBtnProc(HWND hwnd, UINT msg,
                                               WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_MOUSEMOVE: {
    if (!g_machineInfoCopyHover) {
      g_machineInfoCopyHover = true;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    break;
  }
  case WM_MOUSELEAVE:
    if (g_machineInfoCopyHover) {
      g_machineInfoCopyHover = false;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    break;
  case WM_LBUTTONDOWN:
    g_machineInfoCopyPressed = true;
    InvalidateRect(hwnd, NULL, FALSE);
    break;
  case WM_LBUTTONUP:
    g_machineInfoCopyPressed = false;
    InvalidateRect(hwnd, NULL, FALSE);
    SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDC_MACHINE_INFO_COPY,
                                                         BN_CLICKED),
                 (LPARAM)hwnd);
    return 0;
  }
  return CallWindowProcW(g_oldMachineInfoCopyProc, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK MachineInfoDialogProc(HWND hwnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam) {
  switch (msg) {

  case WM_CREATE: {
    HINSTANCE hInst = GetModuleHandleW(NULL);
    g_machineInfoDpi = GetSmartClipUiDpi(hwnd);

    g_mInfoHeaderFont = CreateFontW(
        MIDpi(20), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_mInfoSectionFont = CreateFontW(
        MIDpi(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_mInfoCodeFont = CreateFontW(
        MIDpi(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
    g_mInfoValueFont = CreateFontW(
        MIDpi(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_mInfoBtnFont = CreateFontW(
        MIDpi(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_mInfoCloseFont = CreateFontW(
        MIDpi(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");

    // 复制机器码按钮（位于代码框内右侧）
    RECT btn = MInfoCopyBtnRect();
    g_hwndMachineInfoCopy = CreateWindowExW(
        0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, btn.left,
        btn.top, btn.right - btn.left, btn.bottom - btn.top, hwnd,
        (HMENU)IDC_MACHINE_INFO_COPY, hInst, NULL);
    if (g_hwndMachineInfoCopy) {
      SendMessageW(g_hwndMachineInfoCopy, WM_SETFONT, (WPARAM)g_mInfoBtnFont,
                   TRUE);
      g_oldMachineInfoCopyProc =
          (WNDPROC)SetWindowLongPtrW(g_hwndMachineInfoCopy, GWLP_WNDPROC,
                                     (LONG_PTR)MachineInfoCopyBtnProc);
    }

    // 关闭按钮
    g_machineInfoCloseHover = false;
    g_machineInfoClosePressed = false;
    g_hwndMachineInfoClose = CreateWindowExW(
        0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        MInfoWindowW() - MIDpi(44), MIDpi(4), MIDpi(40), MIDpi(24), hwnd,
        (HMENU)IDC_MACHINE_INFO_CLOSE, hInst, NULL);
    if (g_hwndMachineInfoClose) {
      SendMessageW(g_hwndMachineInfoClose, WM_SETFONT,
                   (WPARAM)g_mInfoCloseFont, TRUE);
      g_oldMachineInfoCloseProc =
          (WNDPROC)SetWindowLongPtrW(g_hwndMachineInfoClose, GWLP_WNDPROC,
                                     (LONG_PTR)MachineInfoCloseBtnProc);
    }
    return 0;
  }

  case WM_ERASEBKGND:
    return 1;

  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC screenDc = BeginPaint(hwnd, &ps);
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    int w = rcClient.right - rcClient.left;
    int h = rcClient.bottom - rcClient.top;

    HDC hdc = CreateCompatibleDC(screenDc);
    HBITMAP memBmp = CreateCompatibleBitmap(screenDc, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(hdc, memBmp);
    SetBkMode(hdc, TRANSPARENT);

    // 背景
    HBRUSH bgBrush = CreateSolidBrush(GetThemeDialogBgColor());
    FillRect(hdc, &rcClient, bgBrush);
    DeleteObject(bgBrush);

    // 标题栏
    RECT rcTitlebar = {0, 0, w, MInfoTitlebarH()};
    HBRUSH tbBrush = CreateSolidBrush(GetThemeTitlebarBgColor());
    FillRect(hdc, &rcTitlebar, tbBrush);
    DeleteObject(tbBrush);
    HPEN sepPen = CreatePen(PS_SOLID, 1, GetThemeSeparatorColor());
    HGDIOBJ oldPen = SelectObject(hdc, sepPen);
    MoveToEx(hdc, 0, MInfoTitlebarH() - 1, NULL);
    LineTo(hdc, w, MInfoTitlebarH() - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(sepPen);

    // 标题文字
    HFONT oldFont = (HFONT)SelectObject(hdc, g_mInfoHeaderFont);
    SetTextColor(hdc, GetThemeTextPrimaryColor());
    RECT rcTitleText = {MInfoPad(), 0, w - MIDpi(60), MInfoTitlebarH()};
    DrawTextW(hdc, T(STR_MACHINE_INFO_TITLE), -1, &rcTitleText,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const MachineInfo &info = GetMachineInfo();

    // 机器码分区标题
    SelectObject(hdc, g_mInfoSectionFont);
    SetTextColor(hdc, GetThemeTextSecondaryColor());
    RECT rcCodeLabel = {MInfoPad(), MInfoCodeLabelY(), w - MInfoPad(),
                        MInfoCodeLabelY() + MIDpi(20)};
    DrawTextW(hdc, T(STR_MACHINE_INFO_CODE_LABEL), -1, &rcCodeLabel,
              DT_LEFT | DT_TOP | DT_SINGLELINE);

    // 机器码代码框
    int boxY = MInfoCodeBoxY();
    RECT rcBox = {MInfoPad(), boxY, w - MInfoPad(), boxY + MInfoCodeBoxH()};
    HBRUSH boxBrush = CreateSolidBrush(GetThemeDialogEditBgColor());
    FillRect(hdc, &rcBox, boxBrush);
    DeleteObject(boxBrush);
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    GraphicsPath boxPath;
    CreateRoundRectPath(&boxPath, rcBox.left, rcBox.top, rcBox.right - rcBox.left,
                        rcBox.bottom - rcBox.top, MIDpi(8));
    COLORREF borderColor =
        g_isDarkMode ? RGB(86, 90, 98) : RGB(210, 214, 220);
    Pen borderPen(Color(255, GetRValue(borderColor), GetGValue(borderColor),
                        GetBValue(borderColor)),
                  1.0f);
    g.DrawPath(&borderPen, &boxPath);

    // 机器码文本（复制按钮左侧）
    RECT btnRect = MInfoCopyBtnRect();
    RECT rcCode = {MInfoPad() + MIDpi(12), boxY, btnRect.left - MIDpi(8),
                   boxY + MInfoCodeBoxH()};
    SelectObject(hdc, g_mInfoCodeFont);
    SetTextColor(hdc, GetThemeAccentColor());
    DrawTextW(hdc, info.machineCode.c_str(), -1, &rcCode,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 硬件信息分区标题
    SelectObject(hdc, g_mInfoSectionFont);
    SetTextColor(hdc, GetThemeTextSecondaryColor());
    RECT rcHwLabel = {MInfoPad(), MInfoHwLabelY(), w - MInfoPad(),
                      MInfoHwLabelY() + MIDpi(20)};
    DrawTextW(hdc, T(STR_MACHINE_INFO_HW_LABEL), -1, &rcHwLabel,
              DT_LEFT | DT_TOP | DT_SINGLELINE);

    // 硬件信息行
    struct RowDef {
      StringId label;
      const std::wstring *value;
    };
    RowDef rows[] = {
        {STR_MACHINE_INFO_COMPUTER, &info.computerName},
        {STR_MACHINE_INFO_OS, &info.osVersion},
        {STR_MACHINE_INFO_CPU, &info.cpu},
        {STR_MACHINE_INFO_RAM, &info.ram},
        {STR_MACHINE_INFO_DISK, &info.diskSerial},
    };
    int rowsY = MInfoRowsY();
    for (int i = 0; i < 5; ++i) {
      int rowY = rowsY + i * MInfoRowH();
      SelectObject(hdc, g_mInfoSectionFont);
      SetTextColor(hdc, GetThemeTextSecondaryColor());
      RECT rcLabel = {MInfoPad(), rowY, MInfoPad() + MInfoRowLabelW(),
                      rowY + MInfoRowH()};
      DrawTextW(hdc, T(rows[i].label), -1, &rcLabel,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

      SelectObject(hdc, g_mInfoValueFont);
      SetTextColor(hdc, GetThemeTextPrimaryColor());
      RECT rcValue = {MInfoPad() + MInfoRowLabelW() + MIDpi(8), rowY,
                      w - MInfoPad(), rowY + MInfoRowH()};
      DrawTextW(hdc, rows[i].value->c_str(), -1, &rcValue,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, oldFont);
    BitBlt(screenDc, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(hdc);

    EndPaint(hwnd, &ps);
    return 0;
  }

  case WM_DRAWITEM: {
    LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
    if (!dis)
      return FALSE;
    RECT rc = dis->rcItem;
    HDC hdc = dis->hDC;

    // 关闭按钮
    if ((int)dis->CtlID == IDC_MACHINE_INFO_CLOSE) {
      COLORREF bg = GetThemeTitlebarBgColor();
      if (g_machineInfoCloseHover)
        bg = RGB(232, 17, 35);
      HBRUSH hBr = CreateSolidBrush(bg);
      FillRect(hdc, &rc, hBr);
      DeleteObject(hBr);
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, g_machineInfoCloseHover ? RGB(255, 255, 255)
                                                : GetThemeTextPrimaryColor());
      HFONT hOldF =
          (HFONT)SelectObject(hdc, (HFONT)SendMessageW(dis->hwndItem,
                                                       WM_GETFONT, 0, 0));
      DrawTextW(hdc, L"\uE8BB", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      SelectObject(hdc, hOldF);
      return TRUE;
    }

    // 复制按钮（内存 DC 双缓冲消除 hover/按下重绘时的闪烁）
    if ((int)dis->CtlID == IDC_MACHINE_INFO_COPY) {
      int w = rc.right - rc.left;
      int h = rc.bottom - rc.top;
      if (w <= 0 || h <= 0)
        return TRUE;
      HDC memDC = CreateCompatibleDC(hdc);
      HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
      HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
      RECT rcMem = {0, 0, w, h};
      SetBkMode(memDC, TRANSPARENT);

      HBRUSH bgBtnBrush = CreateSolidBrush(GetThemeDialogBgColor());
      FillRect(memDC, &rcMem, bgBtnBrush);
      DeleteObject(bgBtnBrush);

      Graphics g(memDC);
      g.SetSmoothingMode(SmoothingModeAntiAlias);
      GraphicsPath path;
      CreateRoundRectPath(&path, 0, 0, w, h, MIDpi(8));

      COLORREF fill;
      if (g_machineInfoCopied) {
        fill = g_isDarkMode ? RGB(52, 148, 94) : RGB(46, 160, 67);
      } else {
        fill = g_machineInfoCopyPressed ? RGB(0, 94, 184)
                                        : GetThemeAccentColor();
      }
      SolidBrush brush(Color(255, GetRValue(fill), GetGValue(fill),
                             GetBValue(fill)));
      g.FillPath(&brush, &path);

      SetTextColor(memDC, RGB(255, 255, 255));
      HFONT hOldF = (HFONT)SelectObject(
          memDC, (HFONT)SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0));
      const wchar_t *label = g_machineInfoCopied ? T(STR_MACHINE_INFO_COPIED)
                                                 : T(STR_MACHINE_INFO_COPY);
      DrawTextW(memDC, label, -1, &rcMem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      SelectObject(memDC, hOldF);

      BitBlt(hdc, rc.left, rc.top, w, h, memDC, 0, 0, SRCCOPY);
      SelectObject(memDC, oldBmp);
      DeleteObject(memBmp);
      DeleteDC(memDC);
      return TRUE;
    }
    return FALSE;
  }

  case WM_COMMAND: {
    int id = LOWORD(wParam);
    if (id == IDC_MACHINE_INFO_COPY) {
      CopyMachineCodeToClipboard();
      // 剪贴板操作后系统可能切换前台窗口，重新激活对话框确保关闭按钮可点击
      if (g_hwndMachineInfo) {
        SetForegroundWindow(g_hwndMachineInfo);
        SetActiveWindow(g_hwndMachineInfo);
        SetFocus(g_hwndMachineInfo);
        BringWindowToTop(g_hwndMachineInfo);
      }
      g_machineInfoCopied = true;
      if (g_hwndMachineInfoCopy)
        InvalidateRect(g_hwndMachineInfoCopy, NULL, FALSE);
      SetTimer(hwnd, ID_MACHINE_INFO_COPIED_TIMER, 1500, NULL);
      return 0;
    }
    // 关闭按钮/ESC（IsDialogMessageW 会把按钮点击与 ESC 转为 WM_COMMAND）
    if (id == IDC_MACHINE_INFO_CLOSE || id == IDCANCEL) {
      g_machineInfoDone = true;
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
      return 0;
    }
    break;
  }

  case WM_TIMER:
    if (wParam == ID_MACHINE_INFO_COPIED_TIMER) {
      KillTimer(hwnd, ID_MACHINE_INFO_COPIED_TIMER);
      g_machineInfoCopied = false;
      if (g_hwndMachineInfoCopy)
        InvalidateRect(g_hwndMachineInfoCopy, NULL, FALSE);
    }
    return 0;

  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) {
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
      return 0;
    }
    break;

  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;

  case WM_DESTROY:
    g_machineInfoDone = true;
    if (g_mInfoHeaderFont)
      DeleteObject(g_mInfoHeaderFont);
    if (g_mInfoSectionFont)
      DeleteObject(g_mInfoSectionFont);
    if (g_mInfoCodeFont)
      DeleteObject(g_mInfoCodeFont);
    if (g_mInfoValueFont)
      DeleteObject(g_mInfoValueFont);
    if (g_mInfoBtnFont)
      DeleteObject(g_mInfoBtnFont);
    if (g_mInfoCloseFont)
      DeleteObject(g_mInfoCloseFont);
    g_mInfoHeaderFont = g_mInfoSectionFont = g_mInfoCodeFont = NULL;
    g_mInfoValueFont = g_mInfoBtnFont = g_mInfoCloseFont = NULL;
    g_hwndMachineInfo = NULL;
    g_hwndMachineInfoClose = NULL;
    g_hwndMachineInfoCopy = NULL;
    g_oldMachineInfoCloseProc = NULL;
    g_oldMachineInfoCopyProc = NULL;
    g_machineInfoCloseHover = g_machineInfoClosePressed = false;
    g_machineInfoCopyHover = g_machineInfoCopyPressed = false;
    g_machineInfoCopied = false;
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowMachineInfoDialog(HWND hwndParent) {
  if (!hwndParent || !IsWindow(hwndParent))
    return;
  HINSTANCE hInst = GetModuleHandleW(NULL);

  // 暂停主窗口的剪贴板监听，避免复制机器码后 WM_CLIPBOARDUPDATE 处理
  // 触发主窗口 UI 更新从而抢夺焦点/遮盖本对话框，导致关闭按钮失效。
  // 同时避免机器码被误录入剪贴板历史（机器码非用户内容）。
  extern bool g_isClipboardPaused;
  bool wasPaused = g_isClipboardPaused;
  g_isClipboardPaused = true;

  // 窗口创建前先按父窗口 DPI 计算尺寸（WM_CREATE 会再次校准）
  g_machineInfoDpi = GetSmartClipUiDpi(hwndParent);

  if (!g_machineInfoClassRegistered) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MachineInfoDialogProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"SmartClipMachineInfoDlg";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    RegisterClassW(&wc);
    g_machineInfoClassRegistered = true;
  }

  // 先采集一次信息（同时用于计算窗口高度）
  GetMachineInfo();

  g_machineInfoDone = false;
  g_machineInfoCopied = false;

  int w = MInfoWindowW();
  int h = MInfoWindowH();
  g_hwndMachineInfo = CreateWindowExW(
      WS_EX_TOOLWINDOW, L"SmartClipMachineInfoDlg", L"",
      WS_POPUP | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, w, h,
      hwndParent, NULL, hInst, NULL);
  if (!g_hwndMachineInfo)
    return;

  // 相对父窗口居中
  RECT rcParent, rcDlg;
  GetWindowRect(hwndParent, &rcParent);
  GetWindowRect(g_hwndMachineInfo, &rcDlg);
  int x = rcParent.left +
          ((rcParent.right - rcParent.left) - (rcDlg.right - rcDlg.left)) / 2;
  int y = rcParent.top +
          ((rcParent.bottom - rcParent.top) - (rcDlg.bottom - rcDlg.top)) / 2;
  SetWindowPos(g_hwndMachineInfo, NULL, x, y, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER);

  ShowWindow(g_hwndMachineInfo, SW_SHOW);
  SetForegroundWindow(g_hwndMachineInfo);
  EnableWindow(hwndParent, FALSE);

  // 模态消息循环
  // 注意：不能用 GetMessageW(hwnd=NULL)，否则主窗口消息队列中的 WM_QUIT
  // 会使循环异常退出（g_machineInfoDone 仍为 false），导致窗口无法关闭。
  // 用 PeekMessageW 只取对话框相关消息，WM_QUIT 被忽略不退出循环。
  MSG msg = {};
  while (!g_machineInfoDone) {
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        // 主窗口 WM_QUIT：不退出模态循环，放回队列待主窗口处理
        PostMessageW(NULL, WM_QUIT, msg.wParam, msg.lParam);
        continue;
      }
      if (!IsDialogMessageW(g_hwndMachineInfo, &msg)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    }
    if (!g_machineInfoDone)
      WaitMessage();
  }

  EnableWindow(hwndParent, TRUE);
  SetForegroundWindow(hwndParent);
  // WM_CLOSE → DestroyWindow → WM_DESTROY 已在对话框过程中完成
  // 此处仅确保窗口句柄清空（避免 DestroyWindow 二次调用）
  if (g_hwndMachineInfo && IsWindow(g_hwndMachineInfo)) {
    DestroyWindow(g_hwndMachineInfo);
  }
  g_hwndMachineInfo = NULL;

  // 恢复剪贴板监听的原始状态
  g_isClipboardPaused = wasPaused;
}

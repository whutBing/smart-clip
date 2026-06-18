#pragma once

#include <vector>
#include <windows.h>

struct ThemedDialogConfig {
  const wchar_t *windowTitle;
  const wchar_t *title;
  const wchar_t *subtitle;
  const wchar_t *bodyText;
  const wchar_t *primaryButtonText;
  const wchar_t *secondaryButtonText;
  int dlgW;
  int dlgH;
  int closeBtnId;
  int bodyFontDelta;
  int titleFontDelta;
  bool drawCardBorder;
  bool drawCardBackground;
  RECT cardRect;
  const int *fieldLabelIds;
  int fieldLabelCount;
  HWND initialFocus;
  bool *doneFlag;
  void *userData;
  bool (*onOk)(HWND, void *);
  bool (*onMessage)(HWND, UINT, WPARAM, LPARAM, void *, LRESULT *);
  bool primaryButtonDanger;
  bool showWarningIcon;
  HFONT hFont;
  HFONT hTitleFont;
  HFONT hCloseFont;
};

struct ThemedConfirmDialogConfig {
  const wchar_t *windowTitle;
  const wchar_t *title;
  const wchar_t *subtitle;
  const wchar_t *bodyText;
  const wchar_t *confirmText;
  const wchar_t *cancelText;
  int dlgW;
  int dlgH;
  RECT cardRect;
  bool danger;
  bool drawCardBackground;
  bool showWarningIcon;
};

struct PasswordToggleBinding {
  int editId;
  int buttonId;
  bool revealed;
};

extern HWND g_hwndActiveThemedDialog;

void ApplyDialogPasswordMask(HWND hEdit, bool revealed);
HWND CreateDialogPasswordToggleButton(HWND hDlg, HINSTANCE hInst, int x, int y,
                                      int buttonId);
int GetDialogPasswordEditWidth(int fullWidth);
int GetDialogPasswordToggleX(int editX, int fullWidth);
HWND CreateThemedDialog(HWND hwndParent, HINSTANCE hInst,
                        ThemedDialogConfig *config);
void ShowThemedDialog(HWND hwndParent, HWND hDlg, ThemedDialogConfig *config);
void RunThemedDialogLoop(HWND hDlg, bool *doneFlag);
void CloseThemedDialog(HWND hwndParent, HWND hDlg, ThemedDialogConfig *config);
bool ShowThemedConfirmDialog(HWND hwndParent,
                             const ThemedConfirmDialogConfig &dialog);

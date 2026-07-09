#include "scrollbar.h"
#include "../include/smartclip.h"
#include "../include/theme.h"
#include <algorithm>

// 滚动条状态变量定义
bool g_scrollbarVisible = false;
bool g_isScrollbarHovered = false;
bool g_isScrollbarDragging = false;
int g_scrollbarDragStartY = 0;
int g_scrollbarDragStartOffset = 0;
RECT g_lastThumbRect = {0, 0, 0, 0};
bool g_lastThumbValid = false;
bool g_lastThumbVisible = false;
bool g_lastThumbHovered = false;
bool g_lastThumbDragging = false;

bool GetCustomScrollbarTrackRect(HWND hwnd, RECT *rcTrack) {
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

bool GetCustomScrollbarReservedRect(HWND hwnd, RECT *rcReserved) {
  if (!hwnd || !rcReserved || !NeedsCustomScrollbar())
    return false;

  RECT rcClient;
  GetClientRect(hwnd, &rcClient);
  int reservedWidth = GetCustomScrollbarReservedWidth();
  rcReserved->left = rcClient.right - reservedWidth;
  rcReserved->top = rcClient.top;
  rcReserved->right = rcClient.right;
  rcReserved->bottom = rcClient.bottom;
  return true;
}

bool GetCustomScrollbarThumbRect(HWND hwnd, RECT *rcThumb) {
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
    // 当已滚动到底部时，强制滑块贴底，避免 contentOffset 与 maxScrollOffset
    // 之间的非线性映射导致滑块回弹、拖拽时反复抖动（"鬼畜"）
    int maxTop = GetListBoxMaxTopIndex();
    if (currentTop >= maxTop && maxTop > 0) {
      thumbY = travel;
    } else {
      int currentOffset = GetContentOffsetForTopIndex(currentTop);
      thumbY = (currentOffset * travel + maxScrollOffset / 2) / maxScrollOffset;
      if (thumbY > travel)
        thumbY = travel;
      if (thumbY < 0)
        thumbY = 0;
    }
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

void StartScrollbarHideTimer(HWND hwnd) {
  KillTimer(hwnd, ID_SCROLLBAR_HIDE_TIMER);
  int hideDelay = g_customScrollbarHideDelayMs;
  if (hideDelay < 600)
    hideDelay = 600;
  if (hideDelay > 2000)
    hideDelay = 2000;
  if (NeedsCustomScrollbar())
    SetTimer(hwnd, ID_SCROLLBAR_HIDE_TIMER, (UINT)hideDelay, NULL);
}

void InvalidateCustomScrollbarArea(HWND hwnd, BOOL erase) {
  RECT rcReserved = {};
  if (GetCustomScrollbarReservedRect(hwnd, &rcReserved)) {
    RedrawWindow(hwnd, &rcReserved, NULL,
                 RDW_INVALIDATE | RDW_NOCHILDREN |
                     (erase ? RDW_ERASE : RDW_NOERASE));
  }
}

void UpdateScrollbarCacheSnapshot(const RECT *thumbRect, bool hasThumb) {
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

void RefreshScrollbarIfChanged(HWND hwnd) {
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

  // 滚动条轨道很窄，直接失效整条轨道更稳；由 WM_PAINT 双缓冲统一绘制，
  // 避免同步擦除屏幕 DC 造成首次黑条和滚动条闪烁。
  InvalidateCustomScrollbarArea(hwnd, FALSE);

  UpdateScrollbarCacheSnapshot(hasNew ? &rcNew : NULL, hasNew);
}

void PaintCustomScrollbarOverlay(HWND hwnd, HDC hdc, const RECT *rcPaint) {
  if (!hwnd || !hdc)
    return;

  RECT rcReserved = {};
  if (!GetCustomScrollbarReservedRect(hwnd, &rcReserved))
    return;

  RECT rcClip = rcReserved;
  if (rcPaint)
    rcClip = *rcPaint;
  else {
    int clipType = GetClipBox(hdc, &rcClip);
    if (clipType == ERROR || clipType == NULLREGION)
      return;
  }

  RECT rcReservedPaint = {};
  if (!IntersectRect(&rcReservedPaint, &rcReserved, &rcClip))
    return;

  HBRUSH hTrackBrush = CreateSolidBrush(GetWhiteColor());
  FillRect(hdc, &rcReservedPaint, hTrackBrush);
  DeleteObject(hTrackBrush);

  int maxTop = GetListBoxMaxTopIndex();
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

void ShowCustomScrollbar(HWND hwnd, bool showQuickPasteHint) {
  (void)showQuickPasteHint;
  if (!NeedsCustomScrollbar())
    return;
  g_scrollbarVisible = true;
  if (!g_isScrollbarDragging)
    StartScrollbarHideTimer(hwnd);
}

void HandleScrollbarDrag(HWND hwnd, int mouseY) {
  if (!g_isScrollbarDragging || !hwnd)
    return;

  RECT rcTrack;
  if (!GetCustomScrollbarTrackRect(hwnd, &rcTrack))
    return;

  RECT rcThumb;
  if (!GetCustomScrollbarThumbRect(hwnd, &rcThumb))
    return;

  int trackHeight = rcTrack.bottom - rcTrack.top;
  int thumbHeight = rcThumb.bottom - rcThumb.top;
  int travel = trackHeight - thumbHeight - 4;
  if (travel <= 0)
    return;

  int deltaY = mouseY - g_scrollbarDragStartY;
  int newThumbY = g_scrollbarDragStartOffset + deltaY;
  if (newThumbY < 0)
    newThumbY = 0;
  if (newThumbY > travel)
    newThumbY = travel;

  int maxScrollOffset = GetMaxListScrollOffset(hwnd);
  int newOffset = (newThumbY * maxScrollOffset + travel / 2) / travel;

  int newTop = GetTopIndexForContentOffset(newOffset);
  int maxTop = GetListBoxMaxTopIndex();
  if (newTop > maxTop)
    newTop = maxTop;
  if (newTop < 0)
    newTop = 0;

  if (newTop != g_listBoxTopIndex) {
    ApplyListBoxTopIndex(hwnd, newTop);
    RefreshScrollbarIfChanged(hwnd);
  }
}

void HandleScrollbarClick(HWND hwnd, int mouseY) {
  if (!hwnd)
    return;

  RECT rcTrack;
  if (!GetCustomScrollbarTrackRect(hwnd, &rcTrack))
    return;

  RECT rcThumb;
  if (!GetCustomScrollbarThumbRect(hwnd, &rcThumb))
    return;

  // 点击滑块上方：向上翻页
  if (mouseY < rcThumb.top) {
    int visibleCount = CalculateVisibleItemCount(g_listBoxTopIndex);
    int newTop = g_listBoxTopIndex - visibleCount;
    if (newTop < 0)
      newTop = 0;
    if (newTop != g_listBoxTopIndex) {
      ApplyListBoxTopIndex(hwnd, newTop);
      ShowCustomScrollbar(hwnd);
      RefreshScrollbarIfChanged(hwnd);
    }
  }
  // 点击滑块下方：向下翻页
  else if (mouseY > rcThumb.bottom) {
    int visibleCount = CalculateVisibleItemCount(g_listBoxTopIndex);
    int newTop = g_listBoxTopIndex + visibleCount;
    int maxTop = GetListBoxMaxTopIndex();
    if (newTop > maxTop)
      newTop = maxTop;
    if (newTop != g_listBoxTopIndex) {
      ApplyListBoxTopIndex(hwnd, newTop);
      ShowCustomScrollbar(hwnd);
      RefreshScrollbarIfChanged(hwnd);
    }
  }
}

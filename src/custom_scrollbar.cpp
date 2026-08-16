// custom_scrollbar.cpp
// 通用自定义滚动条组件实现：样式与主窗体 ListBox 滚动条一致。
// 滑块颜色：拖拽=强调强色；悬停=中灰；默认=浅灰（适配暗黑模式）。
#include "custom_scrollbar.h"
#include "graphics_utils.h"
#include "theme.h"
#include <algorithm>

extern bool g_isDarkMode;

static UINT CSGetDpi(CustomScrollbar *sb) {
  return GetSmartClipUiDpi(sb ? sb->hwndOwner : NULL);
}

void CSInit(CustomScrollbar *sb, HWND hwndOwner, HWND hwndTarget,
            UINT_PTR hideTimerId) {
  if (!sb)
    return;
  UINT dpi = CSGetDpi(sb);
  sb->hwndOwner = hwndOwner;
  sb->hwndTarget = hwndTarget;
  sb->visible = false;
  sb->hovered = false;
  sb->dragging = false;
  sb->dragStartY = 0;
  sb->dragStartScrollTop = 0;
  sb->trackWidth = ScaleForDpi(CS_DEFAULT_TRACK_WIDTH, dpi);
  sb->thumbMinHeight = ScaleForDpi(CS_DEFAULT_THUMB_MIN_HEIGHT, dpi);
  sb->hideTimerId = hideTimerId;
  sb->hideDelayMs = CS_DEFAULT_HIDE_DELAY_MS;
}

bool CSNeedsShow(CustomScrollbar *sb, CSGetTotalHeightFn getTotal,
                 CSGetVisibleHeightFn getVisible) {
  if (!sb || !sb->hwndTarget || !getTotal || !getVisible)
    return false;
  int total = getTotal(sb->hwndTarget);
  int visible = getVisible(sb->hwndTarget);
  return total > visible && visible > 0;
}

int CSReservedWidth(CustomScrollbar *sb) {
  if (!sb)
    return 0;
  return sb->trackWidth + ScaleForDpi(2, CSGetDpi(sb));
}

static bool CSGetReservedRect(CustomScrollbar *sb, RECT *rcReserved) {
  if (!sb || !rcReserved || !sb->hwndOwner)
    return false;
  RECT rcClient;
  GetClientRect(sb->hwndOwner, &rcClient);
  int reservedWidth = CSReservedWidth(sb);
  rcReserved->left = rcClient.right - reservedWidth;
  rcReserved->top = rcClient.top;
  rcReserved->right = rcClient.right;
  rcReserved->bottom = rcClient.bottom;
  return true;
}

bool CSGetTrackRect(CustomScrollbar *sb, RECT *rcTrack) {
  if (!sb || !rcTrack || !sb->hwndOwner)
    return false;
  RECT rcClient;
  GetClientRect(sb->hwndOwner, &rcClient);
  rcTrack->left = rcClient.right - sb->trackWidth;
  rcTrack->top = rcClient.top;
  rcTrack->right = rcClient.right;
  rcTrack->bottom = rcClient.bottom;
  return true;
}

bool CSGetThumbRect(CustomScrollbar *sb, RECT *rcThumb,
                    CSGetTotalHeightFn getTotal,
                    CSGetVisibleHeightFn getVisible,
                    CSGetScrollTopFn getScroll) {
  if (!sb || !rcThumb || !getTotal || !getVisible || !getScroll)
    return false;
  RECT rcTrack;
  if (!CSGetTrackRect(sb, &rcTrack))
    return false;
  UINT dpi = CSGetDpi(sb);
  int total = getTotal(sb->hwndTarget);
  int visible = getVisible(sb->hwndTarget);
  if (total <= 0 || visible <= 0 || total <= visible)
    return false;
  int scrollTop = getScroll(sb->hwndTarget);
  int trackHeight = rcTrack.bottom - rcTrack.top;
  int padTopBottom = ScaleForDpi(4, dpi);
  int drawableTrackHeight = std::max(0, trackHeight - padTopBottom);
  int thumbHeight = (visible * drawableTrackHeight) / total;
  if (thumbHeight < sb->thumbMinHeight)
    thumbHeight = sb->thumbMinHeight;
  if (thumbHeight > drawableTrackHeight)
    thumbHeight = drawableTrackHeight;
  int travel = drawableTrackHeight - thumbHeight;
  int maxScroll = total - visible;
  int thumbY = 0;
  if (travel > 0 && maxScroll > 0) {
    thumbY = (scrollTop * travel + maxScroll / 2) / maxScroll;
  }
  int inset2 = ScaleForDpi(2, dpi);
  int thumbWidth = ScaleForDpi(10, dpi);
  int minThumbHeight = ScaleForDpi(12, dpi);
  rcThumb->left = std::max(rcTrack.left + inset2, rcTrack.right - thumbWidth);
  rcThumb->top = rcTrack.top + thumbY + inset2;
  rcThumb->right = rcTrack.right - inset2;
  rcThumb->bottom = rcThumb->top + thumbHeight;
  if (rcThumb->bottom < rcThumb->top + minThumbHeight)
    rcThumb->bottom = rcThumb->top + minThumbHeight;
  if (rcThumb->bottom > rcTrack.bottom - inset2)
    rcThumb->bottom = rcTrack.bottom - inset2;
  return true;
}

void CSPaint(CustomScrollbar *sb, HDC hdc, CSGetTotalHeightFn getTotal,
             CSGetVisibleHeightFn getVisible, CSGetScrollTopFn getScroll) {
  if (!sb || !hdc)
    return;
  RECT rcReserved;
  if (!CSGetReservedRect(sb, &rcReserved))
    return;
  HBRUSH hTrackBrush = CreateSolidBrush(GetThemeSurfaceColor());
  FillRect(hdc, &rcReserved, hTrackBrush);
  DeleteObject(hTrackBrush);

  RECT rcTrack;
  if (!CSGetTrackRect(sb, &rcTrack))
    return;

  if (!sb->visible)
    return;
  RECT rcThumb;
  if (!CSGetThumbRect(sb, &rcThumb, getTotal, getVisible, getScroll))
    return;
  // 滑块颜色：与主窗体一致
  COLORREF thumbColor =
      sb->dragging
          ? GetThemeAccentStrongColor()
          : (sb->hovered
                 ? (g_isDarkMode ? RGB(142, 148, 158) : RGB(155, 155, 160))
                 : (g_isDarkMode ? RGB(102, 108, 118) : RGB(180, 180, 180)));
  HBRUSH hThumbBrush = CreateSolidBrush(thumbColor);
  FillRect(hdc, &rcThumb, hThumbBrush);
  DeleteObject(hThumbBrush);
}

void CSShow(CustomScrollbar *sb) {
  if (!sb || !sb->hwndOwner)
    return;
  sb->visible = true;
  if (!sb->dragging && sb->hideTimerId) {
    KillTimer(sb->hwndOwner, sb->hideTimerId);
    SetTimer(sb->hwndOwner, sb->hideTimerId, sb->hideDelayMs, NULL);
  }
}

void CSHide(CustomScrollbar *sb) {
  if (!sb)
    return;
  sb->visible = false;
  if (sb->hwndOwner && sb->hideTimerId)
    KillTimer(sb->hwndOwner, sb->hideTimerId);
}

void CSRefresh(CustomScrollbar *sb, CSGetTotalHeightFn getTotal,
               CSGetVisibleHeightFn getVisible, CSGetScrollTopFn getScroll) {
  (void)getScroll; // 当前实现不使用，但保留接口对称
  if (!sb || !sb->hwndOwner)
    return;
  // 内容变化时显示滚动条
  if (CSNeedsShow(sb, getTotal, getVisible)) {
    CSShow(sb);
  } else {
    sb->visible = false;
  }
  RECT rcReserved;
  if (CSGetReservedRect(sb, &rcReserved))
    InvalidateRect(sb->hwndOwner, &rcReserved, FALSE);
}

bool CSOnMouseMove(CustomScrollbar *sb, int x, int y,
                   CSGetTotalHeightFn getTotal,
                   CSGetVisibleHeightFn getVisible,
                   CSGetScrollTopFn getScroll,
                   CSSetScrollTopFn setScroll) {
  if (!sb)
    return false;
  RECT rcTrack;
  if (!CSGetTrackRect(sb, &rcTrack))
    return false;
  // 拖拽中：更新滚动位置
  if (sb->dragging) {
    sb->hovered = true;
    RECT rcThumb;
    if (!CSGetThumbRect(sb, &rcThumb, getTotal, getVisible, getScroll))
      return true;
    int trackHeight = rcTrack.bottom - rcTrack.top;
    int drawableTrackHeight =
        std::max(0, trackHeight - (int)ScaleForDpi(4, CSGetDpi(sb)));
    int total = getTotal(sb->hwndTarget);
    int visible = getVisible(sb->hwndTarget);
    if (total <= visible || drawableTrackHeight <= 0)
      return true;
    int thumbHeight = rcThumb.bottom - rcThumb.top;
    int travel = drawableTrackHeight - thumbHeight;
    if (travel <= 0)
      return true;
    int maxScroll = total - visible;
    int deltaY = y - sb->dragStartY;
    int deltaScroll = (deltaY * maxScroll + travel / 2) / travel;
    int newScroll = sb->dragStartScrollTop + deltaScroll;
    if (newScroll < 0)
      newScroll = 0;
    if (newScroll > maxScroll)
      newScroll = maxScroll;
    setScroll(sb->hwndTarget, newScroll);
    RECT rcReserved;
    if (CSGetReservedRect(sb, &rcReserved))
      InvalidateRect(sb->hwndOwner, &rcReserved, FALSE);
    return true;
  }
  // 非拖拽：检测悬停
  POINT ptTest = {x, y};
  bool isOver = PtInRect(&rcTrack, ptTest) != 0;
  if (isOver != sb->hovered) {
    sb->hovered = isOver;
    if (isOver) {
      CSShow(sb);
    } else if (!sb->dragging && sb->hideTimerId) {
      SetTimer(sb->hwndOwner, sb->hideTimerId, sb->hideDelayMs, NULL);
    }
    RECT rcReserved;
    if (CSGetReservedRect(sb, &rcReserved))
      InvalidateRect(sb->hwndOwner, &rcReserved, FALSE);
  }
  return isOver;
}

bool CSOnLButtonDown(CustomScrollbar *sb, int x, int y,
                     CSGetTotalHeightFn getTotal,
                     CSGetVisibleHeightFn getVisible,
                     CSGetScrollTopFn getScroll,
                     CSSetScrollTopFn setScroll) {
  if (!sb)
    return false;
  RECT rcTrack;
  if (!CSGetTrackRect(sb, &rcTrack))
    return false;
  POINT ptHit = {x, y};
  if (!PtInRect(&rcTrack, ptHit))
    return false;
  RECT rcThumb;
  if (CSGetThumbRect(sb, &rcThumb, getTotal, getVisible, getScroll) &&
      PtInRect(&rcThumb, ptHit)) {
    // 点中滑块：进入拖拽
    sb->dragging = true;
    sb->hovered = true;
    sb->dragStartY = y;
    sb->dragStartScrollTop = getScroll(sb->hwndTarget);
    SetCapture(sb->hwndOwner);
    CSShow(sb);
    RECT rcReserved;
    if (CSGetReservedRect(sb, &rcReserved))
      InvalidateRect(sb->hwndOwner, &rcReserved, FALSE);
    return true;
  }
  // 点击轨道空白处：翻页
  int total = getTotal(sb->hwndTarget);
  int visible = getVisible(sb->hwndTarget);
  if (total <= visible)
    return true;
  int scrollTop = getScroll(sb->hwndTarget);
  int maxScroll = total - visible;
  if (y < rcThumb.top) {
    // 向上翻页
    scrollTop -= visible;
    if (scrollTop < 0)
      scrollTop = 0;
  } else {
    // 向下翻页
    scrollTop += visible;
    if (scrollTop > maxScroll)
      scrollTop = maxScroll;
  }
  setScroll(sb->hwndTarget, scrollTop);
  RECT rcReserved;
  if (CSGetReservedRect(sb, &rcReserved))
    InvalidateRect(sb->hwndOwner, &rcReserved, FALSE);
  return true;
}

bool CSOnLButtonUp(CustomScrollbar *sb) {
  if (!sb)
    return false;
  if (sb->dragging) {
    sb->dragging = false;
    ReleaseCapture();
    if (sb->hwndOwner && sb->hideTimerId)
      SetTimer(sb->hwndOwner, sb->hideTimerId, sb->hideDelayMs, NULL);
    RECT rcReserved;
    if (CSGetReservedRect(sb, &rcReserved))
      InvalidateRect(sb->hwndOwner, &rcReserved, FALSE);
    return true;
  }
  return false;
}

void CSOnMouseLeave(CustomScrollbar *sb) {
  if (!sb)
    return;
  sb->hovered = false;
  if (!sb->dragging && sb->hwndOwner && sb->hideTimerId)
    SetTimer(sb->hwndOwner, sb->hideTimerId, sb->hideDelayMs, NULL);
  RECT rcReserved;
  if (CSGetReservedRect(sb, &rcReserved))
    InvalidateRect(sb->hwndOwner, &rcReserved, FALSE);
}

void CSOnTimer(CustomScrollbar *sb) {
  if (!sb)
    return;
  // 隐藏定时器触发：滑块淡出
  if (!sb->dragging && !sb->hovered) {
    sb->visible = false;
    RECT rcReserved;
    if (CSGetReservedRect(sb, &rcReserved))
      InvalidateRect(sb->hwndOwner, &rcReserved, FALSE);
  }
  if (sb->hwndOwner && sb->hideTimerId)
    KillTimer(sb->hwndOwner, sb->hideTimerId);
}

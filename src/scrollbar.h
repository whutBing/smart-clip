#pragma once
#include <windows.h>

// 滚动条状态变量
extern bool g_scrollbarVisible;
extern bool g_isScrollbarHovered;
extern bool g_isScrollbarDragging;
extern int g_scrollbarDragStartY;
extern int g_scrollbarDragStartOffset;
extern RECT g_lastThumbRect;
extern bool g_lastThumbValid;
extern bool g_lastThumbVisible;
extern bool g_lastThumbHovered;
extern bool g_lastThumbDragging;

// 滚动条几何计算
bool GetCustomScrollbarTrackRect(HWND hwnd, RECT *rcTrack);
bool GetCustomScrollbarReservedRect(HWND hwnd, RECT *rcReserved);
bool GetCustomScrollbarThumbRect(HWND hwnd, RECT *rcThumb);

// 滚动条状态管理
void StartScrollbarHideTimer(HWND hwnd);
void InvalidateCustomScrollbarArea(HWND hwnd, BOOL erase = FALSE);
void UpdateScrollbarCacheSnapshot(const RECT *thumbRect, bool hasThumb);
void RefreshScrollbarIfChanged(HWND hwnd);

// 滚动条绘制
void PaintCustomScrollbarOverlay(HWND hwnd, HDC hdc,
                                 const RECT *rcPaint = NULL);

// 滚动条显示控制
void ShowCustomScrollbar(HWND hwnd, bool showQuickPasteHint = true);

// 滚动条交互处理
void HandleScrollbarDrag(HWND hwnd, int mouseY);
void HandleScrollbarClick(HWND hwnd, int mouseY);

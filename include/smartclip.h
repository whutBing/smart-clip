#pragma once
#include <windows.h>
#include <vector>
#include <string>

// 定时器ID
#define ID_SCROLLBAR_HIDE_TIMER 1001

// 全局变量声明
extern HWND g_hwndListBox;
extern int g_listBoxTopIndex;
extern std::vector<int> g_displayIndexMap;
extern bool g_isCustomScrollbarEnabled;
extern int g_customScrollbarHideDelayMs;
extern bool g_isDarkMode;

// 列表框几何计算函数
int GetItemDisplayHeight(int displayIndex);
int CalculateVisibleItemCount(int startIndex);
int CalculateNextPageIndex(int currentTopIndex);
int CalculatePrevPageIndex(int currentTopIndex);
int GetListBoxVisibleHeight(HWND hwnd);
int GetTotalListContentHeight();
int GetContentOffsetForTopIndex(int topIndex);
int GetMaxListScrollOffset(HWND hwnd);
int GetTopIndexForContentOffset(int contentOffset);
int GetListBoxMaxTopIndex();
bool NeedsCustomScrollbar();
int GetCustomScrollbarTrackWidth();
int GetCustomScrollbarReservedWidth();

// 快捷键相关
void ResetShortcutAssignment();

// 列表框操作
void ApplyListBoxTopIndex(HWND hwnd, int newTop);
bool IsSelectableDisplayIndex(int index);
int FindSelectableDisplayIndex(int startIndex, int step);
int GetFirstSelectableDisplayIndex();
int GetLastSelectableDisplayIndex();
void EnsureListSelectionVisible(int index);
bool SelectListDisplayIndex(int index);
bool MoveListSelection(int delta);
bool JumpListSelectionToBoundary(bool toBottom);
bool JumpListSelectionToPageBoundary(bool toBottom);

// 拖放相关
void SwapHistoryItems(int srcDisplayIndex, int dstDisplayIndex);
void RefreshListBox();

// 颜色相关
COLORREF GetWhiteColor();
COLORREF GetAccentStrongColor();

// 文本选中复制：列表刷新后清除选中状态（显示索引失效）
void ClearTextSelectionAfterRefresh();

#include "listbox_handler.h"
#include "smartclip.h"
#include "history.h"
#include <algorithm>

// 交换两个显示项对应的历史记录顺序（拖放排序用）
void SwapHistoryItems(int srcDisplayIndex, int dstDisplayIndex) {
    if (srcDisplayIndex < 0 || srcDisplayIndex >= (int)g_displayIndexMap.size() ||
        dstDisplayIndex < 0 || dstDisplayIndex >= (int)g_displayIndexMap.size())
        return;

    int srcHistoryIndex = g_displayIndexMap[srcDisplayIndex];
    int dstHistoryIndex = g_displayIndexMap[dstDisplayIndex];

    if (srcHistoryIndex >= 0 && srcHistoryIndex < (int)g_history.size() &&
        dstHistoryIndex >= 0 && dstHistoryIndex < (int)g_history.size()) {
        std::swap(g_history[srcHistoryIndex], g_history[dstHistoryIndex]);
    }
}

// 刷新列表框显示（委托给 history.cpp 中的 UpdateListBox）
void RefreshListBox() {
    UpdateListBox();
}

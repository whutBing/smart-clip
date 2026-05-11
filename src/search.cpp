#include "search.h"
#include "history.h"

// 全局变量定义
HWND g_hwndSearchBox;
HWND g_hwndSearchButton;
HWND g_hwndTabControl;

void PerformSearch(HWND /*hwnd*/) {
    int textLength = GetWindowTextLengthW(g_hwndSearchBox) + 1;
    std::vector<wchar_t> buffer(textLength);
    GetWindowTextW(g_hwndSearchBox, &buffer[0], textLength);

    g_searchKeyword = buffer.data();
    UpdateListBox();
}

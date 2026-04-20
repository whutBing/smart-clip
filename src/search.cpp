#include "search.h"
#include "history.h"

// 全局变量定义
HWND g_hwndSearchBox;
HWND g_hwndSearchButton;
HWND g_hwndTabControl;

// 处理搜索操作
void PerformSearch(HWND /*hwnd*/) {
    // 获取搜索框内容
    int textLength = GetWindowTextLengthW(g_hwndSearchBox) + 1;
    std::vector<wchar_t> buffer(textLength);
    GetWindowTextW(g_hwndSearchBox, &buffer[0], textLength);
    
    // 更新搜索关键词并刷新列表（使用std::wstring构造函数复制内容）
    g_searchKeyword = buffer.data();
    UpdateListBox();
}

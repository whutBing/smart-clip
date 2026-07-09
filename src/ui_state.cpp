#include "ui_state.h"

// 全局 UI 状态实例
UIState g_uiState;

// 构造函数
UIState::UIState() {
    Initialize();
}

// 初始化
void UIState::Initialize() {
    hwndMain = NULL;
    hwndListBox = NULL;
    hwndSearchBox = NULL;
    hwndStatusBar = NULL;
    hwndSettingsDialog = NULL;
    
    isMinimized = false;
    isMaximized = false;
    isVisible = false;
    
    isDarkMode = false;
    currentTheme = 0;
    
    searchText = L"";
    isSearching = false;
    
    selectedIndex = -1;
    hoverIndex = -1;
    
    isShortcutMode = false;
    shortcutStartIndex = 0;
    shortcutEndIndex = 0;
    
    currentPage = 0;
    itemsPerPage = 20;
    totalItems = 0;
}

// 重置状态
void UIState::Reset() {
    Initialize();
}

// 更新 UI 状态
void UpdateUIState() {
    // 根据当前应用状态更新 UI
    if (g_uiState.hwndMain) {
        g_uiState.isVisible = IsWindowVisible(g_uiState.hwndMain);
        
        WINDOWPLACEMENT wp;
        wp.length = sizeof(WINDOWPLACEMENT);
        if (GetWindowPlacement(g_uiState.hwndMain, &wp)) {
            g_uiState.isMinimized = (wp.showCmd == SW_SHOWMINIMIZED);
            g_uiState.isMaximized = (wp.showCmd == SW_SHOWMAXIMIZED);
        }
    }
}

// 刷新 UI
void RefreshUI() {
    if (g_uiState.hwndMain) {
        InvalidateRect(g_uiState.hwndMain, NULL, TRUE);
        UpdateWindow(g_uiState.hwndMain);
    }
}

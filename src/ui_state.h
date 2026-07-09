#pragma once
#include <windows.h>
#include <string>
#include <vector>

// UI 状态管理
struct UIState {
    // 窗口句柄
    HWND hwndMain;
    HWND hwndListBox;
    HWND hwndSearchBox;
    HWND hwndStatusBar;
    HWND hwndSettingsDialog;
    
    // 窗口状态
    bool isMinimized;
    bool isMaximized;
    bool isVisible;
    
    // 主题状态
    bool isDarkMode;
    int currentTheme;
    
    // 搜索状态
    std::wstring searchText;
    bool isSearching;
    
    // 选择状态
    int selectedIndex;
    int hoverIndex;
    
    // 快捷键状态
    bool isShortcutMode;
    int shortcutStartIndex;
    int shortcutEndIndex;
    
    // 分页状态
    int currentPage;
    int itemsPerPage;
    int totalItems;
    
    // 构造函数
    UIState();
    
    // 初始化
    void Initialize();
    
    // 重置状态
    void Reset();
};

// 全局 UI 状态实例
extern UIState g_uiState;

// UI 状态辅助函数
void UpdateUIState();
void RefreshUI();

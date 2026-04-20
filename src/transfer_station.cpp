#include "transfer_station.h"
#include "card_renderer.h"
#include "tray.h"
#include "settings.h"
#include <windows.h>
#include <vector>

// 中转站全局变量定义
HWND g_hwndFlagpole = NULL;
bool g_isCollapseAfterPaste = true;
HWND g_transferStationPreviousWindow = NULL;
bool g_isTransferStationPasting = false;  // 标记中转站正在粘贴

// 外部全局变量声明
extern bool g_isRestoringClipboard;
extern bool g_isNotificationEnabled;

// 计算卡片位置
POINT CalculateCardPosition(int index) {
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);

    const int START_X = screenWidth - CARD_WIDTH - 20;
    const int START_Y = 20;

    int col = index % 3;
    int row = index / 3;

    POINT pos;
    pos.x = START_X - col * (CARD_WIDTH + CARD_MARGIN);
    pos.y = START_Y + row * (CARD_HEIGHT + CARD_MARGIN);

    return pos;
}

void ExecuteQuickPaste(int historyIndex) {
    if (historyIndex < 0 || historyIndex >= (int)g_history.size()) {
        return;
    }

    const ClipboardItem& item = g_history[historyIndex];

    // 只有在没有有效的前台窗口时才需要隐藏窗口来获取
    bool needGetForeground = (g_transferStationPreviousWindow == NULL ||
                              !IsWindow(g_transferStationPreviousWindow));

    if (needGetForeground) {
        // 临时隐藏所有中转站窗口以获取真正的前台窗口
        for (auto& tsItem : g_transferStation) {
            if (IsWindow(tsItem.hwndCard)) {
                ShowWindow(tsItem.hwndCard, SW_HIDE);
            }
        }
        if (g_hwndFlagpole && IsWindow(g_hwndFlagpole)) {
            ShowWindow(g_hwndFlagpole, SW_HIDE);
        }

        Sleep(100);
        g_transferStationPreviousWindow = GetForegroundWindow();

        // 如果是用完收起模式，恢复窗口显示
        if (g_isCollapseAfterPaste) {
            for (auto& tsItem : g_transferStation) {
                if (IsWindow(tsItem.hwndCard)) {
                    ShowWindow(tsItem.hwndCard, SW_SHOWNOACTIVATE);
                }
            }
            if (g_hwndFlagpole && IsWindow(g_hwndFlagpole)) {
                ShowWindow(g_hwndFlagpole, SW_SHOWNOACTIVATE);
            }
        }
    }

    // 设置标志，防止剪贴板监控触发主窗口更新
    g_isTransferStationPasting = true;

    // 设置剪贴板内容
    if (OpenClipboard(NULL)) {
        EmptyClipboard();

        if (item.type == TYPE_TEXT || item.type == TYPE_FILE) {
            // 去除末尾的空白字符（包括换行符）
            std::wstring content = item.content;
            while (!content.empty() && (content.back() == L'\n' || content.back() == L'\r' || content.back() == L' ' || content.back() == L'\t')) {
                content.pop_back();
            }

            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE,
                (content.length() + 1) * sizeof(wchar_t));
            if (hGlobal != NULL) {
                wchar_t* pData = (wchar_t*)GlobalLock(hGlobal);
                if (pData != NULL) {
                    wcscpy_s(pData, content.length() + 1, content.c_str());
                    GlobalUnlock(hGlobal);
                    SetClipboardData(CF_UNICODETEXT, hGlobal);
                }
            }
        } else if (item.type == TYPE_IMAGE) {
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = item.imageWidth;
            bmi.bmiHeader.biHeight = -item.imageHeight;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 24;
            bmi.bmiHeader.biCompression = BI_RGB;

            DWORD imageSize = item.imageData.size();
            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE,
                sizeof(BITMAPINFOHEADER) + imageSize);
            if (hGlobal != NULL) {
                BYTE* pData = (BYTE*)GlobalLock(hGlobal);
                if (pData != NULL) {
                    memcpy(pData, &bmi.bmiHeader, sizeof(BITMAPINFOHEADER));
                    memcpy(pData + sizeof(BITMAPINFOHEADER),
                           &item.imageData[0], imageSize);
                    GlobalUnlock(hGlobal);
                    SetClipboardData(CF_DIB, hGlobal);
                }
            }
        }

        g_isRestoringClipboard = true;
        CloseClipboard();
    }

    Sleep(50);

    // 切换到之前保存的前台窗口并执行粘贴
    if (g_transferStationPreviousWindow != NULL && IsWindow(g_transferStationPreviousWindow)) {
        SetForegroundWindow(g_transferStationPreviousWindow);
        Sleep(100);
    }

    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event('V', 0, 0, 0);
    keybd_event('V', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);

    Sleep(100);  // 等待粘贴完成

    // 恢复便利贴显示（仅在需要获取前台窗口且非用完收起模式下）
    if (needGetForeground && !g_isCollapseAfterPaste) {
        for (auto& tsItem : g_transferStation) {
            if (IsWindow(tsItem.hwndCard)) {
                ShowWindow(tsItem.hwndCard, SW_SHOWNOACTIVATE);
            }
        }
        if (g_hwndFlagpole && IsWindow(g_hwndFlagpole)) {
            ShowWindow(g_hwndFlagpole, SW_SHOWNOACTIVATE);
        }
    }

    SetTimer(g_hwndMain, 1, 100, NULL);

    if (g_isNotificationEnabled) {
        ShowTrayBalloon(g_hwndMain, L"中转站", L"已粘贴");
    }

    // 清除标志
    g_isTransferStationPasting = false;
}

// 从中转站移除
void RemoveFromTransferStation(int historyIndex) {
    for (auto it = g_transferStation.begin(); it != g_transferStation.end(); ++it) {
        if (it->historyIndex == historyIndex) {
            if (historyIndex >= 0 && historyIndex < (int)g_history.size()) {
                g_history[historyIndex].isInTransferStation = false;
            }

            // 销毁卡片窗口
            if (it->hwndCard && IsWindow(it->hwndCard)) {
                DestroyWindow(it->hwndCard);
            }

            g_transferStation.erase(it);
            SaveHistory();

            if (g_isNotificationEnabled) {
                ShowTrayBalloon(g_hwndMain, L"提示", L"已从中转站移除");
            }

            // 重新布局并重建旗杆
            if (g_transferStation.empty()) {
                // 如果没有项目了，隐藏旗杆
                if (g_hwndFlagpole && IsWindow(g_hwndFlagpole)) {
                    DestroyWindow(g_hwndFlagpole);
                    g_hwndFlagpole = NULL;
                }
                g_isTransferStationVisible = false;
            } else if (g_hwndFlagpole && IsWindow(g_hwndFlagpole)) {
                // 如果旗杆存在，直接调整旗杆大小
                RECT workArea;
                SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
                int screenHeight = workArea.bottom;
                int totalItems = g_transferStation.size();
                int flagpoleHeight = totalItems * (CARD_HEIGHT + CARD_MARGIN) + 20;
                int flagpoleStartY = screenHeight - 20 - (flagpoleHeight - 20);

                SetWindowPos(g_hwndFlagpole, HWND_TOPMOST,
                    FLAGPOLE_X, flagpoleStartY - 10,
                    FLAGPOLE_WIDTH, flagpoleHeight,
                    SWP_SHOWWINDOW | SWP_FRAMECHANGED);
                InvalidateRect(g_hwndFlagpole, NULL, TRUE);

                // 重新计算所有卡片位置
                for (size_t i = 0; i < g_transferStation.size(); i++) {
                    int yPos = screenHeight - 20 - (i + 1) * (CARD_HEIGHT + CARD_MARGIN);
                    if (g_transferStation[i].hwndCard && IsWindow(g_transferStation[i].hwndCard)) {
                        SetWindowPos(g_transferStation[i].hwndCard, NULL,
                            20, yPos, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
                        g_transferStation[i].position = {20, yPos};
                    }
                }
            }

            break;
        }
    }
}

// 刷新中转站卡片
void RefreshTransferStationCards() {
    // 销毁所有现有卡片窗口
    for (auto& item : g_transferStation) {
        if (IsWindow(item.hwndCard)) {
            DestroyWindow(item.hwndCard);
        }
    }

    // 重新计算所有卡片位置（从下往上排列，考虑收起状态）
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    int screenHeight = workArea.bottom;

    // 计算总高度（所有卡片都占用相同高度，保持间距一致）
    int totalHeight = g_transferStation.size() * (CARD_HEIGHT + CARD_MARGIN);

    // 从上往下创建卡片（后加入的在下面）
    int currentY = screenHeight - 20 - totalHeight;
    for (int i = 0; i < (int)g_transferStation.size(); i++) {
        int historyIndex = g_transferStation[i].historyIndex;
        if (historyIndex < 0 || historyIndex >= (int)g_history.size()) {
            continue;
        }

        // 确定卡片大小（收起的卡片是正方形）
        int cardWidth = g_transferStation[i].isCollapsed ? CARD_HEIGHT : CARD_WIDTH;
        int cardHeight = CARD_HEIGHT;

        // 创建卡片窗口
        HWND hwndCard = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
            L"TransferStationCard",
            L"",
            WS_POPUP | WS_VISIBLE,
            20, currentY, cardWidth, cardHeight,
            g_hwndTransferStationContainer, NULL,
            GetModuleHandle(NULL), NULL
        );

        g_transferStation[i].hwndCard = hwndCard;
        g_transferStation[i].position = {20, currentY};
        g_transferStation[i].size.cx = cardWidth;
        g_transferStation[i].size.cy = cardHeight;

        // 更新窗口用户数据为 historyIndex（而非数组索引）
        SetWindowLongPtr(hwndCard, GWLP_USERDATA, (LONG_PTR)g_transferStation[i].historyIndex);

        // 如果该卡片已被隐藏，隐藏窗口
        if (g_transferStation[i].isHidden) {
            ShowWindow(hwndCard, SW_HIDE);
        }

        // 更新Y位置（所有卡片都占用相同高度，保持间距一致）
        currentY += CARD_HEIGHT + CARD_MARGIN;
    }

    // 重新创建旗杆
    if (g_hwndFlagpole && IsWindow(g_hwndFlagpole)) {
        DestroyWindow(g_hwndFlagpole);
        g_hwndFlagpole = NULL;
    }

    if (!g_transferStation.empty()) {
        // 计算旗杆高度（始终使用正常状态的高度，不随卡片收起而变化）
        int flagpoleHeight = g_transferStation.size() * (CARD_HEIGHT + CARD_MARGIN) + 20;

        int flagpoleStartY = screenHeight - 20 - (flagpoleHeight - 20);

        g_hwndFlagpole = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
            L"Flagpole",
            L"",
            WS_POPUP | WS_VISIBLE,
            FLAGPOLE_X, flagpoleStartY - 10,
            FLAGPOLE_WIDTH, flagpoleHeight,
            NULL, NULL,
            GetModuleHandleW(NULL), NULL
        );

        if (g_hwndFlagpole) {
            SetLayeredWindowAttributes(g_hwndFlagpole, RGB(255, 255, 255), 0, LWA_COLORKEY);
        }
    }
}

// 添加到中转站
void AddToTransferStation(int historyIndex) {
    if (historyIndex < 0 || historyIndex >= (int)g_history.size()) {
        return;
    }

    for (const auto& item : g_transferStation) {
        if (item.historyIndex == historyIndex) {
            if (g_isNotificationEnabled) {
                ShowTrayBalloon(g_hwndMain, L"提示", L"该项已在中转站中");
            }
            return;
        }
    }

    g_history[historyIndex].isInTransferStation = true;

    // 计算纸条位置（从下往上排列，最早加入的在最下面）
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    int screenHeight = workArea.bottom;

    // 计算已有卡片占用的总高度（所有卡片都占用相同高度，保持间距一致）
    int totalHeight = g_transferStation.size() * (CARD_HEIGHT + CARD_MARGIN);
    // 新卡片也占用空间
    totalHeight += CARD_HEIGHT + CARD_MARGIN;

    int yPos = screenHeight - 20 - totalHeight;

    TransferStationItem tsItem;
    tsItem.historyIndex = historyIndex;
    tsItem.position = {20, yPos};
    tsItem.size = {CARD_WIDTH, CARD_HEIGHT};
    tsItem.isHidden = false;  // 初始状态为未隐藏
    tsItem.isCollapsed = false;  // 初始状态为未收起

    // 先添加到列表
    g_transferStation.push_back(tsItem);

    // 创建独立的纸条窗口
    HWND hwndCard = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        L"TransferStationCard",
        L"",
        WS_POPUP | WS_VISIBLE,
        20, yPos,
        CARD_WIDTH, CARD_HEIGHT,
        NULL,
        NULL,
        GetModuleHandleW(NULL),
        NULL
    );

    if (hwndCard) {
        // 设置窗口透明度（黑色透明）
        SetLayeredWindowAttributes(hwndCard, RGB(0, 0, 0), 0, LWA_COLORKEY);

        // 保存窗口句柄
        g_transferStation.back().hwndCard = hwndCard;

        // 设置窗口用户数据为 historyIndex（而非数组索引，避免删除后索引错乱）
        SetWindowLongPtr(hwndCard, GWLP_USERDATA, (LONG_PTR)g_transferStation.back().historyIndex);

        // 如果中转站是隐藏状态，隐藏新创建的窗口
        if (!g_isTransferStationVisible) {
            ShowWindow(hwndCard, SW_HIDE);
        }
    }

    SaveHistory();

    if (g_isNotificationEnabled) {
        ShowTrayBalloon(g_hwndMain, L"提示", L"已加入中转站");
    }

    // 如果中转站是可见的，重新创建旗杆以更新高度
    if (g_isTransferStationVisible) {
        // 销毁旧的旗杆窗口
        if (g_hwndFlagpole && IsWindow(g_hwndFlagpole)) {
            DestroyWindow(g_hwndFlagpole);
            g_hwndFlagpole = NULL;
        }

        // 重新创建旗杆窗口
        RECT workArea;
        SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
        int screenHeight = workArea.bottom;

        // 计算旗杆高度（始终使用正常状态的高度，不随卡片收起而变化）
        int flagpoleHeight = g_transferStation.size() * (CARD_HEIGHT + CARD_MARGIN) + 20;

        // 计算旗杆的起始Y位置
        int flagpoleStartY = screenHeight - 20 - (flagpoleHeight - 20);

        g_hwndFlagpole = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
            L"Flagpole",
            L"",
            WS_POPUP | WS_VISIBLE,
            FLAGPOLE_X, flagpoleStartY - 10,
            FLAGPOLE_WIDTH, flagpoleHeight,
            NULL, NULL,
            GetModuleHandleW(NULL), NULL
        );

        if (g_hwndFlagpole) {
            SetLayeredWindowAttributes(g_hwndFlagpole, RGB(255, 255, 255), 0, LWA_COLORKEY);
        }
    }

    // 重新排列所有卡片，确保顺序正确（最新的在下面）
    // 无论中转站是否可见都要调用，确保顺序始终正确
    RefreshTransferStationCards();
}

// 显示/隐藏中转站
void ShowTransferStation() {
    if (g_isTransferStationVisible) {
        // 隐藏所有纸条
        for (auto& item : g_transferStation) {
            if (IsWindow(item.hwndCard)) {
                ShowWindow(item.hwndCard, SW_HIDE);
            }
        }

        // 销毁旗杆窗口
        if (g_hwndFlagpole && IsWindow(g_hwndFlagpole)) {
            DestroyWindow(g_hwndFlagpole);
            g_hwndFlagpole = NULL;
        }

        g_isTransferStationVisible = false;
    } else {
        // 保存当前前台窗口，用于后续粘贴操作
        g_transferStationPreviousWindow = GetForegroundWindow();

        // 显示所有纸条
        for (auto& item : g_transferStation) {
            if (IsWindow(item.hwndCard)) {
                ShowWindow(item.hwndCard, SW_SHOW);
            }
        }

        // 创建旗杆窗口
        if (!g_transferStation.empty()) {
            RECT workArea;
            SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
            int screenHeight = workArea.bottom;
            int totalItems = g_transferStation.size();

            // 计算旗杆的起始Y位置（最下面的便签位置）
            int flagpoleStartY = screenHeight - 20 - totalItems * (CARD_HEIGHT + CARD_MARGIN);
            // 旗杆高度覆盖所有便签
            int flagpoleHeight = totalItems * (CARD_HEIGHT + CARD_MARGIN) + 20;

            g_hwndFlagpole = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
                L"Flagpole",
                L"",
                WS_POPUP | WS_VISIBLE,
                FLAGPOLE_X, flagpoleStartY - 10,
                FLAGPOLE_WIDTH, flagpoleHeight,
                NULL, NULL,
                GetModuleHandleW(NULL), NULL
            );

            if (g_hwndFlagpole) {
                SetLayeredWindowAttributes(g_hwndFlagpole, RGB(255, 255, 255), 0, LWA_COLORKEY);
            }
        }

        g_isTransferStationVisible = true;
    }
}

LRESULT CALLBACK FlagpoleProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rect;
            GetClientRect(hwnd, &rect);

            // 使用GDI+绘制
            Graphics graphics(hdc);
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);

            int centerX = rect.right / 2;
            int poleWidth = FLAGPOLE_WIDTH;
            int poleLeft = centerX - poleWidth / 2;
            int poleRight = centerX + poleWidth / 2;

            // 绘制木棍材质的旗杆
            // 使用棕色渐变模拟圆柱体立体感
            LinearGradientBrush poleBrush(
                PointF((REAL)poleLeft, 0),
                PointF((REAL)poleRight, 0),
                Color(255, 101, 67, 33),   // 深棕色（左侧阴影）
                Color(255, 160, 110, 60)   // 浅棕色（右侧高光）
            );
            graphics.FillRectangle(&poleBrush, poleLeft, 0, poleWidth, rect.bottom);

            // 添加木纹效果（横向深色线条）
            Pen woodGrainPen(Color(255, 80, 50, 25), 1.0f);
            int grainSpacing = 15;  // 木纹间距
            for (int y = grainSpacing; y < rect.bottom; y += grainSpacing) {
                graphics.DrawLine(&woodGrainPen, poleLeft, y, poleRight, y);
            }

            // 绘制收起卡片对应的红色部分（旗帜卷在旗杆上）
            RECT flagpoleRect;
            GetWindowRect(hwnd, &flagpoleRect);

            for (const auto& item : g_transferStation) {
                if (item.hwndCard && IsWindow(item.hwndCard)) {
                    bool isCollapsed = item.isCollapsed;
                    bool isCollapsing = (bool)GetPropW(item.hwndCard, L"IsCollapsing");

                    // 如果卡片已收起或正在收起，绘制红色部分
                    if (isCollapsed || isCollapsing) {
                        // 获取卡片窗口位置
                        RECT cardRect;
                        GetWindowRect(item.hwndCard, &cardRect);

                        // 计算卡片相对于旗杆的Y位置
                        int relativeY = cardRect.top - flagpoleRect.top;
                        int cardHeight = cardRect.bottom - cardRect.top;

                        // 计算红色部分的宽度
                        int redWidth = poleWidth;  // 默认全宽（完全收起状态）

                        if (isCollapsing && !isCollapsed) {
                            // 正在收缩但还未完全收起：根据动画进度计算宽度
                            int animFrame = (int)(LONG_PTR)GetPropW(item.hwndCard, L"AnimFrame");
                            float progress = animFrame / 40.0f;  // 40帧
                            if (progress > 1.0f) progress = 1.0f;

                            // 应用缓动效果（与卡片动画一致）
                            progress = progress * progress;

                            // 红色部分宽度从0增加到20像素
                            redWidth = (int)(progress * 20.0f);
                            if (redWidth < 1) redWidth = 1;  // 至少1像素可见
                        }

                        // 在旗杆上绘制红色矩形（表示旗帜卷在旗杆上）
                        LinearGradientBrush redBrush(
                            PointF((REAL)poleLeft, (REAL)relativeY),
                            PointF((REAL)poleRight, (REAL)relativeY),
                            Color(255, 220, 50, 50),   // 深红色（左侧）
                            Color(255, 180, 30, 30)    // 更深红色（右侧）
                        );
                        graphics.FillRectangle(&redBrush, poleLeft, relativeY, redWidth, cardHeight);
                    }
                }
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
}

// 中转站卡片窗口过程
LRESULT CALLBACK TransferStationCardProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // 获取存储的 historyIndex（而非数组索引，避免删除后索引错乱）
    int historyIndex = (int)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    TransferStationItem* pItem = nullptr;
    for (auto& item : g_transferStation) {
        if (item.historyIndex == historyIndex) {
            pItem = &item;
            break;
        }
    }

    switch (message) {
        case WM_MOUSEACTIVATE: {
            // 在窗口被鼠标激活之前，保存当前的前台窗口
            HWND hwndForeground = GetForegroundWindow();
            // 确保不是中转站窗口本身
            bool isTransferStationWindow = (hwndForeground == g_hwndFlagpole);
            if (!isTransferStationWindow) {
                for (const auto& tsItem : g_transferStation) {
                    if (tsItem.hwndCard == hwndForeground) {
                        isTransferStationWindow = true;
                        break;
                    }
                }
            }
            if (!isTransferStationWindow && hwndForeground != NULL) {
                g_transferStationPreviousWindow = hwndForeground;
            }
            return MA_ACTIVATE;
        }

        case WM_CREATE: {
            SetWindowLong(hwnd, GWL_EXSTYLE,
                GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
            SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_HOVER | TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 100;
            TrackMouseEvent(&tme);

            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rect;
            GetClientRect(hwnd, &rect);

            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbmMem = CreateCompatibleBitmap(hdc,
                rect.right - rect.left, rect.bottom - rect.top);
            HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

            // 先填充黑色背景，设为透明色键
            HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(hdcMem, &rect, hBrush);
            DeleteObject(hBrush);

            if (pItem && pItem->historyIndex >= 0 &&
                pItem->historyIndex < (int)g_history.size()) {
                // 检查是否悬停（使用窗口属性存储状态）
                bool isHovered = (GetPropW(hwnd, L"IsHovered") != NULL);
                // 检查是否正在卷起或展开
                bool isCollapsing = (GetPropW(hwnd, L"IsCollapsing") != NULL);
                bool isExpanding = (GetPropW(hwnd, L"IsExpanding") != NULL);
                // 获取动画帧数
                int animFrame = (int)(LONG_PTR)GetPropW(hwnd, L"AnimFrame");

                // 如果是展开动画，反向计算animFrame（从40到0）
                if (isExpanding) {
                    animFrame = 40 - animFrame;  // 改为40帧
                    isCollapsing = true;  // 复用isCollapsing参数，但animFrame是反向的
                }

                // 检查是否正在飘动
                bool isWaving = (GetPropW(hwnd, L"IsWaving") != NULL);
                int waveFrame = (int)(LONG_PTR)GetPropW(hwnd, L"WaveFrame");

                // 判断是否是最高位置（通过比较指针地址判断是否是最后一个元素）
                bool isTopmost = (pItem == &g_transferStation.back());
                DrawTransferStationCard(hdcMem, rect,
                    g_history[pItem->historyIndex], isHovered, isCollapsing, animFrame, isTopmost, isExpanding, isWaving, waveFrame);
            }

            BitBlt(hdc, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                   hdcMem, 0, 0, SRCCOPY);

            SelectObject(hdcMem, hbmOld);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!GetPropW(hwnd, L"IsHovered")) {
                SetPropW(hwnd, L"IsHovered", (HANDLE)1);
                InvalidateRect(hwnd, NULL, FALSE);

                TRACKMOUSEEVENT tme = {};
                tme.cbSize = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            }
            // 旗帜不脱离旗杆，不再移动窗口位置
            return 0;
        }

        case WM_MOUSELEAVE: {
            RemovePropW(hwnd, L"IsHovered");
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            POINT pt = {LOWORD(lParam), HIWORD(lParam)};
            RECT rect;
            GetClientRect(hwnd, &rect);

            int closeX = rect.right - 10 - CARD_CLOSE_BUTTON_SIZE - 5;
            int closeY = (rect.bottom - CARD_CLOSE_BUTTON_SIZE) / 2;  // 垂直居中

            // 检查是否点击了按钮区域
            bool clickedButton = (pt.x >= closeX && pt.x <= closeX + CARD_CLOSE_BUTTON_SIZE &&
                                  pt.y >= closeY && pt.y <= closeY + CARD_CLOSE_BUTTON_SIZE);

            if (clickedButton && pItem) {
                // 根据当前状态决定是展开还是关闭
                if (pItem->isCollapsed) {
                    // 收起状态：点击展开按钮，播放展开动画
                    SetPropW(hwnd, L"IsExpanding", (HANDLE)1);
                    SetPropW(hwnd, L"AnimFrame", (HANDLE)0);

                    // 启动展开动画定时器（40帧，12ms间隔，总时间约480ms）
                    SetTimer(hwnd, 3000 + pItem->historyIndex, 12, NULL);  // 动画定时器
                    SetTimer(hwnd, 4000 + pItem->historyIndex, 500, NULL);  // 结束定时器（0.5秒）
                } else {
                    // 正常状态：点击关闭按钮，删除卡片
                    RemoveFromTransferStation(pItem->historyIndex);
                }
                return 0;
            }

            // 旗帜不脱离旗杆，按下时启动飘动动画
            if (pItem && !pItem->isCollapsed) {
                SetPropW(hwnd, L"IsWaving", (HANDLE)1);
                SetPropW(hwnd, L"WaveFrame", (HANDLE)0);
                SetCapture(hwnd);
                // 启动飘动动画定时器（30ms间隔，约33fps）
                SetTimer(hwnd, 5000 + pItem->historyIndex, 30, NULL);
            }

            return 0;
        }

        case WM_LBUTTONUP: {
            // 停止飘动动画
            if (GetPropW(hwnd, L"IsWaving")) {
                RemovePropW(hwnd, L"IsWaving");
                RemovePropW(hwnd, L"WaveFrame");
                ReleaseCapture();
                // 停止飘动动画定时器
                if (pItem) {
                    KillTimer(hwnd, 5000 + pItem->historyIndex);
                }
                // 触发重绘恢复正常状态
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            // 双击执行粘贴（无论是否收起状态都响应）
            if (pItem && pItem->historyIndex >= 0 &&
                pItem->historyIndex < (int)g_history.size()) {

                // 立即执行粘贴（ExecuteQuickPaste 会处理窗口隐藏/恢复）
                ExecuteQuickPaste(pItem->historyIndex);

                // 如果启用了"用完收起"且卡片未收起，启动收起动画
                if (g_isCollapseAfterPaste && !pItem->isCollapsed) {
                    // 设置收起标志
                    SetPropW(hwnd, L"IsCollapsing", (HANDLE)1);
                    SetPropW(hwnd, L"AnimFrame", (HANDLE)0);

                    // 启动动画定时器（40帧，12ms间隔，约83fps）
                    SetTimer(hwnd, 2000 + pItem->historyIndex, 12, NULL);

                    // 启动结束定时器（500ms后完成）
                    SetTimer(hwnd, 1000 + pItem->historyIndex, 500, NULL);
                }
            }
            return 0;
        }

        case WM_TIMER: {
            if (wParam >= 5000) {
                // 飘动动画定时器：更新动画帧并触发重绘
                int waveFrame = (int)(LONG_PTR)GetPropW(hwnd, L"WaveFrame");
                waveFrame++;
                SetPropW(hwnd, L"WaveFrame", (HANDLE)(LONG_PTR)waveFrame);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (wParam >= 4000) {
                // 展开动画结束定时器
                KillTimer(hwnd, wParam);
                KillTimer(hwnd, wParam - 1000);  // 停止展开动画定时器

                // 获取当前窗口位置
                RECT rect;
                GetWindowRect(hwnd, &rect);

                // 恢复窗口大小到完整宽度
                SetWindowPos(hwnd, NULL, rect.left, rect.top,
                            CARD_WIDTH, CARD_HEIGHT,
                            SWP_NOZORDER | SWP_NOACTIVATE);

                RemovePropW(hwnd, L"IsExpanding");
                RemovePropW(hwnd, L"AnimFrame");

                // 清除isCollapsed标志
                for (auto& tsItem : g_transferStation) {
                    if (tsItem.hwndCard == hwnd) {
                        tsItem.isCollapsed = false;
                        break;
                    }
                }

                // 触发重绘
                InvalidateRect(hwnd, NULL, TRUE);

                // 刷新所有卡片位置和旗杆
                RefreshTransferStationCards();
            } else if (wParam >= 3000) {
                // 展开动画定时器：更新动画帧并触发重绘
                int animFrame = (int)(LONG_PTR)GetPropW(hwnd, L"AnimFrame");
                animFrame++;
                SetPropW(hwnd, L"AnimFrame", (HANDLE)(LONG_PTR)animFrame);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (wParam >= 2000) {
                // 收起动画定时器：更新动画帧并触发重绘
                int animFrame = (int)(LONG_PTR)GetPropW(hwnd, L"AnimFrame");
                animFrame++;
                SetPropW(hwnd, L"AnimFrame", (HANDLE)(LONG_PTR)animFrame);
                InvalidateRect(hwnd, NULL, FALSE);  // 触发重绘

                // 触发旗杆重绘，显示动画过程中的红色部分
                if (g_hwndFlagpole && IsWindow(g_hwndFlagpole)) {
                    InvalidateRect(g_hwndFlagpole, NULL, FALSE);
                }
            } else if (wParam >= 1000) {
                // 结束定时器：动画播放完毕，调整窗口到窄条状态
                KillTimer(hwnd, wParam);
                KillTimer(hwnd, wParam + 1000);  // 停止动画定时器

                // 获取当前窗口位置
                RECT rect;
                GetWindowRect(hwnd, &rect);

                // 调整窗口大小到窄条状态（宽度=高度）
                SetWindowPos(hwnd, NULL, rect.left, rect.top,
                            CARD_HEIGHT, CARD_HEIGHT,
                            SWP_NOZORDER | SWP_NOACTIVATE);

                RemovePropW(hwnd, L"IsCollapsing");
                RemovePropW(hwnd, L"AnimFrame");

                // 设置对应项的isCollapsed标志
                for (auto& tsItem : g_transferStation) {
                    if (tsItem.hwndCard == hwnd) {
                        tsItem.isCollapsed = true;
                        break;
                    }
                }

                // 触发重绘以显示向右箭头
                InvalidateRect(hwnd, NULL, TRUE);

                // 刷新所有卡片位置和旗杆
                RefreshTransferStationCards();
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            POINT pt;
            GetCursorPos(&pt);

            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 1, L"粘贴");
            AppendMenuW(hMenu, MF_STRING, 2, L"从中转站移除");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, 3, L"关闭所有");

            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                     pt.x, pt.y, 0, hwnd, NULL);

            if (pItem) {
                if (cmd == 1) {
                    ExecuteQuickPaste(pItem->historyIndex);
                } else if (cmd == 2) {
                    RemoveFromTransferStation(pItem->historyIndex);
                } else if (cmd == 3) {
                    ShowTransferStation();
                }
            }

            DestroyMenu(hMenu);
            return 0;
        }

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
}

// 中转站容器窗口过程
LRESULT CALLBACK TransferStationContainerProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static HWND hwndCheckBox = NULL;
    static std::vector<HWND> cardButtons;

    switch (message) {
        case WM_CREATE: {
            // 创建"用完即毁"勾选框
            hwndCheckBox = CreateWindowExW(0, L"BUTTON", L"用完即毁",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                15, 15, 120, 25,
                hwnd, (HMENU)5001, GetModuleHandle(NULL), NULL);

            // 设置字体
            HFONT hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
            SendMessageW(hwndCheckBox, WM_SETFONT, (WPARAM)hFont, TRUE);

            // 创建卡片按钮
            int yPos = 0;
            for (size_t i = 0; i < g_transferStation.size(); i++) {
                const auto& item = g_transferStation[i];
                if (item.historyIndex >= 0 && item.historyIndex < (int)g_history.size()) {
                    const ClipboardItem& clipItem = g_history[item.historyIndex];
                    std::wstring displayText = clipItem.content;

                    // 限制显示长度
                    if (displayText.length() > 60) {
                        displayText = displayText.substr(0, 60) + L"...";
                    }

                    // 替换换行符为空格
                    for (size_t j = 0; j < displayText.length(); j++) {
                        if (displayText[j] == L'\n' || displayText[j] == L'\r') {
                            displayText[j] = L' ';
                        }
                    }

                    // 创建卡片按钮（自绘）
                    HWND hwndCard = CreateWindowExW(0, L"BUTTON", displayText.c_str(),
                        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                        10, 50 + yPos, 380, 60,
                        hwnd, (HMENU)(6000 + i), GetModuleHandle(NULL), NULL);

                    cardButtons.push_back(hwndCard);
                    yPos += 70;  // 卡片间距
                }
            }

            return 0;
        }

        case WM_COMMAND: {
            WORD wID = LOWORD(wParam);
            WORD wNotifyCode = HIWORD(wParam);

            if (wID >= 6000 && wID < 6000 + (int)g_transferStation.size() && wNotifyCode == BN_CLICKED) {
                // 卡片按钮点击
                int index = wID - 6000;
                if (index >= 0 && index < (int)g_transferStation.size()) {
                    int historyIndex = g_transferStation[index].historyIndex;

                    // 执行粘贴
                    ExecuteQuickPaste(historyIndex);

                    // 检查是否勾选"用完即毁"
                    LRESULT isChecked = SendMessageW(hwndCheckBox, BM_GETCHECK, 0, 0);
                    if (isChecked == BST_CHECKED) {
                        // 从中转站移除
                        RemoveFromTransferStation(historyIndex);
                    }
                }
            }
            break;
        }

        case WM_DRAWITEM: {
            // 自绘卡片按钮
            LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
            if (pDIS->CtlID >= 6000 && pDIS->CtlID < 6000 + (UINT)g_transferStation.size()) {
                HDC hdc = pDIS->hDC;
                RECT rect = pDIS->rcItem;

                // 绘制卡片背景和阴影
                HBRUSH hBrush;
                if (pDIS->itemState & ODS_SELECTED) {
                    hBrush = CreateSolidBrush(RGB(230, 240, 255));
                } else {
                    hBrush = CreateSolidBrush(RGB(255, 255, 255));
                }

                // 绘制阴影
                RECT shadowRect = rect;
                shadowRect.left += 2;
                shadowRect.top += 2;
                HBRUSH hShadowBrush = CreateSolidBrush(RGB(230, 230, 230));
                FillRect(hdc, &shadowRect, hShadowBrush);
                DeleteObject(hShadowBrush);

                // 绘制卡片
                FillRect(hdc, &rect, hBrush);
                DeleteObject(hBrush);

                // 绘制圆角边框
                HPEN hPen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                RoundRect(hdc, rect.left, rect.top, rect.right - 2, rect.bottom - 2, 10, 10);
                SelectObject(hdc, hOldPen);
                DeleteObject(hPen);

                // 绘制文本
                wchar_t text[256];
                GetWindowTextW(pDIS->hwndItem, text, 256);

                SetTextColor(hdc, RGB(50, 50, 50));
                SetBkMode(hdc, TRANSPARENT);

                RECT textRect = rect;
                textRect.left += 15;
                textRect.right -= 15;
                textRect.top += 10;
                textRect.bottom -= 10;

                HFONT hFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

                DrawTextW(hdc, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_WORDBREAK);

                SelectObject(hdc, hOldFont);
                DeleteObject(hFont);

                return TRUE;
            }
            break;
        }

        case WM_CLOSE: {
            ShowWindow(hwnd, SW_HIDE);
            g_isTransferStationVisible = false;
            cardButtons.clear();
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // 绘制渐变背景
            RECT rect;
            GetClientRect(hwnd, &rect);

            TRIVERTEX vertex[2];
            vertex[0].x = 0;
            vertex[0].y = 0;
            vertex[0].Red = 0xf500;
            vertex[0].Green = 0xf500;
            vertex[0].Blue = 0xf500;
            vertex[0].Alpha = 0x0000;

            vertex[1].x = rect.right;
            vertex[1].y = rect.bottom;
            vertex[1].Red = 0xe800;
            vertex[1].Green = 0xe800;
            vertex[1].Blue = 0xf000;
            vertex[1].Alpha = 0x0000;

            GRADIENT_RECT gRect;
            gRect.UpperLeft = 0;
            gRect.LowerRight = 1;

            GradientFill(hdc, vertex, 2, &gRect, 1, GRADIENT_FILL_RECT_V);

            EndPaint(hwnd, &ps);
            return 0;
        }

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }

    return 0;
}

// 注册中转站容器窗口类
ATOM RegisterTransferStationContainerClass(HINSTANCE hInstance) {
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = TransferStationContainerProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wcex.hbrBackground = NULL;
    wcex.lpszClassName = L"TransferStationContainer";

    return RegisterClassExW(&wcex);
}

// 注册中转站卡片窗口类
ATOM RegisterTransferStationCardClass(HINSTANCE hInstance) {
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = TransferStationCardProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = sizeof(LONG_PTR);
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_HAND);
    wcex.hbrBackground = NULL;
    wcex.lpszClassName = L"TransferStationCard";

    return RegisterClassExW(&wcex);
}

// 注册旗杆窗口类
ATOM RegisterFlagpoleClass(HINSTANCE hInstance) {
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = FlagpoleProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wcex.hbrBackground = NULL;
    wcex.lpszClassName = L"Flagpole";

    return RegisterClassExW(&wcex);
}

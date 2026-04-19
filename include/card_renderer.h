#pragma once

#include <windows.h>
#include <objidl.h>   // MinGW 下必须在 gdiplus.h 之前,提供 PROPID
#include <gdiplus.h>
#include "history.h"

using namespace Gdiplus;

// 卡片绘制相关常量
extern const int CARD_WIDTH;
extern const int CARD_HEIGHT;
extern const int CARD_MARGIN;
extern const int CARD_CLOSE_BUTTON_SIZE;
extern const int CARD_CORNER_RADIUS;
extern const int CARD_PADDING;
extern const int FLAGPOLE_WIDTH;
extern const int FLAGPOLE_X;
extern const int CONNECTOR_LENGTH;

// 绘制函数声明
void DrawTypeIcon(Graphics& graphics, int x, int y, ClipboardItemType type);
void DrawCardText(Graphics& graphics, const RECT& contentRect, const ClipboardItem& item);
void DrawCardImage(Graphics& graphics, const RECT& contentRect, const ClipboardItem& item);
void DrawTransferStationCard(HDC hdc, const RECT& rect, const ClipboardItem& item, bool isHovered, bool isCollapsing = false, int animFrame = 0, bool isTopmost = false, bool isExpanding = false, bool isWaving = false, int waveFrame = 0);

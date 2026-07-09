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

// 绘制函数声明
void DrawTypeIcon(Graphics& graphics, int x, int y, ClipboardItemType type);
void DrawCardText(Graphics& graphics, const RECT& contentRect, const ClipboardItem& item);
void DrawCardImage(Graphics& graphics, const RECT& contentRect, const ClipboardItem& item);

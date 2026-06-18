#include "card_renderer.h"
#include <windows.h>
#include <objidl.h>   // MinGW 下必须在 gdiplus.h 之前,提供 PROPID
#include <gdiplus.h>
#include <cmath>
#include <vector>

using namespace Gdiplus;

// 卡片绘制相关常量定义
const int CARD_WIDTH = 300;
const int CARD_HEIGHT = 50;
const int CARD_MARGIN = 10;
const int CARD_CLOSE_BUTTON_SIZE = 16;
const int CARD_CORNER_RADIUS = 6;
const int CARD_PADDING = 8;

// 绘制类型图标
void DrawTypeIcon(Graphics& graphics, int x, int y, ClipboardItemType type) {
    SolidBrush iconBrush(Color(180, 100, 100, 100));
    Pen iconPen(Color(255, 80, 80, 80), 1.5f);

    if (type == TYPE_TEXT) {
        // 文本图标：T字
        Font font(L"Arial", 12, FontStyleBold);
        graphics.DrawString(L"T", -1, &font, PointF((float)x, (float)y), &iconBrush);
    } else if (type == TYPE_IMAGE) {
        // 图片图标：小图片框
        graphics.DrawRectangle(&iconPen, x, y, 16, 12);
        graphics.DrawLine(&iconPen, x + 4, y + 4, x + 12, y + 8);
    } else if (type == TYPE_FILE) {
        // 文件图标：文件形状
        graphics.DrawRectangle(&iconPen, x, y + 2, 12, 14);
        graphics.DrawLine(&iconPen, x, y + 2, x + 4, y - 2);
        graphics.DrawLine(&iconPen, x + 4, y - 2, x + 12, y + 2);
    }
}

// 绘制卡片文本内容
void DrawCardText(Graphics& graphics, const RECT& contentRect, const ClipboardItem& item) {
    std::wstring displayText = item.content;
    if (displayText.length() > 100) {
        displayText = displayText.substr(0, 100) + L"...";
    }

    Font font(L"Microsoft YaHei", 10);
    SolidBrush textBrush(Color(255, 50, 50, 50));

    RectF rect((float)contentRect.left, (float)contentRect.top,
               (float)(contentRect.right - contentRect.left),
               (float)(contentRect.bottom - contentRect.top));

    StringFormat format;
    format.SetAlignment(StringAlignmentNear);
    format.SetLineAlignment(StringAlignmentNear);
    format.SetTrimming(StringTrimmingEllipsisCharacter);

    graphics.DrawString(displayText.c_str(), -1, &font, rect, &format, &textBrush);
}

// 绘制卡片图片内容
void DrawCardImage(Graphics& graphics, const RECT& contentRect, const ClipboardItem& item) {
    if (item.imageData.empty()) return;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = item.imageWidth;
    bmi.bmiHeader.biHeight = -item.imageHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdcScreen = GetDC(NULL);
    HBITMAP hBitmap = CreateDIBitmap(hdcScreen, &bmi.bmiHeader, CBM_INIT,
                                     &item.imageData[0], &bmi, DIB_RGB_COLORS);
    ReleaseDC(NULL, hdcScreen);

    if (hBitmap) {
        Bitmap bitmap(hBitmap, NULL);

        int rectWidth = contentRect.right - contentRect.left;
        int rectHeight = contentRect.bottom - contentRect.top;

        float scaleX = (float)rectWidth / item.imageWidth;
        float scaleY = (float)rectHeight / item.imageHeight;
        float scale = (scaleX < scaleY) ? scaleX : scaleY;

        int drawWidth = (int)(item.imageWidth * scale);
        int drawHeight = (int)(item.imageHeight * scale);
        int drawX = contentRect.left + (rectWidth - drawWidth) / 2;
        int drawY = contentRect.top + (rectHeight - drawHeight) / 2;

        graphics.DrawImage(&bitmap, drawX, drawY, drawWidth, drawHeight);
        DeleteObject(hBitmap);
    }
}
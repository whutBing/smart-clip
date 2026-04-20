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
const int FLAGPOLE_WIDTH = 8;  // 旗杆宽度
const int FLAGPOLE_X = 12;  // 旗杆X位置
const int CONNECTOR_LENGTH = 8;  // 连接线长度

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

// 绘制中转站卡片
void DrawTransferStationCard(HDC hdc, const RECT& rect, const ClipboardItem& item, bool isHovered, bool isCollapsing, int animFrame, bool /*isTopmost*/, bool /*isExpanding*/, bool isWaving, int waveFrame) {
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // 如果正在收起，应用收起动画效果（淡出+高度缩小）
    float alpha = 1.0f;
    RECT drawRect = rect;  // 创建可修改的绘制区域

    // 收起动画的变换参数
    float widthScale = 1.0f;  // 宽度缩放（模拟向内卷）
    float textOffsetX = 0.0f;  // 文字左移偏移量

    if (isCollapsing) {
        float progress = animFrame / 40.0f;  // 40帧，动画更流畅
        if (progress > 1.0f) progress = 1.0f;

        // 应用缓动效果：都使用ease-in（二次方）
        // 收起动画：progress从0到1，效果是先慢后快
        // 展开动画：progress从1到0，效果是先快后慢
        progress = progress * progress;

        // 计算最小宽度比例（宽度缩小到高度）
        float minScale = (float)CARD_HEIGHT / (float)CARD_WIDTH;  // 50/300 = 0.167

        // 宽度缩放效果：从100%缩小到高度宽度
        widthScale = 1.0f - progress * (1.0f - minScale);
        if (widthScale < minScale) widthScale = minScale;

        // 文字左移效果：文字向左移动并消失
        textOffsetX = -progress * 200.0f;  // 向左移动200像素
    }

    // 绘制旗帜（应用收起动画变换）
    // 保存当前Graphics状态
    GraphicsState state = graphics.Save();

    // 如果正在收起，应用宽度缩放变换（绕左侧边缘向内卷）
    if (isCollapsing) {
        // 计算变换中心点（左侧边缘的中心）
        float centerX = (float)drawRect.left;
        float centerY = (float)(drawRect.top + drawRect.bottom) / 2.0f;

        // 创建变换矩阵：先平移到原点，缩放，再平移回去
        Matrix matrix;
        matrix.Translate(centerX, centerY);
        matrix.Scale(widthScale, 1.0f);  // 只缩放宽度，高度不变
        matrix.Translate(-centerX, -centerY);

        graphics.SetTransform(&matrix);
    }

    // 创建旗帜形状路径（左侧矩形，右侧锯齿状边缘）
    GraphicsPath flagPath;

    // 创建锯齿状右边缘
    std::vector<PointF> flagPoints;
    flagPoints.push_back(PointF((REAL)drawRect.left, (REAL)drawRect.top));  // 左上角

    // 飘动动画：计算波浪偏移
    float waveOffset = 0.0f;
    float waveAmplitude = 0.0f;
    if (isWaving) {
        // 波浪振幅（像素）
        waveAmplitude = 3.0f;
        // 波浪相位（基于帧数）
        waveOffset = waveFrame * 0.2f;
    }

    // 添加顶部边缘点（带波浪效果）
    int numWavePoints = 10;  // 顶部边缘的波浪点数
    float waveWidth = (float)(drawRect.right - 10 - drawRect.left) / numWavePoints;
    for (int i = 0; i <= numWavePoints; i++) {
        float x = drawRect.left + i * waveWidth;
        float y = (float)drawRect.top;
        if (isWaving && i > 0 && i < numWavePoints) {
            // 添加波浪效果，越靠右波动越大
            float waveFactor = (float)i / numWavePoints;
            y += sin(waveOffset + i * 0.8f) * waveAmplitude * waveFactor;
        }
        flagPoints.push_back(PointF((REAL)x, (REAL)y));
    }

    // 添加锯齿（5个锯齿）- 锯齿部分不参与飘动
    int numTeeth = 5;
    int currentHeight = drawRect.bottom - drawRect.top;
    float toothHeight = (float)currentHeight / numTeeth;

    for (int i = 0; i < numTeeth; i++) {
        float y1 = drawRect.top + i * toothHeight;
        float y2 = drawRect.top + (i + 0.5f) * toothHeight;
        float y3 = drawRect.top + (i + 1) * toothHeight;

        flagPoints.push_back(PointF((REAL)(drawRect.right - 10), (REAL)y1));
        flagPoints.push_back(PointF((REAL)(drawRect.right), (REAL)y2));  // 锯齿尖端
        flagPoints.push_back(PointF((REAL)(drawRect.right - 10), (REAL)y3));
    }

    // 添加底部边缘点（带波浪效果，反向）
    for (int i = numWavePoints; i >= 0; i--) {
        float x = drawRect.left + i * waveWidth;
        float y = (float)drawRect.bottom;
        if (isWaving && i > 0 && i < numWavePoints) {
            // 添加波浪效果，越靠右波动越大
            float waveFactor = (float)i / numWavePoints;
            y += sin(waveOffset + i * 0.8f + 3.14159f) * waveAmplitude * waveFactor;
        }
        flagPoints.push_back(PointF((REAL)x, (REAL)y));
    }

    flagPath.AddPolygon(&flagPoints[0], (INT)flagPoints.size());

    // 绘制旗帜背景（红色渐变，应用透明度）
    BYTE alphaValue = (BYTE)(alpha * 255);
    LinearGradientBrush gradientBrush(
        PointF((REAL)drawRect.left, (REAL)drawRect.top),
        PointF((REAL)drawRect.right, (REAL)drawRect.bottom),
        isHovered ? Color(alphaValue, 255, 80, 80) : Color(alphaValue, 220, 50, 50),
        isHovered ? Color(alphaValue, 200, 40, 40) : Color(alphaValue, 180, 30, 30)
    );
    graphics.FillPath(&gradientBrush, &flagPath);

    // 绘制旗帜边框（深红色，应用透明度）
    Pen borderPen(Color(alphaValue, 150, 20, 20), 2.0f);
    graphics.DrawPath(&borderPen, &flagPath);

    // 恢复Graphics状态
    graphics.Restore(state);

    // 绘制关闭按钮（白色X）或展开按钮（向右箭头）
    // 使用已声明的 currentHeight 变量
    int closeX = drawRect.right - 10 - CARD_CLOSE_BUTTON_SIZE - 5;
    int closeY = drawRect.top + (currentHeight - CARD_CLOSE_BUTTON_SIZE) / 2;

    // 判断是否处于收起状态（宽度==高度）
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    bool isCollapsedState = (width == height);

    // 只有在非动画状态下才显示按钮
    if (!isCollapsing) {
        // 半透明白色圆形背景
        SolidBrush closeBgBrush(Color(100, 255, 255, 255));
        Pen closePen(Color(255, 255, 255, 255), 2.0f);

        if (isCollapsedState) {
            // 收起状态：显示向右箭头（展开按钮）
            graphics.FillEllipse(&closeBgBrush, closeX, closeY, CARD_CLOSE_BUTTON_SIZE, CARD_CLOSE_BUTTON_SIZE);

            int arrowMargin = 4;
            int centerY = closeY + CARD_CLOSE_BUTTON_SIZE / 2;

            // 箭头主体（水平线）
            graphics.DrawLine(&closePen,
                closeX + arrowMargin, centerY,
                closeX + CARD_CLOSE_BUTTON_SIZE - arrowMargin, centerY);

            // 箭头尖端（>形状）
            graphics.DrawLine(&closePen,
                closeX + CARD_CLOSE_BUTTON_SIZE - arrowMargin - 4, centerY - 4,
                closeX + CARD_CLOSE_BUTTON_SIZE - arrowMargin, centerY);
            graphics.DrawLine(&closePen,
                closeX + CARD_CLOSE_BUTTON_SIZE - arrowMargin, centerY,
                closeX + CARD_CLOSE_BUTTON_SIZE - arrowMargin - 4, centerY + 4);
        } else if (isHovered) {
            // 正常状态且悬停：显示X按钮（关闭按钮）
            graphics.FillEllipse(&closeBgBrush, closeX, closeY, CARD_CLOSE_BUTTON_SIZE, CARD_CLOSE_BUTTON_SIZE);

            int crossMargin = 5;
            graphics.DrawLine(&closePen,
                closeX + crossMargin, closeY + crossMargin,
                closeX + CARD_CLOSE_BUTTON_SIZE - crossMargin, closeY + CARD_CLOSE_BUTTON_SIZE - crossMargin);
            graphics.DrawLine(&closePen,
                closeX + CARD_CLOSE_BUTTON_SIZE - crossMargin, closeY + crossMargin,
                closeX + crossMargin, closeY + CARD_CLOSE_BUTTON_SIZE - crossMargin);
        }
    }

    // 绘制文本内容（白色文字）
    RECT contentRect = drawRect;
    contentRect.left += CARD_PADDING + 5;
    contentRect.right -= CARD_PADDING + CARD_CLOSE_BUTTON_SIZE + 20;  // 留出锯齿和关闭按钮空间
    contentRect.top += CARD_PADDING;
    contentRect.bottom -= CARD_PADDING;

    // 获取显示文本
    std::wstring displayText = item.content;
    if (item.type == TYPE_FILE) {
        size_t pos = displayText.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            displayText = displayText.substr(pos + 1);
        }
    }

    // 截断过长文本
    if (displayText.length() > 35) {
        displayText = displayText.substr(0, 32) + L"...";
    }

    // 绘制文本（白色，加粗，应用透明度和左移效果）
    FontFamily fontFamily(L"Microsoft YaHei");
    Font font(&fontFamily, 12, FontStyleBold, UnitPixel);
    BYTE textAlpha = (BYTE)(alpha * 255);
    SolidBrush textBrush(Color(textAlpha, 255, 255, 255));  // 白色文字，应用透明度

    StringFormat format;
    format.SetAlignment(StringAlignmentNear);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);

    // 应用文字左移偏移量
    RectF textRect((REAL)(contentRect.left + textOffsetX), (REAL)contentRect.top,
                   (REAL)(contentRect.right - contentRect.left),
                   (REAL)(contentRect.bottom - contentRect.top));

    graphics.DrawString(displayText.c_str(), -1, &font, textRect, &format, &textBrush);
}
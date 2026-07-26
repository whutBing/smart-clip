#pragma once

// MinGW 下 objidl.h 必须在 gdiplus.h 之前,提供 PROPID
#include <objidl.h>
#include <gdiplus.h>
#include <windows.h>

// 生成菜单图标位图(Segoe MDL2 Assets 字符)。调用方负责 DeleteObject。
HBITMAP CreateMenuIconBitmap(const wchar_t *iconChar,
                             COLORREF color = RGB(60, 60, 60),
                             int verticalPadding = 0);

// 生成菜单颜色方块位图（用于标签颜色标识）。调用方负责 DeleteObject。
HBITMAP CreateMenuColorBitmap(COLORREF color);

// 在给定路径上追加一个圆角矩形(GDI+)。
void CreateRoundRectPath(Gdiplus::GraphicsPath *path, int x, int y, int width,
                         int height, int radius);

// 获取窗口原生 DPI（等价于 Win32 GetDpiForWindow，但通过 GetProcAddress
// 动态加载以兼容未声明该 API 的旧 MinGW 头文件，并在 API 不可用时回退到
// GetDeviceCaps(LOGPIXELSX) / 96）。供需要纯系统 DPI（不做 2K/4K 下限放大）
// 的代码使用，例如对话框尺寸计算。
UINT GetWindowDpi(HWND hwnd);

// 获取当前窗口适用的 UI DPI。对高分辨率屏幕设置最低 UI 放大档位：
// 2K 至少 150%，4K 至少 200%，避免主界面显得过小。
UINT GetSmartClipUiDpi(HWND hwnd);

// 按 DPI 缩放设计像素。
int ScaleForDpi(int value, UINT dpi);

// 按当前窗口适用 UI DPI 缩放设计像素。
int ScaleForWindowDpi(HWND hwnd, int value);

// 检测文本中是否包含 emoji 字符（用于决定是否走彩色渲染路径）。
bool TextContainsEmoji(const wchar_t *text, int len = -1);

// 使用 Direct2D + DirectWrite 绘制文本，启用彩色字体支持（彩色 emoji）。
// 用于替代 GDI DrawTextW 绘制列表项主文本：当文本含 emoji 时，GDI 只能渲染
// 单色回退字形；DirectWrite 会自动 fallback 到 Segoe UI Emoji 并渲染彩色。
//
// 参数说明：
//   hdc          目标设备上下文（调用方应已 SelectObject 目标字体）
//   text         文本
//   textLen      文本长度（-1 表示自动计算）
//   rcText       绘制矩形（GDI 坐标，逻辑像素）
//   referenceFont  参考字体句柄；用其 lfHeight 通过 GetTextMetrics 换算出
//                 DirectWrite em size（= tmHeight - tmInternalLeading）。
//                 nullptr 时回退到 lfHeight * 0.8。
//   fontFamily   字体族（如 L"Microsoft YaHei"）
//   fontWeight   字重（DWRITE_FONT_WEIGHT_REGULAR 等）
//   textColor    文本颜色（COLORREF）
//   align        0=左对齐 1=右对齐 2=居中
//   verticalCenter 是否垂直居中
//   endEllipsis  是否超出宽度时显示省略号
//   emojiScale   emoji 字号相对文字 em size 的缩放系数（默认 0.9 让 emoji
//                 视觉上不撑满行高，避免比文字"看起来更大"）
void DrawTextWithColorEmoji(HDC hdc, const wchar_t *text, int textLen,
                            const RECT &rcText,
                            HFONT referenceFont,
                            const wchar_t *fontFamily,
                            int fontWeight, COLORREF textColor,
                            int align = 0, bool verticalCenter = true,
                            bool endEllipsis = true,
                            float emojiScale = 0.9f);

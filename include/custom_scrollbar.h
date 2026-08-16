// custom_scrollbar.h
// 通用自定义滚动条组件：从主窗体 ListBox 滚动条抽取的样式与交互逻辑，
// 可用于任意目标窗口（EDIT / ListBox / 自绘控件等）。
// 调用方通过回调提供"内容总高度 / 可见高度 / 当前 scrollTop / 设置 scrollTop"。
#pragma once

#include <windows.h>

// 默认样式参数（与主窗体滚动条一致）
#define CS_DEFAULT_TRACK_WIDTH 14
#define CS_DEFAULT_THUMB_MIN_HEIGHT 30
#define CS_DEFAULT_HIDE_DELAY_MS 900

struct CustomScrollbar {
  HWND hwndOwner;            // 滚动条所在窗口（接收鼠标/定时器事件）
  HWND hwndTarget;           // 滚动目标窗口（EDIT/ListBox 等）
  bool visible;              // 滑块是否显示
  bool hovered;              // 鼠标悬停在滑块上
  bool dragging;             // 正在拖拽滑块
  int dragStartY;            // 拖拽起始鼠标 y
  int dragStartScrollTop;    // 拖拽起始 scrollTop
  int trackWidth;            // 轨道宽度
  int thumbMinHeight;        // 滑块最小高度
  UINT_PTR hideTimerId;      // 自动隐藏定时器 ID
  UINT hideDelayMs;          // 自动隐藏延迟
};

// 回调函数类型：基于 hwndTarget 获取/设置滚动信息（像素单位）
typedef int (*CSGetTotalHeightFn)(HWND hwndTarget);
typedef int (*CSGetVisibleHeightFn)(HWND hwndTarget);
typedef int (*CSGetScrollTopFn)(HWND hwndTarget);
typedef void (*CSSetScrollTopFn)(HWND hwndTarget, int scrollTop);

// 初始化（使用默认参数）
void CSInit(CustomScrollbar *sb, HWND hwndOwner, HWND hwndTarget,
            UINT_PTR hideTimerId);

// 是否需要显示滚动条（内容超出可见区）
bool CSNeedsShow(CustomScrollbar *sb, CSGetTotalHeightFn getTotal,
                 CSGetVisibleHeightFn getVisible);

// 为滚动条预留的宽度（即使不显示也预留，避免布局抖动）
int CSReservedWidth(CustomScrollbar *sb);

// 获取轨道矩形（在 hwndOwner 客户区坐标系）
bool CSGetTrackRect(CustomScrollbar *sb, RECT *rcTrack);

// 获取滑块矩形
bool CSGetThumbRect(CustomScrollbar *sb, RECT *rcThumb,
                    CSGetTotalHeightFn getTotal,
                    CSGetVisibleHeightFn getVisible,
                    CSGetScrollTopFn getScroll);

// 绘制滚动条（在 hwndOwner 的 WM_PAINT 中调用）
void CSPaint(CustomScrollbar *sb, HDC hdc, CSGetTotalHeightFn getTotal,
             CSGetVisibleHeightFn getVisible, CSGetScrollTopFn getScroll);

// 显示滚动条（启动自动隐藏定时器）
void CSShow(CustomScrollbar *sb);

// 隐藏滚动条
void CSHide(CustomScrollbar *sb);

// 状态变化时刷新（仅必要时重绘）
void CSRefresh(CustomScrollbar *sb, CSGetTotalHeightFn getTotal,
               CSGetVisibleHeightFn getVisible, CSGetScrollTopFn getScroll);

// 鼠标移动：返回 true 表示事件被滚动条处理
// 拖拽中会调用 setScroll 更新目标滚动位置
bool CSOnMouseMove(CustomScrollbar *sb, int x, int y,
                   CSGetTotalHeightFn getTotal,
                   CSGetVisibleHeightFn getVisible,
                   CSGetScrollTopFn getScroll,
                   CSSetScrollTopFn setScroll);

// 左键按下：返回 true 表示事件被滚动条处理（进入拖拽或点击翻页）
bool CSOnLButtonDown(CustomScrollbar *sb, int x, int y,
                     CSGetTotalHeightFn getTotal,
                     CSGetVisibleHeightFn getVisible,
                     CSGetScrollTopFn getScroll,
                     CSSetScrollTopFn setScroll);

// 左键抬起：结束拖拽
bool CSOnLButtonUp(CustomScrollbar *sb);

// 鼠标离开
void CSOnMouseLeave(CustomScrollbar *sb);

// 定时器事件（自动隐藏）
void CSOnTimer(CustomScrollbar *sb);

#pragma once

#include <windows.h>
#include <string>

// 生成并返回本机机器码（基于硬件指纹的稳定哈希）。
// 用于后续"一机一码"授权：用户凭此码购买对应 Pro 激活码。
std::wstring GetMachineCode();

// 弹出"本机信息"对话框（模态），展示机器码与硬件信息，支持一键复制机器码。
void ShowMachineInfoDialog(HWND hwndParent);

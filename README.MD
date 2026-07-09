# SmartClip_Native
✨ 优雅高效的 Windows 剪贴板管理工具

[中文](#中文-简介) | [English](#english-introduction)

---

## 中文 简介
一款**优雅、高效管理剪贴板**的工具，基于 Windows 平台，使用 C++ Native API 实现。
欢迎提交 Pull Request 或者交流您的有趣想法～

### 🎯 特色功能
1. ⌨️ **VIM 风格导航**：支持 h/j/k/l 方向键导航，双击 G 跳转到末尾，Shift+G 跳转到开头
2. ⚡ **C++ Native 原生实现**（无 WebView），内存占用极低
3. 🛡️ **无恶意代码**，安全干净，不会被杀毒软件误报
4. 🧠 **智能识别**：自动识别文件夹、IP、文件路径
5. 🚀 **常用功能一触即达**，使用更顺滑
6. 🌟 **收藏快捷键**：支持修饰键（Alt/Ctrl/Shift/Ctrl+Alt/Ctrl+Shift/Alt+Shift）+ 数字键快速收藏
7. 📁 **文件夹快捷打开**：点击文件夹项直接在资源管理器中打开
8. 📂 **文件类型识别**：自动识别文件类型，支持拖拽操作和快捷打开
9. 🔍 **文件存活检测**：自动清理原始图片文件已丢失的记录
10. 🖥️ **高 DPI 支持**：完美支持 4K 显示器，所有界面元素正确缩放
11. 🌍 **多语言支持**：内置中文/英文双语切换，所有界面文本均可国际化
12. 🎨 **主题系统**：支持亮色、暗色、跟随系统三种主题模式

### 🛠️ 如何构建
1. 确保电脑已安装 MinGW 编译器（必需）
2. 直接运行 `build_all.bat`（自动清理 + 编译 + 运行）

### ⌨️ 快捷键说明
| 快捷键 | 功能 |
|--------|------|
| `修饰键 + 1~9` | 快捷粘贴对应位置的剪贴板内容 |
| `收藏修饰键 + 1~9` | 快速收藏对应位置的剪贴板内容 |
| `h/j/k/l` | VIM 风格列表导航 |
| `G`（双击） | 跳转到列表末尾 |
| `Shift + G` | 跳转到列表开头 |

### 📋 计划功能
1. 🎨 优化设置界面
2. ⌨️ 快捷键冲突检测
3. 🔐 国密支持（收藏内容默认加入密码区）
4. 🚩 完善/重构旗帜功能
5. 🔍 搜索框支持复杂命令
6. 📦 优化批量操作功能
7. 🗄️ 接入轻量级数据库
8. ⚙️ 极限性能基准测试与优化

---

## English Introduction
An **elegant & efficient clipboard manager** for Windows, built with C++ Native API.
Pull Requests and creative ideas are always welcomed!

### 🎯 Features
1. ⌨️ **VIM-style navigation**: h/j/k/l keys for list navigation, double-click G to jump to bottom, Shift+G to jump to top
2. ⚡ **C++ Native implementation** (no WebView), ultra-low memory usage
3. 🛡️ **100% clean and safe**, no false positives from antivirus
4. 🧠 **Smart recognition** for folders, IP addresses and file paths
5. 🚀 **One-click access** to frequently used functions
6. 🌟 **Favorite hotkeys**: Modifier keys (Alt/Ctrl/Shift/Ctrl+Alt/Ctrl+Shift/Alt+Shift) + number keys for quick favorites
7. 📁 **Quick folder opening**: Click folder items to open in File Explorer
8. 📂 **File type detection**: Automatic file type recognition with drag-drop and quick open support
9. 🔍 **File survival check**: Clean records with missing original image files automatically
10. 🖥️ **High DPI support**: Perfectly supports 4K displays with correct scaling for all UI elements
11. 🌍 **Multi-language support**: Built-in Chinese/English bilingual toggle, all UI text is internationalized
12. 🎨 **Theme system**: Supports light, dark, and follow-system theme modes

### 🛠️ How to Build
1. Install MinGW compiler (required)
2. Run `build_all.bat` (clean + build + run automatically)

### ⌨️ Hotkey Reference
| Hotkey | Action |
|--------|--------|
| `Modifier + 1~9` | Quick paste clipboard item at position |
| `Favorite Modifier + 1~9` | Quick favorite clipboard item at position |
| `h/j/k/l` | VIM-style list navigation |
| `G` (double-click) | Jump to list bottom |
| `Shift + G` | Jump to list top |

### 📋 Roadmap
1. 🎨 Optimize settings UI
2. ⌨️ Hotkey conflict detection
3. 🔐 Support for national secret encryption
4. 🚩 Improve or remove flag features
5. 🔍 Advanced command support in search box
6. 📦 Optimize batch operations
7. 🗄️ Integrate lightweight database
8. ⚙️ Performance benchmark & extreme optimization

---
📄 License: MIT
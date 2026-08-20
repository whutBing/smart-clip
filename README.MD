# SmartClip_Native
✨ 优雅高效的 Windows 剪贴板管理工具

[中文](#中文-简介) | [English](#english-introduction)

---

## 中文 简介
一款**优雅、高效管理剪贴板**的工具，基于 Windows 平台，原生 C++ 开发。
欢迎提交 Pull Request 或者交流您的有趣想法～

📥 **微软应用商店下载**：[https://apps.microsoft.com/detail/9PC8K4WZMW38](https://apps.microsoft.com/detail/9PC8K4WZMW38?hl=zh-cn&gl=CN&ocid=pdpshare)

### 🎯 特色功能
1. ⌨️ **VIM 风格导航**：支持 h/j/k/l 方向键导航，双击 g 跳转到当页首项，Shift+G 跳转到当页末项
2. ⚡ **原生 C++ 实现**，内存占用极低
3. 🛡️ **无恶意代码**，安全干净，不会被杀毒软件误报
4. 🧠 **智能识别**：自动识别文件夹、IP、文件路径、网址
5. 🚀 **常用功能一触即达**，使用更顺滑
6. 🌟 **收藏快捷键**：支持修饰键（Alt/Ctrl/Shift/Ctrl+Alt/Ctrl+Shift/Alt+Shift）+ 数字键快速收藏
7. 📁 **文件夹快捷打开**：点击文件夹项直接在资源管理器中打开
8. 📂 **文件类型识别**：自动识别文件类型，支持拖拽操作和快捷打开
9. 🔍 **文件存活检测**：自动清理原始图片文件已丢失的记录
10. 🖥️ **高 DPI 支持**：完美支持 4K 显示器，所有界面元素正确缩放
11. 🌍 **多语言支持**：首次运行自动匹配系统区域设置，支持简体中文、英语、日语、韩语、德语、阿拉伯语、土耳其语、印地语、保加利亚语、西班牙语、泰语共 11 种语言，所有界面文本均可国际化
12. 🎨 **主题系统**：支持亮色、暗色、跟随系统三种主题模式
13. 🗄️ **轻量级数据库存储**：历史记录管理更高效可靠
14. 🕐 **时间与应用筛选**：点击时间戳按日期筛选，点击应用图标按来源应用筛选
15. ⏸️ **托盘暂停/恢复**：右键托盘图标可快速暂停或恢复剪贴板监听
16. ⚡ **滚动式快捷键**：窗体所见内容均可快捷粘贴，列表滚动时当前可见的每条记录都会被自动分配数字快捷键（1~0）
17. 🔌 **快捷键一键开关**：右键托盘图标可快速启用或关闭本 app 所有快捷键，关闭后主窗体不再显示快捷键标签
18. 📁 **多文件拆行记录**：资源管理器中复制多个文件时自动拆分为多条独立记录，顺序与资源管理器一致；粘贴时恢复完整的多文件格式
19. 🖼️ **空格预览与编辑**：选中图片记录按空格键全屏预览；选中文本记录按空格或右键"编辑"打开预览窗口，可直接修改内容并保存，按 Esc 关闭
20. 🗑️ **Del 删除记录**：按 Delete 键删除当前选中记录，行为与右键删除完全一致（收藏记录会弹窗确认）
21. 🎨 **彩色 Emoji 渲染**：列表文本中的 emoji 自动以彩色显示
22. 🏷️ **类型图标**：文件类型前显示与资源管理器一致的系统图标（多文件记录每个文件名前均显示对应图标），网址类型显示 net.png，文本类型显示 text.png，均为 18×18 尺寸
23. 🔗 **在资源管理器中选中**：右键多文件记录可一次性选中所有文件，替代单文件的"打开所在位置"
24. 🖐️ **拖拽多文件到外部**：可将多文件记录直接拖拽到资源管理器或其他窗口进行粘贴，所有文件一并传递
25. ♻️ **重启应用**：右键托盘菜单"退出"上方提供"重启应用"选项，一键重启
26. 📝 **Markdown 双记录**：复制 markdown 内容时自动生成两条记录——上方为带原始语法的文本，下方为纯文本，便于按场景粘贴
27. 🖱️ **悬浮选中**：鼠标悬浮在列表项上即自动选中（通用设置中可开关，默认开启）
28. 🔍 **搜索关键词高亮**：搜索框匹配到的关键词以主题色蓝色高亮显示，文本、文件、文件夹及多文件记录均支持
29. 📎 **收藏分类药丸**：点击收藏分类后自动在搜索框添加对应分类色药丸（收藏#xx），点击删除分类时同步移除
30. 🖱️ **文本选中复制**：鼠标拖选文本内容即高亮选中，按 Ctrl+C 复制选中片段，按 Esc 取消（含 emoji 文本暂不支持选中）
31. 🧲 **文件中转站**：在资源管理器等应用中按住文件（夹）左右晃动两下即可呼出主窗体（带放置提示蒙版，窗体隐藏或最小化时同样有效），松手放置即存入记录；未放置则窗体自动隐藏并还原原状，不打断原拖拽操作

### 📜 用户协议与免责声明
- 本软件仅供合法用途使用，用户须妥善保管剪贴板数据
- 剪贴板内容可能包含敏感信息，系统故障或误操作可能导致数据丢失
- 作者/开发者不对任何数据丢失或损害承担责任
- 首次安装时会显示用户协议对话框，需用户同意后方可使用

### 🛠️ 如何构建
1. 确保电脑已安装 MinGW 编译器（必需）
2. 直接运行 `build_all.bat`（自动清理 + 编译 + 运行）

### ⌨️ 快捷键说明
| 快捷键 | 功能 |
|--------|------|
| `修饰键 + 1~9` | 快捷粘贴当前可见区域对应位置的剪贴板内容（滚动后自动重新分配） |
| `修饰键 + 0` | 快捷粘贴第 10 条可见记录 |
| `收藏修饰键 + 1~9` | 快速收藏对应位置的剪贴板内容 |
| `h/j/k/l` | VIM 风格列表导航 |
| `g`（双击） | 跳转到当页可见首项（与显示的快捷键 1 一致） |
| `Shift + G` | 跳转到当页可见末项（与显示的快捷键 0 一致） |
| `空格` | 选中图片记录时全屏预览/关闭；选中文本记录时打开编辑窗口 |
| `Delete` | 删除当前选中记录（收藏项弹窗确认） |
| `Ctrl + C` | 鼠标拖选文本后复制选中片段 |
| `Esc` | 取消文本选中 |

### 📋 计划功能
1. 🎨 优化设置界面
2. 🔐 国密支持（收藏内容默认加入密码区）
3. 🚩 完善/重构旗帜功能
4. 🔍 搜索框支持复杂命令
5. 📦 优化批量操作功能
6. ⚙️ 极限性能基准测试与优化
7. 🔐 完善用户协议流程

---

## English Introduction
An **elegant & efficient clipboard manager** for Windows, built with native C++.
Pull Requests and creative ideas are always welcomed!

📥 **Microsoft Store**: [Download from Microsoft Store](https://apps.microsoft.com/detail/9PC8K4WZMW38?hl=zh-cn&gl=CN&ocid=pdpshare)

### 🎯 Features
1. ⌨️ **VIM-style navigation**: h/j/k/l keys for list navigation, double-click g to jump to the first visible item of current page, Shift+G to jump to the last visible item of current page
2. ⚡ **Native C++ implementation**, ultra-low memory usage
3. 🛡️ **100% clean and safe**, no false positives from antivirus
4. 🧠 **Smart recognition** for folders, IP addresses, file paths and URLs
5. 🚀 **One-click access** to frequently used functions
6. 🌟 **Favorite hotkeys**: Modifier keys (Alt/Ctrl/Shift/Ctrl+Alt/Ctrl+Shift/Alt+Shift) + number keys for quick favorites
7. 📁 **Quick folder opening**: Click folder items to open in File Explorer
8. 📂 **File type detection**: Automatic file type recognition with drag-drop and quick open support
9. 🔍 **File survival check**: Clean records with missing original image files automatically
10. 🖥️ **High DPI support**: Perfectly supports 4K displays with correct scaling for all UI elements
11. 🌍 **Multi-language support**: Auto-detects system locale on first run; supports Simplified Chinese, English, Japanese, Korean, German, Arabic, Turkish, Hindi, Bulgarian, Spanish and Thai (11 languages in total), all UI text is internationalized
12. 🎨 **Theme system**: Supports light, dark, and follow-system theme modes
13. 🗄️ **Lightweight database storage**: Efficient and reliable history management
14. 🕐 **Time & app filtering**: Click timestamps to filter by date, click app icons to filter by source application
15. ⏸️ **Tray pause/resume**: Right-click the tray icon to quickly pause or resume clipboard listening
16. ⚡ **Scroll-aware hotkeys**: All visible items in the window can be quick-pasted; when the list scrolls, every visible record is automatically assigned a number hotkey (1~0)
17. 🔌 **One-click hotkey toggle**: Right-click the tray icon to quickly enable or disable all hotkeys in this app; when disabled, the main window no longer shows hotkey labels
18. 📁 **Multi-file split records**: Copying multiple files in File Explorer splits them into independent records in the same order; pasting restores the complete multi-file format
19. 🖼️ **Space preview & edit**: Space on an image record opens a fullscreen preview; Space or right-click "Edit" on a text record opens an edit window where you can modify and save, press Esc to close
20. 🗑️ **Del to delete**: Press Delete to remove the selected record, identical to right-click delete (favorites prompt confirmation)
21. 🎨 **Color emoji rendering**: Emoji in list text is rendered in color
22. 🏷️ **Type icons**: File records show system icons consistent with File Explorer (multi-file records show an icon before each filename), URL records show net.png, text records show text.png, all at 18×18
23. 🔗 **Select in File Explorer**: Right-click a multi-file record to select all files at once, replacing "Open Location" for multi-file records
24. 🖐️ **Drag multi-file out**: Drag a multi-file record directly to File Explorer or other windows to paste all files together
25. ♻️ **Restart app**: A "Restart" option is provided above "Exit" in the tray context menu
26. 📝 **Markdown dual records**: Copying markdown content auto-generates two records — the markdown source on top and the plain text below, so you can paste either as needed
27. 🖱️ **Hover select**: Hovering over a list item auto-selects it (toggle in General settings, on by default)
28. 🔍 **Search highlight**: keywords matched in the search box are highlighted in the theme blue color; applies to text, files, folders and multi-file records
29. 📎 **Favorite category pill**: clicking a favorite category adds a color-coded pill (favorite #xx) to the search box for one-click filtering; removed automatically when the category is deleted
30. 🖱️ **Text selection & copy**: drag to select text content with highlight, press Ctrl+C to copy the selection, press Esc to cancel (text containing emoji is not selectable for now)
31. 🧲 **File drop shelf**: While dragging file(s)/folder(s) in File Explorer, shake the mouse left-right twice to summon the main window (with a drop-hint mask; works even when the window is hidden or minimized); release the mouse over it to add them to records. Drop elsewhere and the window auto-hides and restores its previous state without interrupting the original drag

### 📜 User Agreement & Disclaimer
- This software is for legitimate use only; users must safeguard their clipboard data
- Clipboard content may contain sensitive information; system failures or misuse may cause data loss
- The author/developer is not liable for any data loss or damages
- A first-run agreement dialog is shown on initial installation and must be accepted before use

### 🛠️ How to Build
1. Install MinGW compiler (required)
2. Run `build_all.bat` (clean + build + run automatically)

### ⌨️ Hotkey Reference
| Hotkey | Action |
|--------|--------|
| `Modifier + 1~9` | Quick paste clipboard item at the corresponding visible position (re-assigned on scroll) |
| `Modifier + 0` | Quick paste the 10th visible record |
| `Favorite Modifier + 1~9` | Quick favorite clipboard item at position |
| `h/j/k/l` | VIM-style list navigation |
| `g` (double-click) | Jump to the first visible item of current page (consistent with hotkey 1) |
| `Shift + G` | Jump to the last visible item of current page (consistent with hotkey 0) |
| `Space` | Fullscreen preview / close for image records; open edit window for text records |
| `Delete` | Delete selected record (favorites prompt confirmation) |
| `Ctrl + C` | Copy the text selection made by mouse dragging |
| `Esc` | Cancel text selection |

### 📋 Roadmap
1. 🎨 Optimize settings UI
2. 🔐 Support for national secret encryption
3. 🚩 Improve or remove flag features
4. 🔍 Advanced command support in search box
5. 📦 Optimize batch operations
6. ⚙️ Performance benchmark & extreme optimization
7. 🔐 Improve user agreement flow

---
📄 License: MIT
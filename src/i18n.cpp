#include "i18n.h"
#include "history.h"
#include "settings.h"
#include <shlwapi.h>
#include <windows.h>

extern HWND g_hwndMain;
extern HWND g_hwndFilterAll;
extern HWND g_hwndFilterText;
extern HWND g_hwndFilterImage;
extern HWND g_hwndFilterFile;
extern HWND g_hwndFilterFavorite;
extern HWND g_hwndSearchBox;

AppLanguage g_appLanguage = LANG_ZH_CN;

// ==================== 内置语言（中文/英文） ====================

static const wchar_t *kStringsZh[STR_COUNT] = {
    L"Smart Clip",
    L"设置",
    L"通用",
    L"快捷键",
    L"数据",
    L"智能操作",
    L"基本设置和外观",
    L"快捷键配置",
    L"根据内容自动执行操作",
    L"消息通知",
    L"操作时显示系统通知",
    L"平滑滚动",
    L"列表滚动时使用平滑动画",
    L"显示滚动条",
    L"显示右侧悬浮滚动条并支持拖拽",
    L"停留时间",
    L"设置滚动条停止后的停留时长",
    L"主题模式",
    L"切换日间、夜间或跟随系统",
    L"主题风格",
    L"切换内置主题配色方案",
    L"语言",
    L"切换应用显示语言",
    L"图片预览质量",
    L"设置剪贴板图片的预览清晰度",
    L"任务栏显示",
    L"在任务栏显示窗口图标",
    L"历史记录数量",
    L"最多保存的剪贴板记录条数",
    L"切换快捷键",
    L"显示/隐藏 Smart Clip 窗口",
    L"搜索框快捷键",
    L"聚焦搜索框的快捷键",
    L"快捷粘贴",
    L"使用修饰键+数字快速粘贴",
    L"快捷粘贴修饰键",
    L"选择快捷粘贴使用的修饰键",
    L"收藏快捷键修饰键",
    L"选择收藏快捷键使用的修饰键",
    L"占用磁盘空间",
    L"粘贴次数",
    L"设置数据目录",
    L"清理非收藏数据",
    L"删除所有未收藏的历史记录",
    L"删除失效图片",
    L"清理原始图片已丢失的记录",
    L"导出数据",
    L"将全部历史记录导出为 ZIP 压缩包",
    L"文本大小上限",
    L"超过此大小的文本不记录(KB)",
    L"日间",
    L"夜间",
    L"跟随系统",
    L"经典",
    L"石墨",
    L"暖纸",
    L"高对比",
    L"简体中文",
    L"English",
    L"日本語",
    L"한국어",
    L"Deutsch",
    L"العربية",
    L"Türkçe",
    L"关闭",
    L"模糊",
    L"标清",
    L"高清",
    L"全部",
    L"文本",
    L"图像",
    L"文件",
    L"收藏",
    L"键入搜索",
    L"置顶",
    L"取消置顶",
    L"批量编辑",
    L"暗黑",
    L"置顶",
    L"批量编辑",
    L"暗黑模式",
    L"上一页",
    L"下一页",
    L"单击显示，右击编辑",
    L"最小化",
    L"最大化",
    L"关闭",
    L"提示",
    L"设置",
    L"消息通知",
    L"日间模式",
    L"夜间模式",
    L"退出",
    L"重启应用",
    L"设置已更新",
    L"消息通知已启用",
    L"Smart Clip",
    L"已粘贴",
    L"关于",
    L"版本信息与鸣谢",
    L"开机启动",
    L"系统启动时自动运行",
    L"版本",
    L"当前程序版本号",
    L"鸣谢",
    L"感谢为本项目提供帮助的人",
    L"感谢所有支持 SmartClip 的朋友：\n北一、Sulla vetta la "
    L"Saovia、Lion、陈随易",
    L"协议",
    L"最终用户许可协议与隐私政策",
    L"最终用户许可协议",
    L"隐私政策",
    L"安装及使用本软件即视为同意上述协议",
    L"最终用户许可协议",
    L"SmartClip 最终用户许可协议\r\n\r\n"
    L"1. 许可授予\r\n"
    L"本软件授予您非独占、不可转让的使用许可，允许您按照本软件的文档说明使用本"
    L"软件。\r\n\r\n"
    L"2. 使用限制\r\n"
    L"您不得对本软件进行反编译、反汇编或其他逆向工程操作；不得将本软件用于任何"
    L"违法用途。\r\n\r\n"
    L"3. 知识产权\r\n"
    L"本软件及其副本的一切知识产权均归开发者所有。本许可协议不授予您使用本软件"
    L"相关商标、标识的权利。\r\n\r\n"
    L"4. 数据保管与免责声明\r\n"
    L"使用者应妥善保管剪贴板数据。剪贴板内容可能包含敏感信息，因系统故障、误"
    L"操作或意外情况可能导致数据丢失，开发者不对因使用本软件而造成的任何损失"
    L"或数据丢失负责。\r\n"
    L"本软件使用 SQLite3 数据库存储记录，但并未加密，记录内容可能被非法获取并"
    L"查看，请妥善保管数据目录文件（可在 设置-数据 下查看）。\r\n\r\n"
    L"5. 协议终止\r\n"
    L"如果您违反本许可协议的任何条款，许可将自动终止，您必须立即停止使用并销毁"
    L"本软件的所有副本。\r\n\r\n"
    L"安装及使用本软件即视为同意上述协议。",
    L"隐私政策",
    L"SmartClip 隐私政策\r\n\r\n"
    L"1. 数据存储\r\n"
    L"您的剪贴板历史记录仅存储在本地计算机上，不会上传到任何服务器。\r\n\r\n"
    L"2. 数据收集\r\n"
    L"本软件不会收集您的个人信息。剪贴板内容仅在您主动使用时才会被粘贴或分享。"
    L"\r\n\r\n"
    L"3. 第三方服务\r\n"
    L"本软件不使用任何第三方分析服务或广告 SDK。\r\n\r\n"
    L"4. 数据安全\r\n"
    L"我们建议您定期清理不需要的剪贴板历史记录，以保护敏感信息。使用者应妥善"
    L"保管剪贴板数据，因系统故障、误操作或意外情况可能导致数据丢失，开发者不"
    L"承担相关责任。\r\n\r\n"
    L"5. 隐私政策更新\r\n"
    L"我们可能会不时更新本隐私政策。更新后的政策将在软件中显示。\r\n\r\n"
    L"安装及使用本软件即视为同意上述隐私政策。",

    L"收藏快捷键",
    L"次",
    L"选择",
    L"清理",
    L"导出",
    L"导入数据",
    L"从 ZIP 备份恢复历史记录和设置",
    L"导入",
    L"数据备份",
    L"导出或导入你的数据",
    L"立即清理",
    L"选择数据存储目录",
    L"清理非收藏数据",
    L"此操作将删除所有未收藏的历史记录，无法恢复。\n确定要继续吗？",
    L"清理原图已丢失的记录",
    L"此操作将删除所有原始图片文件已丢失的记录，无法恢复。\n确定要继续吗？",
    L"提示",
    L"非收藏数据已清理",
    L"失效图片记录已清理",

    L"确认迁移",
    L"将数据迁移到:\n%s\\SmartClip\n\n确定迁移？",
    L"取消",

    L"删除全部非收藏记录",
    L"仅保留你已收藏的内容",
    L"这会移除所有未收藏的历史记录，操作不可撤销。",

    L"清理原图已丢失的记录",
    L"只删除已经失效的图片项",
    L"这会移除原始图片文件已不存在的记录，操作不可撤销。",

    L"Ctrl",
    L"Alt",
    L"Shift",
    L"Win",
    L"无",
    L"Ctrl+Alt",
    L"Ctrl+Shift",
    L"Alt+Shift",
    L"Ctrl+Alt+Shift",

    // 右键菜单
    L"复制",
    L"执行粘贴",
    L"标签",
    L"打开所在位置",
    L"删除",
    L"批量加入标签",
    L"批量删除",
    L"无可用标签",

    // 删除收藏确认对话框
    L"删除收藏记录",
    L"删除当前收藏",
    L"这条收藏会从历史中移除",
    L"该项目当前已收藏，删除后无法恢复。",
    L"删除记录",

    // 批量操作通知
    L"已批量删除",
    L"已批量加入标签",

    // 快捷键占位文本
    L"请输入快捷键",

    // GitHub 仓库链接
    L"仓库",

    // 通知消息
    L"剪贴板新内容已成功记录到历史列表中",
    L"已复制至SmartClip",
    L"剪贴板图像已成功复制并记录到历史列表中",
    L"文件路径已成功复制并记录到历史列表中",
    L"图片文件已成功添加到历史记录列表中",
    L"选中的记录已从历史中删除",
    L"选中的内容已成功复制到剪贴板",
    L"主窗口已置顶显示在最前",
    L"主窗口已取消置顶显示",
    L"等%d个文件",
    L"图像预览 - 点击或按ESC关闭",
    L"在资源管理器中选中",
    L"快捷粘贴已执行",
    L"快捷键设置",
    L"设置已更新",
    L"快捷键设置已保存并立即生效",
    L"快捷键注册失败，请检查按键冲突",
    L"消息通知已启用，将显示操作提示",
    L"点击仅显示此应用的记录",
    L"点击仅显示此日期的记录",

    // 托盘暂停/恢复
    L"暂停监听",
    L"恢复监听",
    L"剪贴板监听已暂停",
    L"剪贴板监听已恢复",

    // 托盘启用/关闭快捷键（本 app 所有）
    L"启用快捷键",
    L"关闭快捷键",
    L"快捷键已启用",
    L"快捷键已关闭",

    // 首次运行用户协议弹窗
    L"用户协议",
    L"请阅读并同意协议后开始使用",
    L"本软件仅供合法用途，使用者需妥善保管剪贴板数据。\r\n"
    L"剪贴板可能包含敏感信息，因系统故障、误操作或意外情况可能导致数据丢失，"
    L"作者不承担任何由此造成的损失。\r\n"
    L"点击\"同意\" 即视为您已阅读并接受上述约定。",
    L"同意",
    L"拒绝",
    L"安装及使用本软件即视为同意上述协议",
};

static const wchar_t *kStringsEn[STR_COUNT] = {
    L"Smart Clip",
    L"Settings",
    L"General",
    L"Hotkeys",
    L"Data",
    L"Smart Actions",
    L"Basics and appearance",
    L"Hotkey configuration",
    L"Run actions based on content",
    L"Notifications",
    L"Show system notifications for actions",
    L"Smooth scrolling",
    L"Use smooth scrolling in the list",
    L"Show scrollbar",
    L"Show the floating right scrollbar with drag support",
    L"Hide delay",
    L"Set how long the scrollbar stays visible after stopping",
    L"Theme mode",
    L"Switch between light, dark, or follow system",
    L"Theme style",
    L"Switch the built-in theme palette",
    L"Language",
    L"Switch the app display language",
    L"Image preview quality",
    L"Set the clarity of clipboard image previews",
    L"Show in taskbar",
    L"Show the window icon in the taskbar",
    L"History limit",
    L"Maximum number of clipboard history items",
    L"Toggle hotkey",
    L"Show or hide the Smart Clip window",
    L"Search hotkey",
    L"Focus the search box",
    L"Quick paste",
    L"Paste quickly with modifier plus number",
    L"Quick paste modifier",
    L"Choose the modifier for quick paste",
    L"Favorite hotkey modifier",
    L"Choose the modifier for favorite shortcuts",
    L"Disk usage",
    L"Paste count",
    L"Set data directory",
    L"Clear non-favorites",
    L"Delete all non-favorite history records",
    L"Clean invalid images",
    L"Remove records whose original image files are missing",
    L"Export data",
    L"Export all history records to a ZIP archive",
    L"Text size limit",
    L"Skip text larger than this (KB)",
    L"Light",
    L"Dark",
    L"Follow system",
    L"Classic",
    L"Graphite",
    L"Warm Paper",
    L"High Contrast",
    L"简体中文",
    L"English",
    L"日本語",
    L"한국어",
    L"Deutsch",
    L"العربية",
    L"Türkçe",
    L"Off",
    L"Blur",
    L"SD",
    L"HD",
    L"All",
    L"Text",
    L"Image",
    L"File",
    L"Favorite",
    L"Type to search",
    L"Pin",
    L"Unpin",
    L"Batch Edit",
    L"Dark",
    L"Pin",
    L"Batch Edit",
    L"Dark mode",
    L"Previous page",
    L"Next page",
    L"Click to show, right-click to edit",
    L"Minimize",
    L"Maximize",
    L"Close",
    L"Hint",
    L"Settings",
    L"Notifications",
    L"Light mode",
    L"Dark mode",
    L"Exit",
    L"Restart",
    L"Settings updated",
    L"Notifications enabled",
    L"Smart Clip",
    L"Pasted",
    L"About",
    L"Version info and credits",
    L"Launch at startup",
    L"Run automatically when the system starts",
    L"Version",
    L"Application version number",
    L"Credits",
    L"Thanks to those who helped this project",
    L"Thanks to everyone who supports SmartClip:\n北一, Sulla vetta la Saovia, "
    L"Lion, 陈随易",
    L"Agreement",
    L"End user license agreement and privacy policy",
    L"End User License Agreement",
    L"Privacy Policy",
    L"Installing and using this software constitutes acceptance of the above "
    L"agreements",
    L"End User License Agreement",
    L"SmartClip End User License Agreement\r\n\r\n"
    L"1. Grant of License\r\n"
    L"This software grants you a non-exclusive, non-transferable license to "
    L"use this software in accordance with the documentation.\r\n\r\n"
    L"2. Use Restrictions\r\n"
    L"You may not decompile, disassemble, or otherwise reverse engineer this "
    L"software; you may not use this software for any illegal purpose.\r\n\r\n"
    L"3. Intellectual Property\r\n"
    L"All intellectual property rights in this software and its copies belong "
    L"to the developer. This license does not grant you the right to use the "
    L"trademarks or logos associated with this software.\r\n\r\n"
    L"4. Disclaimer and Data Safekeeping\r\n"
    L"Users must properly safeguard their clipboard data. Clipboard content "
    L"may "
    L"contain sensitive information. System failures, accidental operations, "
    L"or "
    L"unexpected issues may cause data loss. The developer is not liable for "
    L"any "
    L"losses or data loss caused by the use of this software.\r\n"
    L"This software uses a SQLite3 database to store records, but the database "
    L"is not encrypted. Recorded content may be illegally obtained and viewed. "
    L"Please keep the data directory files safe (can be viewed under Settings "
    L"- "
    L"Data).\r\n\r\n"
    L"5. Termination\r\n"
    L"If you violate any term of this license agreement, the license will "
    L"automatically terminate and you must immediately stop using and destroy "
    L"all copies of this software.\r\n\r\n"
    L"Installing and using this software constitutes acceptance of the above "
    L"agreement.",
    L"Privacy Policy",
    L"SmartClip Privacy Policy\r\n\r\n"
    L"1. Data Storage\r\n"
    L"Your clipboard history is stored only on your local computer and is not "
    L"uploaded to any server.\r\n\r\n"
    L"2. Data Collection\r\n"
    L"This software does not collect your personal information. Clipboard "
    L"content is only pasted or shared when you actively use it.\r\n\r\n"
    L"3. Third-Party Services\r\n"
    L"This software does not use any third-party analytics services or ad "
    L"SDKs.\r\n\r\n"
    L"4. Data Security\r\n"
    L"We recommend that you regularly clean up clipboard history you no longer "
    L"need to protect sensitive information. Users must properly safeguard "
    L"their "
    L"clipboard data. System failures, accidental operations, or unexpected "
    L"issues may cause data loss; the developer is not liable for such "
    L"loss.\r\n\r\n"
    L"5. Privacy Policy Updates\r\n"
    L"We may update this privacy policy from time to time. The updated policy "
    L"will be displayed in the software.\r\n\r\n"
    L"Installing and using this software constitutes acceptance of the above "
    L"privacy policy.",

    L"Favorite hotkey",
    L" times",
    L"Select",
    L"Clean",
    L"Export",
    L"Import data",
    L"Restore history and settings from ZIP backup",
    L"Import",
    L"Data backup",
    L"Export or import your data",
    L"Clean Now",
    L"Select data storage directory",
    L"Clear non-favorites",
    L"This operation will delete all non-favorite history records and cannot "
    L"be undone.\nAre you sure you want to continue?",
    L"Clean invalid image records",
    L"This operation will delete all records with missing original images and "
    L"cannot be undone.\nAre you sure you want to continue?",
    L"Hint",
    L"Non-favorite data cleared",
    L"Invalid image records cleared",

    L"Confirm migration",
    L"Move data to:\n%s\\SmartClip\n\nConfirm migration?",
    L"Cancel",

    L"Delete all non-favorite records",
    L"Keep only your favorites",
    L"This will remove all non-favorite history records. This action cannot be "
    L"undone.",

    L"Clean records with missing images",
    L"Only delete invalid image items",
    L"This will remove records whose original image files no longer exist. "
    L"This action cannot be undone.",

    L"Ctrl",
    L"Alt",
    L"Shift",
    L"Win",
    L"None",
    L"Ctrl+Alt",
    L"Ctrl+Shift",
    L"Alt+Shift",
    L"Ctrl+Alt+Shift",

    // Context menu
    L"Copy",
    L"Paste",
    L"Tags",
    L"Open location",
    L"Delete",
    L"Batch add tag",
    L"Batch delete",
    L"No tags available",

    // Delete favorite confirmation dialog
    L"Delete Favorite",
    L"Remove this favorite",
    L"This favorite will be removed from history",
    L"This item is favorited. This action cannot be undone.",
    L"Delete",

    // Batch operation notifications
    L"Batch deleted",
    L"Batch tagged",

    // Hotkey placeholder
    L"Press a hotkey",

    // GitHub repository link
    L"Repository",

    // Notification messages
    L"New clipboard content recorded to history",
    L"Copied to SmartClip",
    L"Clipboard image copied and recorded to history",
    L"File path copied and recorded to history",
    L"Image file added to history list",
    L"Selected record deleted from history",
    L"Selected content copied to clipboard",
    L"Main window pinned on top",
    L"Main window unpinned from top",
    L"and %d more files",
    L"Image Preview - Click or press ESC to close",
    L"Select in File Explorer",
    L"Quick paste executed",
    L"Hotkey settings",
    L"Settings updated",
    L"Hotkey settings saved and applied immediately",
    L"Hotkey registration failed, check key conflicts",
    L"Notifications enabled, operation tips will be shown",
    L"Click to filter by this app",
    L"Click to filter by this date",

    // Tray pause/resume
    L"Pause listening",
    L"Resume listening",
    L"Clipboard listening paused",
    L"Clipboard listening resumed",

    // Tray quick paste toggle
    L"Enable hotkeys",
    L"Disable hotkeys",
    L"Hotkeys enabled",
    L"Hotkeys disabled",

    // First-run user agreement dialog
    L"User Agreement",
    L"Please read and accept the agreement to start using",
    L"This software is for lawful purposes only. Users must properly safeguard "
    L"their clipboard data.\r\n"
    L"Clipboard content may contain sensitive information. System failures, "
    L"accidental operations, or unexpected issues may cause data loss. The "
    L"author is not liable for any resulting damages.\r\n"
    L"Clicking \"Accept\" means you have read and accepted the above terms.",
    L"Accept",
    L"Decline",
    L"Installing and using this software constitutes acceptance of the above "
    L"agreement",
};

// StringId 名称到枚举值的映射（用于解析 ini 文件）
struct StringIdEntry {
  const char *name;
  StringId id;
};

static const StringIdEntry kStringIdMap[] = {
    {"STR_APP_TITLE", STR_APP_TITLE},
    {"STR_SETTINGS_TITLE", STR_SETTINGS_TITLE},
    {"STR_SETTINGS_GENERAL", STR_SETTINGS_GENERAL},
    {"STR_SETTINGS_HOTKEY", STR_SETTINGS_HOTKEY},
    {"STR_SETTINGS_DATA", STR_SETTINGS_DATA},
    {"STR_SETTINGS_SMART_ACTION", STR_SETTINGS_SMART_ACTION},
    {"STR_SETTINGS_GENERAL_DESC", STR_SETTINGS_GENERAL_DESC},
    {"STR_SETTINGS_HOTKEY_DESC", STR_SETTINGS_HOTKEY_DESC},
    {"STR_SETTINGS_SMART_ACTION_DESC", STR_SETTINGS_SMART_ACTION_DESC},
    {"STR_ROW_NOTIFICATION", STR_ROW_NOTIFICATION},
    {"STR_ROW_NOTIFICATION_DESC", STR_ROW_NOTIFICATION_DESC},
    {"STR_ROW_SMOOTH_SCROLL", STR_ROW_SMOOTH_SCROLL},
    {"STR_ROW_SMOOTH_SCROLL_DESC", STR_ROW_SMOOTH_SCROLL_DESC},
    {"STR_ROW_SCROLLBAR", STR_ROW_SCROLLBAR},
    {"STR_ROW_SCROLLBAR_DESC", STR_ROW_SCROLLBAR_DESC},
    {"STR_ROW_SCROLLBAR_DELAY", STR_ROW_SCROLLBAR_DELAY},
    {"STR_ROW_SCROLLBAR_DELAY_DESC", STR_ROW_SCROLLBAR_DELAY_DESC},
    {"STR_ROW_THEME_MODE", STR_ROW_THEME_MODE},
    {"STR_ROW_THEME_MODE_DESC", STR_ROW_THEME_MODE_DESC},
    {"STR_ROW_THEME_STYLE", STR_ROW_THEME_STYLE},
    {"STR_ROW_THEME_STYLE_DESC", STR_ROW_THEME_STYLE_DESC},
    {"STR_ROW_LANGUAGE", STR_ROW_LANGUAGE},
    {"STR_ROW_LANGUAGE_DESC", STR_ROW_LANGUAGE_DESC},
    {"STR_ROW_IMAGE_PREVIEW", STR_ROW_IMAGE_PREVIEW},
    {"STR_ROW_IMAGE_PREVIEW_DESC", STR_ROW_IMAGE_PREVIEW_DESC},
    {"STR_ROW_TASKBAR", STR_ROW_TASKBAR},
    {"STR_ROW_TASKBAR_DESC", STR_ROW_TASKBAR_DESC},
    {"STR_ROW_HISTORY_LIMIT", STR_ROW_HISTORY_LIMIT},
    {"STR_ROW_HISTORY_LIMIT_DESC", STR_ROW_HISTORY_LIMIT_DESC},
    {"STR_ROW_HOTKEY_TOGGLE", STR_ROW_HOTKEY_TOGGLE},
    {"STR_ROW_HOTKEY_TOGGLE_DESC", STR_ROW_HOTKEY_TOGGLE_DESC},
    {"STR_ROW_HOTKEY_SEARCH", STR_ROW_HOTKEY_SEARCH},
    {"STR_ROW_HOTKEY_SEARCH_DESC", STR_ROW_HOTKEY_SEARCH_DESC},
    {"STR_ROW_QUICK_PASTE", STR_ROW_QUICK_PASTE},
    {"STR_ROW_QUICK_PASTE_DESC", STR_ROW_QUICK_PASTE_DESC},
    {"STR_ROW_QUICK_PASTE_MOD", STR_ROW_QUICK_PASTE_MOD},
    {"STR_ROW_QUICK_PASTE_MOD_DESC", STR_ROW_QUICK_PASTE_MOD_DESC},
    {"STR_ROW_FAVORITE_HOTKEY", STR_ROW_FAVORITE_HOTKEY},
    {"STR_ROW_FAVORITE_HOTKEY_DESC", STR_ROW_FAVORITE_HOTKEY_DESC},
    {"STR_ROW_DATA_SIZE", STR_ROW_DATA_SIZE},
    {"STR_ROW_PASTE_COUNT", STR_ROW_PASTE_COUNT},
    {"STR_ROW_SET_DATA_DIR", STR_ROW_SET_DATA_DIR},
    {"STR_ROW_CLEAR_NON_FAV", STR_ROW_CLEAR_NON_FAV},
    {"STR_ROW_CLEAR_NON_FAV_DESC", STR_ROW_CLEAR_NON_FAV_DESC},
    {"STR_ROW_CLEAN_INVALID_IMAGES", STR_ROW_CLEAN_INVALID_IMAGES},
    {"STR_ROW_CLEAN_INVALID_IMAGES_DESC", STR_ROW_CLEAN_INVALID_IMAGES_DESC},
    {"STR_ROW_EXPORT_DATA", STR_ROW_EXPORT_DATA},
    {"STR_ROW_EXPORT_DATA_DESC", STR_ROW_EXPORT_DATA_DESC},
    {"STR_ROW_TEXT_SIZE_LIMIT", STR_ROW_TEXT_SIZE_LIMIT},
    {"STR_ROW_TEXT_SIZE_LIMIT_DESC", STR_ROW_TEXT_SIZE_LIMIT_DESC},
    {"STR_THEME_LIGHT", STR_THEME_LIGHT},
    {"STR_THEME_DARK", STR_THEME_DARK},
    {"STR_THEME_SYSTEM", STR_THEME_SYSTEM},
    {"STR_THEME_STYLE_CLASSIC", STR_THEME_STYLE_CLASSIC},
    {"STR_THEME_STYLE_GRAPHITE", STR_THEME_STYLE_GRAPHITE},
    {"STR_THEME_STYLE_WARM", STR_THEME_STYLE_WARM},
    {"STR_THEME_STYLE_HIGH_CONTRAST", STR_THEME_STYLE_HIGH_CONTRAST},
    {"STR_LANGUAGE_ZH_CN", STR_LANGUAGE_ZH_CN},
    {"STR_LANGUAGE_EN_US", STR_LANGUAGE_EN_US},
    {"STR_LANGUAGE_JA_JP", STR_LANGUAGE_JA_JP},
    {"STR_LANGUAGE_KO_KR", STR_LANGUAGE_KO_KR},
    {"STR_LANGUAGE_DE_DE", STR_LANGUAGE_DE_DE},
    {"STR_LANGUAGE_AR_SA", STR_LANGUAGE_AR_SA},
    {"STR_LANGUAGE_TR_TR", STR_LANGUAGE_TR_TR},
    {"STR_PREVIEW_OFF", STR_PREVIEW_OFF},
    {"STR_PREVIEW_BLUR", STR_PREVIEW_BLUR},
    {"STR_PREVIEW_SD", STR_PREVIEW_SD},
    {"STR_PREVIEW_HD", STR_PREVIEW_HD},
    {"STR_FILTER_ALL", STR_FILTER_ALL},
    {"STR_FILTER_TEXT", STR_FILTER_TEXT},
    {"STR_FILTER_IMAGE", STR_FILTER_IMAGE},
    {"STR_FILTER_FILE", STR_FILTER_FILE},
    {"STR_FILTER_FAVORITE", STR_FILTER_FAVORITE},
    {"STR_SEARCH_PLACEHOLDER", STR_SEARCH_PLACEHOLDER},
    {"STR_BTN_PIN", STR_BTN_PIN},
    {"STR_BTN_UNPIN", STR_BTN_UNPIN},
    {"STR_BTN_BATCH_EDIT", STR_BTN_BATCH_EDIT},
    {"STR_BTN_DARKMODE", STR_BTN_DARKMODE},
    {"STR_TOOLTIP_PIN", STR_TOOLTIP_PIN},
    {"STR_TOOLTIP_BATCH_EDIT", STR_TOOLTIP_BATCH_EDIT},
    {"STR_TOOLTIP_DARKMODE", STR_TOOLTIP_DARKMODE},
    {"STR_TOOLTIP_PREV_PAGE", STR_TOOLTIP_PREV_PAGE},
    {"STR_TOOLTIP_NEXT_PAGE", STR_TOOLTIP_NEXT_PAGE},
    {"STR_TOOLTIP_FAVORITE_FILTER", STR_TOOLTIP_FAVORITE_FILTER},
    {"STR_TOOLTIP_MINIMIZE", STR_TOOLTIP_MINIMIZE},
    {"STR_TOOLTIP_MAXIMIZE", STR_TOOLTIP_MAXIMIZE},
    {"STR_TOOLTIP_CLOSE", STR_TOOLTIP_CLOSE},
    {"STR_TRAY_HINT_TITLE", STR_TRAY_HINT_TITLE},
    {"STR_TRAY_MENU_SETTINGS", STR_TRAY_MENU_SETTINGS},
    {"STR_TRAY_MENU_NOTIFICATIONS", STR_TRAY_MENU_NOTIFICATIONS},
    {"STR_TRAY_MENU_LIGHT", STR_TRAY_MENU_LIGHT},
    {"STR_TRAY_MENU_DARK", STR_TRAY_MENU_DARK},
    {"STR_TRAY_MENU_EXIT", STR_TRAY_MENU_EXIT},
    {"STR_TRAY_NOTIFY_UPDATED", STR_TRAY_NOTIFY_UPDATED},
    {"STR_TRAY_NOTIFICATIONS_ENABLED", STR_TRAY_NOTIFICATIONS_ENABLED},
    {"STR_TRAY_TIP", STR_TRAY_TIP},
    {"STR_TRAY_PASTED", STR_TRAY_PASTED},
    {"STR_SETTINGS_ABOUT", STR_SETTINGS_ABOUT},
    {"STR_SETTINGS_ABOUT_DESC", STR_SETTINGS_ABOUT_DESC},
    {"STR_ROW_STARTUP", STR_ROW_STARTUP},
    {"STR_ROW_STARTUP_DESC", STR_ROW_STARTUP_DESC},
    {"STR_ROW_VERSION", STR_ROW_VERSION},
    {"STR_ROW_VERSION_DESC", STR_ROW_VERSION_DESC},
    {"STR_ROW_CREDITS", STR_ROW_CREDITS},
    {"STR_ROW_CREDITS_DESC", STR_ROW_CREDITS_DESC},
    {"STR_CREDITS_TOOLTIP", STR_CREDITS_TOOLTIP},
    {"STR_ROW_AGREEMENT", STR_ROW_AGREEMENT},
    {"STR_ROW_AGREEMENT_DESC", STR_ROW_AGREEMENT_DESC},
    {"STR_EULA", STR_EULA},
    {"STR_PRIVACY_POLICY", STR_PRIVACY_POLICY},
    {"STR_AGREEMENT_NOTE", STR_AGREEMENT_NOTE},
    {"STR_EULA_TITLE", STR_EULA_TITLE},
    {"STR_EULA_BODY", STR_EULA_BODY},
    {"STR_PRIVACY_TITLE", STR_PRIVACY_TITLE},
    {"STR_PRIVACY_BODY", STR_PRIVACY_BODY},
    {"STR_TRAY_FAVORITE_HOTKEY", STR_TRAY_FAVORITE_HOTKEY},
    {"STR_PASTE_COUNT_SUFFIX", STR_PASTE_COUNT_SUFFIX},
    {"STR_BTN_SELECT", STR_BTN_SELECT},
    {"STR_BTN_CLEAN", STR_BTN_CLEAN},
    {"STR_BTN_EXPORT", STR_BTN_EXPORT},
    {"STR_ROW_IMPORT_DATA", STR_ROW_IMPORT_DATA},
    {"STR_ROW_IMPORT_DATA_DESC", STR_ROW_IMPORT_DATA_DESC},
    {"STR_BTN_IMPORT", STR_BTN_IMPORT},
    {"STR_DATA_BACKUP_TITLE", STR_DATA_BACKUP_TITLE},
    {"STR_DATA_BACKUP_DESC", STR_DATA_BACKUP_DESC},
    {"STR_BTN_CONFIRM_CLEAR", STR_BTN_CONFIRM_CLEAR},
    {"STR_DLG_SELECT_DATA_DIR", STR_DLG_SELECT_DATA_DIR},
    {"STR_DLG_CLEAR_NON_FAV_TITLE", STR_DLG_CLEAR_NON_FAV_TITLE},
    {"STR_DLG_CLEAR_NON_FAV_MSG", STR_DLG_CLEAR_NON_FAV_MSG},
    {"STR_DLG_CLEAN_INVALID_IMAGES_TITLE", STR_DLG_CLEAN_INVALID_IMAGES_TITLE},
    {"STR_DLG_CLEAN_INVALID_IMAGES_MSG", STR_DLG_CLEAN_INVALID_IMAGES_MSG},
    {"STR_TRAY_HINT", STR_TRAY_HINT},
    {"STR_TRAY_CLEARED_NON_FAV", STR_TRAY_CLEARED_NON_FAV},
    {"STR_TRAY_CLEARED_INVALID_IMAGES", STR_TRAY_CLEARED_INVALID_IMAGES},
    {"STR_DLG_CONFIRM_MIGRATE_TITLE", STR_DLG_CONFIRM_MIGRATE_TITLE},
    {"STR_DLG_CONFIRM_MIGRATE_MSG", STR_DLG_CONFIRM_MIGRATE_MSG},
    {"STR_DLG_CONFIRM_CANCEL", STR_DLG_CONFIRM_CANCEL},
    {"STR_DLG_CLEAR_NON_FAV_SUBTITLE", STR_DLG_CLEAR_NON_FAV_SUBTITLE},
    {"STR_DLG_CLEAR_NON_FAV_SUB_DESC", STR_DLG_CLEAR_NON_FAV_SUB_DESC},
    {"STR_DLG_CLEAR_NON_FAV_DESC", STR_DLG_CLEAR_NON_FAV_DESC},
    {"STR_DLG_CLEAN_INVALID_IMAGES_SUBTITLE",
     STR_DLG_CLEAN_INVALID_IMAGES_SUBTITLE},
    {"STR_DLG_CLEAN_INVALID_IMAGES_SUB_DESC",
     STR_DLG_CLEAN_INVALID_IMAGES_SUB_DESC},
    {"STR_DLG_CLEAN_INVALID_IMAGES_DESC", STR_DLG_CLEAN_INVALID_IMAGES_DESC},
    {"STR_MOD_CTRL", STR_MOD_CTRL},
    {"STR_MOD_ALT", STR_MOD_ALT},
    {"STR_MOD_SHIFT", STR_MOD_SHIFT},
    {"STR_MOD_WIN", STR_MOD_WIN},
    {"STR_MOD_NONE", STR_MOD_NONE},
    {"STR_MOD_CTRL_ALT", STR_MOD_CTRL_ALT},
    {"STR_MOD_CTRL_SHIFT", STR_MOD_CTRL_SHIFT},
    {"STR_MOD_ALT_SHIFT", STR_MOD_ALT_SHIFT},
    {"STR_MOD_CTRL_ALT_SHIFT", STR_MOD_CTRL_ALT_SHIFT},
    {"STR_CTX_COPY", STR_CTX_COPY},
    {"STR_CTX_PASTE", STR_CTX_PASTE},
    {"STR_CTX_TAG", STR_CTX_TAG},
    {"STR_CTX_OPEN_LOCATION", STR_CTX_OPEN_LOCATION},
    {"STR_CTX_DELETE", STR_CTX_DELETE},
    {"STR_CTX_BATCH_ADD_TAG", STR_CTX_BATCH_ADD_TAG},
    {"STR_CTX_BATCH_DELETE", STR_CTX_BATCH_DELETE},
    {"STR_CTX_NO_TAGS", STR_CTX_NO_TAGS},
    {"STR_DLG_DELETE_FAV_TITLE", STR_DLG_DELETE_FAV_TITLE},
    {"STR_DLG_DELETE_FAV_SUBTITLE", STR_DLG_DELETE_FAV_SUBTITLE},
    {"STR_DLG_DELETE_FAV_BODY1", STR_DLG_DELETE_FAV_BODY1},
    {"STR_DLG_DELETE_FAV_BODY2", STR_DLG_DELETE_FAV_BODY2},
    {"STR_DLG_DELETE_FAV_CONFIRM", STR_DLG_DELETE_FAV_CONFIRM},
    {"STR_TRAY_BATCH_DELETED", STR_TRAY_BATCH_DELETED},
    {"STR_TRAY_BATCH_TAGGED", STR_TRAY_BATCH_TAGGED},
    {"STR_HOTKEY_PLACEHOLDER", STR_HOTKEY_PLACEHOLDER},
    {"STR_GITHUB_REPO", STR_GITHUB_REPO},
    {"STR_TRAY_NEW_CONTENT", STR_TRAY_NEW_CONTENT},
    {"STR_TRAY_COPY_TITLE", STR_TRAY_COPY_TITLE},
    {"STR_TRAY_IMAGE_COPIED", STR_TRAY_IMAGE_COPIED},
    {"STR_TRAY_FILE_PATH_COPIED", STR_TRAY_FILE_PATH_COPIED},
    {"STR_TRAY_IMAGE_ADDED", STR_TRAY_IMAGE_ADDED},
    {"STR_TRAY_DELETED", STR_TRAY_DELETED},
    {"STR_TRAY_COPIED", STR_TRAY_COPIED},
    {"STR_TRAY_PINNED", STR_TRAY_PINNED},
    {"STR_TRAY_UNPINNED", STR_TRAY_UNPINNED},
    {"STR_MULTI_FILES_FMT", STR_MULTI_FILES_FMT},
    {"STR_IMAGE_PREVIEW_TITLE", STR_IMAGE_PREVIEW_TITLE},
    {"STR_CTX_SELECT_IN_EXPLORER", STR_CTX_SELECT_IN_EXPLORER},
    {"STR_TRAY_QUICK_PASTE_TITLE", STR_TRAY_QUICK_PASTE_TITLE},
    {"STR_TRAY_HOTKEY_SETTINGS", STR_TRAY_HOTKEY_SETTINGS},
    {"STR_TRAY_SETTINGS_UPDATED", STR_TRAY_SETTINGS_UPDATED},
    {"STR_TRAY_HOTKEY_SAVED", STR_TRAY_HOTKEY_SAVED},
    {"STR_TRAY_HOTKEY_FAILED", STR_TRAY_HOTKEY_FAILED},
    {"STR_TRAY_NOTIFY_ENABLED", STR_TRAY_NOTIFY_ENABLED},
    {"STR_TOOLTIP_FILTER_BY_APP", STR_TOOLTIP_FILTER_BY_APP},
    {"STR_TOOLTIP_FILTER_BY_DATE", STR_TOOLTIP_FILTER_BY_DATE},
    {"STR_TRAY_MENU_PAUSE", STR_TRAY_MENU_PAUSE},
    {"STR_TRAY_MENU_RESUME", STR_TRAY_MENU_RESUME},
    {"STR_TRAY_PAUSED", STR_TRAY_PAUSED},
    {"STR_TRAY_RESUMED", STR_TRAY_RESUMED},
    {"STR_TRAY_MENU_QUICK_PASTE_ENABLE", STR_TRAY_MENU_QUICK_PASTE_ENABLE},
    {"STR_TRAY_MENU_QUICK_PASTE_DISABLE", STR_TRAY_MENU_QUICK_PASTE_DISABLE},
    {"STR_TRAY_QUICK_PASTE_ENABLED", STR_TRAY_QUICK_PASTE_ENABLED},
    {"STR_TRAY_QUICK_PASTE_DISABLED", STR_TRAY_QUICK_PASTE_DISABLED},
    {"STR_AGREEMENT_DIALOG_TITLE", STR_AGREEMENT_DIALOG_TITLE},
    {"STR_AGREEMENT_DIALOG_SUBTITLE", STR_AGREEMENT_DIALOG_SUBTITLE},
    {"STR_AGREEMENT_DIALOG_BODY", STR_AGREEMENT_DIALOG_BODY},
    {"STR_AGREEMENT_DIALOG_ACCEPT", STR_AGREEMENT_DIALOG_ACCEPT},
    {"STR_AGREEMENT_DIALOG_DECLINE", STR_AGREEMENT_DIALOG_DECLINE},
    {"STR_AGREEMENT_DIALOG_REMIND", STR_AGREEMENT_DIALOG_REMIND},
    {NULL, STR_COUNT},
};

static const int kStringIdMapSize =
    sizeof(kStringIdMap) / sizeof(kStringIdMap[0]) - 1;

// 外部语言数据
static std::map<int, std::map<StringId, std::wstring>> g_externalStrings;
static std::vector<LanguageInfo> g_availableLanguages;
static bool g_languagesLoaded = false;

// 将 ini 文件中的转义序列 \r\n, \n, \t 转换为实际字符
static std::wstring UnescapeString(const std::wstring &s) {
  std::wstring result;
  result.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == L'\\' && i + 1 < s.size()) {
      switch (s[i + 1]) {
      case L'r':
        result += L'\r';
        ++i;
        break;
      case L'n':
        result += L'\n';
        ++i;
        break;
      case L't':
        result += L'\t';
        ++i;
        break;
      case L'\\':
        result += L'\\';
        ++i;
        break;
      default:
        result += s[i];
        break;
      }
    } else {
      result += s[i];
    }
  }
  return result;
}

// 获取程序所在目录
static std::wstring GetExeDirectory() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(NULL, path, MAX_PATH);
  std::wstring p(path);
  size_t pos = p.find_last_of(L"\\/");
  if (pos != std::wstring::npos)
    return p.substr(0, pos);
  return L".";
}

// 解析单个语言文件
static bool LoadLanguageFile(const std::wstring &filePath, AppLanguage lang) {
  HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE)
    return false;

  DWORD fileSize = GetFileSize(hFile, NULL);
  if (fileSize == 0 || fileSize > 1024 * 1024) {
    CloseHandle(hFile);
    return false;
  }

  // 读取文件内容（UTF-8）
  std::string utf8Content(fileSize, '\0');
  DWORD bytesRead = 0;
  ReadFile(hFile, &utf8Content[0], fileSize, &bytesRead, NULL);
  CloseHandle(hFile);
  utf8Content.resize(bytesRead);

  // 跳过 BOM
  if (utf8Content.size() >= 3 && utf8Content[0] == '\xEF' &&
      utf8Content[1] == '\xBB' && utf8Content[2] == '\xBF') {
    utf8Content = utf8Content.substr(3);
  }

  // 转换为 UTF-16
  int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Content.c_str(), -1, NULL, 0);
  if (wlen <= 0)
    return false;
  std::wstring content(wlen, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8Content.c_str(), -1, &content[0], wlen);
  if (!content.empty() && content.back() == L'\0')
    content.pop_back();

  // 解析 INI 内容
  LanguageInfo info = {};
  info.lang = lang;
  info.isExternal = true;
  info.isRtl = false;

  std::map<StringId, std::wstring> strings;
  bool inStringsSection = false;

  // 逐行解析
  size_t pos = 0;
  while (pos < content.size()) {
    size_t lineEnd = content.find(L'\n', pos);
    if (lineEnd == std::wstring::npos)
      lineEnd = content.size();
    std::wstring line = content.substr(pos, lineEnd - pos);
    pos = lineEnd + 1;

    // 去除行首尾空白和 \r
    size_t start = line.find_first_not_of(L" \t\r");
    if (start == std::wstring::npos)
      continue;
    size_t end = line.find_last_not_of(L" \t\r");
    line = line.substr(start, end - start + 1);

    if (line.empty() || line[0] == L'#' || line[0] == L';')
      continue;

    if (line[0] == L'[' && line.back() == L']') {
      std::wstring section = line.substr(1, line.size() - 2);
      inStringsSection = (section == L"strings");
      continue;
    }

    size_t eqPos = line.find(L'=');
    if (eqPos == std::wstring::npos)
      continue;

    std::wstring key = line.substr(0, eqPos);
    std::wstring value = line.substr(eqPos + 1);

    // 去除 key 的空白
    size_t ks = key.find_first_not_of(L" \t");
    size_t ke = key.find_last_not_of(L" \t");
    if (ks != std::wstring::npos)
      key = key.substr(ks, ke - ks + 1);
    else
      continue;

    if (!inStringsSection) {
      // [info] 节
      if (key == L"name") {
        info.name = value;
      } else if (key == L"code") {
        info.code = value;
      } else if (key == L"rtl") {
        info.isRtl = (value == L"true" || value == L"1");
      }
      continue;
    }

    // [strings] 节：匹配 StringId
    std::string keyA(key.begin(), key.end());
    for (int i = 0; i < kStringIdMapSize; ++i) {
      if (strcmp(keyA.c_str(), kStringIdMap[i].name) == 0) {
        strings[kStringIdMap[i].id] = UnescapeString(value);
        break;
      }
    }
  }

  if (strings.empty()) {
    return false;
  }

  // 存储外部语言数据
  g_externalStrings[(int)lang] = strings;

  // 补全 LanguageInfo
  if (info.name.empty()) {
    info.name = GetLanguageCode(lang);
  }
  if (info.code.empty()) {
    info.code = GetLanguageCode(lang);
  }

  // 检查是否已存在，避免重复
  for (auto &existing : g_availableLanguages) {
    if (existing.lang == lang) {
      existing = info;
      return true;
    }
  }
  g_availableLanguages.push_back(info);
  return true;
}

void LoadExternalLanguages() {
  if (g_languagesLoaded)
    return;

  // 添加内置语言
  g_availableLanguages.push_back(
      {LANG_ZH_CN, L"zh-CN", L"简体中文", false, false});
  g_availableLanguages.push_back(
      {LANG_EN_US, L"en-US", L"English", false, false});

  // 外部语言文件映射
  struct ExternalLangEntry {
    AppLanguage lang;
    const wchar_t *fileName;
  };
  static const ExternalLangEntry externalLangs[] = {
      {LANG_JA_JP, L"ja-JP.ini"}, {LANG_KO_KR, L"ko-KR.ini"},
      {LANG_DE_DE, L"de-DE.ini"}, {LANG_AR_SA, L"ar-SA.ini"},
      {LANG_TR_TR, L"tr-TR.ini"},
  };

  std::wstring langDir = GetExeDirectory() + L"\\lang";

  for (const auto &entry : externalLangs) {
    std::wstring filePath = langDir + L"\\" + entry.fileName;
    LoadLanguageFile(filePath, entry.lang);
  }

  g_languagesLoaded = true;
}

const std::vector<LanguageInfo> &GetAvailableLanguages() {
  if (!g_languagesLoaded)
    LoadExternalLanguages();
  return g_availableLanguages;
}

bool IsRtlLanguage() {
  for (const auto &lang : g_availableLanguages) {
    if (lang.lang == g_appLanguage)
      return lang.isRtl;
  }
  return false;
}

const wchar_t *T(StringId id) {
  if (id < 0 || id >= STR_COUNT)
    return L"";

  // 优先检查外部语言
  if (g_appLanguage >= LANG_JA_JP) {
    auto it = g_externalStrings.find((int)g_appLanguage);
    if (it != g_externalStrings.end()) {
      auto strIt = it->second.find(id);
      if (strIt != it->second.end())
        return strIt->second.c_str();
    }
    // 外部语言缺失某个 key 时 fallback 到英文
    return kStringsEn[id];
  }

  return (g_appLanguage == LANG_EN_US) ? kStringsEn[id] : kStringsZh[id];
}

std::wstring GetLanguageCode(AppLanguage lang) {
  switch (lang) {
  case LANG_ZH_CN:
    return L"zh-CN";
  case LANG_EN_US:
    return L"en-US";
  case LANG_JA_JP:
    return L"ja-JP";
  case LANG_KO_KR:
    return L"ko-KR";
  case LANG_DE_DE:
    return L"de-DE";
  case LANG_AR_SA:
    return L"ar-SA";
  case LANG_TR_TR:
    return L"tr-TR";
  default:
    return L"en-US";
  }
}

void ApplyLanguage() {
  if (g_hwndMain && IsWindow(g_hwndMain)) {
    SetWindowTextW(g_hwndMain, T(STR_APP_TITLE));
    if (g_hwndFilterAll)
      SetWindowTextW(g_hwndFilterAll, T(STR_FILTER_ALL));
    if (g_hwndFilterText)
      SetWindowTextW(g_hwndFilterText, T(STR_FILTER_TEXT));
    if (g_hwndFilterImage)
      SetWindowTextW(g_hwndFilterImage, T(STR_FILTER_IMAGE));
    if (g_hwndFilterFile)
      SetWindowTextW(g_hwndFilterFile, T(STR_FILTER_FILE));
    if (g_hwndFilterFavorite)
      SetWindowTextW(g_hwndFilterFavorite, T(STR_FILTER_FAVORITE));
    struct {
      HWND hwnd;
    } btns[] = {g_hwndFilterAll, g_hwndFilterText, g_hwndFilterImage,
                g_hwndFilterFile, g_hwndFilterFavorite};
    for (auto &b : btns) {
      if (b.hwnd) {
        InvalidateRect(b.hwnd, NULL, FALSE);
        UpdateWindow(b.hwnd);
      }
    }
    if (g_hwndSearchBox) {
      InvalidateRect(g_hwndSearchBox, NULL, FALSE);
      UpdateWindow(g_hwndSearchBox);
    }
    InvalidateRect(g_hwndMain, NULL, TRUE);
  }

  if (g_hwndSettingsDlg && IsWindow(g_hwndSettingsDlg)) {
    SetWindowTextW(g_hwndSettingsDlg, T(STR_SETTINGS_TITLE));
    InvalidateRect(g_hwndSettingsDlg, NULL, TRUE);
  }
}

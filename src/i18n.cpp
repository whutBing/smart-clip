#include "i18n.h"
#include "history.h"
#include "settings.h"
#include <windows.h>

extern HWND g_hwndMain;
extern HWND g_hwndFilterAll;
extern HWND g_hwndFilterText;
extern HWND g_hwndFilterImage;
extern HWND g_hwndFilterFile;
extern HWND g_hwndFilterFavorite;
extern HWND g_hwndSearchBox;

AppLanguage g_appLanguage = LANG_ZH_CN;

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
    L"日间",
    L"夜间",
    L"跟随系统",
    L"经典",
    L"石墨",
    L"暖纸",
    L"高对比",
    L"简体中文",
    L"English",
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
    L"感谢所有支持 SmartClip 的朋友：\n北一、Sulla vetta la Saovia、Lion",
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
    L"4. 免责声明\r\n"
    L"本软件按\"现状\"提供，不提供任何形式的担保。开发者不对因使用本软件而造成"
    L"的任何损失负责。\r\n\r\n"
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
    L"我们建议您定期清理不需要的剪贴板历史记录，以保护敏感信息。\r\n\r\n"
    L"5. 隐私政策更新\r\n"
    L"我们可能会不时更新本隐私政策。更新后的政策将在软件中显示。\r\n\r\n"
    L"安装及使用本软件即视为同意上述隐私政策。",

    L"收藏快捷键",
    L"次",
    L"选择",
    L"清理",
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
    L"Light",
    L"Dark",
    L"Follow system",
    L"Classic",
    L"Graphite",
    L"Warm Paper",
    L"High Contrast",
    L"简体中文",
    L"English",
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
    L"Lion",
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
    L"4. Disclaimer\r\n"
    L"This software is provided \"as is\" without any warranty of any kind. "
    L"The developer is not liable for any losses caused by the use of this "
    L"software.\r\n\r\n"
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
    L"need to protect sensitive information.\r\n\r\n"
    L"5. Privacy Policy Updates\r\n"
    L"We may update this privacy policy from time to time. The updated policy "
    L"will be displayed in the software.\r\n\r\n"
    L"Installing and using this software constitutes acceptance of the above "
    L"privacy policy.",

    L"Favorite hotkey",
    L" times",
    L"Select",
    L"Clean",
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
};

const wchar_t *T(StringId id) {
  if (id < 0 || id >= STR_COUNT)
    return L"";
  return (g_appLanguage == LANG_EN_US) ? kStringsEn[id] : kStringsZh[id];
}

std::wstring GetLanguageCode(AppLanguage lang) {
  return (lang == LANG_EN_US) ? L"en-US" : L"zh-CN";
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
    // 显式刷新筛选按钮（OWNERDRAW 按钮在 SetWindowText 后不一定会触发重绘）
    // 使用 UpdateWindow 强制立即重绘，避免设置对话框遮挡时延迟刷新
    struct {
      HWND hwnd;
    } btns[] = {g_hwndFilterAll, g_hwndFilterText, g_hwndFilterImage,
                g_hwndFilterFile, g_hwndFilterFavorite};
    for (auto &b : btns) {
      if (b.hwnd) {
        InvalidateRect(b.hwnd, NULL, TRUE);
        UpdateWindow(b.hwnd);
      }
    }
    // 刷新搜索框（占位符在 WM_PAINT 中根据当前语言绘制，需要主动失效）
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

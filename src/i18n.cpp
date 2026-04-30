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
extern HWND g_hwndFilterPassword;

AppLanguage g_appLanguage = LANG_ZH_CN;

static const wchar_t *kStringsZh[STR_COUNT] = {
    L"Smart Clip",
    L"设置",
    L"通用",
    L"快捷键",
    L"数据",
    L"智能操作",
    L"密码",
    L"基本设置和外观",
    L"快捷键配置",
    L"根据内容自动执行操作",
    L"密码库保护设置",
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
    L"占用磁盘空间",
    L"粘贴次数",
    L"设置数据目录",
    L"清理非收藏数据",
    L"删除所有未收藏的历史记录",
    L"删除失效图片",
    L"清理原始图片已丢失的记录",
    L"开启密码保护",
    L"访问密码库时需要验证身份",
    L"认证方式",
    L"选择解锁密码库的验证方式",
    L"重置主密码",
    L"修改主密码（需验证旧密码）",
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
    L"密码",
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
    L"已粘贴"};

static const wchar_t *kStringsEn[STR_COUNT] = {
    L"Smart Clip",
    L"Settings",
    L"General",
    L"Hotkeys",
    L"Data",
    L"Smart Actions",
    L"Passwords",
    L"Basics and appearance",
    L"Hotkey configuration",
    L"Run actions based on content",
    L"Password vault protection",
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
    L"Disk usage",
    L"Paste count",
    L"Set data directory",
    L"Clear non-favorites",
    L"Delete all non-favorite history records",
    L"Clean invalid images",
    L"Remove records whose original image files are missing",
    L"Enable password protection",
    L"Require authentication before opening the password vault",
    L"Authentication method",
    L"Choose how to unlock the password vault",
    L"Reset master password",
    L"Change the master password after verifying the old one",
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
    L"Password",
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
    L"Pasted"};

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
    if (g_hwndFilterPassword)
      SetWindowTextW(g_hwndFilterPassword, T(STR_FILTER_PASSWORD));
    InvalidateRect(g_hwndMain, NULL, TRUE);
  }

  if (g_hwndSettingsDlg && IsWindow(g_hwndSettingsDlg)) {
    SetWindowTextW(g_hwndSettingsDlg, T(STR_SETTINGS_TITLE));
    InvalidateRect(g_hwndSettingsDlg, NULL, TRUE);
  }
}

#pragma once

namespace copyconfirm {
enum class Language { Chinese, English };

struct Labels {
    const wchar_t* copied;
    const wchar_t* pause;
    const wchar_t* resume;
    const wchar_t* autoStart;
    const wchar_t* exit;
    const wchar_t* runningTooltip;
    const wchar_t* pausedTooltip;
    const wchar_t* startupError;
    const wchar_t* autoStartError;
    const wchar_t* startupNotice;
    const wchar_t* fontFamily;
    float fontSize;
};

inline constexpr Labels ChineseLabels{
    L"已复制到剪贴板", L"暂停提示", L"恢复提示", L"开机自启动", L"退出",
    L"复制确认 · 正在运行", L"复制确认 · 未运行",
    L"无法启动复制确认。请关闭已有实例后重试。",
    L"无法更改开机自启动设置。",
    L"软件已经开始运行",
    L"Microsoft YaHei UI", 12.0f
};

inline constexpr Labels EnglishLabels{
    L"Copied to clipboard", L"Pause notifications", L"Resume notifications", L"Start with Windows", L"Exit",
    L"CopyConfirm · Running", L"CopyConfirm · Not running",
    L"Unable to start CopyConfirm. Close any existing instance and try again.",
    L"Unable to change the Start with Windows setting.",
    L"CopyConfirm is now running",
    L"Segoe UI", 13.0f
};

inline constexpr const Labels& Text(Language language) {
    return language == Language::English ? EnglishLabels : ChineseLabels;
}

struct PopupLabels {
    const wchar_t* menu;
    const wchar_t* title;
    const wchar_t* gap;
    const wchar_t* gapHelp;
    const wchar_t* duration;
    const wchar_t* preview;
    const wchar_t* restore;
    const wchar_t* ok;
    const wchar_t* cancel;
    const wchar_t* invalidGap;
    const wchar_t* invalidDuration;
    const wchar_t* saveError;
    const wchar_t* previewError;
    const wchar_t* openError;
};

inline constexpr PopupLabels ChinesePopupLabels{
    L"提示设置…",
    L"提示设置",
    L"底边距离（0–300 像素）",
    L"按 100% 显示缩放计。任务栏在底部时，0 表示贴齐任务栏上沿。",
    L"弹窗时长（0.5–5.0 秒）",
    L"预览", L"恢复默认", L"确定", L"取消",
    L"请输入 0 到 300 之间的整数。",
    L"请输入 0.5 到 5.0 秒，最多一位小数。",
    L"无法保存设置。请确认软件所在文件夹可写入。",
    L"无法显示预览。",
    L"无法打开提示设置。"
};

inline constexpr PopupLabels EnglishPopupLabels{
    L"Popup Settings…",
    L"Popup Settings",
    L"Bottom Gap (0–300 px)",
    L"At 100% scaling. If the taskbar is at the bottom, 0 aligns the panel with its top edge.",
    L"Popup Duration (0.5–5.0 s)",
    L"Preview", L"Restore Defaults", L"OK", L"Cancel",
    L"Enter a whole number from 0 to 300.",
    L"Enter 0.5 to 5.0 seconds, with at most one decimal place.",
    L"Couldn't save the settings. Check that the app folder is writable.",
    L"Couldn't show the preview.",
    L"Couldn't open Popup Settings."
};

inline constexpr const PopupLabels& PopupText(Language language) {
    return language == Language::English ? EnglishPopupLabels : ChinesePopupLabels;
}
}

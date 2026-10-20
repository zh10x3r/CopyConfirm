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
    const wchar_t* fontFamily;
    float fontSize;
};

inline constexpr Labels ChineseLabels{
    L"已复制到剪贴板", L"暂停提示", L"恢复提示", L"开机自启动", L"退出",
    L"复制确认 · 正在运行", L"复制确认 · 未运行",
    L"无法启动复制确认。请关闭已有实例后重试。",
    L"无法更改开机自启动设置。",
    L"Microsoft YaHei UI", 12.0f
};

inline constexpr Labels EnglishLabels{
    L"Copied to clipboard", L"Pause notifications", L"Resume notifications", L"Start with Windows", L"Exit",
    L"CopyConfirm · Running", L"CopyConfirm · Not running",
    L"Unable to start HaveICopied. Close any existing instance and try again.",
    L"Unable to change the Start with Windows setting.",
    L"Segoe UI", 13.0f
};

inline constexpr const Labels& Text(Language language) {
    return language == Language::English ? EnglishLabels : ChineseLabels;
}
}

#pragma once

#include <windows.h>
#include <optional>
#include <string>
#include <string_view>

namespace copyconfirm {
struct PopupSettings {
    static constexpr int MinGap = 0;
    static constexpr int MaxGap = 300;
    static constexpr int MinTenths = 5;
    static constexpr int MaxTenths = 50;

    int bottomGap{32};
    int durationTenths{13};

    DWORD DurationMs() const { return static_cast<DWORD>(durationTenths * 100); }
    bool operator==(const PopupSettings&) const = default;
};

inline std::wstring_view TrimSpaces(std::wstring_view text) {
    constexpr auto spaces = L" \t\r\n";
    const auto first = text.find_first_not_of(spaces);
    if (first == text.npos) return {};
    return text.substr(first, text.find_last_not_of(spaces) - first + 1);
}

inline std::optional<int> ParseInteger(std::wstring_view text, int minimum, int maximum) {
    if (text.empty()) return {};
    int value{};
    for (const auto digit : text) {
        if (digit < L'0' || digit > L'9') return {};
        value = value * 10 + (digit - L'0');
        if (value > maximum) return {};
    }
    if (value < minimum) return {};
    return value;
}

inline std::optional<int> ParseBottomGap(std::wstring_view text) {
    return ParseInteger(TrimSpaces(text), PopupSettings::MinGap, PopupSettings::MaxGap);
}

inline std::optional<int> ParsePopupDuration(std::wstring_view text) {
    text = TrimSpaces(text);
    const auto separator = text.find_first_of(L".,");
    const auto whole = separator == 0 ? std::optional{0} :
        ParseInteger(text.substr(0, separator), 0, PopupSettings::MaxTenths / 10);
    if (!whole) return {};
    int tenths = *whole * 10;
    if (separator != text.npos) {
        const auto fraction = text.substr(separator + 1);
        if (fraction.size() != 1 || fraction.front() < L'0' || fraction.front() > L'9') return {};
        tenths += fraction.front() - L'0';
    }
    if (tenths < PopupSettings::MinTenths || tenths > PopupSettings::MaxTenths) return {};
    return tenths;
}

inline std::wstring FormatPopupDuration(int tenths) {
    return std::to_wstring(tenths / 10) + L"." + std::to_wstring(tenths % 10);
}

inline PopupSettings LoadPopupSettings(const std::wstring& path) {
    PopupSettings settings;
    if (path.empty()) return settings;
    const auto read = [&path](const wchar_t* key, int minimum, int maximum, int fallback) {
        wchar_t value[32]{};
        const DWORD length = GetPrivateProfileStringW(L"Popup", key, L"", value, _countof(value), path.c_str());
        if (length >= _countof(value) - 1) return fallback;
        return ParseInteger(TrimSpaces(std::wstring_view(value, length)), minimum, maximum).value_or(fallback);
    };
    settings.bottomGap = read(L"BottomGap", PopupSettings::MinGap, PopupSettings::MaxGap, settings.bottomGap);
    settings.durationTenths = read(L"DurationTenths", PopupSettings::MinTenths, PopupSettings::MaxTenths, settings.durationTenths);
    return settings;
}

inline bool SavePopupSettings(const std::wstring& path, PopupSettings settings) {
    if (path.empty()) return false;
    // Write both fields together. The language preference lives in [General].
    std::wstring values = L"BottomGap=" + std::to_wstring(settings.bottomGap);
    values.push_back(L'\0');
    values += L"DurationTenths=" + std::to_wstring(settings.durationTenths);
    values.push_back(L'\0');
    return WritePrivateProfileSectionW(L"Popup", values.c_str(), path.c_str()) != FALSE;
}
}

#pragma once

#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <functional>
#include <utility>
#include "localization.hpp"
#include "popup_settings.hpp"
#include "resource.h"

namespace copyconfirm {
class PopupSettingsDialog {
public:
    using Preview = std::function<bool(HWND, PopupSettings)>;

    PopupSettingsDialog(Language language, PopupSettings settings, const std::wstring& path, Preview preview)
        : labels_(PopupText(language)), settings_(settings), path_(path), preview_(std::move(preview)) {}

    INT_PTR Show(HINSTANCE instance, HWND owner) {
        const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_UPDOWN_CLASS};
        if (!InitCommonControlsEx(&controls)) return -1;
        return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_POPUP_SETTINGS), owner,
            DialogProc, reinterpret_cast<LPARAM>(this));
    }

    PopupSettings Settings() const { return settings_; }

private:
    static INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_INITDIALOG) {
            auto* self = reinterpret_cast<PopupSettingsDialog*>(lParam);
            SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
            self->Initialize(dialog);
            return TRUE;
        }
        auto* self = reinterpret_cast<PopupSettingsDialog*>(GetWindowLongPtrW(dialog, DWLP_USER));
        if (!self) return FALSE;
        if (message == WM_COMMAND) return self->Command(dialog, LOWORD(wParam), HIWORD(wParam));
        if (message == WM_NOTIFY) {
            const auto& notification = *reinterpret_cast<const NMHDR*>(lParam);
            if (notification.code == UDN_DELTAPOS &&
                (notification.idFrom == IDC_POPUP_GAP_SPIN || notification.idFrom == IDC_POPUP_DURATION_SPIN)) {
                self->Step(dialog, *reinterpret_cast<const NMUPDOWN*>(lParam));
                // The edit field is authoritative, including the fractional seconds.
                SetWindowLongPtrW(dialog, DWLP_MSGRESULT, 1);
                return TRUE;
            }
        }
        return FALSE;
    }

    void Initialize(HWND dialog) const {
        SetWindowTextW(dialog, labels_.title);
        SetDlgItemTextW(dialog, IDC_POPUP_GAP_LABEL, labels_.gap);
        SetDlgItemTextW(dialog, IDC_POPUP_GAP_HELP, labels_.gapHelp);
        SetDlgItemTextW(dialog, IDC_POPUP_DURATION_LABEL, labels_.duration);
        SetDlgItemTextW(dialog, IDC_POPUP_PREVIEW, labels_.preview);
        SetDlgItemTextW(dialog, IDC_POPUP_RESTORE, labels_.restore);
        SetDlgItemTextW(dialog, IDOK, labels_.ok);
        SetDlgItemTextW(dialog, IDCANCEL, labels_.cancel);

        ConfigureSpinner(dialog, IDC_POPUP_GAP, IDC_POPUP_GAP_SPIN);
        ConfigureSpinner(dialog, IDC_POPUP_DURATION, IDC_POPUP_DURATION_SPIN);
        WriteFields(dialog, settings_);
    }

    static void ConfigureSpinner(HWND dialog, int field, int spinner) {
        SendDlgItemMessageW(dialog, field, EM_SETLIMITTEXT, 24, 0);
        SendDlgItemMessageW(dialog, spinner, UDM_SETBUDDY,
            reinterpret_cast<WPARAM>(GetDlgItem(dialog, field)), 0);
        // Keep both native arrows available. Step() applies the field's actual limits.
        SendDlgItemMessageW(dialog, spinner, UDM_SETRANGE32, 0, 2);
        SendDlgItemMessageW(dialog, spinner, UDM_SETPOS32, 0, 1);
    }

    static std::wstring FieldText(HWND dialog, int field) {
        wchar_t value[32]{};
        const UINT length = GetDlgItemTextW(dialog, field, value, _countof(value));
        return length < _countof(value) - 1 ? std::wstring(value, length) : std::wstring{};
    }

    static void WriteFields(HWND dialog, PopupSettings settings) {
        SetDlgItemInt(dialog, IDC_POPUP_GAP, static_cast<UINT>(settings.bottomGap), FALSE);
        SetDlgItemTextW(dialog, IDC_POPUP_DURATION, FormatPopupDuration(settings.durationTenths).c_str());
    }

    static void ShowFieldError(HWND dialog, int field, const wchar_t* text) {
        SetDlgItemTextW(dialog, IDC_POPUP_STATUS, text);
        SendMessageW(dialog, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(GetDlgItem(dialog, field)), TRUE);
    }

    std::optional<PopupSettings> ReadFields(HWND dialog) const {
        const auto gap = ParseBottomGap(FieldText(dialog, IDC_POPUP_GAP));
        if (!gap) {
            ShowFieldError(dialog, IDC_POPUP_GAP, labels_.invalidGap);
            return {};
        }
        const auto duration = ParsePopupDuration(FieldText(dialog, IDC_POPUP_DURATION));
        if (!duration) {
            ShowFieldError(dialog, IDC_POPUP_DURATION, labels_.invalidDuration);
            return {};
        }
        return PopupSettings{*gap, *duration};
    }

    void Step(HWND dialog, const NMUPDOWN& spin) const {
        const int step = spin.iDelta > 0 ? 1 : -1;
        if (spin.hdr.idFrom == IDC_POPUP_GAP_SPIN) {
            const int gap = ParseBottomGap(FieldText(dialog, IDC_POPUP_GAP)).value_or(settings_.bottomGap);
            const int next = std::clamp(gap + step, PopupSettings::MinGap, PopupSettings::MaxGap);
            SetDlgItemInt(dialog, IDC_POPUP_GAP, static_cast<UINT>(next), FALSE);
        } else {
            const int duration = ParsePopupDuration(FieldText(dialog, IDC_POPUP_DURATION)).value_or(settings_.durationTenths);
            const int next = std::clamp(duration + step, PopupSettings::MinTenths, PopupSettings::MaxTenths);
            SetDlgItemTextW(dialog, IDC_POPUP_DURATION, FormatPopupDuration(next).c_str());
        }
    }

    INT_PTR Command(HWND dialog, WORD command, WORD notification) {
        switch (command) {
        case IDC_POPUP_GAP:
        case IDC_POPUP_DURATION:
            if (notification == EN_CHANGE) SetDlgItemTextW(dialog, IDC_POPUP_STATUS, L"");
            return TRUE;
        case IDC_POPUP_PREVIEW:
        case IDOK: {
            const auto draft = ReadFields(dialog);
            if (!draft) return TRUE;
            SetDlgItemTextW(dialog, IDC_POPUP_STATUS, L"");
            if (command == IDC_POPUP_PREVIEW) {
                if (!preview_(dialog, *draft)) SetDlgItemTextW(dialog, IDC_POPUP_STATUS, labels_.previewError);
            } else if (!SavePopupSettings(path_, *draft)) {
                SetDlgItemTextW(dialog, IDC_POPUP_STATUS, labels_.saveError);
            } else {
                settings_ = *draft;
                EndDialog(dialog, IDOK);
            }
            return TRUE;
        }
        case IDC_POPUP_RESTORE:
            WriteFields(dialog, PopupSettings{});
            SetDlgItemTextW(dialog, IDC_POPUP_STATUS, L"");
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }
    }

    const PopupLabels& labels_;
    PopupSettings settings_;
    const std::wstring& path_;
    Preview preview_;
};
}

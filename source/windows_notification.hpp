#pragma once

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propsys.h>
#include <hstring.h>
#include <winstring.h>
#include <roapi.h>
#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>
#include <string>

namespace copyconfirm {
namespace windows_notification {

inline constexpr wchar_t AppUserModelId[] = L"zh10x3r.CopyConfirm";

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T* get() const { return value_; }
    T** put() { reset(); return &value_; }
    T* operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
    void reset() {
        if (value_) value_->Release();
        value_ = nullptr;
    }
private:
    T* value_{};
};

class HString {
public:
    HString() = default;
    ~HString() { if (value_) WindowsDeleteString(value_); }
    HString(const HString&) = delete;
    HString& operator=(const HString&) = delete;
    HRESULT Set(const wchar_t* text) {
        return WindowsCreateString(text, static_cast<UINT32>(wcslen(text)), &value_);
    }
    HRESULT Set(const std::wstring& text) {
        return WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), &value_);
    }
    HSTRING get() const { return value_; }
private:
    HSTRING value_{};
};

inline bool EnsureStartMenuShortcut() {
    PWSTR programsPath{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_CREATE, nullptr, &programsPath)))
        return false;
    const std::wstring folder = std::wstring(programsPath) + L"\\CopyConfirm";
    CoTaskMemFree(programsPath);
    if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return false;

    const std::wstring shortcutPath = folder + L"\\CopyConfirm.lnk";
    const std::wstring executable = [] {
        wchar_t path[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(_countof(path)));
        return length && length < _countof(path) ? std::wstring(path, length) : std::wstring{};
    }();
    if (executable.empty()) return false;
    const size_t separator = executable.find_last_of(L"\\/");
    const std::wstring workingDirectory = separator == std::wstring::npos ?
        std::wstring{} : executable.substr(0, separator);

    ComPtr<IShellLinkW> link;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
        IID_IShellLinkW, reinterpret_cast<void**>(link.put()));
    if (FAILED(result)) return false;
    if (FAILED(link->SetPath(executable.c_str())) ||
        FAILED(link->SetWorkingDirectory(workingDirectory.c_str())) ||
        FAILED(link->SetDescription(L"CopyConfirm")) ||
        FAILED(link->SetIconLocation(executable.c_str(), 0))) return false;

    ComPtr<IPropertyStore> properties;
    result = link->QueryInterface(IID_IPropertyStore, reinterpret_cast<void**>(properties.put()));
    if (FAILED(result)) return false;
    constexpr PROPERTYKEY appUserModelIdKey{
        {0x9f4c2855, 0x9f79, 0x4b39, {0xa8, 0xd0, 0xe1, 0xd4, 0x2d, 0xe1, 0xd5, 0xf3}}, 5};
    PROPVARIANT appId{};
    appId.vt = VT_LPWSTR;
    appId.pwszVal = const_cast<PWSTR>(AppUserModelId);
    if (FAILED(properties->SetValue(appUserModelIdKey, appId)) || FAILED(properties->Commit()))
        return false;

    ComPtr<IPersistFile> persist;
    result = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(persist.put()));
    return SUCCEEDED(result) && SUCCEEDED(persist->Save(shortcutPath.c_str(), TRUE));
}

inline std::wstring EscapeXmlText(const wchar_t* text) {
    std::wstring escaped;
    for (const wchar_t* current = text; *current; ++current) {
        switch (*current) {
        case L'&': escaped += L"&amp;"; break;
        case L'<': escaped += L"&lt;"; break;
        case L'>': escaped += L"&gt;"; break;
        case L'\"': escaped += L"&quot;"; break;
        case L'\'': escaped += L"&apos;"; break;
        default: escaped += *current; break;
        }
    }
    return escaped;
}

inline HRESULT ShowStartupNotification(const wchar_t* message) {
    HRESULT result = RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(result)) return result;
    {
        if (!EnsureStartMenuShortcut()) {
            result = E_FAIL;
        } else {
            HString xmlClass;
            result = xmlClass.Set(L"Windows.Data.Xml.Dom.XmlDocument");
            ComPtr<IInspectable> xmlObject;
            if (SUCCEEDED(result)) result = RoActivateInstance(xmlClass.get(), xmlObject.put());

            ComPtr<ABI::Windows::Data::Xml::Dom::IXmlDocumentIO> xmlDocument;
            if (SUCCEEDED(result)) result = xmlObject->QueryInterface(
                __uuidof(ABI::Windows::Data::Xml::Dom::IXmlDocumentIO),
                reinterpret_cast<void**>(xmlDocument.put()));
            ComPtr<ABI::Windows::Data::Xml::Dom::IXmlDocument> xmlDocumentContent;
            if (SUCCEEDED(result)) result = xmlObject->QueryInterface(
                __uuidof(ABI::Windows::Data::Xml::Dom::IXmlDocument),
                reinterpret_cast<void**>(xmlDocumentContent.put()));
            const std::wstring xml = L"<toast><visual><binding template=\"ToastGeneric\"><text>" +
                EscapeXmlText(message) + L"</text></binding></visual></toast>";
            HString toastXml;
            if (SUCCEEDED(result)) result = toastXml.Set(xml);
            if (SUCCEEDED(result)) result = xmlDocument->LoadXml(toastXml.get());

            HString toastClass;
            if (SUCCEEDED(result)) result = toastClass.Set(L"Windows.UI.Notifications.ToastNotification");
            ComPtr<ABI::Windows::UI::Notifications::IToastNotificationFactory> toastFactory;
            if (SUCCEEDED(result)) result = RoGetActivationFactory(toastClass.get(),
                __uuidof(ABI::Windows::UI::Notifications::IToastNotificationFactory),
                reinterpret_cast<void**>(toastFactory.put()));
            ComPtr<ABI::Windows::UI::Notifications::IToastNotification> toast;
            if (SUCCEEDED(result)) result = toastFactory->CreateToastNotification(
                xmlDocumentContent.get(), toast.put());

            HString managerClass;
            if (SUCCEEDED(result)) result = managerClass.Set(L"Windows.UI.Notifications.ToastNotificationManager");
            ComPtr<ABI::Windows::UI::Notifications::IToastNotificationManagerStatics> manager;
            if (SUCCEEDED(result)) result = RoGetActivationFactory(managerClass.get(),
                __uuidof(ABI::Windows::UI::Notifications::IToastNotificationManagerStatics),
                reinterpret_cast<void**>(manager.put()));
            HString appId;
            if (SUCCEEDED(result)) result = appId.Set(AppUserModelId);
            ComPtr<ABI::Windows::UI::Notifications::IToastNotifier> notifier;
            if (SUCCEEDED(result)) result = manager->CreateToastNotifierWithId(appId.get(), notifier.put());
            if (SUCCEEDED(result)) result = notifier->Show(toast.get());
        }
    }
    RoUninitialize();
    return result;
}

} // namespace windows_notification
} // namespace copyconfirm

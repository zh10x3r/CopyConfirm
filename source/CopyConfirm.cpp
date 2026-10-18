#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "detector.hpp"
#include "localization.hpp"

using namespace copyconfirm;

namespace {
constexpr wchar_t AppName[] = L"复制确认";
constexpr UINT CopyMessage = WM_APP + 1;
constexpr UINT ProbeMessage = WM_APP + 2;
constexpr UINT TrayMessage = WM_APP + 3;
constexpr UINT PollTimer = 1;
constexpr UINT ToastTimer = 2;
constexpr UINT PauseCommand = 101;
constexpr UINT ExitCommand = 102;
constexpr UINT ChineseCommand = 201;
constexpr UINT EnglishCommand = 202;
constexpr DWORD ProbeTimeoutMs = 1250;
constexpr ULONGLONG ProbeMagic = 0x43504F5344505231ULL;
std::atomic<std::uint64_t> latestAttempt{0};
std::atomic<bool> paused{false};
HWND mainWindow{};
HINSTANCE appInstance{};

class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE Get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_{};
};

DWORD WindowPid(HWND window) {
    DWORD pid{};
    if (window) GetWindowThreadProcessId(window, &pid);
    return pid;
}

std::wstring ExecutablePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return count && count < buffer.size() ? std::wstring(buffer.data(), count) : L"";
}

std::wstring LanguageConfigPath() {
    const std::wstring path = ExecutablePath();
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : path.substr(0, slash + 1) + L"CopyConfirm.ini";
}

Language LoadLanguage(const std::wstring& path) {
    wchar_t value[16]{};
    if (!path.empty()) GetPrivateProfileStringW(L"General", L"Language", L"zh-CN",
        value, _countof(value), path.c_str());
    return _wcsicmp(value, L"en") == 0 ? Language::English : Language::Chinese;
}

bool SaveLanguage(const std::wstring& path, Language language) {
    return !path.empty() && WritePrivateProfileStringW(L"General", L"Language",
        language == Language::English ? L"en" : L"zh-CN", path.c_str());
}

HMENU BuildTrayMenu(Language language, bool isPaused) {
    const auto& text = Text(language);
    HMENU menu = CreatePopupMenu();
    if (!menu) return nullptr;
    HMENU languages = CreatePopupMenu();
    if (!languages) { DestroyMenu(menu); return nullptr; }
    bool success = AppendMenuW(languages, MF_STRING, ChineseCommand, L"中文") &&
        AppendMenuW(languages, MF_STRING, EnglishCommand, L"English") &&
        CheckMenuRadioItem(languages, ChineseCommand, EnglishCommand,
            language == Language::English ? EnglishCommand : ChineseCommand, MF_BYCOMMAND) &&
        AppendMenuW(menu, MF_STRING, PauseCommand, isPaused ? text.resume : text.pause);
    if (!success) { DestroyMenu(languages); DestroyMenu(menu); return nullptr; }
    if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(languages), text.language)) {
        DestroyMenu(languages); DestroyMenu(menu); return nullptr;
    }
    if (!AppendMenuW(menu, MF_SEPARATOR, 0, nullptr) ||
        !AppendMenuW(menu, MF_STRING, ExitCommand, text.exit)) {
        DestroyMenu(menu); return nullptr;
    }
    return menu;
}

std::wstring ObjectName(HANDLE object) {
    DWORD bytes{};
    GetUserObjectInformationW(object, UOI_NAME, nullptr, 0, &bytes);
    if (!bytes) return L"";
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1);
    if (!GetUserObjectInformationW(object, UOI_NAME, buffer.data(), bytes, &bytes)) return L"";
    return buffer.data();
}

std::wstring CurrentDesktopPath() {
    return ObjectName(GetProcessWindowStation()) + L"\\" +
        ObjectName(GetThreadDesktop(GetCurrentThreadId()));
}

Snapshot CurrentSnapshot(const CopyEvent& event) {
    return Snapshot{GetClipboardSequenceNumber(), WindowPid(GetClipboardOwner()),
        WindowPid(GetForegroundWindow()), IsWindow(reinterpret_cast<HWND>(event.sourceWindow)) != FALSE};
}

struct SharedProbe {
    ULONGLONG magic{ProbeMagic};
    ProbeTask request{};
    ProbeResult result{};
    DWORD testDelayMs{}; // Only used by isolated automated tests; always zero in normal use.
};

bool ReadableFormat(UINT format, HANDLE data) {
    if (!data) return false;
    if (format == CF_BITMAP) {
        BITMAP bitmap{};
        return GetObjectW(data, sizeof(bitmap), &bitmap) && bitmap.bmWidth > 0 && bitmap.bmHeight > 0;
    }
    if (format == CF_ENHMETAFILE) {
        ENHMETAHEADER header{};
        return GetEnhMetaFileHeader(static_cast<HENHMETAFILE>(data), sizeof(header), &header) != 0;
    }
    if (format == CF_HDROP)
        return DragQueryFileW(static_cast<HDROP>(data), 0xFFFFFFFF, nullptr, 0) > 0 &&
            DragQueryFileW(static_cast<HDROP>(data), 0, nullptr, 0) > 0;
    const SIZE_T bytes = GlobalSize(data);
    if (!bytes) return false;
    const void* memory = GlobalLock(data);
    if (!memory) return false;
    bool valid = true;
    if (format == CF_UNICODETEXT)
        valid = bytes >= sizeof(wchar_t) && *static_cast<const wchar_t*>(memory) != L'\0';
    else if (format == CF_TEXT || format == CF_OEMTEXT)
        valid = *static_cast<const char*>(memory) != '\0';
    else if (format == CF_DIB || format == CF_DIBV5) {
        valid = bytes >= sizeof(BITMAPCOREHEADER);
        if (valid) {
            DWORD headerBytes{};
            std::memcpy(&headerBytes, memory, sizeof(headerBytes));
            valid = headerBytes >= sizeof(BITMAPCOREHEADER) && headerBytes <= bytes;
        }
    }
    GlobalUnlock(data);
    return valid;
}

ProbeResult InspectClipboard(const ProbeTask& task) {
    ProbeResult result{task.copy.id};
    if (GetClipboardSequenceNumber() != task.candidateSequence) {
        result.status = ProbeStatus::Stale;
        return result;
    }
    if (!OpenClipboard(nullptr)) {
        result.status = ProbeStatus::Busy;
        result.error = GetLastError();
        return result;
    }
    // No content is copied into application storage. A handle is opened only
    // to establish that a newly published format can actually be retrieved.
    const HWND owner = GetClipboardOwner();
    result.ownerPid = WindowPid(owner);
    if (result.ownerPid != task.copy.sourcePid) {
        result.status = ProbeStatus::WrongOwner;
    } else if (GetClipboardSequenceNumber() != task.candidateSequence) {
        result.status = ProbeStatus::Stale;
    } else if (CountClipboardFormats() <= 0) {
        result.status = ProbeStatus::NoData;
    } else {
        const UINT formats[] = {CF_UNICODETEXT, CF_TEXT, CF_OEMTEXT, CF_HDROP,
            CF_DIBV5, CF_DIB, CF_BITMAP, CF_ENHMETAFILE, CF_METAFILEPICT,
            RegisterClipboardFormatW(L"HTML Format"),
            RegisterClipboardFormatW(L"Rich Text Format"),
            RegisterClipboardFormatW(L"PNG"), RegisterClipboardFormatW(L"image/png"),
            RegisterClipboardFormatW(L"JFIF")};
        result.status = ProbeStatus::Unsupported;
        for (const UINT format : formats) {
            if (!format || !IsClipboardFormatAvailable(format)) continue;
            SetLastError(ERROR_SUCCESS);
            const HANDLE data = GetClipboardData(format);
            if (ReadableFormat(format, data)) {
                result.status = ProbeStatus::Ok;
                result.format = format;
                break;
            }
            result.status = ProbeStatus::Unreadable;
            result.error = GetLastError();
        }
        result.sequence = GetClipboardSequenceNumber();
        if (GetClipboardOwner() != owner || WindowPid(GetClipboardOwner()) != task.copy.sourcePid)
            result.status = ProbeStatus::WrongOwner;
        if (!result.sequence || result.sequence == task.copy.baseline)
            result.status = ProbeStatus::Stale;
    }
    CloseClipboard();
    return result;
}

int ProbeEntry(HANDLE mapping) {
    auto* shared = static_cast<SharedProbe*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedProbe)));
    if (!shared) return 2;
    if (shared->magic != ProbeMagic) { UnmapViewOfFile(shared); return 3; }
    if (shared->testDelayMs) Sleep(std::min<DWORD>(shared->testDelayMs, 5000));
    shared->result = InspectClipboard(shared->request);
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    return 0;
}

// GetClipboardData may invoke another application's delayed renderer and
// block. Run it in a short-lived child, with a timeout and a kill-on-close job,
// so neither keyboard handling nor the resident application can get stuck.
ProbeResult RunProbe(const ProbeTask& task, const std::wstring& desktop,
                     DWORD timeout = ProbeTimeoutMs, DWORD testDelay = 0) {
    ProbeResult result{task.copy.id};
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE,
                                     0, sizeof(SharedProbe), nullptr));
    if (!mapping) { result.error = GetLastError(); return result; }
    auto* shared = static_cast<SharedProbe*>(MapViewOfFile(mapping.Get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedProbe)));
    if (!shared) { result.error = GetLastError(); return result; }
    new (shared) SharedProbe{};
    shared->request = task;
    shared->testDelayMs = testDelay;
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        result.error = GetLastError();
        UnmapViewOfFile(shared);
        return result;
    }
    SIZE_T attributeBytes{};
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<BYTE> attributes(attributeBytes);
    auto* attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeBytes)) {
        result.error = GetLastError(); UnmapViewOfFile(shared); return result;
    }
    HANDLE inherited[] = {mapping.Get()};
    const bool handlesSet = UpdateProcThreadAttribute(attributeList, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr) != FALSE;
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.lpDesktop = const_cast<wchar_t*>(desktop.c_str());
    startup.lpAttributeList = attributeList;
    std::wstring command = L"\"" + ExecutablePath() + L"\" --probe " +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(mapping.Get()));
    PROCESS_INFORMATION process{};
    const bool created = handlesSet && CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_SUSPENDED,
        nullptr, nullptr, &startup.StartupInfo, &process);
    const DWORD startError = created ? ERROR_SUCCESS : GetLastError();
    DeleteProcThreadAttributeList(attributeList);
    if (!created) {
        result.error = startError;
        UnmapViewOfFile(shared);
        return result;
    }
    Handle processHandle(process.hProcess);
    Handle threadHandle(process.hThread);
    if (!AssignProcessToJobObject(job.Get(), processHandle.Get())) {
        result.error = GetLastError();
        TerminateProcess(processHandle.Get(), 5);
        WaitForSingleObject(processHandle.Get(), 1000);
        UnmapViewOfFile(shared);
        return result;
    }
    if (ResumeThread(threadHandle.Get()) == static_cast<DWORD>(-1)) {
        result.error = GetLastError();
        TerminateProcess(processHandle.Get(), 5);
        WaitForSingleObject(processHandle.Get(), 1000);
        UnmapViewOfFile(shared);
        return result;
    }
    const ULONGLONG started = GetTickCount64();
    while (true) {
        const DWORD wait = WaitForSingleObject(processHandle.Get(), 20);
        if (wait == WAIT_OBJECT_0) {
            DWORD exitCode{};
            GetExitCodeProcess(processHandle.Get(), &exitCode);
            if (!exitCode && shared->magic == ProbeMagic) result = shared->result;
            else result.error = exitCode;
            break;
        }
        if (latestAttempt.load() != task.copy.id) {
            result.status = ProbeStatus::Canceled;
            TerminateProcess(processHandle.Get(), 6);
            WaitForSingleObject(processHandle.Get(), 1000);
            break;
        }
        if (GetTickCount64() - started >= timeout || wait == WAIT_FAILED) {
            result.status = ProbeStatus::Timeout;
            TerminateProcess(processHandle.Get(), 7);
            WaitForSingleObject(processHandle.Get(), 1000);
            break;
        }
    }
    UnmapViewOfFile(shared);
    return result;
}

class ProbeWorker {
public:
    explicit ProbeWorker(HWND target) : target_(target), desktop_(CurrentDesktopPath()),
        thread_([this] { Loop(); }) {}
    ~ProbeWorker() { Stop(); }
    void Submit(ProbeTask task) {
        { std::lock_guard lock(mutex_); queued_ = task; }
        ready_.notify_one();
    }
    void Stop() {
        { std::lock_guard lock(mutex_); stopping_ = true; queued_.reset(); }
        latestAttempt.fetch_add(1);
        ready_.notify_one();
        if (thread_.joinable()) thread_.join();
    }
private:
    void Loop() {
        while (true) {
            ProbeTask task;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || queued_.has_value(); });
                if (stopping_) return;
                task = *queued_;
                queued_.reset();
            }
            auto* result = new (std::nothrow) ProbeResult(RunProbe(task, desktop_));
            if (result && !PostMessageW(target_, ProbeMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
        }
    }
    HWND target_{};
    std::wstring desktop_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::optional<ProbeTask> queued_;
    bool stopping_{};
    std::thread thread_;
};

KeyChord chord;
LRESULT CALLBACK KeyboardCallback(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const bool isC = key->vkCode == 'C';
        if (isC && chord.Feed(isC, down, (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0,
            (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0,
            (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 || (key->flags & LLKHF_ALTDOWN),
            ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0,
            (key->flags & LLKHF_INJECTED) != 0, !paused.load())) {
            const HWND source = GetForegroundWindow();
            auto* event = new (std::nothrow) CopyEvent{
                latestAttempt.fetch_add(1) + 1, GetTickCount64(), GetClipboardSequenceNumber(),
                WindowPid(source), reinterpret_cast<std::uintptr_t>(source)};
            if (event && !PostMessageW(mainWindow, CopyMessage, 0, reinterpret_cast<LPARAM>(event))) delete event;
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}

class KeyboardThread {
public:
    bool Start() {
        ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ready_) return false;
        thread_ = CreateThread(nullptr, 0, Entry, this, 0, &threadId_);
        if (!thread_) { CloseHandle(ready_); ready_ = nullptr; return false; }
        return WaitForSingleObject(ready_, 3000) == WAIT_OBJECT_0 && installed_.load();
    }
    ~KeyboardThread() {
        if (thread_) {
            PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
            WaitForSingleObject(thread_, INFINITE);
            CloseHandle(thread_);
        }
        if (ready_) CloseHandle(ready_);
    }
private:
    static DWORD WINAPI Entry(void* context) {
        auto& self = *static_cast<KeyboardThread*>(context);
        MSG message{};
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardCallback, appInstance, 0);
        self.installed_.store(hook != nullptr);
        SetEvent(self.ready_);
        if (!hook) return 1;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        UnhookWindowsHookEx(hook);
        return 0;
    }
    HANDLE thread_{};
    HANDLE ready_{};
    DWORD threadId_{};
    std::atomic<bool> installed_{false};
};

class ToastSurface {
public:
    ~ToastSurface() { Reset(); }
    void Reset() {
        if (dc_) {
            SelectObject(dc_, previous_);
            DeleteDC(dc_);
            DeleteObject(bitmap_);
        }
        dc_ = nullptr; bitmap_ = nullptr; pixels_ = nullptr;
    }
    bool Render(UINT dpi, Language language = Language::Chinese) {
        Reset();
        textFits_ = false;
        const float scale = static_cast<float>(dpi ? dpi : 96) / 96.0f;
        width = static_cast<int>(156 * scale);
        height = static_cast<int>(46 * scale);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        dc_ = CreateCompatibleDC(nullptr);
        bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &pixels_, nullptr, 0);
        if (!dc_ || !bitmap_) {
            if (dc_) DeleteDC(dc_);
            if (bitmap_) DeleteObject(bitmap_);
            dc_ = nullptr; bitmap_ = nullptr; pixels_ = nullptr;
            return false;
        }
        previous_ = SelectObject(dc_, bitmap_);
        std::memset(pixels_, 0, static_cast<size_t>(width) * height * 4);
        Gdiplus::Bitmap canvas(width, height, width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(pixels_));
        Gdiplus::Graphics graphics(&canvas);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        graphics.ScaleTransform(scale, scale);
        auto rounded = [](Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float radius) {
            const float d = radius * 2;
            path.AddArc(x, y, d, d, 180.0f, 90.0f);
            path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
            path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
            path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
            path.CloseFigure();
        };
        for (int layer = 3; layer >= 1; --layer) {
            Gdiplus::GraphicsPath shadow;
            rounded(shadow, 4.0f - layer, 5.0f - layer,
                148.0f + layer * 2, 38.0f + layer * 2, 8.0f + layer);
            Gdiplus::SolidBrush shade(Gdiplus::Color(6, 0, 0, 0));
            graphics.FillPath(&shade, &shadow);
        }
        Gdiplus::GraphicsPath panel;
        rounded(panel, 4, 3, 148, 38, 8);
        Gdiplus::SolidBrush paper(Gdiplus::Color(250, 253, 253, 253));
        Gdiplus::Pen border(Gdiplus::Color(80, 210, 210, 210), 0.7f);
        graphics.FillPath(&paper, &panel);
        graphics.DrawPath(&border, &panel);
        const auto& text = Text(language);
        Gdiplus::Font font(text.fontFamily, text.fontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush ink(Gdiplus::Color(255, 57, 57, 57));
        Gdiplus::StringFormat format;
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        const Gdiplus::RectF textBounds(8, 3, 140, 38);
        Gdiplus::RectF measured;
        const Gdiplus::PointF origin(0, 0);
        const bool measuredOk = graphics.MeasureString(text.copied, -1, &font, origin, &format, &measured) == Gdiplus::Ok;
        textFits_ = measuredOk && measured.Width <= textBounds.Width && measured.Height <= textBounds.Height;
        graphics.DrawString(text.copied, -1, &font, textBounds, &format, &ink);
        return canvas.GetLastStatus() == Gdiplus::Ok;
    }
    bool SavePng(const wchar_t* path) {
        Gdiplus::Bitmap image(width, height, width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(pixels_));
        const CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
        return image.Save(path, &png, nullptr) == Gdiplus::Ok;
    }
    HDC Dc() const { return dc_; }
    bool TextFits() const { return textFits_; }
    int width{}, height{};
private:
    HDC dc_{};
    HBITMAP bitmap_{};
    HGDIOBJ previous_{};
    void* pixels_{};
    bool textFits_{};
};

class App {
public:
    explicit App(std::wstring settingsPath = LanguageConfigPath())
        : settingsPath_(std::move(settingsPath)), language_(LoadLanguage(settingsPath_)) {}
    Language CurrentLanguage() const { return language_; }
    void Attach(HWND window) { window_ = window; }
    bool Initialize(HWND window) {
        window_ = window;
        taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
        WNDCLASSEXW toastClass{sizeof(toastClass)};
        toastClass.lpfnWndProc = DefWindowProcW;
        toastClass.hInstance = appInstance;
        toastClass.lpszClassName = L"CopyConfirm.Toast.1";
        if (!RegisterClassExW(&toastClass)) return false;
        toast_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
            WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT, toastClass.lpszClassName, L"",
            WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, appInstance, nullptr);
        if (!toast_ || !AddClipboardFormatListener(window_)) return false;
        listening_ = true;
        if (!AddTray()) return false;
        worker_ = std::make_unique<ProbeWorker>(window_);
        return true;
    }
    void Shutdown() {
        worker_.reset();
        if (listening_) RemoveClipboardFormatListener(window_);
        listening_ = false;
        KillTimer(window_, PollTimer);
        KillTimer(window_, ToastTimer);
        if (toast_) { DestroyWindow(toast_); toast_ = nullptr; }
        NOTIFYICONDATAW icon{sizeof(icon)};
        icon.hWnd = window_; icon.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &icon);
    }
    LRESULT Message(UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == taskbarCreated_ && taskbarCreated_) { AddTray(); return 0; }
        switch (message) {
        case CopyMessage: {
            std::unique_ptr<CopyEvent> copy(reinterpret_cast<CopyEvent*>(lParam));
            if (paused.load() || copy->id != latestAttempt.load()) return 0;
            HideToast();
            gate_.Begin(*copy);
            if (gate_.Pending()) { SetTimer(window_, PollTimer, 25, nullptr); Observe(); }
            return 0;
        }
        case WM_CLIPBOARDUPDATE:
            if (visible_ && GetClipboardSequenceNumber() != confirmedSequence_) HideToast();
            Observe();
            return 0;
        case ProbeMessage: {
            std::unique_ptr<ProbeResult> result(reinterpret_cast<ProbeResult*>(lParam));
            if (!gate_.Attempt() || result->id != latestAttempt.load()) return 0;
            const CopyEvent copy = *gate_.Attempt();
            if (gate_.Complete(GetTickCount64(), *result, CurrentSnapshot(copy))) {
                KillTimer(window_, PollTimer);
                ShowToast(reinterpret_cast<HWND>(copy.sourceWindow), result->sequence);
            } else Observe();
            return 0;
        }
        case WM_TIMER:
            if (wParam == PollTimer) Observe();
            if (wParam == ToastTimer) AnimateToast();
            return 0;
        case TrayMessage:
            if (LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == WM_RBUTTONUP)
                TrayMenu();
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == ExitCommand) DestroyWindow(window_);
            if (LOWORD(wParam) == PauseCommand) {
                paused.store(!paused.load());
                latestAttempt.fetch_add(1);
                gate_.Cancel(); HideToast(); KillTimer(window_, PollTimer);
                UpdateTooltip();
            }
            if (LOWORD(wParam) == ChineseCommand || LOWORD(wParam) == EnglishCommand) {
                language_ = LOWORD(wParam) == EnglishCommand ? Language::English : Language::Chinese;
                SaveLanguage(settingsPath_, language_);
                HideToast();
                UpdateTooltip();
            }
            return 0;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_DESTROY:
            Shutdown();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window_, message, wParam, lParam);
        }
    }
private:
    void Observe() {
        if (!gate_.Attempt()) { KillTimer(window_, PollTimer); return; }
        if (paused.load() || gate_.Attempt()->id != latestAttempt.load()) {
            gate_.Cancel(); KillTimer(window_, PollTimer); return;
        }
        if (auto task = gate_.Observe(GetTickCount64(), CurrentSnapshot(*gate_.Attempt())))
            worker_->Submit(*task);
        if (!gate_.Pending()) KillTimer(window_, PollTimer);
    }
    bool AddTray() {
        NOTIFYICONDATAW icon{sizeof(icon)};
        icon.hWnd = window_; icon.uID = 1;
        icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        icon.uCallbackMessage = TrayMessage;
        icon.hIcon = LoadIconW(appInstance, MAKEINTRESOURCEW(101));
        const auto& text = Text(language_);
        wcscpy_s(icon.szTip, _countof(icon.szTip), paused.load() ? text.pausedTooltip : text.runningTooltip);
        if (!Shell_NotifyIconW(NIM_ADD, &icon)) return false;
        icon.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &icon);
        return true;
    }
    void UpdateTooltip() {
        NOTIFYICONDATAW icon{sizeof(icon)};
        icon.hWnd = window_; icon.uID = 1; icon.uFlags = NIF_TIP;
        const auto& text = Text(language_);
        wcscpy_s(icon.szTip, _countof(icon.szTip), paused.load() ? text.pausedTooltip : text.runningTooltip);
        Shell_NotifyIconW(NIM_MODIFY, &icon);
    }
    void TrayMenu() {
        HMENU menu = BuildTrayMenu(language_, paused.load());
        if (!menu) return;
        POINT point{}; GetCursorPos(&point);
        SetForegroundWindow(window_);
        const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            point.x, point.y, 0, window_, nullptr);
        DestroyMenu(menu);
        if (command) PostMessageW(window_, WM_COMMAND, command, 0);
        PostMessageW(window_, WM_NULL, 0, 0);
    }
    void ShowToast(HWND source, DWORD sequence) {
        const UINT dpi = GetDpiForWindow(source);
        if (!surface_.Render(dpi, language_)) return;
        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoW(MonitorFromWindow(source, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        position_.x = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - surface_.width) / 2;
        position_.y = monitor.rcWork.bottom - surface_.height - MulDiv(70, dpi ? dpi : 96, 96);
        confirmedSequence_ = sequence;
        toastStarted_ = GetTickCount64();
        visible_ = true;
        PaintToast(0);
        ShowWindow(toast_, SW_SHOWNOACTIVATE);
        SetTimer(window_, ToastTimer, 16, nullptr);
    }
    void PaintToast(BYTE opacity) {
        POINT origin{};
        SIZE size{surface_.width, surface_.height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
        UpdateLayeredWindow(toast_, nullptr, &position_, &size, surface_.Dc(), &origin,
                            0, &blend, ULW_ALPHA);
    }
    void AnimateToast() {
        if (!visible_) return;
        const ULONGLONG age = GetTickCount64() - toastStarted_;
        if (age >= 1300 || GetClipboardSequenceNumber() != confirmedSequence_) { HideToast(); return; }
        const float alpha = age < 120 ? static_cast<float>(age) / 120.0f :
            age > 1120 ? static_cast<float>(1300 - age) / 180.0f : 1.0f;
        PaintToast(static_cast<BYTE>(std::clamp(alpha, 0.0f, 1.0f) * 255));
    }
    void HideToast() {
        if (toast_) ShowWindow(toast_, SW_HIDE);
        visible_ = false;
        if (window_) KillTimer(window_, ToastTimer);
    }
    HWND window_{}, toast_{};
    UINT taskbarCreated_{};
    CopyGate gate_;
    std::unique_ptr<ProbeWorker> worker_;
    ToastSurface surface_;
    POINT position_{};
    ULONGLONG toastStarted_{};
    DWORD confirmedSequence_{};
    bool visible_{}, listening_{};
    std::wstring settingsPath_;
    Language language_{Language::Chinese};
};

LRESULT CALLBACK MainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        static_cast<App*>(create->lpCreateParams)->Attach(window);
    }
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    // WM_NCCREATE arrives before App::Initialize has its window handle.
    if (!app || message == WM_NCCREATE || message == WM_CREATE || message == WM_GETMINMAXINFO)
        return DefWindowProcW(window, message, wParam, lParam);
    return app->Message(message, wParam, lParam);
}

int RunSelfTests(const wchar_t* reportPath);
int RunSmokeTest(const wchar_t* reportPath);
int RunReadOnlyProbeTest(const wchar_t* reportPath);
int RunLanguageTests(const wchar_t* reportPath);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    appInstance = instance;
    int argumentCount{};
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    std::vector<std::wstring> args;
    for (int i = 0; arguments && i < argumentCount; ++i) args.emplace_back(arguments[i]);
    if (arguments) LocalFree(arguments);
    if (args.size() == 3 && args[1] == L"--probe")
        return ProbeEntry(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::wcstoull(args[2].c_str(), nullptr, 10))));
    if (args.size() == 3 && args[1] == L"--self-test") return RunSelfTests(args[2].c_str());
    if (args.size() == 3 && args[1] == L"--read-only-probe-test") return RunReadOnlyProbeTest(args[2].c_str());
    Gdiplus::GdiplusStartupInput startup;
    ULONG_PTR graphicsToken{};
    if (Gdiplus::GdiplusStartup(&graphicsToken, &startup, nullptr) != Gdiplus::Ok) return 2;
    if (args.size() == 3 && (args[1] == L"--render-preview" || args[1] == L"--render-preview-en")) {
        int result{};
        { ToastSurface surface; result = surface.Render(144, args[1] == L"--render-preview-en" ?
            Language::English : Language::Chinese) && surface.TextFits() && surface.SavePng(args[2].c_str()) ? 0 : 3; }
        Gdiplus::GdiplusShutdown(graphicsToken);
        return result;
    }
    if (args.size() == 3 && args[1] == L"--language-test") {
        const int result = RunLanguageTests(args[2].c_str());
        Gdiplus::GdiplusShutdown(graphicsToken);
        return result;
    }
    if (args.size() == 3 && args[1] == L"--smoke-test") {
        const int result = RunSmokeTest(args[2].c_str());
        Gdiplus::GdiplusShutdown(graphicsToken);
        return result;
    }
    Handle singleInstance(CreateMutexW(nullptr, FALSE, L"Local\\CopyConfirm.Resident.1"));
    if (!singleInstance || GetLastError() == ERROR_ALREADY_EXISTS) {
        Gdiplus::GdiplusShutdown(graphicsToken);
        return 0;
    }
    int exitCode{};
    {
        App app;
        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.lpfnWndProc = MainWindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = L"CopyConfirm.Main.1";
        RegisterClassExW(&windowClass);
        mainWindow = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            windowClass.lpszClassName, AppName, WS_OVERLAPPED, 0, 0, 0, 0,
            nullptr, nullptr, instance, &app);
        KeyboardThread keyboard;
        if (!mainWindow || !app.Initialize(mainWindow) || !keyboard.Start()) {
            const auto& text = Text(LoadLanguage(LanguageConfigPath()));
            MessageBoxW(nullptr, text.startupError, AppName, MB_OK | MB_ICONERROR);
            if (mainWindow && IsWindow(mainWindow)) DestroyWindow(mainWindow);
            exitCode = 1;
        } else {
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }
    mainWindow = nullptr;
    MSG leftover{};
    while (PeekMessageW(&leftover, nullptr, CopyMessage, ProbeMessage, PM_REMOVE)) {
        if (leftover.message == CopyMessage) delete reinterpret_cast<CopyEvent*>(leftover.lParam);
        if (leftover.message == ProbeMessage) delete reinterpret_cast<ProbeResult*>(leftover.lParam);
    }
    Gdiplus::GdiplusShutdown(graphicsToken);
    return exitCode;
}

#include "selftest.inc"

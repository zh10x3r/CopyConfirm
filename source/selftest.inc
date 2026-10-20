// Tests create an independent, noninteractive window station before they
// write any clipboard data. They never send keystrokes to the user's desktop.
namespace {
struct TestCase { std::string name; bool passed; DWORD detail; bool skipped{}; };

void WriteReport(const wchar_t* path, const std::vector<TestCase>& cases,
                 bool isolated, DWORD stationError = 0, bool originalSequenceUnchanged = true,
                 const char* scope = nullptr) {
    unsigned failures{}, skipped{};
    std::string json = "{\n  \"clipboard_scope\": \"" + std::string(scope ? scope : (isolated ?
        "isolated_noninteractive_window_station" : "no_clipboard_writes")) +
        "\",\n  \"station_error\": " + std::to_string(stationError) +
        ",\n  \"original_clipboard_sequence_unchanged\": " +
        (originalSequenceUnchanged ? "true" : "false") + ",\n  \"cases\": [\n";
    for (size_t i = 0; i < cases.size(); ++i) {
        const auto& test = cases[i];
        if (test.skipped) ++skipped;
        else if (!test.passed) ++failures;
        json += "    {\"name\": \"" + test.name + "\", \"passed\": " +
            (test.passed ? "true" : "false") + ", \"detail\": " + std::to_string(test.detail) +
            ", \"skipped\": " + (test.skipped ? "true" : "false") + "}";
        json += i + 1 < cases.size() ? ",\n" : "\n";
    }
    json += "  ],\n  \"passed\": " + std::to_string(cases.size() - failures - skipped) +
        ",\n  \"failed\": " + std::to_string(failures) +
        ",\n  \"skipped\": " + std::to_string(skipped) + "\n}\n";
    Handle output(CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (output) { DWORD written{}; WriteFile(output.Get(), json.data(), static_cast<DWORD>(json.size()), &written, nullptr); }
}

bool SetTestText(HWND owner, const wchar_t* text) {
    if (!OpenClipboard(owner)) return false;
    if (!EmptyClipboard()) { CloseClipboard(); return false; }
    const SIZE_T bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    void* buffer = memory ? GlobalLock(memory) : nullptr;
    if (!buffer) { if (memory) GlobalFree(memory); CloseClipboard(); return false; }
    std::memcpy(buffer, text, bytes);
    GlobalUnlock(memory);
    const bool success = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    if (!success) GlobalFree(memory);
    CloseClipboard();
    return success;
}

bool delayedRenderAllowed = true;
LRESULT CALLBACK TestSourceProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_RENDERFORMAT && wParam == CF_UNICODETEXT) {
        if (delayedRenderAllowed) {
            constexpr wchar_t text[] = L"CopyConfirm delayed rendering test";
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(text));
            void* buffer = memory ? GlobalLock(memory) : nullptr;
            if (buffer) {
                std::memcpy(buffer, text, sizeof(text)); GlobalUnlock(memory);
                if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
            } else if (memory) GlobalFree(memory);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

ProbeResult ProbeWithMessagePump(const ProbeTask& task, DWORD timeout = ProbeTimeoutMs,
                                DWORD testDelay = 0, bool cancel = false) {
    latestAttempt.store(task.copy.id);
    const std::wstring desktop = CurrentDesktopPath();
    std::atomic<bool> finished{false};
    ProbeResult result;
    std::thread worker([&] { result = RunProbe(task, desktop, timeout, testDelay); finished.store(true); });
    const ULONGLONG started = GetTickCount64();
    while (!finished.load()) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (cancel && GetTickCount64() - started >= 100) latestAttempt.fetch_add(1);
        Sleep(3);
    }
    worker.join();
    return result;
}

int RunSelfTests(const wchar_t* reportPath) {
    std::vector<TestCase> tests;
    auto check = [&](const char* name, bool passed, DWORD detail = 0) {
        tests.push_back(TestCase{name, passed, detail});
    };
    const CopyEvent copy{1, 1000, 10, 42, 1};
    const Snapshot updated{11, 42, 42, true};
    const ProbeResult success{1, ProbeStatus::Ok, 11, 42, CF_UNICODETEXT, 0};
    auto ready = [&](CopyGate& gate) {
        gate.Begin(copy); gate.Observe(1001, updated); return gate.Observe(1066, updated);
    };
    {
        CopyGate gate;
        check("clipboard_change_without_ctrl_c_is_silent", !gate.Observe(1100, updated));
        gate.Begin(copy);
        check("ctrl_c_with_unchanged_clipboard_is_silent", !gate.Observe(1200, {10, 42, 42, true}));
        gate.Observe(1201, updated);
        check("new_content_waits_for_clipboard_settle", !gate.Observe(1230, updated));
        auto task = gate.Observe(1266, updated);
        check("fresh_matching_owner_starts_verification", task.has_value());
        check("readable_matching_data_confirms_success", gate.Complete(1270, success, updated));
        check("one_confirmation_per_key_press", !gate.Complete(1271, success, updated));
    }
    {
        CopyGate gate; gate.Begin(copy);
        gate.Observe(1001, {11, 43, 42, true});
        check("background_application_update_is_silent", !gate.Observe(1200, {11, 43, 42, true}));
        check("same_old_data_does_not_confirm_a_failed_copy", !gate.Complete(1250, {1, ProbeStatus::Ok, 10, 42}, {10, 42, 42, true}));
    }
    {
        CopyGate gate; ready(gate);
        check("overwritten_clipboard_rejects_old_confirmation", !gate.Complete(1070, success, {12, 43, 42, true}));
    }
    {
        CopyGate gate; ready(gate);
        check("switching_applications_cancels_pending_copy", !gate.Complete(1070, success, {11, 42, 43, true}) && !gate.Pending());
    }
    {
        CopyGate gate; gate.Begin(copy);
        check("closed_source_window_cancels_pending_copy", !gate.Observe(1100, {11, 42, 42, false}) && !gate.Pending());
        gate.Begin(copy);
        check("late_updates_do_not_confirm_old_shortcuts", !gate.Observe(2801, updated) && !gate.Pending());
        gate.Begin({1, 1000, 0, 42, 1});
        check("missing_clipboard_access_never_reports_success", !gate.Pending());
    }
    {
        CopyGate gate; ready(gate);
        check("empty_clipboard_never_confirms", !gate.Complete(1070, {1, ProbeStatus::NoData, 11, 42}, updated));
        check("unsupported_format_is_not_retried_in_a_loop", !gate.Observe(1300, updated));
        gate.Begin(copy); ready(gate);
        check("renderer_timeout_never_confirms", !gate.Complete(1070, {1, ProbeStatus::Timeout, 11, 42}, updated) && !gate.Pending());
    }
    {
        CopyGate gate; ready(gate);
        gate.Begin({2, 1100, 11, 42, 1});
        check("rapid_new_copy_rejects_previous_probe", !gate.Complete(1110, success, updated) && gate.Pending());
        gate.Observe(1110, {12, 42, 42, true});
        auto task = gate.Observe(1175, {12, 42, 42, true});
        check("latest_rapid_copy_can_still_confirm", task && gate.Complete(1180,
            {2, ProbeStatus::Ok, 12, 42}, {12, 42, 42, true}));
    }
    {
        KeyChord keys;
        check("plain_c_does_not_trigger", !keys.Feed(true, true, false, false, false, false, false, true));
        keys.Feed(true, false, false, false, false, false, false, true);
        check("ctrl_c_is_detected", keys.Feed(true, true, true, false, false, false, false, true));
        check("holding_c_does_not_repeat", !keys.Feed(true, true, true, false, false, false, false, true));
        keys.Feed(true, false, true, false, false, false, false, true);
        check("ctrl_shift_c_does_not_trigger", !keys.Feed(true, true, true, true, false, false, false, true));
        keys.Feed(true, false, true, false, false, false, false, true);
        check("ctrl_alt_c_does_not_trigger", !keys.Feed(true, true, true, false, true, false, false, true));
        keys.Feed(true, false, true, false, false, false, false, true);
        check("ctrl_win_c_does_not_trigger", !keys.Feed(true, true, true, false, false, true, false, true));
        keys.Feed(true, false, true, false, false, false, false, true);
        check("synthetic_key_injection_does_not_trigger", !keys.Feed(true, true, true, false, false, false, true, true));
        check("paused_monitor_does_not_trigger", !keys.Feed(true, true, true, false, false, false, false, false));
    }

    // Exercise native format handles without changing any clipboard.
    {
        constexpr wchar_t text[] = L"Native text handle 测试";
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(text));
        void* buffer = GlobalLock(memory);
        std::memcpy(buffer, text, sizeof(text)); GlobalUnlock(memory);
        check("native_unicode_memory_handle_is_readable", ReadableFormat(CF_UNICODETEXT, memory));
        buffer = GlobalLock(memory); std::memset(buffer, 0, sizeof(text)); GlobalUnlock(memory);
        check("native_empty_unicode_data_is_rejected", !ReadableFormat(CF_UNICODETEXT, memory));
        GlobalFree(memory);
        check("null_clipboard_data_is_rejected", !ReadableFormat(CF_UNICODETEXT, nullptr));
        HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(BITMAPINFOHEADER) + 16);
        auto* header = static_cast<BITMAPINFOHEADER*>(GlobalLock(dib));
        header->biSize = sizeof(BITMAPINFOHEADER); header->biWidth = 2; header->biHeight = 2;
        header->biPlanes = 1; header->biBitCount = 32; header->biSizeImage = 16;
        GlobalUnlock(dib);
        check("native_bitmap_data_is_readable", ReadableFormat(CF_DIB, dib));
        header = static_cast<BITMAPINFOHEADER*>(GlobalLock(dib)); header->biSize = 0xFFFFFFF;
        GlobalUnlock(dib);
        check("out_of_bounds_bitmap_header_is_rejected", !ReadableFormat(CF_DIB, dib));
        GlobalFree(dib);
        constexpr wchar_t files[] = L"C:\\CopyConfirm.synthetic.txt\0";
        HGLOBAL drop = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + sizeof(files));
        auto* dropData = static_cast<DROPFILES*>(GlobalLock(drop));
        dropData->pFiles = sizeof(DROPFILES); dropData->fWide = TRUE;
        std::memcpy(reinterpret_cast<BYTE*>(dropData) + sizeof(DROPFILES), files, sizeof(files));
        GlobalUnlock(drop);
        check("native_file_list_data_is_readable", ReadableFormat(CF_HDROP, drop));
        GlobalFree(drop);
        const ProbeTask task{CopyEvent{1, GetTickCount64(), 1, GetCurrentProcessId(), 1}, 2};
        const ULONGLONG start = GetTickCount64();
        auto probe = ProbeWithMessagePump(task, 150, 4000);
        check("helper_timeout_is_bounded_without_clipboard_writes", probe.status == ProbeStatus::Timeout &&
            GetTickCount64() - start < 1500, probe.error);
        probe = ProbeWithMessagePump(task, 1000, 4000, true);
        check("helper_cancellation_is_prompt_without_clipboard_writes", probe.status == ProbeStatus::Canceled, probe.error);
    }

    HWINSTA originalStation = GetProcessWindowStation();
    HDESK originalDesktop = GetThreadDesktop(GetCurrentThreadId());
    const DWORD originalSequence = GetClipboardSequenceNumber();
    // Naming a station requires an administrator token. Ask Windows for the
    // caller's noninteractive station; verify it is invisible and empty before
    // writing synthetic test data. WinSta0 is never used for write tests.
    HWINSTA testStation = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    DWORD stationError = testStation ? ERROR_SUCCESS : GetLastError();
    bool isolated = false;
    bool usingEmptyToolStation = false;
    HDESK testDesktop{};
    if (testStation && ObjectName(testStation) != ObjectName(originalStation) && SetProcessWindowStation(testStation)) {
        testDesktop = CreateDesktopW(L"TestDesktop", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        isolated = testDesktop && SetThreadDesktop(testDesktop);
        if (!isolated) stationError = GetLastError();
        if (isolated) {
            USEROBJECTFLAGS candidateFlags{}; DWORD flagBytes{};
            isolated = GetUserObjectInformationW(testStation, UOI_FLAGS, &candidateFlags,
                sizeof(candidateFlags), &flagBytes) && !(candidateFlags.dwFlags & WSF_VISIBLE) &&
                CountClipboardFormats() == 0;
            if (!isolated) stationError = ERROR_BUSY;
        }
    } else if (testStation) stationError = GetLastError();
    // The execution host itself may already have an empty, noninteractive
    // station. That clipboard is also separate from WinSta0 (the user's
    // visible desktop). Reuse it only after proving it is invisible and empty.
    if (!isolated && !testStation) {
        USEROBJECTFLAGS originalFlags{}; DWORD flagBytes{};
        if (GetUserObjectInformationW(originalStation, UOI_FLAGS, &originalFlags,
            sizeof(originalFlags), &flagBytes) && !(originalFlags.dwFlags & WSF_VISIBLE) &&
            ObjectName(originalStation) != L"WinSta0" && CountClipboardFormats() == 0) {
            usingEmptyToolStation = true;
            isolated = true;
            stationError = 0;
        }
    }
    if (isolated) check("clipboard_tests_are_isolated_from_visible_desktop", true);
    else tests.push_back({"isolated_clipboard_integration_tests_unavailable", false, stationError, true});
    if (isolated) {
        USEROBJECTFLAGS flags{}; DWORD flagBytes{};
        const bool gotFlags = GetUserObjectInformationW(usingEmptyToolStation ? originalStation : testStation,
            UOI_FLAGS, &flags, sizeof(flags), &flagBytes) != FALSE;
        check("test_desktop_is_not_visible", gotFlags && !(flags.dwFlags & WSF_VISIBLE));
        WNDCLASSW windowClass{};
        windowClass.hInstance = appInstance;
        windowClass.lpfnWndProc = TestSourceProc;
        windowClass.lpszClassName = L"CopyConfirm.TestSource";
        RegisterClassW(&windowClass);
        HWND owner = CreateWindowW(windowClass.lpszClassName, L"", WS_OVERLAPPED,
            0, 0, 200, 100, nullptr, nullptr, appInstance, nullptr);
        // The fixture is intentionally narrow. Enable vertical scrolling so
        // WM_PASTE is not rejected when the synthetic text wraps beyond the
        // visible lines; viewport limits are unrelated to clipboard validity.
        HWND edit = CreateWindowW(L"EDIT", L"CopyConfirm native edit copy 测试", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL,
            0, 0, 100, 40, owner, nullptr, appInstance, nullptr);
        HWND paste = CreateWindowW(L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL,
            0, 40, 100, 40, owner, nullptr, appInstance, nullptr);
        check("hidden_test_edit_controls_created", owner && edit && paste);
        const DWORD sourcePid = GetCurrentProcessId();
        auto makeTask = [&](DWORD baseline) {
            return ProbeTask{CopyEvent{1, GetTickCount64(), baseline, sourcePid,
                reinterpret_cast<std::uintptr_t>(owner)}, GetClipboardSequenceNumber()};
        };
        if (owner && edit && paste) {
            check("test_clipboard_seeded_inside_isolated_station", SetTestText(owner, L"isolated seed"));
            DWORD baseline = GetClipboardSequenceNumber();
            SendMessageW(edit, EM_SETSEL, 0, -1);
            SendMessageW(edit, WM_COPY, 0, 0);
            auto task = makeTask(baseline);
            auto probe = ProbeWithMessagePump(task);
            check("actual_edit_copy_is_retrievable_from_helper", probe.status == ProbeStatus::Ok, probe.error);
            SendMessageW(paste, WM_PASTE, 0, 0);
            wchar_t pasted[160]{};
            GetWindowTextW(paste, pasted, 160);
            check("actual_edit_paste_matches_copied_test_text", wcscmp(pasted, L"CopyConfirm native edit copy 测试") == 0,
                static_cast<DWORD>(GetWindowTextLengthW(paste)));
            const DWORD firstSequence = GetClipboardSequenceNumber();
            SendMessageW(edit, WM_COPY, 0, 0);
            probe = ProbeWithMessagePump(makeTask(firstSequence));
            check("copying_identical_text_again_still_confirms", probe.status == ProbeStatus::Ok &&
                probe.sequence != firstSequence, probe.error);
            baseline = GetClipboardSequenceNumber();
            SetTestText(owner, L"wrong-owner-test");
            task = makeTask(baseline); task.copy.sourcePid = sourcePid + 1;
            probe = ProbeWithMessagePump(task);
            check("helper_rejects_different_source_process", probe.status == ProbeStatus::WrongOwner);

            baseline = GetClipboardSequenceNumber();
            OpenClipboard(owner); EmptyClipboard(); CloseClipboard();
            probe = ProbeWithMessagePump(makeTask(baseline));
            check("helper_rejects_empty_clipboard", probe.status == ProbeStatus::NoData);

            baseline = GetClipboardSequenceNumber();
            delayedRenderAllowed = true;
            OpenClipboard(owner); EmptyClipboard(); SetClipboardData(CF_UNICODETEXT, nullptr); CloseClipboard();
            probe = ProbeWithMessagePump(makeTask(baseline));
            check("delayed_text_is_rendered_before_confirmation", probe.status == ProbeStatus::Ok, probe.error);

            baseline = GetClipboardSequenceNumber();
            delayedRenderAllowed = false;
            OpenClipboard(owner); EmptyClipboard(); SetClipboardData(CF_UNICODETEXT, nullptr); CloseClipboard();
            probe = ProbeWithMessagePump(makeTask(baseline));
            check("failed_delayed_renderer_never_confirms", probe.status != ProbeStatus::Ok);

            baseline = GetClipboardSequenceNumber();
            OpenClipboard(owner); EmptyClipboard();
            const UINT custom = RegisterClipboardFormatW(L"CopyConfirm.Tests.Private");
            HGLOBAL privateData = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 16);
            SetClipboardData(custom, privateData); CloseClipboard();
            probe = ProbeWithMessagePump(makeTask(baseline));
            check("unknown_private_format_is_safely_skipped", probe.status == ProbeStatus::Unsupported);

            baseline = GetClipboardSequenceNumber();
            OpenClipboard(owner); EmptyClipboard();
            HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(BITMAPINFOHEADER) + 16);
            auto* header = static_cast<BITMAPINFOHEADER*>(GlobalLock(dib));
            header->biSize = sizeof(BITMAPINFOHEADER); header->biWidth = 2; header->biHeight = 2;
            header->biPlanes = 1; header->biBitCount = 32; header->biSizeImage = 16;
            GlobalUnlock(dib); SetClipboardData(CF_DIB, dib); CloseClipboard();
            probe = ProbeWithMessagePump(makeTask(baseline));
            check("bitmap_clipboard_data_is_supported", probe.status == ProbeStatus::Ok, probe.error);

            baseline = GetClipboardSequenceNumber();
            OpenClipboard(owner); EmptyClipboard();
            constexpr wchar_t files[] = L"C:\\CopyConfirm.synthetic.txt\0";
            HGLOBAL drop = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + sizeof(files));
            auto* dropData = static_cast<DROPFILES*>(GlobalLock(drop));
            dropData->pFiles = sizeof(DROPFILES); dropData->fWide = TRUE;
            std::memcpy(reinterpret_cast<BYTE*>(dropData) + sizeof(DROPFILES), files, sizeof(files));
            GlobalUnlock(drop); SetClipboardData(CF_HDROP, drop); CloseClipboard();
            probe = ProbeWithMessagePump(makeTask(baseline));
            check("file_list_clipboard_data_is_supported", probe.status == ProbeStatus::Ok, probe.error);

            SetTestText(owner, L"timeout-seed");
            task = makeTask(GetClipboardSequenceNumber() - 1);
            const ULONGLONG timeoutStart = GetTickCount64();
            probe = ProbeWithMessagePump(task, 150, 4000);
            check("slow_helper_is_terminated_at_timeout", probe.status == ProbeStatus::Timeout &&
                GetTickCount64() - timeoutStart < 1500);
            probe = ProbeWithMessagePump(task, 1000, 4000, true);
            check("superseded_helper_is_canceled_promptly", probe.status == ProbeStatus::Canceled);
        }
        if (owner && OpenClipboard(owner)) {
            EmptyClipboard(); CloseClipboard();
        }
        if (paste) DestroyWindow(paste);
        if (edit) DestroyWindow(edit);
        if (owner) DestroyWindow(owner);
        UnregisterClassW(windowClass.lpszClassName, appInstance);
    }
    SetProcessWindowStation(originalStation);
    SetThreadDesktop(originalDesktop);
    if (testDesktop) CloseDesktop(testDesktop);
    if (testStation) CloseWindowStation(testStation);
    WriteReport(reportPath, tests, isolated, stationError, originalSequence == GetClipboardSequenceNumber());
    return std::any_of(tests.begin(), tests.end(), [](const auto& test) { return !test.passed && !test.skipped; }) ? 1 : 0;
}

int RunReadOnlyProbeTest(const wchar_t* reportPath) {
    std::vector<TestCase> tests;
    const DWORD sequence = GetClipboardSequenceNumber();
    HWND owner = GetClipboardOwner();
    const DWORD pid = WindowPid(owner);
    if (!sequence || !owner || !pid || CountClipboardFormats() <= 0) {
        tests.push_back({"current_clipboard_has_no_owned_data_to_verify", false, 0, true});
    } else {
        const ProbeTask task{CopyEvent{1, GetTickCount64(), sequence - 1, pid,
            reinterpret_cast<std::uintptr_t>(owner)}, sequence};
        const auto result = ProbeWithMessagePump(task);
        tests.push_back({"current_clipboard_retrieved_in_read_only_helper", result.status == ProbeStatus::Ok,
            result.error ? result.error : static_cast<DWORD>(result.status), false});
    }
    WriteReport(reportPath, tests, false, 0, sequence == GetClipboardSequenceNumber(), "read_only_current_clipboard");
    return std::any_of(tests.begin(), tests.end(), [](const auto& test) { return !test.passed && !test.skipped; }) ? 1 : 0;
}

int RunLanguageTests(const wchar_t* reportPath) {
    std::vector<TestCase> tests;
    auto check = [&](const char* name, bool passed, DWORD detail = 0) {
        tests.push_back(TestCase{name, passed, detail});
    };
    for (const Language language : {Language::Chinese, Language::English}) {
        HMENU menu = BuildTrayMenu(language, false);
        HMENU submenu = menu ? GetSubMenu(menu, 1) : nullptr;
        const bool isEnglish = language == Language::English;
        check(isEnglish ? "english_language_submenu_has_two_options" : "chinese_language_submenu_has_two_options",
            menu && GetMenuItemCount(menu) == 5 && submenu && GetMenuItemCount(submenu) == 2);
        const UINT selected = isEnglish ? EnglishCommand : ChineseCommand;
        const UINT other = isEnglish ? ChineseCommand : EnglishCommand;
        check(isEnglish ? "english_radio_selected_without_chinese" : "chinese_radio_selected_without_english",
            submenu && (GetMenuState(submenu, selected, MF_BYCOMMAND) & MF_CHECKED) &&
            !(GetMenuState(submenu, other, MF_BYCOMMAND) & MF_CHECKED));
        wchar_t caption[96]{};
        if (menu) GetMenuStringW(menu, 1, caption, _countof(caption), MF_BYPOSITION);
        check(isEnglish ? "english_menu_heading_is_lowercase_language" : "chinese_menu_heading_is_lowercase_language",
            wcscmp(caption, L"language") == 0);
        if (menu) DestroyMenu(menu);
    }
    {
        HMENU menu = BuildTrayMenu(Language::English, true);
        wchar_t caption[96]{};
        if (menu) GetMenuStringW(menu, PauseCommand, caption, _countof(caption), MF_BYCOMMAND);
        check("paused_english_menu_offers_resume", menu && wcscmp(caption, Text(Language::English).resume) == 0);
        if (menu) DestroyMenu(menu);
    }
    for (const UINT dpi : {96U, 144U, 192U}) {
        ToastSurface chinese, english;
        const bool chineseRendered = chinese.Render(dpi, Language::Chinese);
        const bool englishRendered = english.Render(dpi, Language::English);
        const std::string suffix = std::to_string(dpi) + "dpi";
        const std::string chineseCase = "chinese_bold_caption_fits_at_" + suffix;
        const std::string englishCase = "english_bold_caption_fits_at_" + suffix;
        const std::string sizeCase = "languages_keep_same_panel_size_at_" + suffix;
        check(chineseCase.c_str(), chineseRendered && chinese.TextFits());
        check(englishCase.c_str(), englishRendered && english.TextFits());
        check(sizeCase.c_str(), chineseRendered && englishRendered &&
            chinese.width == english.width && chinese.height == english.height);
    }

    // Test real menu commands and persistence using a temporary INI beside
    // the test report. Never read or overwrite the user's actual settings.
    const std::wstring configPath = std::wstring(reportPath) + L".settings.ini";
    const bool newFixture = GetFileAttributesW(configPath.c_str()) == INVALID_FILE_ATTRIBUTES;
    check("temporary_language_settings_will_not_overwrite_existing_file", newFixture);
    if (newFixture) {
        App app(configPath);
        WNDCLASSW windowClass{};
        windowClass.hInstance = appInstance;
        windowClass.lpfnWndProc = MainWindowProc;
        windowClass.lpszClassName = L"CopyConfirm.LanguageTest";
        RegisterClassW(&windowClass);
        HWND window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            windowClass.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 0, 0,
            nullptr, nullptr, appInstance, &app);
        check("new_user_defaults_to_chinese", app.CurrentLanguage() == Language::Chinese);
        check("running_chinese_tooltip_matches_agreed_text",
            wcscmp(app.TrayTooltip().szTip, L"复制确认 · 正在运行") == 0);
        const auto tooltip = app.TrayTooltip();
        check("standard_hover_tooltip_enabled_for_version_four",
            window && tooltip.hWnd == window && tooltip.uID == 1 &&
            (tooltip.uFlags & (NIF_TIP | NIF_SHOWTIP)) == (NIF_TIP | NIF_SHOWTIP));
        if (window) SendMessageW(window, WM_COMMAND, EnglishCommand, 0);
        check("english_menu_command_switches_active_language", window && app.CurrentLanguage() == Language::English);
        check("language_switch_updates_running_tooltip",
            wcscmp(app.TrayTooltip().szTip, L"CopyConfirm · Running") == 0);
        App restarted(configPath);
        check("english_selection_survives_application_restart", restarted.CurrentLanguage() == Language::English);
        if (window) SendMessageW(window, WM_COMMAND, PauseCommand, 0);
        check("pause_command_updates_english_tooltip",
            paused.load() && wcscmp(app.TrayTooltip().szTip, L"CopyConfirm · Not running") == 0);
        if (window) SendMessageW(window, WM_COMMAND, ChineseCommand, 0);
        check("language_switch_preserves_paused_tooltip_state",
            paused.load() && wcscmp(app.TrayTooltip().szTip, L"复制确认 · 未运行") == 0);
        if (window) SendMessageW(window, WM_COMMAND, PauseCommand, 0);
        check("resume_command_updates_chinese_tooltip",
            !paused.load() && wcscmp(app.TrayTooltip().szTip, L"复制确认 · 正在运行") == 0);
        App switchedBack(configPath);
        check("switching_back_to_chinese_is_persisted", window && app.CurrentLanguage() == Language::Chinese &&
            switchedBack.CurrentLanguage() == Language::Chinese);
        if (window) DestroyWindow(window);
        UnregisterClassW(windowClass.lpszClassName, appInstance);
        MSG quit{};
        PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
        if (GetFileAttributesW(configPath.c_str()) != INVALID_FILE_ATTRIBUTES) DeleteFileW(configPath.c_str());
    }
    WriteReport(reportPath, tests, false);
    return std::any_of(tests.begin(), tests.end(), [](const auto& test) { return !test.passed; }) ? 1 : 0;
}

int RunStartupTests(const wchar_t* reportPath) {
    std::vector<TestCase> tests;
    const DWORD clipboardSequence = GetClipboardSequenceNumber();
    auto check = [&](const char* name, bool passed, DWORD detail = 0) {
        tests.push_back(TestCase{name, passed, detail});
    };
    // A unique volatile key never participates in Windows startup. All commands
    // below use this injected path, leaving the real Run key untouched.
    const std::wstring keyPath = L"Software\\CopyConfirm.Test." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(GetTickCount64());
    HKEY fixture{};
    DWORD disposition{};
    const LSTATUS created = RegCreateKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, nullptr,
        REG_OPTION_VOLATILE, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &fixture, &disposition);
    const bool newFixture = created == ERROR_SUCCESS && disposition == REG_CREATED_NEW_KEY;
    check("unique_non_startup_test_registry_key_created", newFixture, static_cast<DWORD>(created));
    if (newFixture) {
        const std::wstring executable = L"C:\\复制确认 Test\\CopyConfirm.exe";
        const std::wstring command = L"\"" + executable + L"\"";
        const auto empty = ReadStartup(executable, keyPath);
        check("missing_registration_is_unchecked", !empty.enabled && empty.error == ERROR_SUCCESS);
        const wchar_t unrelated[] = L"leave this value unchanged";
        const bool otherValue = RegSetValueExW(fixture, L"UnrelatedProgram", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(unrelated), sizeof(unrelated)) == ERROR_SUCCESS;
        check("enable_accepts_unicode_and_spaces", SetStartup(true, executable, keyPath) == ERROR_SUCCESS);
        wchar_t stored[261]{};
        DWORD bytes = sizeof(stored), type{};
        const LSTATUS storedResult = RegGetValueW(fixture, nullptr, StartupValueName,
            RRF_RT_REG_SZ, &type, stored, &bytes);
        check("startup_command_is_quoted_reg_sz", storedResult == ERROR_SUCCESS && type == REG_SZ &&
            command == stored && bytes == (command.size() + 1) * sizeof(wchar_t));
        const auto registered = ReadStartup(executable, keyPath);
        check("registration_is_read_back_from_windows", registered.enabled && registered.error == ERROR_SUCCESS);
        check("different_executable_is_not_shown_as_enabled",
            !ReadStartup(L"C:\\Other\\CopyConfirm.exe", keyPath).enabled);
        for (const Language language : {Language::Chinese, Language::English}) {
            const bool english = language == Language::English;
            HMENU menu = BuildTrayMenu(language, false, registered);
            const UINT state = menu ? GetMenuState(menu, AutoStartCommand, MF_BYCOMMAND) : 0;
            check(english ? "english_startup_menu_is_checked" : "chinese_startup_menu_is_checked",
                menu && state != static_cast<UINT>(-1) && (state & MF_CHECKED));
            wchar_t caption[96]{};
            if (menu) GetMenuStringW(menu, AutoStartCommand, caption, _countof(caption), MF_BYCOMMAND);
            check(english ? "english_startup_label_is_correct" : "chinese_startup_label_is_correct",
                wcscmp(caption, english ? L"Start with Windows" : L"开机自启动") == 0);
            if (menu) DestroyMenu(menu);
        }
        check("overlong_path_is_rejected_without_changing_setting",
            SetStartup(true, std::wstring(259, L'x'), keyPath) == ERROR_FILENAME_EXCED_RANGE &&
            ReadStartup(executable, keyPath).enabled);
        check("embedded_quote_is_rejected_without_changing_setting",
            SetStartup(true, L"C:\\bad\"path.exe", keyPath) == ERROR_INVALID_PARAMETER &&
            ReadStartup(executable, keyPath).enabled);
        check("disable_removes_startup_registration", SetStartup(false, executable, keyPath) == ERROR_SUCCESS &&
            !ReadStartup(executable, keyPath).enabled);
        wchar_t other[64]{};
        bytes = sizeof(other);
        check("disable_preserves_unrelated_registry_values", otherValue &&
            RegGetValueW(fixture, nullptr, L"UnrelatedProgram", RRF_RT_REG_SZ, nullptr, other, &bytes) ==
                ERROR_SUCCESS && wcscmp(other, unrelated) == 0);
        check("disabling_an_absent_registration_is_safe", SetStartup(false, executable, keyPath) == ERROR_SUCCESS);
        HMENU unchecked = BuildTrayMenu(Language::English, false, ReadStartup(executable, keyPath));
        const UINT uncheckedState = unchecked ? GetMenuState(unchecked, AutoStartCommand, MF_BYCOMMAND) : 0;
        check("disabled_registration_has_no_checkmark", unchecked && uncheckedState != static_cast<UINT>(-1) &&
            !(uncheckedState & MF_CHECKED));
        if (unchecked) DestroyMenu(unchecked);
        const DWORD invalidValue = 1;
        const bool invalidWritten = RegSetValueExW(fixture, StartupValueName, 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&invalidValue), sizeof(invalidValue)) == ERROR_SUCCESS;
        const auto invalid = ReadStartup(executable, keyPath);
        check("unexpected_registry_type_is_reported", invalidWritten && invalid.error != ERROR_SUCCESS);
        HMENU unavailable = BuildTrayMenu(Language::English, false, invalid);
        const UINT unavailableState = unavailable ? GetMenuState(unavailable, AutoStartCommand, MF_BYCOMMAND) : 0;
        check("unreadable_setting_is_disabled_in_menu", unavailable &&
            unavailableState != static_cast<UINT>(-1) && (unavailableState & (MF_GRAYED | MF_DISABLED)));
        if (unavailable) DestroyMenu(unavailable);
        RegDeleteValueW(fixture, StartupValueName);

        // Exercise the same native menu command used by the resident app.
        App app(L"", keyPath);
        WNDCLASSW windowClass{};
        windowClass.hInstance = appInstance;
        windowClass.lpfnWndProc = MainWindowProc;
        windowClass.lpszClassName = L"CopyConfirm.StartupTest";
        RegisterClassW(&windowClass);
        HWND window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            windowClass.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 0, 0,
            nullptr, nullptr, appInstance, &app);
        check("startup_menu_command_test_window_created", window != nullptr);
        if (window) SendMessageW(window, WM_COMMAND, AutoStartCommand, 0);
        check("startup_menu_command_enables_current_executable", window && app.CurrentStartup().enabled);
        App restarted(L"", keyPath);
        check("startup_checkmark_survives_application_restart", restarted.CurrentStartup().enabled);
        if (window) SendMessageW(window, WM_COMMAND, AutoStartCommand, 0);
        check("startup_menu_command_unchecks_and_unregisters", window && !app.CurrentStartup().enabled &&
            !restarted.CurrentStartup().enabled);
        if (window) DestroyWindow(window);
        UnregisterClassW(windowClass.lpszClassName, appInstance);
        MSG quit{};
        PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
    }
    if (fixture) RegCloseKey(fixture);
    if (newFixture) check("temporary_registry_fixture_removed",
        RegDeleteKeyW(HKEY_CURRENT_USER, keyPath.c_str()) == ERROR_SUCCESS);
    WriteReport(reportPath, tests, false, 0, clipboardSequence == GetClipboardSequenceNumber());
    return std::any_of(tests.begin(), tests.end(), [](const auto& test) { return !test.passed; }) ? 1 : 0;
}

int RunSmokeTest(const wchar_t* reportPath) {
    std::vector<TestCase> tests;
    const DWORD initialSequence = GetClipboardSequenceNumber();
    paused.store(true);
    WNDCLASSW windowClass{};
    windowClass.hInstance = appInstance;
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.lpszClassName = L"CopyConfirm.SmokeTest";
    RegisterClassW(&windowClass);
    mainWindow = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, windowClass.lpszClassName,
        L"", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, appInstance, nullptr);
    tests.push_back({"native_hidden_window_created", mainWindow != nullptr, GetLastError()});
    const bool listener = mainWindow && AddClipboardFormatListener(mainWindow);
    tests.push_back({"clipboard_notifications_registered", listener, listener ? 0UL : GetLastError()});
    {
        KeyboardThread keyboard;
        const bool started = keyboard.Start();
        tests.push_back({"global_keyboard_hook_installed_on_dedicated_thread", started, started ? 0UL : GetLastError()});
    }
    ToastSurface surface;
    tests.push_back({"native_chinese_toast_rendered_at_150_percent_dpi", surface.Render(144), 0});
    HWND toast = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_LAYERED |
        WS_EX_TOPMOST, windowClass.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0,
        nullptr, nullptr, appInstance, nullptr);
    const LONG_PTR style = toast ? GetWindowLongPtrW(toast, GWL_EXSTYLE) : 0;
    tests.push_back({"overlay_has_click_through_and_no_activate_styles", toast &&
        (style & WS_EX_NOACTIVATE) && (style & WS_EX_TRANSPARENT) && (style & WS_EX_TOOLWINDOW), 0});
    if (toast) DestroyWindow(toast);
    if (listener) RemoveClipboardFormatListener(mainWindow);
    if (mainWindow) DestroyWindow(mainWindow);
    mainWindow = nullptr;
    UnregisterClassW(windowClass.lpszClassName, appInstance);
    paused.store(false);
    WriteReport(reportPath, tests, false, 0, initialSequence == GetClipboardSequenceNumber());
    return std::any_of(tests.begin(), tests.end(), [](const auto& test) { return !test.passed; }) ? 1 : 0;
}
}

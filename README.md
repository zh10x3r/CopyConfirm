# CopyConfirm

A lightweight copy confirmation utility for Windows 10 / 11 (64-bit). Run the EXE directly; it creates a per-user Start menu shortcut so Windows can associate its notifications. It runs without Python, .NET, or third-party DLLs.

[Download the latest CopyConfirm.exe](https://github.com/zh10x3r/CopyConfirm/releases/latest/download/CopyConfirm.exe) · [View all releases](https://github.com/zh10x3r/CopyConfirm/releases)

Chinese:

![Chinese copy confirmation](preview.png)

English:

![English copy confirmation](preview-en.png)

## Usage

1. Double-click `CopyConfirm.exe`. A fresh installation starts in English; the language you choose is saved for future launches. After the program starts successfully, Windows displays a system notification in the selected language: **软件已经开始运行** in Chinese or **CopyConfirm is now running** in English. You can then find the CopyConfirm icon in the system tray at the bottom-right corner of the taskbar. On some computers, you may need to expand the hidden icons.
2. Select some content and press **Ctrl+C** as usual.
3. Once the program confirms that the clipboard has been updated and the new data is readable, it briefly displays **Copied to clipboard** near the bottom of the screen containing the active application. The popup lasts 1.3 seconds by default, including its fade.
4. Right-click the system tray icon to pause or resume notifications, or exit the program.
5. Right-click → **language → 中文** to switch to Chinese, or choose **English** to switch back. The notification, context menu, and tray tooltip follow the saved language choice. The submenu heading always reads **language**, in either interface language.
6. Check **Start with Windows** (**开机自启动** in Chinese) to run the program automatically when you sign in to Windows. Click it again to disable this. The option is off by default and applies to the current user.
7. Open **Popup Settings…** (**提示设置…** in Chinese) from the tray menu to adjust **Bottom Gap** (0–300 pixels at 100% scaling, default 32) and **Popup Duration** (0.5–5.0 seconds, default 1.3). The gap is measured from the white panel to the bottom of the display's work area. With a bottom taskbar, 0 aligns the panel with the top of the taskbar. Use **Preview** to try the draft and **OK** to save it. **Restore Defaults** fills in the original values.

Hover over the tray icon to see the current notification state in the selected language. English tooltips read **CopyConfirm · Running** or **CopyConfirm · Not running**; "Not running" means notifications are paused. The tooltip updates after changing the language or pausing/resuming notifications.

The keyboard listener renews its registration every two seconds and immediately after sleep resumes. This allows notifications to recover if Windows silently removes the low-level keyboard hook. A failed renewal is retried automatically.

Keep the executable in a permanent folder when using automatic startup. If you move it, select **Start with Windows** again from the new location.

The program responds only to actual **Ctrl+C** keystrokes without Shift, Alt, or Win modifiers. Other operations, such as copying through a context menu, pressing Ctrl+X, or taking screenshots, do not trigger notifications.

## Confirmation Rules

For each Ctrl+C operation, the program records the clipboard sequence number and the active application before the keystroke. It then checks the following conditions within a 1.8-second observation window:

- The clipboard sequence number has actually changed.
- The process responsible for writing to the clipboard matches the foreground application's process at the time of the keystroke.
- At least one supported format in the new clipboard data can actually be read.
- Immediately before displaying the notification, the successfully read data still corresponds to the current clipboard, and the user remains in the same application.

Supported formats include common text formats, file lists, bitmaps, enhanced metafiles, HTML, RTF, PNG, JFIF, and others. Even when the copied text is identical to the previous clipboard content, the operation can still be confirmed as long as the application writes to the clipboard again.

Clipboard data is read in a subprocess with enforced timeouts, preventing applications that delay clipboard data generation from blocking keyboard monitoring. The program does not poll or read the clipboard when there is no pending copy operation to confirm.

## When Notifications May Not Appear

- The application reuses existing clipboard data for identical content instead of performing a new write.
- The clipboard write is performed by another process, a clipboard manager, or a Remote Desktop agent.
- The data is available only in application-specific private formats, or data generation exceeds the observation or reading timeout.
- The user switches applications, clears the clipboard, or copies something else immediately after copying.
- Permission restrictions or a secure desktop prevent the program from monitoring the application's operations.

**The absence of a notification does not necessarily mean that copying failed.** Windows does not provide independent applications with a universal acknowledgment that user-selected content has been successfully copied. This utility therefore uses a conservative confirmation mechanism based on keyboard shortcuts, the writing process, clipboard sequence numbers, and actual data readability.

## Data Handling

During normal operation, the program only reads the clipboard to verify data availability. It does not modify clipboard contents, save copied data or clipboard history, record unrelated keystrokes, or access the network. Automatic startup is registered only when you check **Start with Windows**; unchecking the option removes that registration. The program also creates or updates one per-user Start menu shortcut for Windows notification attribution.

The verification subprocess returns only status information and clipboard format identifiers through memory. It does not return the copied content.

`CopyConfirm.ini` sits next to the executable and stores the language and popup preferences. Existing language settings are used on upgrade, and missing popup settings use the defaults. The file contains no clipboard data and is excluded from Git tracking through `.gitignore`.

## Source Code and Building

- `source/CopyConfirm.cpp` implements the Windows interface, keyboard monitoring, clipboard verification, and notification rendering.
- `source/popup_dialog.hpp` and `source/popup_settings.hpp` implement the native settings dialog and its saved preferences.
- `source/windows_notification.hpp` sends the localized Windows startup notification and maintains its per-user Start menu registration.
- `source/detector.hpp` implements the confirmation state machine.
- `source/localization.hpp` defines the Chinese and English interface strings and fonts.
- `source/selftest.inc` contains automated checks.
- `source/build-msvc.ps1` builds with an existing Visual Studio C++ desktop toolchain and a static C++ runtime.
- `source/CopyConfirm.png` is the approved icon artwork; `source/CopyConfirm.ico` contains the Windows icon at sizes from 16 to 256 pixels.

With either MSYS2 UCRT64 or Visual Studio's C++ desktop tools installed, run the following command in PowerShell:

```powershell
.\source\build.ps1
```

The script first looks for MSYS2 UCRT64 at `C:\msys64\ucrt64\bin\` (or the location passed to `-Toolchain`). If it is unavailable, it finds an installed Visual Studio C++ toolchain. Neither build path installs software. Both produce a standalone x64 executable using a static C++ runtime.

The generated executable is placed in the project root directory, while temporary build files are stored in the system's temporary directory. Git ignores the executable and build caches.

## Validation

The local v1.2.6 build passed 134 checks, including native dialog tests for both languages, preview timing, save and cancel, restoring defaults, and loading existing preferences. Placement was tested with simulated work areas and display scaling, and the live preview was exercised in a hidden layered window on one display. The current environment blocked the isolated clipboard, temporary startup registry, and read-only clipboard helper tests. The same access restrictions were reproduced with v1.2.5.

The v1.2.5 keyboard listener investigation is in `validation/keyboard-failure-analysis.md`. The v1.2.4 startup notification report and the previous successful startup and read-only clipboard tests are retained as historical evidence. Actual sleep/unlock cycles and a multi-day interactive soak have not been performed.

Reports are in `validation/`. A full Windows sign-out/reboot startup test has not been performed; compatibility with individual third-party applications still depends on their clipboard behavior.

## Implementation References

- [Using the Windows Clipboard](https://learn.microsoft.com/en-us/windows/win32/dataxchg/using-the-clipboard)
- [GetClipboardSequenceNumber](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardsequencenumber)
- [GetClipboardData](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboarddata)
- [LowLevelKeyboardProc](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc)
- [Run and RunOnce registry keys](https://learn.microsoft.com/en-us/windows/win32/setupapi/run-and-runonce-registry-keys)

# CopyConfirm

A lightweight copy confirmation utility for Windows 10 / 11 (64-bit). Run the EXE directly; it creates a per-user Start menu shortcut so Windows can associate its notifications. It runs without Python, .NET, or third-party DLLs.

[Download the latest CopyConfirm.exe](https://github.com/zh10x3r/CopyConfirm/releases/latest/download/CopyConfirm.exe) · [View all releases](https://github.com/zh10x3r/CopyConfirm/releases)

Chinese:

![Chinese copy confirmation](preview.png)

English:

![English copy confirmation](preview-en.png)

## Usage

1. Double-click `CopyConfirm.exe`. After it starts successfully, Windows displays a system notification: **软件已经开始运行** in Chinese or **CopyConfirm is now running** in English. You can then find the CopyConfirm icon in the system tray at the bottom-right corner of the taskbar. On some computers, you may need to expand the hidden icons.
2. Select some content and press **Ctrl+C** as usual.
3. Once the program confirms that the clipboard has been updated and the new data is readable, it briefly displays **Copied to clipboard** near the bottom of the screen containing the active application. The notification disappears after approximately 1.3 seconds.
4. Right-click the system tray icon to pause or resume notifications, or exit the program.
5. Right-click → **language → English** to switch to English. The notification will display **Copied to clipboard**, and the context menu and tray tooltip will also switch to English. Select **language → 中文** to switch back to Chinese. The submenu heading always reads **language**, in either interface language.
6. Check **Start with Windows** (**开机自启动** in Chinese) to run the program automatically when you sign in to Windows. Click it again to disable this. The option is off by default and applies to the current user.

Hover over the tray icon to see the current notification state in the selected language. English tooltips read **CopyConfirm · Running** or **CopyConfirm · Not running**; "Not running" means notifications are paused. The tooltip updates after changing the language or pausing/resuming notifications.

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

`CopyConfirm.ini` stores only the language preference and contains no clipboard data. It is excluded from Git tracking through `.gitignore`.

## Source Code and Building

- `source/CopyConfirm.cpp` implements the Windows interface, keyboard monitoring, clipboard verification, and notification rendering.
- `source/windows_notification.hpp` sends the localized Windows startup notification and maintains its per-user Start menu registration.
- `source/detector.hpp` implements the confirmation state machine.
- `source/localization.hpp` defines the Chinese and English interface strings and fonts.
- `source/selftest.inc` contains automated checks.
- `source/CopyConfirm.png` is the approved icon artwork; `source/CopyConfirm.ico` contains the Windows icon at sizes from 16 to 256 pixels.

If the MSYS2 UCRT64 toolchain is already installed, run the following command in PowerShell:

```powershell
.\source\build.ps1
```

The default toolchain location is `C:\msys64\ucrt64\bin\`. You can specify a different location using the `-Toolchain` parameter. The build script does not install any software.

The generated executable is placed in the project root directory, while temporary build files are stored in the system's temporary directory. Git ignores the executable and build caches.

## Validation

The v1.2.3 build passed 110 automated checks. The read-only probe also confirmed that the current clipboard could be read without changing it. The startup-notification integration check confirmed Windows accepted the localized notification through the native notification API. Windows loaded all eight embedded icon sizes successfully, and each embedded image matched the source icon. These checks cover clipboard confirmation and native copy/paste in an isolated test desktop, native windows and rendering, both interface languages, the fixed **language** heading, startup registration and menu commands, and the localized startup message. Startup tests use a temporary registry key that Windows does not execute, and verify registration, quoted paths containing spaces and Chinese characters, persistence, unregistration, and preservation of unrelated values. The notification test creates or updates the current user's Start menu shortcut; the real Windows automatic-start setting and visible desktop clipboard remain unchanged. Windows controls whether the notification appears as a banner or in its notification center.

Reports are in `validation/`. A full Windows sign-out/reboot startup test has not been performed; compatibility with individual third-party applications still depends on their clipboard behavior.

## Implementation References

- [Using the Windows Clipboard](https://learn.microsoft.com/en-us/windows/win32/dataxchg/using-the-clipboard)
- [GetClipboardSequenceNumber](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardsequencenumber)
- [GetClipboardData](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboarddata)
- [LowLevelKeyboardProc](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc)
- [Run and RunOnce registry keys](https://learn.microsoft.com/en-us/windows/win32/setupapi/run-and-runonce-registry-keys)

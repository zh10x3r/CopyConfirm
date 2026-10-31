# Keyboard notification outage: investigation and fix

## User observation

Ctrl+C still copies successfully, but the CopyConfirm overlay sometimes stops appearing. Restarting CopyConfirm restores it. No failed resident process or historical trace was available during this investigation.

## Confirmed defect

v1.2.4 installed `WH_KEYBOARD_LL` once in `KeyboardThread::Entry`. The `installed_` flag only described the initial installation. The message loop had no periodic renewal or resume recovery.

[Microsoft's LowLevelKeyboardProc documentation](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc) states that Windows can silently remove a low-level hook after a timeout, without a supported removal notification. On current Windows versions, the timeout is capped at 1 second. An apparently running process can therefore stop receiving copy key events while Ctrl+C continues to work normally. Reopening v1.2.4 installs a new hook, explaining the recovery after restart.

The historical trigger cannot be proven without a trace from the failed instance. The reproduced defect is the permanent loss of monitoring after a removed hook; the safe test removes the hook directly rather than intentionally delaying a keyboard callback on the user's desktop.

## Baseline reproduction

The baseline harness compiled the unchanged v1.2.4 source, wrapping only `SetWindowsHookExW` to count successful registrations. It then removed its own real native hook with `UnhookWindowsHookEx` and left the listener thread running for 2,600 ms.

- Before removal: one successful registration.
- After waiting: still one registration; no recovery.
- Clipboard sequence and foreground window unchanged; no keys injected.

Raw results: `keyboard-baseline-v1.2.4.json`.

## v1.2.5 behavior

- Renew the hook on its owning thread every 2,000 ms; retry a failed installation on the next tick.
- Install a replacement before releasing the previous hook, preserving a healthy listener when installation fails and avoiding a registration gap. Do not remove a replacement if Windows recycled a stale handle value.
- Synchronize the C-key held state at renewal, allowing missed key-up state to recover while suppressing held-key repeats.
- Refresh immediately after a resume message and invalidate pre-suspend copy attempts.
- Stop keyboard monitoring before destroying the clipboard worker/window.

## Verification

`keyboard-recovery-test.json` contains 13 passing checks using real native hooks, including recovery with the production timer, eight consecutive losses, transient installation failures, pause preservation, resume-message handling, restart, and cleanup. Fault injection removes only the test process's own hooks. The entire current-build suite records 126 passed, 0 failed, and 0 skipped. No test injects input into the user's desktop or writes its clipboard. The real Windows automatic-start value is not modified.

The native tests verify registration recovery; they do not deliberately force an OS hook timeout. A physical sleep/unlock cycle and multi-day interactive soak remain untested.

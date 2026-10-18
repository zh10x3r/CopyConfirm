# 复制确认 · HaveICopied

Windows 10 / 11 的轻量复制提示工具，适用于 64 位 Windows。免安装，运行时不需要 Python、.NET 或第三方 DLL。

[下载最新版 CopyConfirm.exe](https://github.com/zh10x3r/CopyConfirm/releases/latest/download/CopyConfirm.exe) · [查看全部版本](https://github.com/zh10x3r/CopyConfirm/releases)

中文提示：

![中文复制提示](preview.png)

English:

![English copy confirmation](preview-en.png)

## 使用

1. 双击 `CopyConfirm.exe`。运行后可在任务栏右下角托盘中找到“复制确认”图标，部分电脑需要展开隐藏图标。
2. 照常选中内容并按 **Ctrl+C**。
3. 程序确认这次剪贴板更新可读取后，在当前应用所在屏幕的下方短暂显示 **已复制到剪贴板**，约 1.3 秒后消失。
4. 右键托盘图标，可以暂停、恢复提示，或退出程序。
5. 右键 → **语言 → English** 可切换到英语，提示文字为 **Copied to clipboard**，菜单和托盘提示也会切换为英语。通过 **Language → 中文** 可切回中文。

弹窗为白色、粗体纯文字、小圆角和浅阴影，鼠标可穿透，弹出时不取得输入焦点。

中文使用 12 px 微软雅黑 UI 粗体，英文使用 13 px Segoe UI 粗体，两种语言的视觉大小接近，提示框大小一致。语言选择保存在程序旁的 `CopyConfirm.ini`，重新打开后会沿用选择；程序目录需要允许当前用户写入才能保存设置。

程序仅响应不带 Shift、Alt、Win 的实际 Ctrl+C 按键。右键菜单复制、Ctrl+X、截图等其他操作不触发提示。

## 确认规则

每次 Ctrl+C 都记录操作前的剪贴板序号和当前应用。随后在 1.8 秒的观察窗口内确认：

- 剪贴板序号确实更新。
- 剪贴板的写入进程与按键时的前台应用进程一致。
- 新数据至少有一种受支持格式能够实际读取。
- 显示提示前，读取结果仍对应当前剪贴板，且用户还在该应用中。

支持常见文字、文件列表、位图、增强型图元文件，以及 HTML、RTF、PNG、JFIF 等格式。即使两次复制的文字完全相同，只要应用重新写入了剪贴板，也可以确认。

读取在受超时限制的子进程内完成，避免某些应用延迟生成剪贴板数据时阻塞键盘监听。没有正在确认的复制操作时，不轮询读取剪贴板。

## 可能不出现提示的情况

- 应用对重复内容直接复用旧剪贴板，没有产生新的写入。
- 复制由另一个进程、剪贴板管理器或远程桌面代理代写。
- 数据只有该软件独有的私有格式，或数据生成超过观察、读取时限。
- 在复制后马上切换应用、清空剪贴板，或又复制了其他内容。
- 权限或安全桌面阻止程序观察该应用的操作。

**没有提示不一定表示复制失败。** Windows 没有向独立程序提供“用户选中内容已成功复制”的通用回执；本工具根据快捷键、写入进程、更新序号和实际可读取的数据谨慎确认。

## 数据处理

正常运行时只读取剪贴板以确认数据可用，不改写剪贴板，不保存复制内容或历史，不记录其他按键，不联网，也不注册开机启动。验证子进程通过内存传回状态和格式编号，不传回复制内容。

`CopyConfirm.ini` 仅记录语言偏好，不包含剪贴板内容，并已加入 Git 忽略规则。

## 源码和编译

`source/CopyConfirm.cpp` 是 Windows 窗口、键盘监听、剪贴板验证和弹窗实现；`source/detector.hpp` 是确认状态机；`source/localization.hpp` 定义中英文界面文字和字体；`source/selftest.inc` 是自动检查。

已有 MSYS2 UCRT64 工具链时，在 PowerShell 中运行：

```powershell
.\source\build.ps1
```

工具链默认位置为 `C:\msys64\ucrt64\bin`，可以使用 `-Toolchain` 参数指定。脚本不安装任何软件。生成的可执行文件位于项目根目录，临时编译文件放在系统临时目录。Git 会忽略可执行文件和编译缓存。

## 验证

具体结果见 `validation/`。检测逻辑、原生数据句柄、子进程超时及取消、Windows 剪贴板监听注册、全局键盘钩子安装和中文弹窗渲染均有自动检查。

语言选项另有检查，覆盖原生菜单的中英文单选状态、菜单命令切换、重启后的设置恢复，以及 100%、150%、200% 缩放下两种语言的文字是否超出边界。

本次发布通过 76 项检查，包含隔离环境中的标准编辑框实际复制、数据读取和粘贴、重复复制同一内容、图片和文件列表、延迟生成数据、超时退出、中英文切换与缩放检查。测试没有改动可见桌面的剪贴板。

当前桌面没有可用于额外只读探针验证的、带所有者信息的数据，该项被跳过，未计作通过。浏览器、Office、微信和资源管理器仍需按实际使用情况检查兼容性。

自动检查命令（输出为状态信息，不包含复制内容）：

```powershell
Start-Process .\CopyConfirm.exe -ArgumentList '--self-test "C:\path\self-test.json"' -WindowStyle Hidden -Wait
Start-Process .\CopyConfirm.exe -ArgumentList '--smoke-test "C:\path\smoke-test.json"' -WindowStyle Hidden -Wait
Start-Process .\CopyConfirm.exe -ArgumentList '--language-test "C:\path\language-test.json"' -WindowStyle Hidden -Wait
```

`--self-test` 只有确认测试剪贴板与可见桌面隔离后，才会用合成内容进行写入测试；无法建立隔离时会跳过这些测试。`--smoke-test` 不写入剪贴板，也不模拟真实按键。

## 实现参考

- [Windows 剪贴板监听](https://learn.microsoft.com/en-us/windows/win32/dataxchg/using-the-clipboard)
- [GetClipboardSequenceNumber](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardsequencenumber)
- [GetClipboardData](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboarddata)
- [低级键盘钩子](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc)

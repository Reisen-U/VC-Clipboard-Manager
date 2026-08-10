# VC Clipboard Manager

一个面向 Windows 的轻量级剪贴板历史管理器。

> **VC = Vibe Coding**
>
> 本项目是一个 Vibe Coding 项目，名称中的 VC 不是 Visual C++，而是 Vibe Coding。

## 项目起因

我看到不少人使用 AI agent 编写基于 Tauri 的剪贴板管理器。Tauri 让跨平台开发和界面迭代变得很方便，但这类应用的 UI 本质上仍然是 WebView/Web UI。

所以我也用 Vibe Coding 做了这个原生 C/C++ Win32 项目：不依赖网页渲染层，直接使用 Windows API 处理剪贴板、全局快捷键、系统托盘、缓存和开机启动。它是一个面向 Windows 的小工具，也是一份对“让 AI agent 直接写原生桌面程序”这一方向的实践记录。

## 功能

- 系统托盘常驻，支持全局快捷键唤出：`Ctrl + Shift + V`、`Alt + V`、`Win + V`
- 保存并浏览文本、图片和文件列表剪贴板内容
- 双击历史项即可粘贴回当前应用
- 文本换行、显示行数、字体大小、历史条数和保留天数可配置
- 图片异步生成 PNG 缓存和缩略图，避免阻塞界面
- 可配置复制提示音
- 可选的“开机自动启动”设置；取消勾选并保存即可移除启动项
- 支持 Windows 非整数显示缩放，并声明 Per-Monitor V2 DPI 感知

## 界面截图

![VC Clipboard Manager 界面](./界面截图.jpg)

## 构建

项目使用原生 Win32 C++、GDI/GDI+ 和 MinGW-w64 构建，不依赖 Tauri、Electron 或其他 UI 框架。

需要安装：

- MinGW-w64 g++
- GNU windres

在项目目录执行：

```bat
windres resource.rc -o resource.o
g++ main.cpp resource.o -o ClipboardManager.exe -mwindows -municode -static -lgdi32 -lshell32 -lole32 -lgdiplus -lwinmm -ladvapi32
```

也可以直接运行 `compile-command.bat`。

`app.manifest` 会通过 `resource.rc` 嵌入程序，保证 Windows 能正确识别 DPI 感知设置。

## 数据与设置

剪贴板历史默认保存在：

```text
%LOCALAPPDATA%\ClipManager
```

开机自启动使用当前用户注册表项：

```text
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run\VCClipboardManager
```

程序不需要管理员权限。设置页取消“开机自动启动”并点击“保存并应用”后，会删除该注册表值。

## 为什么选择原生 Win32

相对于常见的 Tauri 或 Electron 剪贴板管理器，本项目的取舍是：

- **更小的运行时开销**：不打包 Chromium；也不需要额外的 WebView 前端层，通常能降低安装体积、内存占用和启动负担。
- **更直接的 Windows 集成**：剪贴板监听、全局热键、托盘菜单、注册表启动项和文件缓存都直接调用 Windows API，减少跨语言桥接层。
- **更少的渲染缩放层**：界面使用 GDI/GDI+ 绘制，并显式启用 Per-Monitor V2 DPI 感知，避免窗口整体被系统按位图放大。
- **单文件部署简单**：编译后的程序可以直接运行，不需要 Node.js、Chromium 或单独的前端运行时。

这也是一个明确的 Windows-only 取舍：Tauri/Electron 更适合跨平台产品和复杂 Web UI，而原生 Win32 更适合本项目这种常驻、快速唤出、低开销的系统工具。

## Vibe Coding 声明

本项目在 Vibe Coding 工作流中完成和迭代，使用了以下模型协助设计、编写、调试和优化：

- Claude Opus
- GPT-5.6
- DeepSeek V4 Pro

## 音效来源

项目示例提示音来自 Mixkit 的 Correct 音效资源：

- [Mixkit — Correct sound effects](https://mixkit.co/free-sound-effects/correct/)

使用或再分发音效时，请同时遵循 Mixkit 当时适用的授权条款。

## 许可

本项目使用 [MIT License](./LICENSE) 开源。

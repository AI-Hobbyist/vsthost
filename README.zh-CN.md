**🌐 [English](README.md) | [中文](README.zh-CN.md)**

<div align="center">

# vsthost — 单插件 VST 宿主

**一个 exe = 一个插件** 的轻量 VST 宿主：把宿主改名为插件同名放到插件目录，双击即自动加载出声。
支持 **VST2 / VST3** 插件、**ASIO / JACK2** 音频后端，内置 **ITU-R BS.1770 响度电平表** 与 MIDI 参数映射。

![Platform](https://img.shields.io/badge/platform-Windows-blue)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C)
![UI](https://img.shields.io/badge/UI-MFC-green)
![Audio](https://img.shields.io/badge/audio-ASIO%20%7C%20JACK2-orange)
![License](https://img.shields.io/badge/license-LGPL--2.1-lightgrey)

</div>

> **衍生与致谢**：本项目衍生于 [https://github.com/Arakula/vsthost](https://github.com/Arakula/vsthost)（Hermann Seib 的 VSTHost 开源版），在此尊重并感谢原开发者的工作。
>
> **AI 声明**：本仓库的改造、重构与大部分代码由 **AI（GitHub Copilot）辅助编写**。

---

## 特性

- **同名自动加载**：`vsthost.exe` 改名为插件同名（`MyEffect.dll` / `MyEffect.vst3` → 旁放 `MyEffect.exe`），双击即自动加载该插件并出声；也支持把插件**拖到窗口**加载或用命令行指定。
- **单插件精简**：一次只加载 1 个插件，无机架/链等冗余；32 位插件配 x86 宿主、64 位插件配 x64 宿主。
- **双音频后端**：**ASIO**（驱动选择 / 控制面板 / 通道映射 / 采样率缓冲配置）与 **JACK2**（原生 JACK client，音频与 MIDI 端口随插件能力动态注册，客户端名=插件名）。
- **响度电平表**：I/O 双路 dB 表 + 独立窗口的 **Momentary / Short-term / Integrated / LRA / True Peak**（BS.1770-4 / EBU R128 / ATSC 等标准）+ **CSV 响度日志**。
- **MIDI**：winmm 输入（设备/通道过滤）+ **VSTi 乐器** + **CC→参数映射**；JACK 下走 `midi_in / midi_out` 端口。
- **Shell 支持**：Waves WaveShell 内部效果器，支持 `(Shell文件名)内部效果器名.exe` 直选与菜单内随时切换（记忆 `last_uid`）。
- **托盘与全局设置**：最小化到托盘、关闭行为可配（每次询问/最小化/完全关闭，可“不再提示”）。
- **稳定性**：插件状态 30s 自动保存（防崩溃丢失）、JACK 服务器退出自动停音频、JACK 不可用自动回退 ASIO。

## 快速开始

1. 把 `vsthost.exe` **改名为插件同名**放到插件目录：
   - `MyEffect.dll` → 旁放 `MyEffect.exe`
   - `MyEffect.vst3`（目录或单文件）→ 旁放 `MyEffect.exe`
   - 两者并存时默认加载 VST3
2. 双击该 exe 即自动加载并出声；也可以把插件**拖到窗口**上加载，或用命令行 `vsthost.exe C:\path\plugin.vst3`。
3. **Shell 内部效果器**：把 exe 改为 `(Shell文件名)内部效果器名.exe`，如
   `(WaveShell1-VST3 16.6_x64)Magma StressBox Stereo.exe`，启动直接加载该内部效果器；
   也可在菜单“插件 → 内部效果器”随时切换，选择会被记忆（`last_uid`）。

## 功能一览

### 音频后端

- 菜单 **音频 → 使用 ASIO / 使用 JACK**（Radio）切换；`[View] backend` 记忆。
- **启动自动检测**：未配置过后端时，若检测到 JACK 服务器在运行则默认 JACK，否则 ASIO。
- **JACK 未运行自动回退 ASIO**：启用 JACK 但服务器未运行/库缺失时自动改用 ASIO 出声并提示。
- **ASIO**：默认设备 = FL Studio ASIO（避免抢占其他 DAW）；`音频 → ASIO 设备` 选择驱动、
  `驱动控制面板`、`ASIO 通道分配`（插件通道 ↔ ASIO 通道映射）；采样率/缓冲在“全局设置”配置。
- **JACK2**（动态链接 `libjack64.dll` / `libjack.dll`，缺失时提示安装）：**端口随插件能力注册**——
  音频 `in_1..N / out_1..M` + MIDI `midi_in / midi_out`（按插件是否支持 MIDI）；客户端名 = 当前插件名
  （如 `Magma StressBox Stereo_1`，多实例序号唯一）；采样率/块大小取自服务器，变化自动重配；
  服务器退出自动停音频。状态栏显示 `DSP xx%` 与 `JACK: <客户端名>`。

### 电平表与响度

- **主窗口左右垂直电平表**（I/O，dB 刻度 -60 ~ +12，0dB 以上红区，峰值保持线 + 数值框）。
- 菜单 **视图 → 独立电平表窗口**：I/O 峰值 + **Momentary / Short-term / Integrated / LRA / True Peak**；
  响度标准（BS.1770-4 / EBU R128 / ATSC / 平台参考）、响度源切换、参考线、真峰值上限。
- **CSV 响度日志**：按间隔记录到 `loudness_YYYYMMDD_HHMMSS.csv`，重置后自动开新文件。
- 设置集中在 **视图 → 电平表设置**（刷新频率/峰值保持/静音重置/静音阈值/CSV/标准等，滑块+输入框）。

### MIDI

- **全局设置**里启用 MIDI 输入（winmm）、选设备与通道（Omni/1~16 过滤）。
- JACK 模式下 MIDI 输入来自 `midi_in` 端口（仅通道过滤可设）。
- **插件 → MIDI 参数映射**：把外部控制器的 CC 映射到插件参数（VST2/VST3 均支持）。
- 支持 **VSTi 乐器**（`midi_in` 有信号即可弹，输入电平表自动隐藏）。

### 全局设置与托盘

- **文件 → 全局设置**：关闭行为（每次询问/最小化到托盘/完全关闭）+ ASIO 采样率/缓冲 + MIDI。
- **托盘**：最小化到托盘、关闭询问（可“不再提示”）；托盘图标左键/双击恢复、右键菜单。
- 文件 → 退出 直接完全关闭。

## 开发模板（二次开发）

本项目可作为一个**最小单插件宿主开发模板**使用——结构精简、无资源脚本（菜单运行时构建）、
双平台单工程，适合在其上扩展自己的功能：

- **源码入口**：
  - `src/app/MainWnd.cpp` — 主窗口/菜单（运行时构建）/电平表/后端调度/托盘；
  - `src/host/IPlugin.h` — 插件抽象接口（新增插件格式从实现它开始）；
  - `src/host/AsioBackend.*` / `JackBackend.*` — 音频后端（新增后端同样实现 `IAudioBackend`）；
  - `src/dsp/LoudnessCore.*` — 响度核心（BS.1770）；
  - `src/ui/*` — 各对话框（全局设置/电平表设置/关闭询问/MIDI 映射）。
- **如何改造成自己的宿主**：
  1. 修改 `vsthost.vcxproj` 的 `<TargetName>`（默认 `vsthost`）与 `src/app/vsthost.rc` 的图标；
  2. 在 `src/app/AppMain.cpp` 调整入口/窗口类，或直接复用现有 `CMainFrame`；
  3. 用 `tools\pack.ps1` 一键生成“同名即用”exe，即完成一个可发布的单插件宿主。
- **约定**：配置文件用 `WritePrivateProfileStringW` 写在插件旁 `*.ini`（`AsioConfigPath()`）；
  插件状态以 `<内部效果器名>_<序号>.fxp` 原子写入；重启传参用环境变量 `VSTHOST_ORDINAL`
  （MFC `ParseCommandLine` 会把裸数字当作文件路径，故不用命令行序号）。
- 详细设计与里程碑见 [`重构计划书.md`](重构计划书.md)。

## 构建

Visual Studio 2022+（`vsthost.sln` / `vsthost.vcxproj`），**Win32 + x64 双平台**，Debug / Release：

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  vsthost.vcxproj /p:Configuration=Release /p:Platform=x64
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  vsthost.vcxproj /p:Configuration=Release /p:Platform=Win32
```

输出到 `bin\x64\Release\vsthost.exe` / `bin\Win32\Release\vsthost.exe`（中间文件在 `obj\`）。

- 32 位插件配 **x86** 版宿主，64 位插件配 **x64** 版宿主（位数不匹配会提示）。

## 依赖（第三方 SDK）

第三方 SDK **不随仓库提交**（见 `.gitignore` 的 `VST/`），克隆后需自行放置到 `VST\` 目录：

| SDK | 用途 | 版本 |
|---|---|---|
| `vst2sdk` | VST2 插件接口 | — |
| `vst3sdk` | VST3 插件接口（hosting） | 3.8.0 |
| `asio` | ASIO 驱动接口 | 2.3 |
| `jack` | JACK2 客户端接口（`libjack64.dll` / `libjack.dll`） | 1.9.22 |

JACK 运行库为**动态加载**：缺失时提示安装 JACK2（可从 [jackaudio.org](https://jackaudio.org) 获取）。

## 打包

`tools\pack.ps1` 一键生成“同名即用”exe（按插件位数自动选 x86/x64 宿主）：

```powershell
# 单插件
.\tools\pack.ps1 -Plugin "D:\plugins\Foo.dll"
# Shell 内部效果器（生成 (Shell名)内部名.exe + .ini 直选）
.\tools\pack.ps1 -Plugin "D:\plugins\WaveShell1-VST3 16.6_x64.vst3" -Internal "Magma StressBox Stereo"
```

本地测试用快捷方式 exe 由 `tools\refresh_test_exes.ps1` 维护（生成到 `test_exe\`，该目录不入库）。

## 目录结构

```
src/
  app/      AppMain / MainWnd（主窗口、运行时菜单、电平表、后端调度、托盘）
  host/     IPlugin / Vst2Plugin / Vst3Plugin / SingleHost / AsioBackend / JackBackend / MidiInput / MidiOutput
  dsp/      LoudnessCore（BS.1770 响度核心）
  ui/       LevelMeterDlg / MeterSettingsDlg / GlobalSettingsDlg / MidiMapDialog / ClosePromptDlg / loudness_std
  wrapper/  shell2vst2 / shell2vst3（Shell 内部效果器包装）
tools/      refresh_test_exes.ps1 / pack.ps1 / build_wrappers.ps1 / make_icon.py
res/        图标与位图资源（icon.png → res/icon.ico 全尺寸）
Deprecated/ 已弃用的旧版机架代码/旧工程文件（不再编译，仅存档）
VST/        第三方 SDK（不入库，见「依赖」）
```

## 许可与致谢

- 本仓库以 **LGPL-2.1** 发布，详见 [`LICENSE`](LICENSE)。
- 衍生于 [https://github.com/Arakula/vsthost](https://github.com/Arakula/vsthost)（Hermann Seib 的 VSTHost 开源版），感谢原作者的开源贡献。
- 本仓库的改造与代码由 AI（GitHub Copilot）辅助编写。


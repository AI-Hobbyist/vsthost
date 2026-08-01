**🌐 [English](README.md) | [中文](README.zh-CN.md)**

<div align="center">

# vsthost — Single-Plugin VST Host

**One exe = one plugin**: rename the host to match your plugin and drop it next to it — double-click and it loads and plays.
Supports **VST2 / VST3** plugins, **ASIO / JACK2** audio backends, and a built-in **ITU-R BS.1770 loudness meter** with MIDI parameter mapping.

![Platform](https://img.shields.io/badge/platform-Windows-blue)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C)
![UI](https://img.shields.io/badge/UI-MFC-green)
![Audio](https://img.shields.io/badge/audio-ASIO%20%7C%20JACK2-orange)
![License](https://img.shields.io/badge/license-LGPL--2.1-lightgrey)

</div>

> **Derived from**: this project is a fork/rework of [https://github.com/Arakula/vsthost](https://github.com/Arakula/vsthost) (the open-source VSTHost by Hermann Seib). Respect and thanks to the original author.
>
> **AI notice**: the rework and most of the code in this repository were written with AI assistance (GitHub Copilot).

---

## Comparison with Upstream

This repository is a deep rework of [Arakula/vsthost](https://github.com/Arakula/vsthost) (the open-source VSTHost by Hermann Seib, V1.16r). Legacy rack-era code is archived under `Deprecated/` (not built).

### Added

| Feature | Notes |
|---|---|
| **VST3 support** | `Vst3Plugin` on the vst3sdk 3.8.0 hosting layer (module / component / editor / state) |
| **JACK2 backend** | Native JACK client — audio & MIDI ports registered per plugin capability, client name = plugin name, auto-detect on startup, fallback to ASIO |
| **Same-name auto-load** | Rename the exe to match your plugin (`Foo.dll` → `Foo.exe`) and it loads on startup; drag & drop and command line too |
| **Loudness meters (BS.1770)** | Standalone window with M/S/I/LRA/True Peak, EBU R128 / ATSC etc., CSV loudness logging |
| **MIDI CC → parameter mapping** | `MidiMapDialog` for VST2 / VST3 |
| **Tray, close behavior & global settings** | Minimize to tray, configurable close action, global settings dialog |
| **App icon / version / Chinese manual** | Full-size icon, `VERSIONINFO` (1.1.0.0), `docs/说明书.md` bundled on packaging |
| **`shell2vst` standalone tool** | CLI that unpacks shell effects into standalone exe / dll / vst3 — fully decoupled from the host |
| **Packaging tooling** | `tools/pack.ps1`, `refresh_test_exes.ps1`, `build_wrappers.ps1`, `make_icon.py` |
| **Bilingual README + .gitignore** | EN (default) + zh-CN; ignores build / test / third-party SDK artifacts |

### Removed (simplified)

| Component | Notes |
|---|---|
| **MDI rack / multi-plugin / chains** | `MainFrm` / `ChildFrm` / `ChildView` / `EffChainDlg` / `EffectWnd` / `EffEditWnd` / `EffSecWnd` → single-plugin only |
| **MME / DirectSound backends** | `WaveDev` / `DSoundDev` / `SpecWave` / `SpecDSound` (ASIO + JACK2 only) |
| **Virtual MIDI keyboard** | `MidiKeybDlg` |
| **Assorted dialogs** | `ProgNameDlg` / `ShellSelDlg` / `EffMidiChn` / `AsioChannelSelectDialog` |
| **VS2008 project files** | `.dsp` / `.dsw` / `.vcproj` → single `vsthost.vcxproj` (Win32 + x64) |
| **Host extension layer** | `SmpVSTHost` / `specmidi` / `mfcmidi` / `mfcwave` → lean `MidiInput` / `MidiOutput` |
| **Resource script / binaries** | `vsthost.rc` UI scripts, `resource.h`, `Release/mkbd.ocx`, etc. |

### Kept & reworked

- **VST2 loading** — re-factored into lean `Vst2Plugin` + `CEffect`
- **ASIO backend** — wrapped as `AsioBackend` (driver picker / control panel / channel mapping)
- **`.fxp`/`.fxb` state** — atomic writes, per-internal-effect slots, 30 s auto-save
- **Loading entry points** — same-name, command line, drag & drop

---

## Features

- **Same-name auto-load**: rename `vsthost.exe` to match your plugin (`MyEffect.dll` / `MyEffect.vst3` → place `MyEffect.exe` beside it), double-click to auto-load and play; you can also drag & drop a plugin onto the window or pass it on the command line.
- **Single-plugin, minimal**: only 1 plugin instance at a time — no rack / chain cruft; 32-bit plugins pair with the x86 host, 64-bit plugins with the x64 host.
- **Two audio backends**: **ASIO** (driver picker / control panel / channel mapping / sample-rate & buffer settings) and **JACK2** (native JACK client; audio & MIDI ports registered dynamically per plugin capability; client name = plugin name).
- **Loudness metering**: dual I/O dB meters + a standalone window with **Momentary / Short-term / Integrated / LRA / True Peak** (BS.1770-4 / EBU R128 / ATSC etc.) and **CSV loudness logging**.
- **MIDI**: winmm input (device/channel filtering) + **VSTi instruments** + **CC→parameter mapping**; over JACK it uses the `midi_in / midi_out` ports.
- **Shell support**: Waves WaveShell internal effects — direct-load via `(ShellFilename)InternalName.exe` and switch anytime from the menu (remembers `last_uid`).
- **Tray & global settings**: minimize to tray, configurable close behavior (ask each time / minimize to tray / quit; "don't ask again").
- **Stability**: plugin state auto-saved every 30 s (crash-safe), audio stops automatically when the JACK server exits, and it falls back to ASIO when JACK is unavailable.

## Quick Start

1. Rename `vsthost.exe` to match your plugin and place it in the plugin folder:
   - `MyEffect.dll` → place `MyEffect.exe` beside it
   - `MyEffect.vst3` (directory or single file) → place `MyEffect.exe` beside it
   - when both exist, VST3 is preferred
2. Double-click the exe to auto-load and play; you can also drag & drop a plugin onto the window or use `vsthost.exe C:\path\plugin.vst3`.
3. **Shell internal effects**: rename the exe to `(ShellFilename)InternalName.exe`, e.g.
   `(WaveShell1-VST3 16.6_x64)Magma StressBox Stereo.exe`, to load that internal effect on startup;
   you can also switch anytime from the menu "Plug-in → Internal Effects" (selection is remembered via `last_uid`).

## Feature Overview

### Audio Backends

- Menu **Audio → Use ASIO / Use JACK** (radio); remembered in `[View] backend`.
- **Auto-detect on startup**: if no backend is configured, JACK is chosen when a JACK server is running, otherwise ASIO.
- **Auto fallback to ASIO**: enabling JACK while no server is running / the library is missing falls back to ASIO with a notice.
- **ASIO**: default device = FL Studio ASIO (avoids grabbing other DAWs); `Audio → ASIO Devices` to pick a driver, `Driver Control Panel`, `ASIO Channel Mapping` (plugin ↔ ASIO channel map); sample rate / buffer are configured in Global Settings.
- **JACK2** (dynamically links `libjack64.dll` / `libjack.dll`; prompts to install if missing): **ports registered per plugin capability** — audio `in_1..N / out_1..M` + MIDI `midi_in / midi_out` (when the plugin supports MIDI); client name = current plugin name (e.g. `Magma StressBox Stereo_1`, unique across instances); sample rate / buffer are taken from the server and reconfigured automatically on change; audio stops when the server exits. The status bar shows `DSP xx%` and `JACK: <client name>`.

### Meters & Loudness

- **Main-window vertical I/O meters** (dB scale −60…+12, red above 0 dB, peak-hold line + value box).
- Menu **View → Standalone Meter Window**: I/O peaks + **Momentary / Short-term / Integrated / LRA / True Peak**; loudness standards (BS.1770-4 / EBU R128 / ATSC / platform references), loudness source switch, reference line, true-peak ceiling.
- **CSV loudness logging**: recorded at a configurable interval to `loudness_YYYYMMDD_HHMMSS.csv`; a new file is started after each reset.
- Settings are centralized in **View → Meter Settings** (refresh rate / peak hold / silence reset / silence threshold / CSV / standard; sliders + edit boxes).

### MIDI

- Enable MIDI input (winmm) in **Global Settings**, pick the device and channel filter (Omni / 1–16).
- Under JACK, MIDI input comes from the `midi_in` port (only the channel filter is configurable).
- **Plug-in → MIDI Parameter Mapping**: map CCs from an external controller to plug-in parameters (VST2 / VST3).
- **VSTi instruments** supported (play via `midi_in`; input meters hide automatically).

### Global Settings & Tray

- **File → Global Settings**: close behavior (ask each time / minimize to tray / quit) + ASIO sample rate & buffer + MIDI.
- **Tray**: minimize to tray, close prompt (can be set to "don't ask again"); left-click / double-click restores, right-click menu.
- **File → Exit** quits completely (unlike the close button).

## Development Template

This project can be used as a **minimal single-plugin VST host template** — lean structure, no resource scripts (menus are built at runtime), a single dual-platform project, easy to extend:

- **Source entry points**:
  - `src/app/MainWnd.cpp` — main window / runtime-built menus / meters / backend dispatch / tray;
  - `src/host/IPlugin.h` — plug-in abstraction (start here to add a new plug-in format);
  - `src/host/AsioBackend.*` / `JackBackend.*` — audio backends (implement `IAudioBackend` to add one);
  - `src/dsp/LoudnessCore.*` — loudness core (BS.1770);
  - `src/ui/*` — dialogs (Global Settings / Meter Settings / Close Prompt / MIDI Mapping).
- **Make it your own host**:
  1. Change `<TargetName>` (default `vsthost`) in `vsthost.vcxproj` and the icon in `src/app/vsthost.rc`;
  2. Tweak the entry / window class in `src/app/AppMain.cpp`, or reuse the existing `CMainFrame`;
  3. Run `tools\pack.ps1` to generate a "same-name, ready-to-use" exe — that's a shippable single-plugin host.
- **Conventions**: settings are written with `WritePrivateProfileStringW` into a `*.ini` next to the plug-in (`AsioConfigPath()`); plug-in state is saved atomically as `<InternalName>_<ordinal>.fxp`; restart arguments are passed via the `VSTHOST_ORDINAL` environment variable (MFC's `ParseCommandLine` treats bare numbers as file paths, so a command-line ordinal is not used).
- Detailed design & milestones: [`重构计划书.md`](重构计划书.md) (Chinese).

## Building

Visual Studio 2022+ (`vsthost.sln` / `vsthost.vcxproj`), **Win32 + x64**, Debug / Release.
The host and the standalone shell-unpacker tool (`shell2vst.vcxproj`) share the same config:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  vsthost.vcxproj /p:Configuration=Release /p:Platform=x64
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  vsthost.vcxproj /p:Configuration=Release /p:Platform=Win32
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  shell2vst.vcxproj /p:Configuration=Release /p:Platform=x64
```

Outputs to `bin\x64\Release\vsthost.exe` / `bin\Win32\Release\vsthost.exe` (intermediate files under `obj\`).
The tool outputs to `bin\x64\Release\shell2vst.exe` / `bin\Win32\Release\shell2vst.exe`.

- 32-bit plugins need the **x86** host, 64-bit plugins the **x64** host (a mismatch is reported).

## Dependencies (Third-Party SDKs)

Third-party SDKs are **not committed** (see `VST/` in `.gitignore`); after cloning, place them under `VST\`:

| SDK | Purpose | Version |
|---|---|---|
| `vst2sdk` | VST2 plug-in interface | — |
| `vst3sdk` | VST3 plug-in interface (hosting) | 3.8.0 |
| `asio` | ASIO driver interface | 2.3 |
| `jack` | JACK2 client interface (`libjack64.dll` / `libjack.dll`) | 1.9.22 |

The JACK runtime is **dynamically loaded**: if missing, install JACK2 (available from [jackaudio.org](https://jackaudio.org)).

## Packaging

`tools\pack.ps1` generates a "same-name, ready-to-use" exe in one shot (auto-picks x86/x64 host per plug-in bitness):

```powershell
# single plug-in
.\tools\pack.ps1 -Plugin "D:\plugins\Foo.dll"
# shell internal effect (produces (ShellName)InternalName.exe + .ini)
.\tools\pack.ps1 -Plugin "D:\plugins\WaveShell1-VST3 16.6_x64.vst3" -Internal "Magma StressBox Stereo"
```

Local test launchers are maintained by `tools\refresh_test_exes.ps1` (generated into `test_exe\`, which is not committed).

### shell2vst — standalone shell-unpacker tool

`shell2vst.exe` (built from `shell2vst.vcxproj`) is a **standalone CLI tool fully decoupled from the main host**: it enumerates a shell plug-in (e.g. Waves WaveShell) and generates independent effects / launchers — useful for DAWs that cannot scan shells:

```powershell
# enumerate & generate all: launcher exe + VST2 wrapper + VST3 wrapper
.\shell2vst.exe "D:\plugins\WaveShell1-VST3 16.6_x64.vst3" --out "D:\out"
# only VST2 wrappers
.\shell2vst.exe "D:\plugins\WaveShell1-VST3 16.6_x64.vst3" --dll
```

Options: `--exe|--dll|--vst3|--all` (default all), `--host <host.exe>` (template for launcher exes; default: a `vsthost*.exe` next to the tool), `--out <dir>` (default `standalone_<shell>`, output is grouped by channel folders like `Mono/`, `Stereo/`, `5.x/`). The wrapper templates (`wrapper\shell2vst2.dll` / `shell2vst3.dll`, built by `tools\build_wrappers.ps1`) are looked up next to the tool.

## Directory Layout

```
src/
  app/      AppMain / MainWnd (main window, runtime menus, meters, backend dispatch, tray)
  host/     IPlugin / Vst2Plugin / Vst3Plugin / SingleHost / AsioBackend / JackBackend / MidiInput / MidiOutput
  dsp/      LoudnessCore (BS.1770 loudness core)
  ui/       LevelMeterDlg / MeterSettingsDlg / GlobalSettingsDlg / MidiMapDialog / ClosePromptDlg / loudness_std
  tool/     shell2vst_main (standalone shell-unpacker CLI: shell2vst.exe)
  wrapper/  shell2vst2 / shell2vst3 (shell wrapper DLL templates)
tools/      refresh_test_exes.ps1 / pack.ps1 / build_wrappers.ps1 / make_icon.py
res/        icons & bitmaps (icon.png → res/icon.ico, all sizes)
Deprecated/ obsolete rack-era code / old project files (not built; kept for reference)
VST/        third-party SDKs (not committed; see Dependencies)
```

## License & Credits

- Licensed under **LGPL-2.1**; see [`LICENSE`](LICENSE).
- Derived from [https://github.com/Arakula/vsthost](https://github.com/Arakula/vsthost) (the open-source VSTHost by Hermann Seib) — thanks to the original author.
- The rework and most of the code in this repository were written with AI assistance (GitHub Copilot).


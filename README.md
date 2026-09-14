<div align="center">

# Lost Odyssey Recomp

**An experimental native PC port of Lost Odyssey for Xbox 360.**

Windows x64 · Direct3D 12 · Vulkan · PowerPC static recompilation

<img src="docs/images/title-screen.png" alt="Lost Odyssey title screen — Press START" width="960">

Optional diagnostics are off by default and can be disabled in Settings. See [Privacy](PRIVACY.md).

### [Latest download](https://github.com/freefrank/LostOdysseyRecomp/releases/latest) · [Installation guide](docs/INSTALLING.md) · [Report an issue](https://github.com/freefrank/LostOdysseyRecomp/issues)

[简体中文](README.zh-CN.md) · [Changelog](CHANGELOG.md) · [Projects](https://github.com/users/freefrank/projects/3) · [Build from source](docs/BUILDING.md)

</div>

> [!IMPORTANT]
> **This project is still in early testing.** Opening areas and selected scenes have been tested; a complete playthrough has not. Rendering and stability issues remain. You must supply your own supported game files.

## New in v0.5.13

Published Windows x64 package: [v0.5.13](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.13). The ZIP SHA-256 is `a993071c24f7324a3ae3a0dbe24688afc60450d3da9f1a78fbf532c69f65bacd`.

- **Alt+Enter** toggles between Windowed and Borderless presentation modes without selecting DXGI exclusive fullscreen. Left Alt, Right Alt and AltGr are supported. Live local acceptance confirmed both Alt variants in the actual game window; whole-game, exclusive-fullscreen, mixed-DPI and mouse validation remain unverified. The published package was not launched for gameplay validation.

## New in v0.5.12

Published Windows x64 package: [v0.5.12](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.12).

- Fix USA/Europe FMV and event subtitle language mapping for the supported language IDs. A local Spanish opening-FMV check was accepted; German, French and Italian FMV visuals, complete event coverage, whole-game validation and reporter acceptance remain unverified. See the [edition support note](docs/notes/europe-support.md).
- The ZIP SHA-256 is `7cc99618cee509bdea000b736772344de60279f4a439a4f876cc7d49d4c8e60a`; the public ZIP and checksum downloads are available from the [latest release](https://github.com/freefrank/LostOdysseyRecomp/releases/latest).

## New in v0.5.11

Published Windows x64 package: [v0.5.11](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.11).

- Lower-layer GPU failures now record the API, raw error code and resource context, while startup and early allocation failures retain environment, build and memory details. WinHTTP failures preserve raw errors, repeated failures are rate-limited, and GPU adapter/renderer formatting has an emergency fallback. Issues #6 and #22 remain under investigation; these diagnostics do not claim either report is fixed.

## New in v0.5.10

Published Windows x64 package: [v0.5.10](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.10).

- The Vulkan TAA jitter mapping now covers 11 additional material and light vertex shader paths found in captures f5997, f5912 and f16385. The reported flicker scenes were accepted by the user; other scenes and whole game coverage remain unverified. See the [TAA coverage notes](docs/notes/taa-f5997-2026-09-13.md) and [f5912/f16385 TAA note](docs/notes/taa-f5912-f16385-2026-09-13.md).
- Startup shader preparation now includes verified original CPX metadata and the captured packed static mesh declaration for `1474db97dfc0afad`. Focused startup coverage checks and the Release build passed. Gameplay frame time remains unverified; see the [startup coverage note](docs/notes/shader-startup-coverage-2026-09-13.md).

## New in v0.5.9

- Published Windows x64 package: [v0.5.9](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.9). It adds conservative Vulkan depth-clear coalescing, texture-key avalanche mixing, captured-shader identity caching, graphics descriptor binding suppression, Plume binding suppression and per-slot descriptor reuse. The retained two-slot/fence contract is unchanged.
- A matched static 4K Vulkan observation at 60 W improved mean FPS from 7.49638 to 43.47614. This is bounded candidate evidence; it does not establish 4K60, 1080p60 at 15 W, whole-game behavior or player acceptance. See the [Vulkan depth-clear performance note](docs/notes/vulkan-depth-clear-performance-2026-09-13.md).

## New in v0.5.8

- Published Windows x64 package: [v0.5.8](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.8). It extends current-scene TAA jitter coverage with seven reviewed main-camera paths and adds bounded sampled-content SIMD plus `LO_QUERY_TRACE` caching.
- These changes retain limited diagnostic and fixed-scene evidence. Original-scene TAA visual acceptance and a stable whole-game 60 FPS result remain unverified. See the [changelog](CHANGELOG.md) for details.

## New in v0.5.7

- Published Windows x64 package: [v0.5.7](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.7). It recovers updater-only installations and stale metadata, asks before launching after an update with **No** as the default, and includes guarded shadow-loop and resolve-copy reductions, the HDR16 TAA bloom prefilter, and the material vertex-shader jitter repair.
- The HDR-off/materials-on repair was accepted in the reported lighting-flicker scene. Other scenes and hardware remain unverified.

## New in v0.5.6

- Fix updater manifest transactions, add the narrow Issue #16 particle-material fallback, and include the PPC prebuilt release path with bounded renderer performance improvements.
- Published Windows x64 package: [v0.5.6](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.6). The Issue #16 fix passed the target scene on a local main build using D3D12/Asia Disc 3; the formal package passed integrity checks. Whole-game, Vulkan, other-region and player acceptance remain unverified.

## New in v0.5.4

- Guard PPC source generation against stale inputs, incomplete output and obsolete 64-bit jump-table switches.
- Add an optional Win64 external assembly profiler with offline reports.
- Extend the F1 menu ZIP archive wait for large captures and fix installer drag dispatch.

Earlier release details are maintained in the [changelog](CHANGELOG.md).

## Start playing

1. **Download and extract** the entire Windows release ZIP to a writable folder.
2. **Run `LostOdysseyRecomp.exe`** and import your game files when prompted. The importer accepts an extracted folder, `default.xex`, an XDVDFS ISO or a GOD container.
3. **Choose your language and graphics settings.** The game continues after setup and shader preparation.

No Python or Visual Studio installation is needed for the release package. Later launches reuse the shader cache. Keep your save and profile folders when updating.

The published updater checks GitHub's latest Release: a higher numeric version updates, and an equal numeric version with a different `-suffix` also triggers an update. The v0.5.7 release additionally permits recovery from an empty updater-only folder and stale or malformed local metadata. After a successful update, the helper asks whether to launch the game and defaults to **No**; silent runs complete without launching. Download integrity checks, safe extraction and rollback remain enabled.

| Requirement | Supported configuration |
| :--- | :--- |
| System | Windows x64, AVX-capable CPU, Direct3D 12 or Vulkan graphics driver |
| Game data | Audited Europe, Asia or USA, Europe edition; Disc 1 is required to start |
| Additional discs | Import with `InstallGame.exe`; later-disc progression is not fully verified |

See the [installation guide](docs/INSTALLING.md) for accepted disc versions, file locations and updating.

## In-game screenshots

| Ring combat | City exploration |
| :---: | :---: |
| ![Kaim attacking with the Ring timing interface](docs/images/ring-battle.png) | ![Exploring the industrial city](docs/images/city-exploration.png) |

*Unmodified screenshots from development builds leading up to v0.1.*

## Current features

| Feature | What to expect |
| :--- | :--- |
| Game importer | Folder, XEX, ISO and GOD input; original source files are copied |
| First-launch setup | Language and graphics settings before game initialization |
| Language settings | English, Japanese, Korean, Traditional and Simplified Chinese interface options; game language selection |
| Graphics settings | Auto/manual internal resolution up to 4K, Off/FXAA/SMAA/experimental TAA, Standard/High filtering, 30/60 FPS and output/display controls; fullscreen and mixed DPI need more testing |
| Settings menu | Original game fonts and menu styling; one-click Graphics save/apply and Now/Later restart choices |
| Shader preparation | Built-in resource index, parallel compilation and cache reuse |
| CPU use | Reduced unnecessary polling and reuse of rendering work |
| Input and debug | Controller and keyboard input; English/Simplified Chinese F1 menu with capture, map information and same-map POI teleport |

Open `InstallGame.exe` and choose **Files** or **Folder** to import game discs and supported DLC. See the [installation guide](docs/INSTALLING.md#automatic-content-import).

Validation progress and remaining work are tracked in the [Maintainer Project](https://github.com/users/freefrank/projects/3).

<details>
<summary><strong>Game edition and compatibility details</strong></summary>

The two supported editions correspond to [Lost Odyssey (Europe, Asia) (En,Ja,Zh,Ko) (Disc 1), Redump 39111](https://redump.info/disc/39111) and [Lost Odyssey (USA, Europe) (En,Ja,Fr,De,Es,It) (Disc 1), Redump 11817](https://redump.info/disc/11817). The former is called the Asian edition here: Disc 1 has title ID `4D5307FA`, media ID `39F7D748`, title/base version `0.0.0.4` and XeMID `MS204204H0X14`. The USA, Europe Disc 1 has media ID `368DE6DD`, version `0.0.0.3` and XeMID `MS204203W0X14`. These identities match the audited sets; a complete ISO hash comparison against Redump has not been performed. The importer strictly checks each supported XEX hash; a region label alone is insufficient.

The **USA, Europe version 0.0.0.3** four-disc set is supported, with strict XEX checks and protection against mixing editions. Game-language choices follow the installed edition: English/Japanese/German/French/Spanish/Italian for USA, Europe; the audited Europe, Asia resources retain English/Japanese/Korean/Traditional Chinese/Simplified Chinese choices. See [edition details](docs/notes/europe-support.md).

With all four discs imported, the game selects them automatically; no manual disc swap is needed. See [disc handling](docs/notes/disc-selection.md).

Language options do not imply a complete playthrough in every language. Regional builds outside the audited sets, title updates and modified XEX files are not validated. See [edition evidence](docs/notes/xex.md).

</details>

<details>
<summary><strong>Build commands and repository layout</strong></summary>

### Build and run

Prepare your own extracted data, dependencies and generated sources using the [build guide](docs/BUILDING.md). Helper scripts discover installed tools; custom paths can be supplied through environment variables.

```powershell
.\tools\build_runtime.bat
$gameData = (Resolve-Path .\LostOdysseyRecompLib\private\disc1).Path
Push-Location .\out\build\windows-clang\LostOdysseyRecomp
.\LostOdysseyRecomp.exe --game $gameData --quiet-kernel
Pop-Location
```

Keep the working directory consistent so the intended save/profile folders are used.

**Startup and failure logs.** Each normal launch writes `logs/runtime-<timestamp>.log` in the working directory and mirrors output to `stderr`; set `LO_LOG_FILE=<path>` to choose another file, or `LO_LOG_FILE=0` to disable the duplicate file sink. v0.5.11 additionally records the Windows build, process/native architecture, source/build revision, PE image metadata, compiler, startup memory baseline, GPU, raw driver version, vendor/type and `reported_device_memory_bytes`. When reporting a startup or renderer failure, attach the complete current runtime log and include the executable/source version, backend, GPU and driver details recorded near startup. The diagnostic records preserve raw API codes and the failed resource or allocation context, but they are investigation evidence and do not by themselves identify a root cause. See the [build and logging guide](docs/BUILDING.md) for the path and retention rules.

| Action | Keyboard |
| :--- | :--- |
| Start / Back | Enter / Backspace |
| A / B / X / Y | Z / X / A / S |
| D-pad / left stick | Arrow keys / I, J, K, L |
| Left / right shoulder | Q / W |
| Left / right trigger | E / R |
| Debug menu | F1 |

SDL-mapped controllers and the keyboard can be used together for player 1. Unmapped joysticks need an SDL controller mapping. See [input details](docs/notes/controller-input.md).

Rumble is disabled by default; `LO_CONTROLLER_RUMBLE=1` enables it. For Ring actions, use the controller's right trigger or the R key.

### Development

| Directory | Contents |
| :--- | :--- |
| `LostOdysseyRecomp/` | Host kernel, graphics, audio, input and debugging |
| `LostOdysseyRecompLib/` | Configuration; ignored `private/` game data and generated `ppc/` code |
| `tools/` | Recompilers, dependency patches, Ghidra scripts and the optional [assembly profiler](tools/asm-profiler/README.md) |
| `thirdparty/` | Rendering, audio and other dependencies |
| `docs/` | Current status, guides, research and historical archives |

[Roadmap](docs/ROADMAP.md) · [Handoff](docs/notes/handoff.md) · [Rendering tests](docs/notes/rendering-validation.md) · [Audio](docs/notes/audio-output.md) · [Archive](docs/archive/README.md)

</details>

## Credits and game data

With research and tools from [UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp), [re:Blue](https://github.com/zolaware/reblue), [XenonRecomp](https://github.com/hedge-dev/XenonRecomp), [XenosRecomp](https://github.com/hedge-dev/XenosRecomp), [plume](https://github.com/renderbag/plume) and [Xenia](https://github.com/xenia-project/xenia). Audio uses the pinned [Xenia FFmpeg fork](https://github.com/xenia-project/FFmpeg), with its [license](thirdparty/ffmpeg-LICENSE.txt).

Lost Odyssey and its assets belong to their respective owners. This is an unofficial project. Supply data extracted from your own discs; do not submit game executables, resource archives, textures, audio, video, generated game code or captures. Dependencies retain their respective licenses.

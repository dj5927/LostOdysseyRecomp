# Project status

## R3 CPU waiting-path modernization and native Vulkan test — unpublished development — 2026-09-15

The source implements the R3 CPU waiting-path modernization on the local `deck` branch (uncommitted development checkpoint, source version remains `0.5.13`, not a release).
- **Condition-variable kernel waits**: Adds `LostOdysseyRecomp/notified_wait.h` with predicate/deadline condition-variable helpers (`notified_wait::For` and `notified_wait::Until`). Replaces 200 µs polling sleep loops in `kernel/imports.cpp` for finite-timeout Event, Semaphore, and Mutant waits with condition-variable predicate and deadline waits while preserving consume, recursive ownership, and timeout semantics.
- **GPU command processor notification**: `gpu/command_processor.{h,cpp}` notifies on write-pointer updates (`SetWritePointer`) and shutdown (`Shutdown`), replacing the arbitrary 200-iteration yield loop in `WorkerMain` with a bounded 500 µs `notified_wait::For` wait while preserving SDL event pumping (`video::PumpEvents()`).
- **Direct fixture**: Adds `LoNotifiedWaitTest` (`tools/tests/notified_wait_test.cpp`) to `LostOdysseyRecomp/CMakeLists.txt` covering pre-notification, early wake, deadline timeout, and CommandProcessor write-pointer wake behavior.

Focused verification and bounded evidence:
- Windows unit fixture `LoNotifiedWaitTest` passed all cases: pre-notify, early wake, deadline, and CP-pointer wake.
- Existing regression fixture `LoPollWaitTest` passed without regression.
- Full Windows target compiled cleanly; diff check passed.
- Native Linux build and codegen passed after applying maintained dependency patches.
- Actual 15 W native Vulkan run on AMD Radeon 8060S:
  - Created and resized 1280x720 swapchain, selected Vulkan backend, started guest runtime.
  - Power limits configured to STAPM 15 W / Fast 25 W / Slow 20 W.
  - Startup shader preparation at allowed 60 W completed 28,484 known shaders (28,482 ready, 2 deterministic failures), then runtime consumed the startup bundle.
  - Ran 90 seconds with zero errors or fatal diagnostics.
  - Frame rates: target was 60 FPS; stable earlier windows observed around 58.46–58.58 FPS; later heavier scene observed around 37–40 FPS.

Validation limits and open boundaries:
- Do NOT claim locked 60 FPS (heavier scene drops to 37–40 FPS at 15 W).
- Bounded 90-second run only; no full-game playthrough, cutscene progression, or long-term stability validation.
- No player visual acceptance has been performed.
- Development code is uncommitted on the local `deck` branch; no push, PR, CI run, or GitHub Release exists for this checkpoint.

## Unified main binary installer and updater - unpublished development - 2026-09-15

The source unifies the content importer and updater into the single `LostOdysseyRecomp.exe` runtime binary. Separate `InstallGame.exe` and `LostOdysseyUpdater.exe` helper executables are excluded from the release payload; the legacy updater target remains available for fixtures. Source version remains `0.5.13` (development executable, not a release).

Key changes across the checkpoint (committed through `fffa572` and the follow-up commits below):
- **Installer controller & UI**: Mixed disc and DLC discovery and import (`ScanContent`/`InstallContent`), transactional staging, cancellable background scanning and import via worker thread event queue, game path preserved for DLC-only runs, and partial-disc preservation on DLC error. Error reading now uses destination `InstallResult` object. Escape/B during scan requests cancellation and immediately returns to the source browser, with late finished scans intentionally not auto-transitioning. Review source and destination paths are converted via UTF-8 and width-clipped; DLC display names are width-clipped (168 px) to avoid overwriting the Files column. UI visual updates include Lost Odyssey game-style brushed steel panels, graphic folder and file icons, vertically centered selection bar, and precise UTF-8 width-measured text truncation with CJK glyph rendering. Review screen action button navigation is constrained to Left/Right only (Up/Down navigation preserves current selection). Source, destination, and review screen footers replace the text PAD label with a cached 32x32 transparent dark-cross D-pad bitmap icon with pale triangular arrows. Left stick controller navigation maps SDL `LEFTX`/`LEFTY` axis motion to directional input using strongest-axis resolution with deadzone 16000 and held-threshold 10000, initial 350 ms delay and 120 ms repeat rate, window focus and worker-busy input guards, and controller device hotplug handling.
- **Embedded updater**: Integrated into main executable startup before XEX inspection, installer dispatch and game initialization. Normal startup still allocates guest memory in a global constructor; only private apply mode skips that allocation. Reads lightweight preferences (`automatic_updates` and `ui_language`) without altering edition game language. Parses English and Chinese release notes from GitHub Release API payload with consent dialog before downloading. Updates run from a private copy of the main binary in `--apply-plan` mode, executing the update and auto-launching the updated binary upon completion when configured. Silent malformed `--apply-plan` invocations exit with code 1 without showing UI.
- **Packaging payload**: `package_release.py` updated to package only `LostOdysseyRecomp.exe`, DXC runtime libraries, licenses, and manifests (3/3 payload tests pass). Upstream font notices (`FONT-PROVENANCE.md` and `Unifont-OFL-1.1.txt`) are included.
- **Cross-platform importer fixes**: Win32 `HANDLE`/`GetCurrentProcessId` lock and PID dependencies in importer replaced with POSIX `open`/`flock` and `getpid` fallbacks. Synthetic importer tests pass on Linux WSL g++ C++20 (`LoImportGameTest`).

Focused verification and bounded evidence:
- Configured Windows main build and installer controller tests passed (`LoInstallerControllerTest`), with 14 explicit navigation and axis mapper unit checks passing.
- Lead personally verified real keyboard navigation: Right arrow highlights Change destination, Up/Down leaves selection unchanged, Right clamps at Change source, Left clamps at Start import, and Right + Enter from Start import opens the custom destination picker (`out/navigation-destination.png`).
- Visual captures recorded under repository `out/`: `navigation-review-right.png`, `navigation-review-vertical.png`, `navigation-review-end.png`, `navigation-review-start.png`, `navigation-destination.png`, and `navigation-source.png`.
- An independent visual Oracle opened 5 navigation captures and returned a bounded PASS on icon rendering and button highlight state. Prior source Oracle review bounded PASS noted missing relative screenshot paths as documentation artifact rather than code defect.
- Synthetic importer transactions, cancellation, mixed-edition, and DLC tests pass (`LoImportGameTest`). Prior tests were reused, not rerun.
- Real local sources verified: 4 USA/Europe ISOs (`G:/ROMS/US`), 4 Asia GOD discs + 3 DLC packages (`G:/ROMS/X360CH176`), and 4 extracted Asia disc folders (`D:/Mihoyo/LostOdysseyRecomp-windows-x64/game`). Original source files remained untouched; no copy or import was executed during source checks.
- Lead personally drove real keyboard navigation in the source browser to `G:/ROMS/X360CH176`, scanned 4 Asia GOD discs and 3 DLC packages, observed the fixed review layout at `out/installer-god-review-fixed.png`, and opened the custom destination picker at `out/installer-destination.png`. Input automation initially failed due to missing key scan codes and was corrected, confirming no application keyboard defect.
- Updater startup preferences, changelog extraction, apply plan, and restart tests pass (5/5 in `LoUpdaterStartupOrderTest`).
- Production main runner startup/restart test passed (`LoUpdaterTest --startup-restart` using parent/ready handshake, staged dummy replacement, and verified marker `updated process started`).
- Interactive UI check: Lead observed `out/installer_capture.png` showing Chinese folder name readable, selected row vertically centered, folder icons visible, and direction-key navigation functional (`.update` -> `CMakeFiles`).

Validation limits and open boundaries:
- Neither real analog stick controller hardware nor full interactive UI visual acceptance is claimed; verification remains bounded to keyboard navigation, mapper unit checks, and captured frames.
- Extracted DLC detection/import is verified for the supplied three packages: `LoImportGameTest --extracted-dlc` imported all three into an isolated temporary destination, compared every payload and sidecar byte, verified unchanged duplicates, rejected modified/missing payloads, traversal and overlapping destinations, and verified mid-copy cancellation cleanup. The original directories were read-only. Main build passed; lead's live scan of `D:/Mihoyo/LostOdysseyRecomp-windows-x64/game` displayed four Asia discs and three Ready DLC entries (`out/extracted-dlc-review.png`). This is not an interactive full-import or gameplay acceptance claim. The manifest's source SHA-256 is retained provenance, not proof of re-authenticating the original STFS archive.
- Real network update downloads and in-place installed-game binary replacement have NOT been tested against live GitHub releases.
- No full live import run has been executed inside the interactive UI.
- No full-game gameplay testing was conducted with the unified binary.
- Game visual fidelity and actual controller hardware acceptance are NOT established.
- Residual UI visual state and aesthetic fidelity across all screens remain unverified.
- Non-Windows updater is not implemented (updater remains Windows-only); macOS is untested; no Linux AppImage/Flatpak package is built.
- POSIX lock fix resolves non-Windows compilation in importer only; it does NOT claim the game engine runs or is compatible across all platforms.

## Published v0.5.13 — Alt+Enter window/fullscreen toggle — 2026-09-14

The source change adds an **Alt+Enter** presentation toggle between **Windowed** and **Borderless**. It does not select DXGI exclusive fullscreen, and `DXGI_MWA_NO_ALT_ENTER` remains set. The chord accepts SYSKEY scancode-only `RETURN`, `windowID=0`, left Alt, right Alt and AltGr (`KMOD_RALT|KMOD_CTRL` or `KMOD_MODE`). On Win32, `GetAsyncKeyState(VK_MENU)` is combined with left/right Alt handling because both Alt keys report key code 18 and right Alt may arrive as Ctrl without `KMOD_ALT`. `FitBorderless` is best-effort and cannot roll a successful toggle back to Windowed; Shift/GUI rejection, placement and debounce remain unchanged.

The implementation is in `LostOdysseyRecomp/gpu/window_mode.h` and `LostOdysseyRecomp/gpu/video.cpp`, with corresponding fixture coverage in `tools/tests/game_window_pixels_test.cpp`. The focused `LoGameWindowPixelsTest --rendering-fixes-only` run passed after the SYSKEY, `windowID=0`, right-Alt and AltGr/`VK_MENU=18` fixture updates.

Live local acceptance on 2026-09-14 confirmed left Alt+Enter and right Alt+Enter in the actual game window. The accepted run used a local RelWithDebInfo build of the same chord code, Vulkan on an RTX 5080, `window_mode=0` Windowed, a 3840×2160-class display, and `LO_NO_UPDATE=1 --game disc1`. This is local shortcut acceptance, not acceptance of the published package.

Published at [GitHub Release v0.5.13](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.13) on 2026-09-14T23:36:59Z. Release CI [34908617463](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/34908617463) succeeded after the synchronized PPC identity was retargeted; source/tag commit `545af9be26ebafed364f68c1b2725c10b5b03206` and annotated tag `v0.5.13` are published. CI used the prebuilt PPC artifact from `LostOdysseyRecomp-build-inputs`, not `rebuild_ppc`; cache key `bdf6dce2af1112a1dd0e1ed161bd66a2d7959ed1f174ad049a684b74a4367fc7` came from private commit `d4feb17917189a5b9f051b48b783f5fc62081658`. Only the root `CMakeLists.txt` hash changed versus v0.5.12; the PPC contract and library chunks were unchanged.

The Windows ZIP `LostOdysseyRecomp-windows-x64-v0.5.13.zip` is 44,304,038 bytes with SHA-256 `a993071c24f7324a3ae3a0dbe24688afc60450d3da9f1a78fbf532c69f65bacd`; the inner runtime is 83,536,896 bytes with SHA-256 `d144762040db8918e34282f429044833266fdd3f5b9c48bf221c27752fbc6e58`; the inner updater is 846,336 bytes with SHA-256 `5e5c7957947969431ae499a3acc18536dc5940aaef0598fd7ba2faa2f6c76b96`. The ZIP hash matches its sidecar, and both public ZIP and `.sha256` downloads returned HTTP 302 to GitHub release assets; followed downloads returned HTTP 200 and matched the recorded hash and size.

The published package was not launched for gameplay validation. Whole-game, DXGI exclusive-fullscreen, mixed-DPI and mouse validation remain unverified; publication checks do not claim 50-file manifest re-verification or packaged gameplay acceptance.

## Published v0.5.12 — 2026-09-14

Published at [GitHub Release v0.5.12](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.12) on 2026-09-14T21:34:13Z. Release CI [34895591364](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/34895591364) succeeded for source/tag commit `36e574b64e0cbb11ddcdc7f68cbc36214b5c084f` after PPC cache synchronization. CI used the prebuilt PPC artifact from `LostOdysseyRecomp-build-inputs`, not `rebuild_ppc`. The ZIP is 44,304,445 bytes with SHA-256 `7cc99618cee509bdea000b736772344de60279f4a439a4f876cc7d49d4c8e60a`; the runtime is 83,536,384 bytes with SHA-256 `d394c173cfa8ecc6ea57c1b9671a1a574ae02d2cd4bb76008392caca66686974`; the updater is 846,336 bytes with SHA-256 `36787c2314d9d554010193a4f020eb8d1a848292920e40af3bb7fb44f1a3893e`. The ZIP hash matches its sidecar, and both public ZIP and `.sha256` downloads returned HTTP 302 to GitHub release assets. The PPC cache key was `cb4a75c2d3fa6e3d5f92e3431e7006f77fd7927dd97f05d27eb488ea4cf87e44`, from private commit `5df5b6efb38537386f6c9522ae9126d800032559`.

The release contains the USA/Europe FMV and event subtitle language-table mapping fix. The local `game_language=5` USA/Europe Spanish opening-FMV check was accepted on 2026-09-14 using Vulkan at 3840×2160 and a startup-bundle hit. This is opening-scene acceptance only; German, French and Italian FMV visuals, complete event coverage, whole-game validation and reporter acceptance remain unverified. Issue [#27](https://github.com/freefrank/LostOdysseyRecomp/issues/27) remains open pending reporter confirmation. No 50-file manifest re-verification is claimed.

## Published v0.5.11 — 2026-09-14

Published at [GitHub Release v0.5.11](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.11) on 2026-09-14T02:09:50Z. Release CI [34797755460](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/34797755460) succeeded for source/tag commit `624729cdb1263b96061b1fa14d4d1c5ba0b50239`. The ZIP is 44,304,683 bytes with SHA-256 `5de068c4e77c82feb0bbe7cfcf1dacbca3d44aa94bde064f7f59f5ad6944e132`; the runtime is `33a460410b7f397187a12ac6984718c1716997936127f94f5dbcb2ef40315283`; the updater is 846,336 bytes with SHA-256 `dd38ab687b7ab0713e3ce6b29bce058bfd4b3a7ffebe8d266335532ba6f21750` and matches the ZIP. All 50 payload hashes/CRCs, clean provenance, four public asset downloads and PPC consumption passed. Existing diagnostics and bounded logging checks were reused; no new gameplay acceptance is claimed.

CI consumed PPC key `921d26c12c98de289e659f30f490e09b23d8e2ca08624690e38f1b3dd9294f51` from private commit `ad34fdc295f374c4787c13343be4af7ae3facafc`, matching the synchronized receipt and reusing the existing library. Evidence: `out/v0.5.11/release/{release-source.json,ci-run.json,ci-ppc-consumption.json,delivery-verification.json,public-download-check.json,published-release.json}`.

## Europe FMV subtitle mapping — included in published v0.5.12 — 2026-09-14

The host-side `82481BE8` PPC hook now returns the original executable language-table pointer for host `GameLanguage()` IDs 1–9 when `r3=0x8336A5F0` and `r4` is 0 or the current ID. The guest table at `0x832455F0` maps those IDs to INT/JPN/DEU/FRA/SPA/ITA/KOR/CHI/SCH. This extends the earlier SCH-only alias repair: callers that pre-filter with `r4=0` no longer receive the ID-0 English suffix when selecting USA/Europe event and FMV subtitle packages.

A local USA/Europe Disc 1 test with `game_language=5`, Vulkan at 3840×2160 and a startup-bundle hit was accepted by the user for the opening FMV subtitles on 2026-09-14. This is opening-scene acceptance only; German, French and Italian visual confirmation, complete FMV/event coverage, whole-game validation and reporter acceptance remain unverified. No new fixture was added. The fix is included in published v0.5.12; publication checks did not re-verify the 50-file manifest.

## Published v0.5.10 — 2026-09-13

Published at [GitHub Release v0.5.10](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.10) on 2026-09-13T21:20:10Z. Release CI [34783107248](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/34783107248) passed for source/tag commit `db63ebaf50fed9612ca66a498f162ad6be69a54e`. The clean ZIP is 44,288,884 bytes with SHA-256 `e1b6b9a84bcf0360f104db2e001ca5e740552834b554a4bf5812dd9c8f52f6eb`; runtime SHA-256 is `25c3c83366143b5f74943ee0cd88789cca0042b3d4a06a7ef986fa8d8de94995`. All 50 manifest payload hashes and ZIP CRCs passed; all four public assets matched anonymous HTTP downloads, sizes, SHA-256 and API digests. CI consumed PPC key `481e10e3e18a083fbc8f3207422a2065c5ba548c39b2588336b02f3da75effdf` from private commit `eb883038c92fdfc0e154b5e3f8b61e346a98034e`. Existing functional checks were reused; no new gameplay or frame-time acceptance is claimed.

The release adds 11 captured Vulkan TAA paths accepted for the reported flicker scenes, plus original CPX/declaration coverage for startup shader preparation. Startup frame-time benefit and whole-game coverage remain unverified. Evidence: `out/release-v0.5.10/`.

## Published v0.5.9 — 2026-09-13

The v0.5.9 package adds conservative Vulkan depth-clear coalescing for 720 compatible EDRAM tile rectangles, preserving D3D12 mapping output, holes and uncleared regions. It also includes texture-key avalanche mixing, captured-shader identity caching, graphics descriptor same-handle suppression, same-framebuffer Plume rebind suppression and per-`GpuSlot` immutable descriptor reuse; the two-slot/fence contract remains unchanged. TAA behavior is retained from v0.5.8.

The matched 45-second static-view comparison used copied `user01` state, 3840x2160 internal rendering, Vulkan, AA3, a 60 FPS cap and 60 W AMD AI MAX+395 power. Mean FPS improved 7.49638→43.47614, with GPU time 132.91221→21.06364 ms. This remains bounded pre-release candidate evidence, not a clean release-build benchmark and not 4K60, 1080p60 at 15 W, whole-game validation or player acceptance. The binding-cache fixture passed the retained duplicate, replacement, incompatible-prefix and post-fence cases; the masked-load register-SIMD experiment was reverted after slower constants and no FPS benefit.

Release CI `34778434518` succeeded for source/tag commit `d26ee8b021784d7232b5319d816227867f98050d` and published the package at 2026-09-13 19:50:00 UTC. The clean package is 44,269,995 bytes with SHA-256 `fd71bf65f92b242f81a350b5b6e97ea7f1991107a2c30e91ece2dd261bac0591`; its runtime hash is `ef93c01db40433fb6d463357ea3e6979b3a1f45cf0d2d3f81be2fcf3f8885faa`. All 50 manifest payload hashes and CRCs passed, four public assets matched anonymous HTTP/size/hash/API digest checks, and CI consumed PPC key `ab194913725bd44df7ea9e248d4e60c561ac4d73f7b87d4c5bb080613bc5567a` from private commit `6433064e547a9460249b1162b938ac0b2c332688` without PPC recompilation. The retained measured candidate predates the release source commit; evidence and detailed boundaries are recorded in the [Vulkan depth-clear performance note](notes/vulkan-depth-clear-performance-2026-09-13.md). Player acceptance and 1080p60 at 15 W remain pending.

## Published v0.5.8 — 2026-09-13

The v0.5.8 source change set includes the seven current-scene TAA `PositionVPSlot` mappings, the bounded sampled-content SIMD comparison, the `LO_QUERY_TRACE` presence cache and the synchronized private PPC cache `main` state. Release CI `34764115203` passed for source/tag commit `6e6f11cf56ef69082f5be5b049e5d48d58154415`; the package was published at GitHub on 2026-09-13. The TAA and CPU evidence is reused from the repository-relative records under `out/`; no source-0.5.7 gameplay result is presented as v0.5.8 package validation.

The reused checks include the 33,927-case Clang 19.1.5/22.1.8 fixtures, the original WPR diagnosis and limited fixed-scene 40 W comparisons (4K about 47 FPS; 1080p about 59.98 FPS). These do not establish stable whole-game performance, power benefit or original-scene TAA visual acceptance. ZIP verification covered all 50 manifest files, CRCs, version 0.5.8 and clean commit provenance.

Evidence: `out/v0.5.8/release-verification.{json,md}`, `out/cpu-query-runtime-20260913/REPORT.md`, `out/taa-live-20260913/` and the detailed [CPU diagnostic note](notes/cpu-live-profile-2026-09-13.md). Anonymous downloads of all four published assets matched bytes, hashes, sidecars and API digests. The PPC key `ec7f34708ff870d6ec940a7a4fe83686d4ec5802344934c9084b85e2cf113c3d` matched private commit `65a6869ea7ec36987f1ca7026c0588d4fa6c40d7`; no new v0.5.8 gameplay or player visual acceptance is claimed.

## Published v0.5.6 — 2026-09-13

Source version **0.5.6** is published at [GitHub](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.6), with Release CI [34726533463](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/34726533463) succeeding for source commit `7124f4b3912df715167acf01f469974045cc3e08` and publication at `2026-09-13T00:05:47Z`. The Windows ZIP is 44,255,182 bytes with SHA-256 `ad6616480fa8905936b3b36d202deb2dad356f07670a2e0f1570984016e897d9`; the standalone updater is 849,920 bytes with SHA-256 `d3356d3fcac410e3ee86c012dc4971ffa4ee507b76f28eebf79e2c575a7eaf74` and is byte-identical to the copy extracted from the ZIP. All 50 manifest files passed hash and CRC checks with clean source-version provenance. The four public assets passed anonymous HTTP 200 and hash/size verification. The release includes the updater manifest transaction fix, Issue #16 particle-material compatibility fallback, PPC prebuilt CI path and the city renderer work; bounded gameplay validation remains separate.

The published package runtime SHA-256 is `1fff598e1a0872da2a7728ceb9921aa2e4a1bff0bd818c224827b84eaf3f5aaa`; CI package verification did not repeat gameplay. The separate local main executable `d50c240d24bcd6cda7a1abc23107fa97f11d18dc5a68167310da6e6f892fe0ed` supplies the retained D3D12/local Asia Disc 3 target-scene validation. Whole-game, Vulkan, other-region and player acceptance remain unverified.

Evidence: `out/v0.5.6/release/{published-release.json,public-download-check.json,delivery-verification.json,ci-run.json,ci-ppc-consumption.json,ppc-ci-upload.json,release-source.json}`.

## Published v0.5.4 — 2026-09-11

Source version **0.5.4** is published at [GitHub](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.4), with Release CI [34550200618](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/34550200618) succeeding for commit `2ad94d418bb0478417ab9589109f1f685ed92eb3`. The ZIP is 44,237,061 bytes with SHA-256 `104ced8b60c16cd1b9013543a3940c9ed8d7cf904c3d05a6a8ef8d591f51d218`; the standalone updater is 848,896 bytes with SHA-256 `7285d0f24331387973a44e4240353d7857f1967163440fb49ba0546c7b9dd844` and is byte-identical to the copy extracted from the ZIP. All 50 manifest files passed hash, CRC, version 0.5.4 and clean-build provenance checks. The runtime executable hash is `3c3b4073f1b7abbcce38747dc08763335ac019edf560df849177176d0399f949`.

All four public assets matched the local verification artifacts and returned anonymous HTTP 200 responses; at that historical checkpoint, the GitHub latest-release API reported v0.5.4. Evidence: `out/v0.5.4/release/{delivery-verification.json,public-download-check.json,published-release.json}`. The release includes the PPC generation guard, optional external assembly profiler, extended F1 archive wait and confirmed installer drag-dispatch fix. Focused validation and package integrity passed; no whole-game, visual or complete F1 acceptance is claimed.

## Published v0.5.3 — 2026-09-10

Source version **0.5.3** is published at [GitHub](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.3), with Release CI [34505504344](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/34505504344) succeeding for commit `6fa9adc281c5693ac0af8a47fec815b20c29ff50`. The ZIP is 44,237,807 bytes with SHA-256 `53beb197b753fa26c5c436f37c4b03bb636857c7390171d3ce6353940a9d81d0`; the standalone updater is 848,896 bytes with SHA-256 `1ad8a0b6e605f050376d59050bf94d598bbd9ec2965610df30cd5e6083fcc5a2`. The 50-file manifest, hashes, CRCs, clean-build provenance and version checks passed; the runtime executable hash is `9dbcef81412c683d4fd76d5a13c16663c3893b4e0944804f8aaf6e1b7fd1f2c0`.

The release adds compact opt-in TAA diagnostics, schema 3 binding evidence, the updater ZIP-root staging fix and private feedback archiving. TAA remains diagnostic research: no new player visual acceptance, complete update transaction or flicker fix is claimed. Evidence is retained under `out/v0.5.3/release/`.

The v0.5.3 entry is retained as historical release provenance. Its asset digests and validation boundaries remain unchanged.

## Published v0.5.2 — 2026-09-10

Source version **0.5.2** is published at [GitHub](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.2), with Release CI `34446196620` succeeding for commit `08a0192713435892f3c1b772abc9ab31bca36359`. The ZIP is 44,209,471 bytes with SHA256 `5d20e569b74c75418cefc9fdd5537477ce4b0a9ca976ed9d64ec77771f78ce7c`; the standalone updater is 849,920 bytes with SHA256 `4aa5e491a2bff885c668cdc4473488fd05acf44e56eba2aa9d4c242a76a4db6b`. All 50 payload hashes and CRC checks passed. The release adds original VS/PS microcode to F1 captures, consent-gated incremental D1 uploads and bounded nonblocking collection.

The recorded runtime evidence remains tied to the retained source-0.5.0 development binary. Release CI built new 0.5.2 executables; existing functional validation was reused without another game run. Credential scans found no management credentials in the audited source or expanded release package. All four public assets returned HTTP 200 and matched the audited hashes in anonymous download verification.

The standalone updater retains the behavior validated for [v0.5.1](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.1). The separate updater download is byte-identical to the helper inside the 0.5.2 ZIP.

## Current validation and limits

### Startup and lower-layer failure diagnostics — published v0.5.11 — 2026-09-14

The v0.5.11 release adds diagnostic evidence for Issues [#6](https://github.com/freefrank/LostOdysseyRecomp/issues/6) and [#22](https://github.com/freefrank/LostOdysseyRecomp/issues/22). Plume D3D12/Vulkan failures route through the runtime logger with the original HRESULT, VkResult or Win32 code, API context and bounded repeated-failure reporting. Renderer initialization identifies the failed resource stage, byte count and slot. Startup records the actual Windows build through `RtlGetVersion`, process/native architecture, source/build revision, compiler, a startup memory baseline and a detectable Wine version; the video device log records GPU, raw driver version, vendor/type and `reported_device_memory_bytes` (on Vulkan UMA this can come from a `DEVICE_LOCAL` heap, without implying that every device reports dedicated memory). PE timestamp and image-size fields remain image metadata; the executable hash is the artifact identity. Main-thread guest-address allocation failures retain a pre-logger `FailureInfo` with operation/API, raw error, parameters, timestamp/thread/process context and allocation-time memory status, then report it separately from the later memory snapshot. Terminal WinHTTP failures retain the API and raw Win32 code; GPU adapter/renderer failure formatting uses a fixed stack buffer and `EmergencyWrite` fallback. Existing snapshots, retention and crash `EmergencyWrite` behavior are unchanged.

The independent Release build linked `LostOdysseyRecomp.exe` and the four targets with clang-cl 22.1.8 at historical source 0.5.10 revision `d8cdf39ca637-dirty`; the source and documentation now target v0.5.11 without rebuilding this validation binary. `LoPlumeLogTest` passed 23 checks, the extended existing `LoMemoryFailureTest` passed 259, `LoUpdaterHttpFailureTest` passed 94, and `LoRenderResolutionGpuTest --diagnostic-log <path>` passed: a real D3D12 invalid-width 16385 texture reported `0x80070057` once with API, raw HRESULT and resource context, then a legal 1x1 resource succeeded. The environment banner and runtime snapshot also passed. The extended build provenance check passed 25 checks, including acceptance of patch-added files while verifying the complete expected tree. Evidence: `out/diagnostic-logging-20260914/REPORT.md`, `out/v0.5.11/release/provenance-after-fix.json`, its stdout/stderr/exit-code records and `gpu-runtime-snapshot.log`. Later isolated D3D12 and Vulkan processes using EXE SHA-256 `975f0b31dd831f916b417df0a90b634010179cfed340861d03582ebea5a1650f` also recorded all new startup fields in complete runtime logs; D3D12 reached guest swap 181, while Vulkan was closed after the log check before renderer initialization. These are logging-recording checks, not complete game/backend acceptance; no shader, draw, network or user-scene acceptance was performed. Issues #6 and #22 remain open with unknown root causes.

Published at [GitHub Release v0.5.11](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.11) on 2026-09-14T02:09:50Z. Release CI [34797755460](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/34797755460) succeeded for source/tag commit `624729cdb1263b96061b1fa14d4d1c5ba0b50239`; package, updater, payload/CRC, public-download and PPC provenance verification passed. The v0.5.11 package includes this diagnostic instrumentation. No additional gameplay or reporter acceptance is claimed.

### Linux first-playable evaluation — historical planning checkpoint — 2026-09-13

The [Linux port evaluation](notes/linux-port-evaluation-2026-09-13.md) preserves the original first-playable direction and its then-current conclusion: Vulkan-only, unbundled ELF using host Mesa, SDL2 and X11/XWayland. Its statement that Linux had not been configured, compiled or run is historical to that checkpoint and must not be read as current status. The published Windows v0.5.11 release and its publication facts are unchanged.

### Linux first-playable — local implementation and WSL validation — 2026-09-14

The `linux` working tree now implements a Vulkan-only, unbundled Linux ELF path. The `linux-clang` CMake preset uses Ninja, clang/clang++ and RelWithDebInfo; Linux compiles generated PowerPC sources directly instead of consuming the Windows prebuilt library. Host path handling, case-folded resource resolution, Linux CRT mappings, fixed guest mapping validation, SDL Vulkan presentation, swapchain-format fallback and the Linux DXC `dlopen` path are included. An explicit `--game` candidate is isolated to the supplied install root, `disc1` or `default.xex`; when `--game` is used, launch CWD must already be the ELF directory because executable-directory chdir is skipped.

The resulting ELF at `out/build/linux-clang/LostOdysseyRecomp/LostOdysseyRecomp` was built with clang 22.1.8 and run in WSL2 Manjaro on 2026-09-14. The user watched the first-playable window for approximately ten minutes and closed it; the process exited 0. The run recognized the supplied `disc1`, loaded the XEX, created a 1280x720 Vulkan swapchain, prepared 28,484 known shaders (28,482 ready; two rejected during preparation), submitted frames and produced nonzero audio. Vulkan was provided by Mesa Dozen wrapping Microsoft Direct3D12 on an NVIDIA GeForce RTX 5080; Dozen is not conformant, so this is WSL Vulkan-on-D3D12 evidence rather than native Linux NVIDIA/Mesa validation. The log also confirms a nonempty SHA-256 identity for the adjacent `libdxcompiler.so` and the Vulkan backend reaching ready state.

This is accepted first-playable window/boot evidence for the tested WSL path, not whole-game validation, native-Linux-GPU acceptance, Steam Deck support, an AppImage/Flatpak/installer package, CI coverage or a Linux GitHub Release. Linux remains local and unreleased; source version remains 0.5.11. Evidence is retained in `out/build/linux-clang/LostOdysseyRecomp/logs/runtime-1789367206166882.log`.

### Card A city measurement — experiment-only on published v0.5.11 — 2026-09-14

**Implementation:** No runtime source was changed. This is an experiment-only measurement of the already-published v0.5.11 Windows package, using source version 0.5.11 at commit `624729cdb1263b96061b1fa14d4d1c5ba0b50239`. The ZIP SHA-256 is `5de068c4e77c82feb0bbe7cfcf1dacbca3d44aa94bde064f7f59f5ad6944e132`; the runtime SHA-256 is `33a460410b7f397187a12ac6984718c1716997936127f94f5dbcb2ef40315283`.

**Validation:** The bounded protocol used hidden 1280x720 D3D12, AA3, a 60 FPS cap and an isolated `user01` city route. It collected 1,836 city frames with `draws >= 800`; `over_budget` was 0%. `fence_wait_ms` mean/max was 0.0007/0.1371, with zero frames over 1 ms; nested flush remained zero; descriptor, upload and arena splits were all zero at D3D12 1800. `gpu_batches` mean/max was 1.0005/3, `gpu_queue` mean/max was 0.8115/1.5575 with zero samples over 16.67 ms, and `draw_ms` mean/max was 3.4395/6.3522. The dated evidence is [Card A city measurement](notes/cpu-card-a-city-2026-09-14.md).

**Acceptance and interpretation:** `exhausted_resource_class=null`. Do not add GPU slots, raise the 1800/2048 limit or add rings; under guide §9.2, add-slot and raise-limit work should remain low priority. The next measurement is Card B's current profile; Card B was not measured here and `LO_VERTEX_TIMING` was not set. This is not 4K, Vulkan, a 3C6T-restricted run or whole-game validation, and it is not player acceptance. The player's `settings.ini` was not mutated. Historical source-0.5.4 `fence_wait` 15.40 ms and two-slot 1.67 ms figures remain historical context and must not replace this baseline. A later same-package run with `LO_VERTEX_TIMING=1` is recorded in the [Card B city measurement](notes/cpu-card-b-city-2026-09-14.md).

**Publication:** This record does not change the published v0.5.11 release, source version or runtime package, and there is no new GitHub Release. No user acceptance of a CPU performance result has been given.

### Card B city measurement — experiment-only on published v0.5.11 — 2026-09-14

**Implementation:** No runtime source was changed. This is an experiment-only remeasurement of the already-published v0.5.11 Windows package with `LO_VERTEX_TIMING=1`, using source version 0.5.11 at commit `624729cdb1263b96061b1fa14d4d1c5ba0b50239`. The ZIP SHA-256 is `5de068c4e77c82feb0bbe7cfcf1dacbca3d44aa94bde064f7f59f5ad6944e132`; the runtime SHA-256 is `33a460410b7f397187a12ac6984718c1716997936127f94f5dbcb2ef40315283`; the executable is 83,536,384 bytes.

**Validation:** The bounded protocol used hidden 1280x720 D3D12, AA3, a 60 FPS cap and isolated `user01`; Down was never sent. `original_saves_changed=false` and `player_saves_changed=false`, and the player's `settings.ini` was untouched. The run produced 1,846 city frames and 891 menu frames with `over_budget=0%`; `last_swap=1920` and `last_draws=893`. The runtime log is `%TEMP%\\lo-city-logs\\runtime-639250002081609096.log`. The vertex histogram emitted 3,622 timing lines, including 1,846 city vertex frames.

City vertex-stage means in milliseconds were `find=0.0693`, `match=0.1535`, `erase=0.0098`, `capture=0.0422`, `copy=0.0091` and `insert=0.0777`, for `stage_sum=0.3616`; p95 was 0.4297 and maximum 0.6323. `match_share=0.424`, `vertex_ms=0.476`, `shader_lookup=0.051`, `pipeline_lookup=0.03` and `draw_ms=3.82`. Vertex-cache means were 1,014 calls and 289 uploads; mean bytes were 168,594 with a maximum of 932,120, `cache_after` maxed at 65,536, buckets were 131,072 and summed rehashes were zero. Evictions averaged 175.7 and summed to 324,386; this is cache-cap turnover, not texture hash-chain activity. Evidence is retained in the [Card B city measurement](notes/cpu-card-b-city-2026-09-14.md); the ignored persisted result is `out/perf-ring/card-b-results.json`.

**Acceptance and interpretation:** `exhausted_resource_class=null`. This experiment provides no basis for B1 (`implement=false`): it did not emit a memcmp-length histogram, `LO_VERTEX_TIMING` is wall time rather than a length histogram, and `EqualSampleBlock64` already covers 64-byte comparisons. It provides no basis for B2 (`implement=false`): no texture-chain snapshot was collected, and lookup is not hot after the avalanche change; do not add another mix. It provides no basis for B3 (`implement=false`): no new guest spin was measured; retain 32 polls followed by yield and `GpuPoll` at 50 microseconds. Do not expand SIMD, hash mixing or poll-wait work from this run. A later same-log Card C prepare gate is recorded separately. This is not 4K, Vulkan, a 3C6T-restricted run or whole-game validation, and it is not player acceptance. Historical source-0.5.8 memcmp 7% render-self and hash-chain 84 figures must not replace this baseline.

**Publication:** This record does not change the published v0.5.11 release, source version or runtime package, and there is no new GitHub Release. No player or CPU-performance acceptance has been given.

### Card C prepare gate — experiment-only on published v0.5.11 — 2026-09-14

**Implementation:** No runtime source was changed and no new city drive was launched. This is an experiment-only prepare gate on the already-published v0.5.11 Windows package, using source version 0.5.11 at commit `624729cdb1263b96061b1fa14d4d1c5ba0b50239`. The ZIP SHA-256 is `5de068c4e77c82feb0bbe7cfcf1dacbca3d44aa94bde064f7f59f5ad6944e132`; the runtime SHA-256 is `33a460410b7f397187a12ac6984718c1716997936127f94f5dbcb2ef40315283`. Evidence reuses the Card B city log `%TEMP%\\lo-city-logs\\runtime-639250002081609096.log` plus a static inventory of `PrepareKnownShaders`, `PrepareKnownPipelines` and XMA `WorkerMain`.

**Validation:** Startup shader prepare already ran on that process: metadata snapshot 158 ms; bundle 2248 ms including 56 ms device-module creation; 28,547 records / 28,545 ready / 2 cached failures; 0 source reads, translations or DXC attempts. Pipeline preparation used 222 recipes, 4 workers and 21 ms. Per-frame city leftovers were `copy_ms` 0.0091, `shader_lookup_ms` 0.051, `pipeline_lookup_ms` 0.03, vertex `stage_sum` 0.3616 and `draw_ms` 3.82. `LO_TRACE_XMA` was unset. Dated evidence is [Card C prepare gate](notes/cpu-card-c-prepare-gate-2026-09-14.md); the ignored persist is `out/perf-ring/card-c-results.json`.

**Acceptance and interpretation:** `exhausted_resource_class=null`. `implement=false` for a new Parallel Prepare / Serial Commit path: startup shader/pipeline prepare already exists and already parallelizes; per-frame leftovers are below queue/copy overhead; a new per-frame task lacks snapshot, output ownership, join, stale cancel, serial fallback, independent task size and input-stability evidence. Do not add a third prepare pool, and do not parallelize guest scene traversal, descriptor mutation or `FSceneRenderer::Render`. The later Card D affinity experiment is recorded separately and remains default off. This is not 4K, Vulkan, a 3C6T-restricted run or whole-game validation, and it is not player acceptance.

**Publication:** This record does not change the published v0.5.11 release, source version or runtime package, and there is no new GitHub Release. No player or CPU-performance acceptance has been given.

### Card D 3C6T city measurement — experiment-only on published v0.5.11 — 2026-09-14

**Implementation:** No runtime source was changed. This is an experiment-only process-level affinity measurement of the already-published v0.5.11 Windows package on a 9800X3D + RTX 5080 host, using source version 0.5.11 at commit `624729cdb1263b96061b1fa14d4d1c5ba0b50239`. The ZIP SHA-256 is `5de068c4e77c82feb0bbe7cfcf1dacbca3d44aa94bde064f7f59f5ad6944e132`; the runtime SHA-256 is `33a460410b7f397187a12ac6984718c1716997936127f94f5dbcb2ef40315283`. The observed process affinity was `0x3F`, limiting the process to logical CPUs 0-5.

**Validation:** The bounded protocol used hidden 1280x720 D3D12, AA3, a 60 FPS cap and isolated `user01` city route. It collected 1,370 city frames; `over_budget` was 0%. `fence_wait_ms` mean/max was 0.0008/0.1336, with zero frames over 1 ms; nested flush remained zero; splits were zero. `gpu_queue` mean/max was 0.8162/1.8092 with zero samples over 16.67 ms, and `draw_ms` mean/p95/max was 3.805/4.556/13.65 with zero samples over 16.67 ms. Compared with the unconstrained Card A run, fence/queue remained in the same class while `draw_ms` increased from 3.44 to 3.81. Cold boot still missed the startup bundle; shader preparation requested 15 workers because `hardware_concurrency=16` ignores process affinity, and the bundle was published in 215.360 seconds. The player's `settings.ini` hash stayed `f41265ad611d2241cc5522f18ea1908156045856b43856730797e62849d9d8a1`, `original_saves_changed=false`, and `player_saves_changed=false`. Evidence is retained in the [Card D 3C6T city measurement](notes/cpu-card-d-3c6t-city-2026-09-14.md) and runtime log `%TEMP%\lo-city-logs\runtime-639250355668305659.log`.

**Acceptance and interpretation:** `exhausted_resource_class=null`. `implement=false` for default host pinning and for writing placement into `KeSetAffinityThread`: the constrained run did not expose a new exhausted CPU/GPU resource class, did not improve fence/queue behavior versus Card A, and slightly increased draw time. The UnleashedRecomp `KeSetAffinityThread` stub matches this project's no-op behavior and is not a pinning recipe. This is not Steam Deck, not 15 W, not a 4C8T measurement, not 1080p60 at 15 W, not Vulkan, not whole-game validation and not player CPU-performance acceptance.

**Publication:** This record does not change the published v0.5.11 release, source version or runtime package, and there is no new GitHub Release. Further development is deferred; no player or CPU-performance acceptance has been given.

### Current-scene TAA jitter coverage — local candidate — 2026-09-13

The local candidate extends the current-scene TAA jitter repair by seven vertex-shader paths: `8d3c80b318235b22` to c4, and `3eb16ad927f44289`, `83b23507725f85bf`, `6742ec1abe49589e`, `0f2b89c7eb1c409e`, `fecf2f9d9bef2702` and `2a7867b5eed37f8a` to c7. These paths were selected from the latest three-frame capture (frames 13429–13431): 39 draws per frame, 117 draws total. The strict audit matched two paths; the position fetch 95 versus auxiliary fetch 94 distinction recovered the other five. All seven original vertex-shader microcode hashes match the historical `f24842` family. The current PS `67b10` was not present in these frames and was not moved into the compensation policy. The capture recorded 2,229 submitted draws per frame with no reported drops; actual CPU-uploaded VS/PS constant banks and per-draw enabled/applied records were not instrumented.

The candidate CPU validation covered seven real current-scene world/VP draws at 1920×1080, frame 13429 and jitter phase 22, with three synthetic vertices per path. The maximum physical sample error was `0.000277985496` pixels and the canonical error was zero. An independent canonical oracle, Z/W and non-VP constants, and retained PS constant banks were checked; this does not verify PS execution behavior. Historical all-phase, resolution and guard fixtures were reused. This is implementation and bounded CPU evidence only: no game run, GPU replay or player visual acceptance is claimed. The Release build completed with clang 22.1.8 in 151.525 seconds and exit code 0; actual link provenance succeeded. PPC was reused under the verified equivalent input/compiler/Release contract without recompilation. Candidate delivery is `LostOdysseyRecomp-taa-scene-20260913.exe`, 83,437,568 bytes, SHA-256 `f47a894ec51f50b099f97f53c08717019eb02330aae943f8cdc33e1568d459b3`, source 0.5.7 with link identity `e6031efe0fe1da1effdcbb26670eb7463e6b20bb6a140c265a53b2bd82da5535`. It was copied into the existing installation without replacing the primary executable (`81705475dafb360c8c30a30acef14f1e365afc0368cd5aaf858889d54f675ac4`) and was not launched. The independent shader-discovery scan is complete; its conservative candidates and exclusions are recorded below and are not a bug count.

### Historical TAA path discovery — static coverage, no runtime mapping — 2026-09-13

The completed offline inventory covered 2,893 cached VS programs and 20,077 cached PS programs. Of the VS corpus, 47 reused an existing microcode SHA/proof and 2,846 were translated with the current production translator and analyzed by `position_evidence`; process/file failures were zero. Translation notes were present for 611 new programs, so generated HLSL does not establish complete microcode semantics. No DXC, GPU operation, cache writeback or production mapping was performed.

The conservative shortlist contains 81 static families: 76 cache-only candidates and five historical main-camera associations, all without translator notes. Eight historical main-camera associations remain separately recorded; three are held by conservative constant-predicate taint and require manual reuse of earlier derivations. The two unmapped matrices observed in the current capture were excluded as non-main-camera paths. Thirteen of the initial secondary-projection-bank families were downgraded; secondary projection candidates are not automatically TAA paths. The eight historical associations and 81-family shortlist are coverage leads, not bug counts. The runtime candidate still contains only the seven reviewed `PositionVPSlot` mappings. Evidence: `out/taa-live-20260913/discovery/DISCOVERY.md`, `out/taa-live-20260913/discovery/discovery.json` and `out/taa-live-20260913/discovery/SHORTLIST.json`.

The bug-fix implementation and its recorded local validation are committed as `2019cd017ab939d0b728cec340f7835c2082e615` and are included in the pushed `main` alongside its existing city-performance work. The 0.5.7 release is published; hosted CI and package verification passed.

### Published v0.5.7 — 2026-09-13

The v0.5.7 package is published at [GitHub](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.7), with Release CI [34743383193](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/34743383193) succeeding for source commit `954d0e17dbe63e49189873db3949ebf5df2462aa` and publication at `2026-09-13T06:51:32Z`. The Windows ZIP is 44,262,080 bytes with SHA-256 `618a96aef78ac79f566d78bed48159e65b6275f1d02f741ed66eeb1c2856a5a7`; the standalone updater is 845,824 bytes with SHA-256 `2bc0bcceefaea3b23731ba939b04d3dd1762b9c3b80c75c85cdc74bb6187d6da`. All 50 manifest files passed hash and CRC checks with clean 0.5.7 source provenance; four public assets passed anonymous HTTP 200 and hash/size verification. Evidence: `out/release-v0.5.7/{public-downloads.json,published-release.json,ci-result.json,assets/verification.json,ppc/ci-consumption.json}`.

The release combines updater recovery/launch-consent work, the guarded shadow-loop and resolve-copy fixes, the HDR16 TAA bloom prefilter, and the material vertex-shader jitter repair. The user accepted the HDR-off/materials-on result for the reported lighting-flicker scene; other scenes and hardware remain unverified. Offline shader, cache and resolve checks are recorded separately; no new functional test, build launch or gameplay run is claimed here.

The matching PPC cache key `3260d975104d2cdf612e5858a7712a23f7264a8c4ebb857adf962b8c16c842eb` was consumed by Release CI from private commit `4894650b407255e0783026e4b73816f7640ec00f`. PPC inputs, 250 generated files, 471 headers and the compile contract match; the original four shards and library `ba3e4c4dff009d6d8e844c007186a6e5040266875bca6423f8fe26f8d27fb21b` were retained without compilation or relinking. Evidence: `out/release-v0.5.7/ppc/ci-consumption.json`.

### Standalone updater recovery and completion policy — v0.5.6-hotfix1 development record (included in v0.5.7; never released separately)

The `v0.5.6-hotfix1` development change, included in v0.5.7, removes local package-provenance and executable-hash gates from the standalone recovery path. It prefers a valid `source-version.txt`, falls back to a valid version-only `manifest.json`, and uses `0.0.0` when metadata is missing or unusable. An updater-only empty folder, or a missing game executable, therefore requests the latest release; stale or malformed metadata no longer blocks the check, although a selected valid version can still be up to date. `PrepareAtStartup` compares the selected version with the latest release without requiring an installed manifest/version match. Download SHA-256 verification, safe archive extraction, transaction rollback and update path-safety checks remain in force.

After a successful update, the local helper asks whether to launch the game, with **No** as the default. Silent mode does not launch the game; failed updates do not auto-restart it; and a requested launch failure leaves the installed update in place. The handoff runner uses the locally installed updater so this completion behavior remains active when the downloaded package contains an older helper.

The recorded checks and local helper for this historical development record were built before the suffix-only version metadata change, as source version 0.5.6: `LoUpdaterStandaloneTest` passed 44/44 (`out/updater-simple/standalone.log`), alongside the companion updater version/asset/integrity/staging/rollback/helper/preservation log and `LoUpdaterHelperContextTest` for a Unicode caller working directory and no unsolicited launch. The standalone count covers recovery and handoff behavior; transaction rollback remains covered by the companion updater fixture. These are hidden synthetic-process checks only. No real game or visible Yes/No dialog interaction was performed.

### GPU shadow-loop and resolve-copy fixes — included in published v0.5.7

The bounded GPU fixes are implemented in [`4715b60`](https://github.com/freefrank/LostOdysseyRecomp/commit/4715b60) and merged with `github/main` commit `b39c2c9` in [`1d139c9`](https://github.com/freefrank/LostOdysseyRecomp/commit/1d139c9). The translator exits only for the proven structured LoopEnd cases, and the renderer reuses identical adjacent color-resolve copies within one command batch while invalidating reuse across Draw, clear, transfer, allocation, submission and external access. Shader cache version 22 rejects older binaries and startup bundles. These changes are included in published v0.5.7.

Recorded validation includes 11 LoopEnd guard cases and 12 D3D12 cases across 64 mixed-lane inputs, nine related guest shaders compiling to both DXIL and SPIR-V with eight conservative optimizations and one original path, 99 cache checks, `LoResolveCopyPolicyTest` 30/30, and existing Vulkan/D3D12 resolve GPU checks covering three allocations and two passes per backend with zero mismatched pixels and correct copy/skipped/resolve behavior. The production PS SPIR-V matches the previously measured probe, so the single-frame ABBA and byte-identical PNG evidence is reused; the prior offline ABBA means were shadow 7.711424 → 1.244464 ms and total timed events 14.026832 → 7.1088 ms. These results do not establish hardware power, whole-frame gain, gameplay hit coverage, cross-scene behavior or player acceptance.

The built diagnostic executable is `D:/Mihoyo/LostOdysseyRecomp-windows-x64/LostOdysseyRecomp-gpu-perf.exe`, SHA-256 `313CDD34712154A88FEEAF9C25D8AB1409705D327D8B0FD15252A52562C1C211`, 83,422,720 bytes. It came from a dirty source-0.5.6-hotfix1 working tree with local extras and is not evidence for the clean published binary. A complete merged-binary game run remains pending. Delivery evidence: `out/gpu-profile-20260912/production-01/delivery.json`.

### TAA bloom prefilter and geometry diagnostics — included in published v0.5.7

The current candidate adds a guarded HDR16 area prefilter for the identified TAA bloom input, linear sampling after that filter, and an explicit `LoBloomPrefilterTest` target. D3D12 and Vulkan fixture runs passed the HDR negative-value, alpha, area-weight and subpixel checks. Captured inputs from frames 10170–10172 matched an independent 3x3 area-average reference within half-precision output error. The linear-only fixture also passed on both backends, covering 65 phase responses and the recorded red-step bound. These checks validate filter behavior and do not establish a live visual fix.

At the earlier bloom-only checkpoint, the user's same-scene result was partial: the upper large robot was stable while the lower enemy eyes still flickered. That candidate and its lower-eye diagnosis remain historical; the later material-jitter repair is accepted for the reproduced whole-lighting scene and is recorded in the published v0.5.7 section above. The follow-up diagnostic retained the AA3 condition while removing the transient `temporalJitter` gate and added process-scoped AA, jitter, history and bloom controls plus optional geometry/resolve tracing. An eight-frame trace showed history reuse without gaps, and the frame-7208 capture found four compared draw pairs with identical VP, world, active-bone, vertex-buffer and index data, excluding a CPU upload mismatch for those pairs. Cross-backend comparison covered four 84-index pairs; the later report includes additional 144-index eyes outside that capture, so complete scene coverage is not established. Detailed evidence is in [the TAA bloom prefilter note](notes/taa-bloom-prefilter.md); prior fixture results are reused.

### Local main 0.5.6 gameplay validation and PPC provenance

The merged local `main` at source version 0.5.6 built successfully with the normal CMake Release configuration in 173.782 seconds. The runtime executable SHA-256 is `d50c240d24bcd6cda7a1abc23107fa97f11d18dc5a68167310da6e6f892fe0ed`; the updater SHA-256 is `732681bf2a1c74106bb1ea90b32912b3269304dea3abe8ba351f6e1d5b94cc3e`. The build used no diagnostic object overlay or battle bridge. Evidence: `out/main-bugfix-0.5.6/REPORT.md`, `artifacts.json` and `build-result.json`.

The producer PPC key is `50b8ad415be405b302252558e0fd960913c3ce6a15d991ae3607142f1a3821a5`, uploaded at private commit `6a6ed03152431a232165e35b19b7f94f09bbbda9`. The CI-compatible key is `d89197759478260d7e135b654993d57cff30127f1d41cf30c410fd0ae4be27f4`, uploaded at private commit `a6cd91ea35261dd202b78e93b4acb65973369d07` for runner representation. Both use the byte-identical verified library SHA-256 `ba3e4c4dff009d6d8e844c007186a6e5040266875bca6423f8fe26f8d27fb21b`; no PPC recompilation occurred. The 19 key differences are fully explained by five line-ending differences and fourteen symlink placeholder/target-content representations; 250 generated outputs, 471 PPC headers, CMake fingerprints and the Release compile contract are identical. CI run `34726533463` completed Resolve, Record, Retrieve, Restore, Release build and packaging successfully. The resulting draft was published after package verification. `lo.ppcAutoSync=false` remains unchanged. Evidence: `out/main-bugfix-0.5.6/ppc-bundle/REPORT.md`, `upload-plan.md`, `out/v0.5.6/release/ppc-ci-upload.json` and `out/v0.5.6/release/ppc-ci-compatible-plan/compatibility-provenance.json`.

The main-binary target-scene replay passed on D3D12/local Asia Disc 3 using the new 0.5.6 executable (SHA-256 `d50c240d24bcd6cda7a1abc23107fa97f11d18dc5a68167310da6e6f892fe0ed`). It completed the unsuppressed freeze sequence, subsequent map229/menu progression and visible movement. This remains bounded scene validation, not whole-game, Vulkan, other-region or player acceptance; do not substitute the retained source-0.5.4 gameplay evidence for validation of this executable.

### Issue #14–#16 triage — 2026-09-12

GitHub Issues [#14](https://github.com/freefrank/LostOdysseyRecomp/issues/14), [#15](https://github.com/freefrank/LostOdysseyRecomp/issues/15) and [#16](https://github.com/freefrank/LostOdysseyRecomp/issues/16) were read as **OPEN** on 2026-09-12. Their tracker state remains separate from implementation, validation and reporter acceptance.

- **#14:** The two attachments describe different historical signatures from source 0.5.0 and 0.4.2. The current source already contains the earlier word-selector guard and render-flush mitigation, but neither attachment proves the current root cause or a 0.5.4 reproduction. A clean-package cage-scene reproduction and save are still needed.
- **#15:** The log shows a successful 206,000-byte first-save write and a later full read, then continued rendering until the window closed; the reported first-save progression hang therefore remains unresolved. The updater staging defect is fixed for subsequent transactions using the new `StageArchive`: the transaction now applies `manifest.json` itself and rolls back on failure. The focused `LoUpdaterTest --manifest-transaction` run passed three scenarios with zero failures: successful update and post-apply rollback, failure after manifest replacement, and tamper rejection, including plan serialization round-trip. This does not automatically repair an already mixed installation or remove old resources, and is updater transaction coverage only; no helper/game launch, network update or player acceptance is claimed. `PrepareAtStartup` compares manifest and executable source versions only; `up-to-date` does not prove every payload is current. The logged `dxcompiler.dll` and `dxil.dll` identities match the retained official v0.5.4 manifest hashes, and `DxcIdentity` hashes the actually loaded module paths; this supports those two DLLs only. Mixed installation is not established as the cause of the reported hang. Evidence: `out/bug-fix-evidence/updater-manifest-build/REPORT.md`.
- **#16:** The published v0.5.4 executable reproduces the King Train freeze/crash on local Asia Disc 3 D3D12 with the supplied `user08` path. The zero-count cooked and runtime `FParticleVF8336CA10` tables for `gt9_0_map.cs__frzShader1` / raw `xf_shd_aniflz.freeze` explain the null shader read at guest `0x823DFE14`. The narrow `particle_material_compat` fallback preserves sprite parameter updates and original logic, then uses the engine default material only for raw blend 2 when the normal particle shader is absent; its caller guard is limited to the ordinary sprite builder. `LoParticleMaterialCompatTest` compiled and ran with zero failures, covering the material compatibility policy only. The final branch native build in `build/branch-native-r1` succeeded, and `final-reload-01` successfully reread the new slot 11/`user10` checkpoint at the saved position and produced field shot `9326`. The r2 candidate separately demonstrated map 229 movement and a normal menu, but predates the final caller guard and is not final-branch build evidence. Final-branch `final-freeze-01` then completed the target sequence with visible frozen King/guards/carriage frames, later train animation, normal map229/menu progression and visible movement/camera change. Asia Disc 3 D3D12 target-scene validation is complete; Vulkan, other-region coverage and player acceptance remain pending.

Detailed evidence and boundaries are recorded in [Issue #14–#16 triage](notes/issues14-16-triage.md). The fixes and validation records above are included in published v0.5.6.

### Issue #6 startup allocation — current diagnosis — 2026-09-12

GitHub Issue [#6](https://github.com/freefrank/LostOdysseyRecomp/issues/6) is **OPEN** as of 2026-09-12. Three source-0.5.4 logs from 2026-09-11 consistently fail during the preferred and fallback `VirtualAlloc2` reservations with error 6 (`ERROR_INVALID_HANDLE`), before `CreateFileMapping` or `MapViewOfFile3` is reached: [log 1](https://github.com/user-attachments/files/32122228/runtime-1789145203661201.log), [log 2](https://github.com/user-attachments/files/32124989/runtime-1789146389573701.log) and [log 3](https://github.com/user-attachments/files/32125052/runtime-1789146749477451.log). The second log records `available_physical=14880874496` and `available_commit=33202167808` bytes at the delayed error report; those values do not support a RAM-exhaustion conclusion.

The 2026-09-12 follow-up reports that a clean extraction still fails ([comment](https://github.com/freefrank/LostOdysseyRecomp/issues/6#issuecomment-5647622343)). The current [`guest_address_space.cpp`](../LostOdysseyRecomp/kernel/guest_address_space.cpp#L54) passes `nullptr` for the process handle in both `VirtualAlloc2` calls, which is permitted by Microsoft's API contract; this is not a confirmed source defect. No code change, new local validation, recovery acceptance or root-cause determination is recorded. The historical v0.4.2 closure and its request to reopen on recurrence remain provenance, not evidence that the current path is resolved. The maintainer has requested the Windows version/build, architecture and compatibility environment, plus a complete current clean-extraction log ([comment](https://github.com/freefrank/LostOdysseyRecomp/issues/6#issuecomment-5648995559)); these remain pending.

Issue #6 therefore remains an unresolved startup-allocation diagnosis. The current evidence does not establish whether the failure is caused by the OS, compatibility environment, API behavior or another condition, and does not establish shared causation with Issue #5.

The local source-0.5.10 instrumentation above implements the previously pending observability items: OS/build/architecture identity, allocation-time API/flags/process-handle/memory context before logging initializes, and original terminal WinHTTP error codes. This changes the available evidence only; it does not determine the cause of Issue #6 or #22 and does not change guest mapping, arena size, slot/wait behavior or network request policy.

### PPC auto-sync and key-resolved prebuilt — current main synchronization, local hook off

The current synchronization update publishes an ordinary fast-forward to the private
`main` branch, preserves unrelated archive files and retains existing `ppc/<key>` branches
as historical build-selection references. Concurrent advances receive bounded retries;
the update does not create new PPC branches. Private `main` currently includes the merged
cache at `9e387adc045fe3b8ba4e6d1956812d11050bc9ed`. Release CI checks out that branch with
the SSH deploy key, validates the required fingerprint and compile contract before
restoring PPC, and records the immutable private HEAD in the identity artifact; a mismatch
fails with synchronization or `rebuild_ppc` guidance. The implementation remains
unreleased; real GitHub Release CI has not yet proved this new workflow. `lo.ppcAutoSync`
remains unset locally, so this does not claim automatic enablement. `actionlint`, the
23-test PPC sync suite (including six bare-Git integrations), and two embedded CI identity
fixtures passed. Runtime/gameplay validation remains separate.

Historical branch-based behavior was pushed to github/main as [`2c0456c`](https://github.com/freefrank/LostOdysseyRecomp/commit/2c0456c). The opt-in configuration remains `git config --local lo.ppcAutoSync true`; CMake `LO_PPC_AUTO_SYNC` reads that setting. After a successful PPC library build, the post-build hook runs `ppc_sync.py sync --already-built`. It is not a file watcher. At that historical checkpoint, a matching input/compiler key reused the immutable private `ppc/<key>` branch; a changed key published a new branch with shards of at most 40 MiB. CI, imported libraries and `LO_PPC_SYNC_ACTIVE` never upload. The historical release workflow resolved the library by key from `ppc/<key>` instead of a pinned private SHA; `rebuild_ppc: true` still compiles from source.

At the historical checkpoint, nineteen synthetic sync cases in `tools/tests/test_ppc_sync.py` passed. Built-library roundtrip and change-during-build checks passed. The real `LoPpcAutoSync` hook uploaded an existing library only to private commit `5e80263491b39dc0012146dd3a31cf5eea533225` on branch `ppc/4d21302a4eef224c82691878fbcb6cd2f427b60d676b3e692e78598257b5d1b4`; a subsequent same-key sync was unchanged and produced no PPC C++ compile or game run. The recorded local setting was `lo.ppcAutoSync=false`. The new 0.5.6 key was subsequently uploaded to private commit `6a6ed03152431a232165e35b19b7f94f09bbbda9`; hosted CI consumed the compatible retrieved artifact successfully. Sparse-clone restore/check and a simulated-CI Release contract key match passed. Evidence: `out/ppc-auto-sync-evidence/build-sync.log`, `github-output.txt`, `github-output-second.txt` and `out/ppc-sync/receipt.json`. The earlier manual prebuilt path remains historical at commit [`2b5b1d1`](https://github.com/freefrank/LostOdysseyRecomp/commit/2b5b1d1) and CI [34553414428](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/34553414428). See [release packaging](notes/release-packaging.md).

The historical branch-based workflow is included in published v0.5.6 and was absent from published v0.5.4. Hosted [PPC prebuilt tests](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/34565564964) passed for `2c0456c`; user gameplay acceptance remains separate.

### PPC generation guard — v0.5.4

PPC source generation now runs through `python -B tools/ppc_codegen.py generate`. `tools/build_tools.bat` records the generator binary and source receipt; generation and `check` validate the TOML/recompiler inputs, generated output hashes and the absence of obsolete 64-bit jump-table switches. The wrapper preserves the prior output tree if generation fails. The configured runtime build exposes `LoPpcCodegenCheck` as an order dependency before guest objects. The seven-case synthetic guard passed, and a fresh generation followed by `python -B tools/ppc_codegen.py check` completed successfully, producing 247 C++ files with 843 low-word (`u32`) and zero `u64` switch sites; 246 `ppc_recomp` instruction-comment stream hashes are unchanged. Existing 3,258 instruction checks, 109 word-switch checks, 843 recognized tables and 44,523 selector evaluations are reused from the earlier semantic evidence; no new gameplay acceptance is claimed. Package integrity and release provenance are recorded in the published v0.5.4 section above.

### TAA crowd coverage follow-up — local, unpublished

The current local executable hash `56e9e8d53f798a9ada10726c0141b0746b4dc3d0fbd763ae159eb8ee153b5d2d` matches the retained source-0.5.4 crowd candidate. The implementation adds seven local VS coverage paths: `fe3efe042c311110` to c4, and `1474db97dfc0afad`, `97b5d441419b5533`, `6742ec1abe49589e`, `3eb16ad927f44289`, `0f2b89c7eb1c409e` and `fecf2f9d9bef2702` to c7. VS `99c2` and PS `67b10` now share `IsSceneDepthReconstructionPair` in `temporal_jitter.h` and `renderer.cpp`, so resolve, compensation and diagnostic parsing use the same pair classification. Existing guards and the c4 algorithm are retained.

The audit baseline contains 63 observations: 20 cross-pass groups and 43 PS reconstruction observations. The postfix policy covers the first four VS paths; the 43 strategy candidates disappear from that policy output, while 20 older upload differences remain diagnostic evidence rather than visual acceptance. The audit is retained under `out/tools/taa-audit-latest/AUDIT.md`.


### F1 capture archive timeout adjustment

The v0.5.4 source change increases the local F1 menu ZIP archive wait from 60 seconds to 180 seconds for large captures. Optimal compression and background behavior are unchanged. The retained local evidence records two separate approximately 2.5 GB captures timing out at 60 seconds; it does not record a game run or F1 acceptance. The published package identity and clean-build evidence are recorded above.

The earlier local source-0.5.3 executable and installation matched SHA-256 `CF70EA663ED230334145E1135CA97A58DFE3A34C65ED7D43E9E428C41B53270B` (`out/f1-zip-180s/install.json`; previous EXE/metadata in `out/f1-zip-180s/backup`). That build linked successfully but required copying the existing installation's DXC DLLs after the source-directory DLL copy failed. This remains historical local-build provenance, separate from the v0.5.4 CI package above.

### Installer drag dispatch — v0.5.4

The installer drag-dispatch re-entrancy path now posts `WM_NCLBUTTONDOWN` with signed screen coordinates instead of synchronously calling the window procedure. `DragDispatch` passed 1/1, and the reporter confirmed the real installer drag fix. The installer-only local package is retained at `out/installer-drag-fix/dist/InstallGame.exe` (11,888,743 bytes; SHA-256 `707CD7D2E9F4AB3BF33363E172FAAD5CFCFA6B1A53161FEE0E7F53735B7C7FA7`). No game or importer regression validation is included; the fix is included in published v0.5.4.

### Optional assembly profiler

The standalone `tools/asm-profiler` utility is implemented for Win64 external attachment. It samples live thread RIPs by briefly suspending each target thread, reading its context, and resuming it before allocation or I/O; after collection it uses DbgHelp for a post-capture module, symbol, source-line and 32-byte code snapshot. `report.py` uses Capstone 5.0.6 to produce offline HTML and JSON with instruction hotspots, function self-sample rankings, a per-thread OS CPU-time table (not allocated to RIP), `--tid` filtering, HTML filtering and optional matching generated PPC comment context via `--source-root`. The HTML has no external resources. Existing output is rejected unless `--overwrite` is supplied, and the target executable is protected from replacement.

The Python reporting fixture passed 6 tests (`tools/asm-profiler/test_report.py`), followed by the independent thread CPU-time table check (1/1), for 7 covered checks. Coverage includes disassembly, aggregation, thread filtering, unknown/empty samples, changed code snapshots, HTML escaping, guest-comment boundaries and the thread table. MSVC 19.44 Release native build completed without warnings; a synthetic 2-second/10-ms background run collected 187 samples with zero failures, all code bytes, successful PDB/source resolution and a 1.84375-second busy-thread CPU delta. That synthetic capture remains historical fixture evidence. Samples include sleeping/waiting threads and are wall-clock shares; there is no call stack. Matching PDBs and generated sources are required, and PPC comments are source-line context rather than verified guest PCs.

Two isolated 2026-09-11 gameplay captures sampled the published v0.5.4 runtime (SHA-256 `3c3b4073f1b7abbcce38747dc08763335ac019edf560df849177176d0399f949`) with `lo_asm_profiler.exe` (SHA-256 `d6326a19c51e97ee1338364a98f36a7f2cb29aeb537c82f6b100bc3104b9d0f1`). There was no matching PDB beside the EXE, so every EXE sample is unresolved. Both runs used isolated working copies, `LO_AUDIO_MUTE=1` and `LO_TEST_INPUT_FILE` only; original install save timestamps were unchanged. Session 1 (`out/asm-profiler/play-2026-09-11/`) loaded user00 and walked `xenon_scr.fpd` for 15.036 s (5,092 samples, 0 failed, 38 threads). Hottest OS CPU TID 52396 used 8.11 s (that thread: EXE 38.8% + `NtWaitForSingleObject` 36.6% + AMD/D3D12). All-thread wall share was dominated by ntdll waits; EXE was about 2.18%. Heartbeats showed about 36 fps, 1,700–2,300 draws/frame and a 1280×720 frontbuffer; sampling dipped to 15.8 fps (suspend perturbation). Session 2 (`out/asm-profiler/play-2026-09-11-city/`) loaded user01 (02:15 Lv.10) for a city walk on `xenon_scr.fpd` for 15.017 s (10,736 samples, 0 failed, 44 threads). Hottest TID 5092 used 7.27 s (EXE 35.2% + `NtWaitForSingleObject` 39.8% + amdxc64/D3D12). After sampling: about 43–44 fps, about 1,913 draws/frame, 1280×720. The bottleneck picture matches session 1; without a PDB there is no new function-level hotspot. Wall-clock all-thread suspend snapshots include waits. There are no stacks, ETW, cycles or GPU pass timing, and no CPU-utilization, instruction-latency, cache or branch counters. This is diagnostic evidence, not a performance fix, player acceptance or a new Release. Session detail: [assembly profiler gameplay captures](notes/asm-profiler-gameplay.md). Fixture and capture evidence remains under `out/asm-profiler/`.

### GPU command-list ring and descriptor reuse — included in published v0.5.6

Branch `perf-gpu-ring` implements a 2-slot D3D12 command-list ring and raises the D3D12 descriptor-set limit to 1800 with 2D texture-set reuse (`b91d279`). A follow-up binds unused 2D/3D/cube banks to static dummy sets, uses BatchCache last-hit, and skips unchanged constant uploads (`ed90fe9`). The work is included in published v0.5.6; the retained measurements are diagnostic and are not player acceptance, Vulkan coverage or a 60 FPS claim.

Header fixtures pass: LoRenderBatchPolicyTest 22/22 and LoTextureDescriptorCacheTest 12/12, including a 2000 last-hit loop. An isolated Continue-sequence city walk loaded user01 (Lv.10) at 1280×720 D3D12 TAA=3 cap 60 with `LO_BACKGROUND=1`, `LO_AUDIO_MUTE=1` and `s@120,a@240,a@360,a@480,a@700,a@900`. Screenshots confirm an Uhra street walk. Original install save timestamps were unchanged (user00 2026-09-10 18:22:37, user01 18:28:28).

Ring EXE SHA-256 `5917F389F9FD9E88FDEC6DBD3437ADE76D415F1653FB6924575ACCF478C1B9AD`. Stable city (1664 frames, swap 1367–3030): about 57.7 fps (49.1–60), draws 1816, batches 2.00, splits 0, draw_ms 9.82, fence_wait_ms 1.67, gpu_queue 3.91, bind_ms 1.99, descriptor hits/misses 5289/160. Published v0.5.4 city diagnostic: 31–44 fps, about 5.2 batches, fence_wait_ms 15.40, draw_ms 21.19. Dummy EXE SHA-256 `02E303F1462546FB98236446E24B2397DF762179923DE1D7C02852317ED37BC4`. Stable city (1639 frames): about 56.0 fps (28–60), bind_ms 1.58, hits/misses 837/156. Dummy is bind-path only and shows no fps win versus the ring run. The two EXEs are sequential diagnostic captures, not a laboratory A/B.

These city numbers are diagnostic. They are not 60 fps acceptance, player acceptance, Vulkan coverage or a new Release. Compare note: [GPU ring measured comparison](notes/perf-gpu-ring-compare.md).

### City CPU conversion follow-up — included in published v0.5.6

The measured vertex-cache diagnosis and bounded-cache follow-up are now the
current city CPU result. With `LO_VERTEX_TIMING=1`, frame 1858 isolated a
41.8241 ms `unordered_map` insertion during a 262,144-to-524,288 bucket rehash;
the frame's 495 endian copies took 0.0254 ms in total. `gpu/vertex_cache.h` now
reserves a 65,536-entry metadata cache and, when full, examines at most 16
rotating candidates for eviction. It does not change GPU arena bytes, offsets,
slots or waits, and does not add a flush. `LoVertexCacheTest` passed 3,569,548
checks once; existing SIMD and arena results were reused.

Three single Hidden Uhra residential captures used the same 1280x720 D3D12,
AA=3, 60-cap, background and muted setup. The final same-EXE control moved only
the driver input/screenshot-request files to TEMP. The final all-city sample
had 1,790 frames at 59.651 FPS mean, 45.989 FPS 1% low and 43.0117 ms worst
accepted-present. In the paired render-frame 1600–2800 window, mean was 59.918
FPS, 1% low 54.495 FPS, draw max 13.162 ms, vertex max 2.2981 ms and there
were no draw over-budget samples or rehashes. The requested mean of at least
58 FPS is met on this route with the 60 cap. The strict present ratio remains
50.375% over 16.67 ms, so this is near-60 route evidence rather than a locked
60 FPS result or strict S4 pass.

The bounded run exposed a 412.9283 ms previous-swap post-present interval;
moving the polled control files to TEMP reduced that maximum to 0.4193 ms and
the long-stall class did not recur. This localizes the measurement-path I/O,
but does not establish a Syncthing filesystem or scheduler root cause. The
new previous-swap, post-present and command-processor-idle fields are
diagnostic only. `LO_VERTEX_TIMING` remains off by default. All captures were
Hidden and muted, every `original_saves_changed` result was false, and the
independent original-save SHA-256 inventories had zero differences. The final
image was visually checked as the expected Uhra residential area.

The final executable is `out/perf-ring/run/LostOdysseyRecomp.exe` with SHA-256
`9D9460248FEB72AC7239ABD40AC1DA6619847F176CF4AA38AD6F725C6923852B`; its PDB
is alongside it. Compilation, linking and provenance completed; the known
post-build `dxcompiler.dll` copy failure was reused because the run directory
already contained the DLL. This v0.5.6-targeted work is included in the published v0.5.6 package; its measured evidence remains tied to source version 0.5.4. It establishes neither whole-game behavior,
player acceptance nor a new release. Evidence: [city vertex-cache follow-up](notes/city-60fps-handoff.md),
`out/perf-ring/vertex-stage/{REPORT.md,comparison.json,identity.json,vertex-cache-test-evidence.json}`.

The current source and published release are v0.5.6. The executable and
performance evidence above remain from the measured source-0.5.4 runtime; no
rebuild or test rerun was performed for this version increment.

The implementation is recorded in code commit
`ae287f2a43a73c6f6bea61c40822c37f40afe052`. The measured executable was built
from the same runtime source before that commit; the commit did not trigger a
rebuild or rerun, so its hash and the reported performance figures are
unchanged.

The earlier SIMD conversion run remains historical context: its 41.736 ms
vertex hitch did not identify the cause. Current follow-up should focus on
broader-scene and longer-session coverage of bounded-cache eviction and on
remaining accepted-present timing variability; do not reclassify the TEMP
comparison as proof of a filesystem root cause.

### SIMD conversion diagnostic — included in published v0.5.6

The local renderer now uses `gpu::geometry_prepare::CopyDwordsSwapped` for the
two vertex-buffer conversion paths. Endian 0 uses `memcpy`; endian modes 1/2/3
use an SSSE3 four-dword loop where available, followed by scalar tails and the
portable fallback. The production object contains the expected `vpshufb`
instruction. The focused geometry fixture passed 16,685,865 checks, including
unaligned inputs and inaccessible-page boundary guards
(`out/perf-ring/simd-copy/geometry_prepare_test.log`).

A same-harness Hidden user01 city run used the local executable
`AFCC4BE89B42C033FB4041185E35F33CDDFCF7832FEACE2F6FA8328F10BFA7C2` and
completed in 57.5 seconds with 1,801 city frames, 884 menu frames and
`original_saves_changed=false`. The run kept 1280x720, window mode 0, backend 0,
AA 3 and frame-rate target 60, with the original configuration unchanged. City draw mean was 8.203 ms (p95 10.473 ms,
p99 11.547 ms, max 49.265 ms), vertex mean 1.467 ms, fence wait 0.003 ms and
`gpu_batches` 1.001. On the fixed render-frame 1600–2800 window, draw mean was
7.838 ms and vertex mean 1.396 ms; the matching same-harness no-rebuild baseline
was 7.766 ms and 1.436 ms. In the fixed 1600–2800 window, present mean was
16.779 ms, 51.457% of intervals exceeded 16.67 ms, and the 1% low was 36.539
fps; the baseline was 16.831 ms, 50.458% and 31.044 fps. The SIMD run still contained a 41.736 ms vertex hitch and a
separate 47.467 ms flush sample. Its 779.572 ms worst accepted-present interval
was a load-in `between_ms` interval, not a flush. Neither redirected-log run
reproduced the earlier 200–400 ms flush class, but the residual stalls remain
unexplained.

This is implementation and bounded diagnostic evidence only. The earlier
handoff attribution of the vertex hitch directly to `CopySwapped` is not
confirmed. `tVertex` covers the whole vertex-fetch loop
(`renderer.cpp:3339–3380`), including lookup, sample matching, capture/vector
resize, conversion and map insertion (`renderer.cpp:2818–2850`); rehash,
allocation, page fault and scheduling effects remain hypotheses. No SIMD
performance gain, S4 pass, player acceptance, Vulkan coverage or new release
is claimed. This local source remains version 0.5.4 and is absent from the
published v0.5.4 package. Full evidence is in the [city 60 FPS
handoff](notes/city-60fps-handoff.md) and
`out/perf-ring/city-simd-comparison.json`.

### TAA binding evidence — 0.5.2 historical build

The 0.5.2 development client now adds an opt-in schema 3 binding-evidence record for a bounded TAA draw. It preserves the VS/PS renderer hashes and draw classification, then records the consumer slot and phase, guest and uploaded VP values, raster viewport and jitter bits, selected PS `c0` values, and the referenced texture's format, extents, resolve rectangle, producer state, producer draw count, frame ages and resolve gap. The record uses an independent fixed 64-entry POD queue; collection is batched in groups of eight and is triggered by the existing 60-second background cadence or F1 request. It does not add GPU readback, a new wait, pointer or address data, or a jitter mapping.

The production build completed successfully with executable SHA-256 `c3711463fd4d21f17b68af27cecd2df35851af49e9567e956596b48a7ae1e259` and size 82,904,064 bytes. CPU queue and producer fixtures passed 82 and 32 checks with zero producer allocations. The updated 9,761-byte, eight-record serializer passed three real C++ → Worker → SQLite uploads with HTTP 200 responses; replay preserved eight deduplicated rows, 1,048 record leaf values and 640 IEEE bit words. Evidence: `out/v0.5.2/taa-binding-collection/production-build-resume.json`, `run.log` and `worker-integration.json`.

The earlier schema 3 deployment was Worker version `4db7e156-8a21-4f57-8f0d-bacd0c4576ab`; it advertised schemas 1, 2 and 3. The current Worker is `14e74c54-1213-4240-9877-047f35cdda75` and advertises schemas 1 through 4. No D1 schema migration was needed. There is still no new game run, F1 menu acceptance or visual acceptance for the `e810cfacc107fd3c` path, so schema 3 remains diagnostic evidence and does not authorize a jitter mapping.

For schema 3, `draws` and D1 `max_draws` count samples of the same recorded state, not all draws in a frame or a total frame draw count. `producerDraws` is a conservative CPU writer count; values above 65,535 become `0` with `Unknown` state. `Uniform` records consistency among CPU-recorded writer matrix and jitter values, without proving GPU completion, per-pixel use or recursive texture dependency. Detailed serializer and artifact evidence is in `out/v0.5.2/taa-binding-collection/REPORT.md`.

### Compact automatic diagnostics — v0.5.3

The new compact diagnostic contract uses the existing opt-in and records one fixed 32-frame CPU window at most every 180 seconds. It bounds the window at 24 VS/PS pairs and eight binding records, then includes final TAA history/rejection booleans, coverage reason counters, pending counts and delivery results. The request is capped at 32 KiB and uses a strict compact receipt; sparse GPU data is D3D12-only and remains unlinked to this window. No color preview, raw F1 ZIP, random/session/player/device identifier or new GPU completion claim is added.

The Worker validator passed 9/9 checks in `out/v0.5.2/compact-diagnostics/worker-tests.json`. The C++ fixture passed 68 checks with zero allocations; its corrected 18,905-byte request passed two loopback HTTP 200 uploads into the actual SQLite schema, preserving one deduplicated row and all window fields. The 0.5.2 client build completed at 2026-09-10 16:42:36 UTC with SHA-256 `59f039ea84404c1ab85095a95a10b32d435bf1d39b1ca610b38d15edb44ce62e` and size 82,946,560 bytes. Eight new ledger checks and one archive check passed; the installed wrapper's current archive sync remained unchanged across 18 cases with identical source hashes and modification times. The private archive schema 4 allowlist is published in commit `728d6030980a7be39feca493319f54bfb1a62e74`, and current Worker deployment `14e74c54-1213-4240-9877-047f35cdda75` advertises schemas 1 through 4 with temporal and shader-source capabilities enabled. Existing F1 force flush remains optional and is not a dependency. These are bounded diagnostic checks; no new game or visual acceptance is claimed.

Window completeness means 32 final CPU frame snapshots, not all draws or bindings and not GPU completion. Counters describe observation calls within the window; delivery counts are cumulative since the consent/device reset. Existing summary, source, binding and sparse streams use HTTP 200 acceptance, while compact delivery requires the validated compact receipt. The wire carries source version and protocol build information; an unknown `runtimeCommit` does not identify the exact executable.

The private [research archive repository](https://github.com/freefrank/LostOdysseyRecomp-build-inputs) is now enabled by a daily GitHub Action. Its first successful run is [Action 34493751731](https://github.com/freefrank/LostOdysseyRecomp-build-inputs/actions/runs/34493751731), with content-addressed deduplication and a data commit beginning `8da2b10e`. The first snapshot validated 3,401 diagnostics, 431 unique VS/PS programs, 909 GPU associations, 31 temporal records and 462 payloads. This archive is a private, long-term research copy; it has no D1-style automatic expiry.

### Updater validation

The v0.5.3 updater fixes ZIP staging for the explicit top-level directory entry emitted by Python `shutil.make_archive`; the entry's trailing slash is accepted while the archive must still contain exactly one package root. Standalone updater text and error dialogs are English, and native buttons are requested with `en-US`.

The focused archive runner passed 12/12 cases, including successful release-style staging, implicit and root-last directory handling, and rejection of multiple roots, top-level files, absolute or parent roots, traversal, duplicate and unlisted payloads, hash mismatch and missing `manifest.json`. At that earlier updater checkpoint, the updater-only `LoUpdaterTest` build succeeded while the game executable was not rebuilt; the retained embedded-updater evidence therefore belongs to that checkpoint. The later full game build includes the updated embedded updater source, but the updater transaction was not re-accepted. Render-only UI checks passed with English text and font assertions and no clipping; a live native dialog was not exercised. The v0.5.3 package is published, but no complete update transaction or player acceptance is claimed. Evidence: `out/updater-fix/REPORT.md`, `out/updater-fix/archive-test.log` and `out/updater-fix/ui/render-manifest.json`.

The retained D3D12 RTX 5080 background run used source-0.5.0 executable SHA256 `1beb8a50c5bef1ebf0fb147338b33d558a2193e87f82b0b8c579ecbcb856959d`, 1280×720 experimental TAA, 60 FPS target and Map 16 Main Street. Two periodic uploads added 23 then 32 programs, for 55 total (15 VS / 40 PS, 19,188 bytes). All received sources matched the local cache by bytes, SHA-256, renderer FNV and length; the final snapshot had 55 source rows and associations without duplicate keys, plus 28 matching structured diagnostics. Map 16 windows measured 59.67 FPS / p95 18.036 ms before the second upload and 59.84 FPS / p95 18.067 ms during it. This is bounded background acceptance, not F1 product export, opt-out A/B, universal zero-overhead proof, Vulkan/AMD coverage, flicker repair acceptance or whole-game validation. Evidence: runtime report (`out/v0.5.0/shader-source-collection/runtime/REPORT.md`, retained locally), D1 verification (`out/v0.5.0/shader-source-collection/runtime/D1-VERIFICATION.md`, retained locally) and shader-source report (`out/v0.5.0/shader-source-collection/REPORT.md`, retained locally).

Product F1 ZIP/manifest and immediate post-capture upload remain pending. The attempted `LO_CAPTURE_REQUEST` was a legacy trace request. The latest flicker diagnostic used older executable `ececed95`, before the four c7 paths; it is not a regression acceptance result. F1 frame costs were 1.690/3.156/1.561 seconds, and the later background ZIP attempt timed out at 325.117→385.166 seconds with the original directory retained. See flicker analysis (`out/v0.5.0/taa-flicker-20260910/analysis/REPORT.md`, retained locally).

The current Windows delivery scope is D3D12/Vulkan. DX11, Linux, macOS, experimental Switch work, other GPU coverage, DLC rewards/dungeons and full-game compatibility remain separate validation areas. Open work-item status and priorities are maintained in the [public Maintainer Project](https://github.com/users/freefrank/projects/3); completed change history is in the [CHANGELOG](../CHANGELOG.md).

<a id="live-issue-reconciliation"></a>

## Issue and acceptance boundary

GitHub Issue state, reporter acceptance and implementation evidence are separate facts. Use the [public Maintainer Project](https://github.com/users/freefrank/projects/3) for current work-item state and the [CHANGELOG](../CHANGELOG.md) for completed releases. Dated implementation and investigation evidence remains in `docs/notes/`.

Earlier Issue closure sources and their acceptance limits are preserved in the [archived reconciliation](archive/STATUS-2026-09-10.md#live-issue-reconciliation).

## Archived status snapshot

The former detailed status ledger, including historical candidate identities and dated validation narratives, is preserved in [STATUS-2026-09-10.md](archive/STATUS-2026-09-10.md). It is historical evidence and does not define the current source or release state.

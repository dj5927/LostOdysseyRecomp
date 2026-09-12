# City 60 FPS handoff — 2026-09-12

Diagnostic work, not player acceptance and not a new Release. Version stays **0.5.4**. Do not push unless asked.

Session: `ses_f6c1d11f0ffeuJGmPTH4jjhMvK`. Goal: city exploration 60 FPS via CPU efficiency only (no GPU/quality/resolution fallback). S4 gate: mean + `>16.67 ms` ratio + 1% low on the same Hidden city path.

## User requests (verbatim)

- 获取上一次的开发进度
- 想办法达到fps60。提示，现在fps低完全是因为cpu效率问题。不存在任何硬件性能限制或gpu不足的问题。你可以委派metis去做思考和研究。
- 尽你所能，自主完成目标，不要问我，我只需要结果
- 为什么又停了。继续
- 先commit
- ok,收尾，commit，总结，写一个给别的agent的handoff，今天就到这里了

## What landed on `main` (unpushed)

Ahead of `github/main` by 9 after the wrap-up commit below (was 8 before `frame_timing`).

Earlier GPU-ring (already on `main` before this session): `b91d279` 2-slot ring / descriptor 1800; `ed90fe9` dummy banks / skip constants; `54b8619` GpuPoll 50µs; DrawImpl timers; `e58fb88` city classifier; item 1 `daea69c`+`5025e8b` 128MB halves (regressed 1% low); item 3 locate-only timers.

This session:

| Commit | What |
|---|---|
| `1c6f237` | Vertex cache hits any slot on content match; wrap only when both 128MB halves are full; 1 GiB arena (`kVertexArenaSize = 1024ull << 20`); tests in `tools/tests/render_arena_policy_test.cpp` (35/35) |
| `bae54b6` | `PreparePresent` records COPY_SOURCE on the still-open swap list; `AcquireResolvedSurface` is lookup-only (no second Flush). Fixes city `gpu_batches` 2.005 → ~1.00 and fence 8.97 → ~0.005 |
| `frame_timing` (this wrap-up) | Snapshot under mutex, then skip the 1s `LOG_INFO` pair when `gpu::render_timing::Enabled()`; still logs outside the lock if only `LO_FRAME_TIMING` |

Do **not** commit: `AGENTS.md`, `ROADMAP.md`, `ROADMAP.zh-CN.md`, `docs/project/items.json` (pre-existing docs drift), `.omo/`, `.stignore`, `nul`, dirty submodules.

## City harness (reuse, do not invent)

- Script: `out/perf-ring/drive-city.ps1` (not git). Hidden, `LO_BACKGROUND=1`, `LO_AUDIO_MUTE=1`, `LO_RENDER_TIMING=1`, `LO_FRAME_TIMING=1`, `LO_GPU_STATS=1`, `LO_AUTO_BUTTONS=s@120,a@240,a@360,a@480,a@700,a@900`, `LO_AUTO_STICK=0,28000,1400,2800`. Isolated `user01`. 28s `city_hold`. city = draws ≥ 800. `original_saves_changed` vs `D:\Games\LostOdysseyRecomp-windows-x64\save`.
- Game: `D:\Games\LostOdysseyRecomp-windows-x64\game\disc1` (`out/perf-ring/run/game-path.txt`).
- Copy RelWithDebInfo exe+pdb from `out/build/windows-clang/LostOdysseyRecomp/` into `out/perf-ring/run/` after each link. ninja then fails copying missing `tools/XenosRecomp/thirdparty/dxc-bin/bin/x64/dxcompiler.dll` — ignore, `run/` already has the DLL.
- Build: git-bash must use `cmd.exe //c "tools\\build_runtime.bat LostOdysseyRecomp"` (`/c` becomes `C:\`).
- Classifier: `tools/perf/classify-city-timing.ps1` (budget 16.67, no fence mean / 1% low — compute from log rows).
- Heartbeat line: `heartbeat: swap #N X.X fps, D draws/frame, ... last file 'xenon_scr.fpd'`.
- Present line: `sample_valid=true|false frame_ms=N|unknown ...`.
- Script now: FileStream last-64KB tail; **no tail during `city_hold`**; shot marks 200–1000 + one city-entry shot (not 1800/2200). `last_swap` in `drive-status.json` stays stale after hold starts.

## Latest Hidden city numbers

Log `out/perf-ring/run/logs/runtime-1789233141663996.log` (frame_timing skip, 1 GiB arena, present-merge). 58.4s, `original_saves_changed=false`, city 1743 / menu 851.

| Metric | Value |
|---|---|
| fence_wait mean | 0.006 |
| gpu_batches | 1.001 |
| nested_flush max | 0 |
| splits / arena_splits | 0 |
| draw_ms | 10.264 / p99 13.144 / max 48.983 |
| draw >16.67 | 4/1743 = 0.23% (3 vertex, 1 bind) |
| heartbeat xenon_scr n=29 | min 42.1 mean 58.0 max 59.8 |
| stable present 1% low | 51.09 fps (max frame 427.9 ms) |
| all-city 1% low | 44.04 fps |

Typical stable: draw ~9.4–10.3 ms, present p50 ~16.70 (60 cap). `>16.67` on present is mostly pacer 16.67–17.3, not a mean miss. S4 fails on **1% low / hitches**.

Four draw over-budget frames: load-in bind/vertex (1279/1335/1466) plus mid-hold **vertex 40.83 ms** at frame 1983 (present 1984 = 54.53 ms).

428 ms stall at present 2548: `flush_ms=413.25`, draw of previous frame 9.59 ms. Swap thread blocked inside `PreparePresent`+`gpu::Flush` (per-frame `LOG_INFO`). Log lives on the Syncthing tree. Same class as earlier 205/263 ms stalls. **Measurement artifact until logs leave Syncthing.**

Ring baseline (docs `perf-gpu-ring-compare.md`, pre-item1): heartbeat 57.7 (49–60), fence 1.67, gpu_queue 3.91, gpu_batches 2.00. Present-merge beats that fence; 1% low is now hitch-limited.

## Next attacks (do these first)

1. **Move city logs off Syncthing** — in `drive-city.ps1` set `LO_LOG_FILE` to `%TEMP%\lo-city-logs\runtime.log` (override is `LostOdysseyRecomp/main.cpp` ~100–125). No rebuild. Remeasure. Expected: no 200–400 ms `flush_ms` spikes.
2. **SIMD `CopySwapped`** — `LostOdysseyRecomp/gpu/renderer.cpp` ~204 `GpuSwap`, ~216 `CopySwapped`: endian 0 is memcpy; else scalar per-dword. Mid-hold 40 ms `vertex_ms` is real `tVertex`/`CopySwapped`, not the 5 KB `SampledContent::Matches`. Prefer extracting `CopyDwordsSwapped` into `LostOdysseyRecomp/gpu/geometry_prepare.h` (already has templated `Convert` / `ConvertIndices`) + `tools/tests/geometry_prepare_test.cpp`. Endian 2: `_mm_shuffle_epi8`. Then RelWithDebInfo remasure.
3. Only then: remaining DrawImpl (bind/record/sets on p99) and pacer overshoot 0.2–0.5 ms. Do not raise `kGpuSlots` (shrinks vertex halves). Do not `WaitForGpu` on wrap. Do not revert item 1.

## Constraints

- CPU-only. No GPU upscale, resolution drop, quality drop, or hardware-upgrade story.
- Hidden `LO_BACKGROUND=1`. Never steal focus. Report `original_saves_changed`; non-zero invalidates the run.
- Version 0.5.4 until the user changes it. No commit/push unless asked (this wrap-up is the asked commit).
- Do not treat menu FPS or mean-only as S4. Item 3 timers are locate-only (~0.39 ms), not savings.
- `tools/benchmark_city.ps1` does not exist; use `out/perf-ring/drive-city.ps1`.

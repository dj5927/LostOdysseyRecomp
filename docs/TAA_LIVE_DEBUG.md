# Local TAA live debug

The local panel at `http://127.0.0.1:8769` provides bounded Vulkan TAA
diagnostics without uploading captures or game assets. Start it from the
repository root with:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\start_taa_debug.ps1
```

The launcher expects a prebuilt `LostOdysseyRecomp-debug.exe` in the selected
run directory; it does not build the game. The default run directory is
`D:\Mihoyo\LostOdysseyRecomp-windows-x64`; use `-RunDir` to select another
directory. It then starts the debug executable, Python 3 server and browser panel.
`-NoAutoContinue` skips the native Continue request. Native Continue selects
the latest save for the active profile; it is not an arbitrary slot loader.
Native Continue now has bounded runtime evidence: `build-native-profile.log`
records a successful build, `runtime-1789835047430819.log` records reading
`save/user01/save.bin` and native save load success, and
`out/bell-resume/native-bell.png` shows the Bell scene. An existing server on
port 8769 is reused. The current stationary-MV and color-clipping candidate was
built in `out/bell-resume/live-tuning/build-usage-color-runtime.log`; its SHA-256 is
`42BE7A2C3A7C2E973FA530504EBB160005BCBD31DEA1B2BF855FAF31EDCF7251`.
The run directory's `taa-debug-build.json` records its provenance and controls.

Controls are validated and written with a serial. The renderer normally applies
them on a later frame within about 250 ms. Confirm consumption through
`/api/state` and its `applied_serial`; an accepted POST response alone is not
runtime evidence. The panel supports one or 32 frame screenshots, bounded GPU
traces, ROI previews and pixel reads:

```text
GET  /api/state
GET  /api/files
GET  /api/preview?file=shot.ppm&x=...&y=...&w=...&h=...
GET  /api/pixel?file=shot.ppm&x=...&y=...
POST /api/controls
POST /api/screenshot   {"frames": 1..32}
POST /api/trace        {"frames": 1..8, "targets": ["source", "output", ...]}
POST /api/launch       {}
```

Trace targets are `source`, `output`, `depth`, `mv`, `motion_depths` and
`reactive`. Preset Bell ROIs use 3840x2160 coordinates: upper beam
`(1062,255,1326,188)` and video marker `(1166,299,98,90)`. Adjust for other
resolutions. `LO_TAA_ACCEPTANCE=2` colors rejection causes without changing
history: blue reactive, yellow current/replay depth mismatch, black
primary/support mismatch, magenta third layer, cyan invalid or boundary, red
accepted and green color rejected. Experimental stationary-MV snapping defaults
off. Parameters do not require a rebuild; shader algorithm or coverage changes
still require a new build and corresponding validation.

The current diagnostic controls also expose `stationary_color_clip`, which
defaults to `0`; the observation run used stationary snapping `1`,
`motion_min=.002` and color clipping `1`. Orange diagnostic pixels mean that a
color outlier was accepted after clipping. This path requires the strict
stationary and depth guards described in the investigation note.

Current bounded evidence includes 826 Vulkan GPU/translation checks in
`out/bell-resume/gpu-live-test.log`; live parser and trace checks are separate.
Runtime state at 3840x2160 shows
`history_reused=true`, `motion_ready=true` and `motion_consumed=true`, a live
`LO_TAA_ACCEPTANCE=2` request that was applied and then returned to mode 0, and
a same-frame source/depth/MV/reactive trace at frame 2390 with pixel results in
`out/bell-resume/live-trace-pixels.json`. A corrected same-frame trace at frame
1289 is recorded in `out/bell-resume/live-trace-corrected.json`: depth is
`0.0029113872442394495`, `motion_depths` is the float2 pair
`[0.0029113872442394495, 0.002911387011408806]`, with format ID 16 and
`R32G32_FLOAT`; output RGBA is `[73,61,46,192]`. The corrected trace confirms
the float2 protocol. `POST /api/launch {}` restarts an exited game; requests
while the game is running are rejected. These checks do not establish a visual
fix or complete player acceptance of the Bell shimmer. The user confirmed the
current exact-stationary/color-clip candidate is clearly steadier, while
residual shimmer remains. Same-session tuning results are
recorded in `out/bell-resume/live-tuning/color-off|color-on/result.json`; they
are image-stability deltas, not performance measurements.

## 中文说明

本地面板地址为 `http://127.0.0.1:8769`，用于限定的 Vulkan TAA 排查，不上传
截图或游戏资源。运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\start_taa_debug.ps1
```

`-NoAutoContinue` 跳过 native Continue；原生 Continue 按当前 profile 读取最新
存档，不是任意 slot 读取器。当前已有实测：成功读取 `save/user01/save.bin`，
完成 native save load，并进入 Bell 场景；这不代表 Bell 抖动已经修复。
参数请求带 serial，通常在约 250 ms 内后续帧应用；用 `/api/state` 的
`applied_serial` 确认游戏已消费。面板支持截图、GPU trace、ROI 预览和像素读取，
`LO_TAA_ACCEPTANCE=2` 的颜色含义见上文。`POST /api/launch {}` 可重启已退出的
游戏，运行中会拒绝该请求。

当前诊断还提供 `stationary_color_clip`，默认值为 `0`；本次观察使用了静止快照
`1`、`motion_min=.002` 和颜色裁剪 `1`。橙色诊断像素表示颜色越界经裁剪后接受。

参数调节无需编译；shader 算法或 coverage 改动仍需重新构建和验证。当前证据包括
需先将已构建的 `LostOdysseyRecomp-debug.exe` 复制到运行目录；脚本不会自动构建。
默认目录为 `D:\Mihoyo\LostOdysseyRecomp-windows-x64`，可用 `-RunDir` 修改。
当前证据包括 `out/bell-resume/gpu-live-test.log` 中 826 项 Vulkan GPU/translation
检查；live parser 和 trace 检查独立统计。live `applied_serial` 响应、
同帧 trace 像素结果和 3840x2160 Bell 截图均已有证据。修正后的同帧 trace 位于
`out/bell-resume/live-trace-corrected.json`，`motion_depths` 已按 `fmt16` 的
`R32G32_FLOAT` float2 正确读取；工具本身仍未完成用户验收，且不代表全游戏
覆盖或完整玩家画面验收。用户已确认当前 exact-stationary/color-clip 候选明显
更稳定，但仍有残余闪烁。
同 session 调参结果位于 `out/bell-resume/live-tuning/color-off|color-on/result.json`，
表示画面稳定性 delta，不是性能数据。

# RenderDoc GPU profiling

用于 GPU 高占用、draw/pass 耗时归因、shader 替换 ABBA，以及复制/屏障参数审计。CPU 与汇编采样继续使用 `tools/asm-profiler`，流程见 [lost-odyssey.md](lost-odyssey.md)。

## 工具与启动入口

可维护脚本位于本技能 `scripts/`，不依赖日期命名的诊断输出目录。RenderDoc 的二进制和 RDC 大文件仍保留在本地，不打包进技能。脚本来自本项目 2026-09-12 的 RenderDoc 1.46 实测流程；换 RenderDoc 版本时先核实 API。

使用 `qrenderdoc.exe --python` 的内置 Python；普通系统 Python 不能直接代替它加载配套 `renderdoc` 扩展。指定 RenderDoc 安装目录或 `RENDERDOC_HOME`：

```powershell
$runner = '.agents/skills/lo-profiling/scripts/Run-RenderDocJob.ps1'
& $runner -Job 'C:/audit/analyse.json' -RenderDocDirectory 'C:/Tools/RenderDoc' -ValidateOnly
& $runner -Job 'C:/audit/analyse.json' -RenderDocDirectory 'C:/Tools/RenderDoc'
```

`-ValidateOnly` 校验参数、输入路径和 worker 路由，不启动 RenderDoc、游戏或 GPU 重放。实际执行返回 helper PID；随后读任务输出文件，不能把 PID 存在或 helper 退出码当成任务成功。worker 遇错写 `error.json`，仍可能以零退出码退出 qrenderdoc。离线任务使用新的输出目录，避免旧结果被误认为新结果。

当前机器最初下载的 portable 目录是仓库内 `out/gpu-profile-20260912/tools/RenderDoc_1.46_64`；这是定位线索，调用前确认存在。原始工具来源记录在该次诊断的 `renderdoc-tool-manifest.json`。不要自动改系统 Vulkan layer 或覆盖已有 qrenderdoc 配置。

## 任务 JSON

所有文件与输出路径使用绝对路径。`mode` 决定 worker，启动器不根据自由填写的脚本路径执行任意 worker。

| mode | 额外字段 | 结果 |
|---|---|---|
| `probe` | 无 | `api-probe.json`，内置版本和 API 信息 |
| `launch` | `executable`, `workingDirectory`; 可选 `commandLine`, `environment` | `launch-result.json`，含 target `ident` |
| `status` | `ident` | `target-status.json`，API 是否 supported/presenting |
| `capture` | `ident` | `capture-request.json`, `capture-result.json` |
| `analyse` | `capture`; 可选 `cacheDirectory` | GPU 事件 JSON/CSV、分类/PS 汇总与 shader 映射 |
| `structure` | `capture`, `event_window: [first,last]`; 可选 `events`, `resources` | API 参数、附近 actions 和指定资源 usage；不采计时 |
| `ab` | `capture`, `replacement`, `originalSha256`, `shaderEventId`, `hotspotEventIds`, `imageEventId` | `ab-counters.json`、两份 PNG、含图像 SHA256 的 `complete.json` |

每种任务都必须提供 `output`。`ab` 还可选 `shaderStage`（`Pixel` 默认或 `Vertex`）、`entry`（默认 `main`）、`outputTargetIndex`（默认 0）和 `replacementSha256`。替换文件必须是对应 stage 的 SPIR-V；这不是 DXIL 替换入口。

普通分析示例：

```json
{"mode":"analyse","capture":"C:/audit/frame.rdc","output":"C:/audit/analysis-01","cacheDirectory":"C:/game/cache/shaders"}
```

ABBA 示例中事件号仅演示字段，必须从实际捕获选取，不能沿用旧场景 ID：

```json
{"mode":"ab","capture":"C:/audit/frame.rdc","output":"C:/audit/ab-01","replacement":"C:/audit/patched.spv","originalSha256":"填写原始shader的SHA256，64个十六进制字符","shaderEventId":100,"hotspotEventIds":[100,200],"imageEventId":300,"outputTargetIndex":0}
```

API 参数审计示例：

```json
{"mode":"structure","capture":"C:/audit/frame.rdc","output":"C:/audit/structure-01","event_window":[90,210],"events":[100,200],"resources":["ResourceId::123"]}
```

## 采集与测量要点

Vulkan 必须在图形 API 初始化前注入；先按任务授权安排诊断启动，再让用户到达场景。使用相同可执行文件时记录 SHA256；需要符号才构建匹配版本，不能因使用 profiler 就默认换 Debug build。`launch` 可用 `-ExpectedSha256` 校验 EXE。同一个 EXE 仍在运行时启动器拒绝再次启动。

portable Vulkan layer 若未被发现，可在该诊断启动任务的 `environment` 中指定 `VK_LAYER_PATH` 为 portable 目录、`VK_INSTANCE_LAYERS=VK_LAYER_RENDERDOC_Capture`、`ENABLE_VULKAN_RENDERDOC_CAPTURE=1`。以 `status` 的 Vulkan `supported` / `presenting` 及实际新 RDC 为准；返回 ident 不证明注入成功。启动器隐藏 helper，不保证启动的游戏窗口不会获取焦点；前台游戏交互仍需当前任务的用户安排。

`capture` 先读取已有捕获通知，再触发单帧，以旧 capture ID、路径和时间排除排队的历史通知，并核对新文件大小。以 `capture-result.json` 的路径/帧号选取 RDC，不能挑目录中任意文件。原始捕获和基线都保留。

计时前用完整 `Get-CimInstance Win32_Process` 枚举并核实目标和其他游戏/重放进程，避免它们争用 GPU；不要依靠曾漏报进程的 provider filter 判断游戏已经退出。无法排除干扰时只做结构分析，计时另标为探索性。`structure` 不采 counters，但打开 capture 仍可能初始化 GPU，不能宣称完全零 GPU 开销。

`analyse` 的 `EventGPUDuration` 从秒换算为毫秒；分类总和是已计时操作之和，不是游戏帧墙钟时间、FPS 或功耗。RenderDoc shader resource ID 是捕获内 ID。可选 shader 映射按实际 SPIR-V SHA256/长度匹配 `LOSHDR1` 144 字节缓存头，支持带版本/identity 后缀的文件名；它不重算所有磁盘缓存 payload，也不把多候选当成唯一 guest hash。

`ab` 固定使用原版→替换→替换→原版顺序，只替换指定事件绑定的一个 shader 资源，并检查原二进制 hash。`imageEventId` 应选最终目标已经完成写入的事件；最终 PNG hash 相同仅证明该捕获输出一致。编译器/flags/原始 shader 必须匹配；如果正式修复产物与已测探针字节相同，可复用此前受控 ABBA 与图像证据，不机械重复计时。

检查重复 copy 时，用 `structure` 比较完整源/目标、子资源、区域及中间命令；名称相同或没有 draw 不足以证明冗余。`EventUsage` 不依赖不存在的 `view` 字段。更深入的 descriptor/常量反射按当前捕获定制；旧 `out` 内 `focus_worker.py`、`constant_worker.py` 等保留为案例，不当作通用 worker 直接运行。

工具整理只进行了离线参数/脚本验证；此次没有再次启动游戏或重放 GPU。历史实测证据与边界见仓库 `docs/notes/gpu-shadow-loop-audit-2026-09-12.md`。

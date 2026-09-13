# 路线图

[English](ROADMAP.md) · [当前状态](STATUS.md) · [更新日志](../CHANGELOG.md) · [历史路线图快照](archive/ROADMAP.zh-CN-2026-09-10.md)

`[ ]` 待完成 · `[~]` 进行中 · `[x]` 在所述范围内已有证据。[公开维护者 Project](https://github.com/users/freefrank/projects/3) 是当前工作项的事实来源。本镜像只保留方向、未完成事项和验证边界；实现、玩家验收和发布状态彼此独立。

## 交付

[v0.5.6](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.5.6) 已于 2026-09-13（UTC）公开发布，为当前最新版本。Release CI、干净包来源、全部 50 个成员 hash／CRC 及四个匿名公开下载均通过。历史 v0.5.4 产物和测量保留其原身份；源码提交、hash 和验证边界见 [STATUS](STATUS.md)。

<a id="v050-pc-graphics"></a>
<a id="下一主版本v050--pc-vulkan-与-direct3d-11"></a>
## PC 图形方向

D3D12 仍是可用基线。Windows Vulkan 已有 RTX 5080 实机场景的界定验证，与更广 GPU 和全游戏覆盖分开。Direct3D 11、Linux/Steam Deck 和 Switch 是彼此独立的未来平台工作，未声明完成或发布日期。

<a id="近期优先事项"></a>
## 当前事项与验收边界

- [x] **v0.5.4 发布：**已于 2026-09-11T01:36:30Z 从 `2ad94d418bb0478417ab9589109f1f685ed92eb3` 公开发布；CI 34550200618 通过。44,237,061-byte ZIP 的 SHA-256 为 `104ced8b60c16cd1b9013543a3940c9ed8d7cf904c3d05a6a8ef8d591f51d218`；包来源、版本、全部 50 个文件 hash/CRC 及四个匿名资源下载均通过。本条只记录发布交付；各项运行时和玩家验收边界仍见下文。
- [~] **发布流程复用 PPC 库：**自动精确 key 同步已实现。历史 opt-in hook（`lo.ppcAutoSync=true`）仅把既有库上传至私有 commit `5e80263491b39dc0012146dd3a31cf5eea533225`，同 key 随后保持 unchanged；没有编译 PPC C++ 或运行游戏。当前 `lo.ppcAutoSync=false`；本地输入变化不会上传，除非另获授权。19 个 sync fixture、两项直接受影响的抽取 case、两份 workflow actionlint、sparse-clone/restore/hash 与 CI-contract 检查均通过。CI 按 key 解析而不再更新 workflow SHA pin；CI/imported/recursive 路径不可上传，auth/network 失败不视为 cache miss。实现已作为 [`2c0456c`](https://github.com/freefrank/LostOdysseyRecomp/commit/2c0456c) 推送到 github/main。托管 [PPC prebuilt tests](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/34565564964) 已通过；未包含在已发布的 v0.5.4 安装包中，也无玩家验收。此前手动路径的 [`2b5b1d1`](https://github.com/freefrank/LostOdysseyRecomp/commit/2b5b1d1) 与[合成 CI](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/34553414428)保留为历史证据。
- [x] **PPC 0.5.6 fingerprint 审核：**[Release CI 34726533463](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/34726533463) 已通过 v0.5.6 的 hosted PPC 消费与发布。runner key `d8919775…` 获取不可变私有 cache `a6cd91ea…`，恢复并校验库 `ba3e4c4d…`，没有生成 PPC 编译或新库链接。公开非预发布版本现为 latest；四个公开资产均通过匿名 HTTP 200、大小和 hash 核验。producer `50b8ad…`/`6a6ed031…` 和五项行尾／十四项 symlink 的表示差异证明保留为历史。`lo.ppcAutoSync=false`；源层 key 规范化尚未实现。
- [x] **安装器拖动闪退：**v0.5.4 已公开包含本地验收的 `PostMessageW` 修复，针对 v0.5.3 安装器拖动卡顿／退出。message-only HWND DragDispatch case 通过 1/1；旧 fixture 仍受未变的前台 setup 断言限制。用户于 2026-09-10 验收报告的拖动路径；更广安装器交互覆盖仍为独立事项。
- [~] **更新器简化：**用户声明的源码和发布目标为 `v0.5.6-hotfix1`，目前尚未发布。本地实现已移除“有更新时”对已安装 release manifest 和 EXE hash 的阻断，保留安全解压、回滚及退出后替换。standalone 与游戏启动路径会把空安装识别为版本 `0.0.0`；成功安装后 helper 先询问是否启动，默认 No，silent、失败及启动失败路径均不启动游戏。复用 source-0.5.6 的隐藏 synthetic 检查：standalone 44/44；`LoUpdaterTest` 的 version/asset/integrity/staging/rollback/helper/preservation；以及 Unicode cwd 下 `HelperContext` 的空安装且不擅自启动。为在获取旧包时保留同意机制，使用本地复制的 helper。行为证据未变，故未重新构建或重跑测试。尚未记录真实游戏、公开下载、可见提示交互、玩家验收或发布；验证仍待完成。
- [~] **可选 shader 收集：**v0.5.3 已发布紧凑的 opt-in 诊断和 schema 3 绑定证据；Worker schema 1–4 与私有归档、账本已集成。紧凑收集使用 32 帧／180 秒 CPU 窗口、最多 24 个配对和 8 个绑定，限制在 32 KiB 内。18 个账本案例仍待复核。schema 3 只覆盖 e810 的两对绑定；新候选仍需程序复核及足够的最终绑定、生产者时序和 jitter 证据。画面修复、玩家验收、更广 GPU 覆盖和全部提交绘制的覆盖审计仍待完成。
- [~] **Capture 导出超时：**两次 F1 ZIP 归档均在约 60 秒后超时。archive worker 现改为在把子进程视为超时前最多等待 180 秒（`60000` → `180000` ms）；既有进程、job object 和 `Optimal` 压缩行为保持不变。v0.5.3 EXE 已成功链接并核对 `build.json` 来源，安装替换后的 SHA256 为 `CF70EA663ED230334145E1135CA97A58DFE3A34C65ED7D43E9E428C41B53270B`；旧 EXE／metadata 已备份至 `out/f1-zip-180s/backup`。整体构建最终因源码依赖 DLL 缺失而在复制 DXC 时失败；未重编译，仅从安装目录复用 DLL 补齐 build 输出。尚无超时恢复测试、游戏运行或玩家验收记录；v0.5.4 安装包及公开下载核验已通过。
- [~] **4K TAA 与地面／阴影反馈：**17468–17470 capture 来自早于四路径 c7 修复的较早 EXE。它不重开后续已验收的四路径 Sol 修复，不定位新根因，也不建立更广画面覆盖。继续通过 Project 记录场景、硬件和报告者针对性的验证。
- [~] **PPC 生成源码一致性检查：**生成树 provenance 子范围已完成：`ppc_codegen generate` 产出 247 个 C++ 单元，含 843 个 `.u32`、零个 `.u64` selector，246 个指令流哈希保持不变。7 个 guard fixture 通过，覆盖旧输出、input/tool/context drift 和 return-0-without-output recovery；历史 3,258／109／843 表-44,523 证据已复用而未重跑。CMake 配置及编译前检查依赖已核验；更广 PPC 契约审计仍开放。Issue #14 调查明确区分原始历史 v0.5.0 日志（LR `829DFDCC`、CTR `829DFEF0`）和第二份历史 v0.4.2 日志（LR `823CB53C`、CTR `0`）。后者的 guest virtual-call 位置 `823CB538` 与 Issue #12 material-call 分析相似，但没有对象释放现场可证明同一根因。`gc_render_flush.cpp` 已存在于 `17e3ab7`；尚无新的运行时验证、当前版本复现、报告者验收或发布证据。
- [~] **新增游戏流程调查（#15、#16）：**#15 的 source-v0.5.4 日志在用户关闭进程前保持 60 fps 渲染和音频运行；同一存档之后可加载，第二次存档未挂起。其目录名 `0.5.0` 不是版本证据。两项 compiler DLL hash 与 v0.5.4 manifest 相符，其余组件仍未知。更新器调查发现 `StageArchive` 漏将 `manifest.json` 纳入 `ApplyPlan`，更新后会残留旧清单。其关联修复已验证：当前 worktree 的 clang-cl `/O2 /MT` 定向编译成功，`--manifest-transaction` 三场景零失败，覆盖 plan 往返、替换后 post-apply 回滚、manifest 替换后注入失败回滚和 manifest 篡改拒绝。原 updater-only 验证未运行 helper／游戏，也不构成玩家验收；实现现已包含在公开 v0.5.6 中。up-to-date 路径仍只对比版本而非每个已安装文件 hash，故混装 DLL／资源尚未排除，Issue #15 挂起根因仍未知。#16 实现和有界本地验证已完成，等待报告者验证。最终 branch-native-r1 EXE `f2015ad7…` 在 D3D12／亚洲 Disc 3 从 user09 通过 final-freeze-01，未跳过国王冻结。1527.792s，raw `0A33A8C0` 使用缺失 particle-shader fallback；稳定内存匹配 `xf_shd_aniflz.freeze`、GUID `0fd4ca6d4bb67581c9c9f5b8f0af62c0`、子表 count 16 和 particle shader 0。180 张 PNG 全部完成（shot45508 冻结、45823 对白、47673 外景）；1733.599s 回到 map229，shot53430-53517 显示 30 tick 向下输入后的角色移动和镜头变化。本次无坐标 telemetry。两个 native checkpoint 均可重读，原 seed 未变。报告者／玩家验收和 Vulkan／其他区域覆盖仍待完成。实现随后已提交、推送并包含在公开 v0.5.6 中；报告者验收保持独立。
- [~] **#14–#16 本地合并记录：**本地提交 `2019cd017ab939d0b728cec340f7835c2082e615` 记录本任务的 triage、updater-manifest 修复及 particle compatibility 修复／证据，已包含在从 `7124f4b…` 发布的 v0.5.6 中。[Release CI 34726533463](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/34726533463)、包来源／50 个成员 hash 及四个匿名公开下载检查均通过。各项调查、报告者验收与覆盖边界仍以上述内容为准。
- [~] **#16 main 0.5.6 回放：**正常 main Release 构建（`d50c240d…`，173.782 s）在 D3D12／本地亚洲 Disc 3 从 user09 通过完整目标回放，无诊断对象或 binary bridge。原生 F1 两次判胜；冻结未跳过，1759.793 s 在 raw `0a54ce40` 命中 fallback，剧情继续，map229／菜单／30 tick 移动通过，360 张截图完成。修复已包含在公开 v0.5.6；CI／包／公开下载只核验交付，未重复游戏。报告者／玩家验收、全游戏、Vulkan 和其他区域证据仍待完成。较早 branch-native-r1 v0.5.4 通过记录保持独立。
- [ ] **呈现与输入：**全屏、Alt+Enter、混合 DPI 和鼠标验收仍开放。判断用户报告的 underscan 时应保留正常宽高比黑边。
- [ ] **游戏流程与稳定性：**后续推进、存档读回、遇敌、长时间稳定性、其余渲染反馈和跨 GPU／正式包证据仍为待办。既有界定修复不代表可完整通关。
- [~] **Issue #6 启动地址预留：**Issue 当前为 OPEN。报告者在 2026-09-11 提供的三份 v0.5.4 日志均记录 `reserve any address` 及 Win32/preferred error 6；2026-09-12 又称干净解压后仍会失败。历史 v0.4.0 诊断增强仍已实现，但不构成修复、根因或原机器恢复证据。待提供的信息包括 Windows build、架构、兼容层、RAM/pagefile、ZIP 版本、旧版是否成功，以及最新完整的干净解压日志。
- [~] **汇编级性能分析：**外部 Win64 工具现已记录 wall-clock RIP 快照，解析 DbgHelp PDB 符号／源码位置，并生成 Capstone x64 HTML/JSON 热点、线程 CPU 时间／筛选和函数 self 样本排名。7 个定向报告用例、MSVC Release 构建和一次合成端到端采集均通过。2026-09-11 两次隔离实机采集使用已发布 v0.5.4 EXE `3c3b4073…`：user00 走图 5,092 样本／0 失败／最忙线程 8.11 s CPU，user01 城市走图 10,736 样本／0 失败／最忙线程 7.27 s CPU；均为 `xenon_scr.fpd`，约 36–44 fps、1,900–2,300 draws/frame、frontbuffer 1280×720。最忙线程为无符号 EXE 样本、`NtWaitForSingleObject` 与 AMD/D3D12 的混合。无匹配游戏 PDB、无 GPU 指令分析、无调用栈、无玩家验收。见[实机采集](notes/asm-profiler-gameplay.md)。其 PPC 注释是生成源码上下文，不是精确 guest PC；请求的采样间隔也不代表实际频率。
- [~] **性能与 shader 启动：**本地 `main` 提交 `ae287f2` 记录了已完成的有界 Hidden 城市子项：41.8241 ms vertex 卡顿来自 `std::unordered_map` 插入／rehash，而非 `CopySwapped`；新的 65,536 项预留 dense metadata cache 使用 16 候选 LRU 淘汰，保持 1280×720 D3D12 AA=3、画质、双槽和 arena/wait 策略；`LoVertexCacheTest` 一次通过 3,569,548 项检查。最终同 EXE 运行仅把 driver input／screenshot-request 文件移至 TEMP，观测到的 post-present 最大值从 412.9283→0.4193 ms，但未证明文件系统原因。1,790 个城市帧的平均为 59.651463 FPS；固定 1,201 帧窗口为 59.918376 FPS、1% low 54.494611、超 16.67 ms 为 50.374688%、最大 present 22.5913 ms、最大 vertex 2.2981 ms、零 rehash 且无 draw 超预算。平均≥58 的目标已达到；全城 1% low 45.989346 和 43.0117 ms 最大 present 保留城市入口 load/bind 尾部。这是接近 60 的路线证据，不是每帧锁 60、严格 S4／全游戏通过或玩家验收。实现已包含在公开 v0.5.6 中，托管 CI、包及公开下载核验均通过；上述性能测量保留历史二进制身份；原存档未变，本地 EXE SHA-256 为 `9D9460248FEB72AC7239ABD40AC1DA6619847F176CF4AA38AD6F725C6923852B`。见[vertex-stage 报告](../out/perf-ring/vertex-stage/REPORT.md)。

<a id="当前反馈与回归"></a>
## 保留的有效验证范围

用户于 2026-09-08 接受了有限的 D3D12/Vulkan 实机场景边界。四条 c7 路径在一个 Ghost Town 场景中约 11.37 秒内获得验收；这不是连续视频、跨场景、跨 GPU 或全游戏覆盖。此前已发布修复及其证据继续见[STATUS.md](STATUS.md)、[CHANGELOG.md](../CHANGELOG.md)和历史快照。

## 导航

以[Project](https://github.com/users/freefrank/projects/3)查看工作项状态、依赖和详细未完成需求；以[STATUS.md](STATUS.md)查看当前验证与发布证据；以[CHANGELOG.md](../CHANGELOG.md)查看已发布改动；以[历史路线图](archive/ROADMAP.zh-CN-2026-09-10.md)查看保留的详细历史。

<!-- 保留兼容锚点，供既有 Project 和 notes 链接使用。 -->
<a id="已发布里程碑v042--修复与验证"></a>
<a id="阶段-1产出可编译代码"></a>
<a id="阶段-2进入主菜单"></a>
<a id="阶段-3推进完整通关"></a>
<a id="阶段-4现代化"></a>
<a id="阶段-5可选探索"></a>

# TAA 铃铛架闪烁排查（Issue #46 相关）

状态：排查中，未做任何着色器映射或补偿改动。
台账：18 个 `taa-position` case 全部保持 `needs_review` /
`not_implemented` / `not_validated` / `not_accepted`，本文不把历史结论绑定到新证据。

## 代码改动（仅 opt-in 诊断，默认行为不变）

文件：`LostOdysseyRecomp/gpu/renderer.cpp`。

- `LO_TEMPORAL_LOG_ALL=1`：记录每一帧的 `renderer temporal f...` 摘要和
  gates/相机/探针明细，不需要填起始帧。之前只记
  `LO_TEMPORAL_LOG_START_FRAME` 之后的前 256 帧，后期帧（f2349、f31390 等）
  完全看不见。
- `LO_TEMPORAL_DRAW_LOG_ALL=1`：按逐 draw 记录抖动，不需要起始帧，既有的
  VS/几何过滤仍然生效。
- `LO_TEMPORAL_DRAW_LOG_UNKNOWN=1`：额外记录落在场景视口内但没有位置矩阵槽
  （`temporalSlot<0`）的 draw，一次跑全就能收齐未白名单 draw，不用逐个猜 hash。

## 已有证据

- `render-17897018090240431-f9876`（3 帧，AA=3）：相机稳定
  （`candidate_ready=true`，VP 相同，深度分配 14），但
  `temporal_history_verified=false`。同包 `shader.jsonl` 只覆盖 f0-f255，
  目标帧没有抖动计数。
- `render-17897637349644857-f2349`（铃铛场景）：UI 稳定，只有铃铛架闪，
  且只在 TAA 下出现。静态对照：45 个 VS 里 34 个在白名单，11 个不在。
- `render-17897643027668846-f31390`（AA=3）对
  `render-17897644282377816-f36731`（AA=0）：同一相机（VP 完全相同，988
  draw）。铃铛区域在两包里都动，TAA 是把本来就存在的位移放大了。
- `render-17897654353054983-f2111`（开 `LO_TEMPORAL_LOG_ALL` 后）：
  f2111/f2112 为 `reused=false gap=true`，抖动 0/0/0，gates 是
  `invalid_history|previous_incomplete|epoch_changed`。gap 的根因是抓帧
  readback 必然 stall 超过 250 毫秒阈值，每帧都重置历史并递增 epoch。
  因此抓包看不到稳态 TAA。
- 真实游玩日志（3491 个 temporal 帧）：2381 帧 `reused=true gap=false`，
  约 1800-1856 个 draw 被施加抖动，misses 全 0，unknowns 稳定 1-2 个。
  gates 干净（`mask=0x0`）。全局历史混合工作正常，问题是局部的。
- 未知 draw 排查：4 个未知（`6318`、`81217dc9`、`69349140`、`8bbd`）加上
  之前怀疑的（`760a`、`f95a`）全是 3-12 个 index 的微型 draw，都不是铃铛
  几何。铃铛的深度 draw 是 `b030`（前 25 个 draw，经 draw-step 预览和
  live draw 日志确认）。
- Bloom 隔离：TAA 开 + `LO_DISABLE_BLOOM_PREFILTER=1` 后闪得更厉害，说明
  只在 TAA 下走的 bloom prefilter 原本在 smoothing，闪的源头在它上游
  （被抖动的场景）。

## 结论（不修代码）

- 混合 shader（`temporal_aa.cpp`）对全部深度 tap 做校验，历史颜色钳制在
  当前 3x3 邻域内（TAA 模式下生效），cubic 负瓣也有约束，混合本身没有缺陷。
- 铃铛场景 `f1848` 全量表：深度 `702c`/`b030`、材质 `4053`（slot 7，约 300
  draw，7 组 PS）、阴影 `99c2`/`d55` 已补偿，全部施加，misses 为 0，
  映射和补偿完整。
- 只关抖动（TAA 开，`1 3 0 -1 -1`）后不闪，但超采样收益也没了，符合预期，
  证明驱动是抖动本身，不是坏掉的混合。
- 历史权重压到 0.1 也没用：闪不是 ghosting 比例问题，这条路已排除。
- 稳态格点模式（跳过 display 重建、抖动照样开）还是闪：display 重建排除。
- 剩下的机制是细摆动物体在 Halton 相位下的当前帧混叠，没有 motion vector
  无解。`LO_TEMPORAL_HISTORY_WEIGHT` 只保留为 opt-in 诊断，默认 0.85 不动，
  AA 档位就是现成的用户侧开关。

## HDR 说明（Issue #46 原文）

交换链固定为 `R8G8B8A8_UNORM`（`video.cpp`），游戏不输出 HDR 信号，也不切换
DXGI 色彩空间。bloom 日志里的 `HDR_area linear` 是 tonemap 前的内部线性阶段。
关显示器 HDR 就恢复，更指向系统 SDR-to-HDR 映射，与上面 TAA 排查分开处理。

# TAA Bell-Stand Flicker Investigation (Issue #46 adjacent)

Status: investigation open. The exact-stationary MV plus stationary color-clip
candidate received user confirmation of a clear stability improvement, but
residual shimmer remains and the Bell Vulkan 4K issue is not resolved.
Ledger: all 18 `taa-position` cases remain `needs_review` / `not_implemented` /
`not_validated` / `not_accepted`. This note binds no historical review to new evidence.

## Previous diagnostic controls (opt-in; default behavior unchanged)

File: `LostOdysseyRecomp/gpu/renderer.cpp`.

- `LO_TEMPORAL_LOG_ALL=1`: per-frame `renderer temporal f...` summary plus
  gates/camera/probe details for every frame, no start frame needed. Previously
  only the first 256 frames after `LO_TEMPORAL_LOG_START_FRAME` were logged,
  so late-game frames (f2349, f31390, ...) were invisible.
- `LO_TEMPORAL_DRAW_LOG_ALL=1`: per-draw jitter records without a start frame.
  Existing VS/geometry filters still apply.
- `LO_TEMPORAL_DRAW_LOG_UNKNOWN=1`: additionally records draws that sit in the
  scene viewport but have no position-matrix slot (`temporalSlot<0`). This
  catches every unwhitelisted scene draw in one run instead of guessing hashes.

## Uncommitted candidate repair

The current working tree allows the cubic footprint to include the predicted
primary depth surface plus one background depth surface, and adds a guarded
`stabilizeStationaryGeometry` path. The latter applies
only when the geometry is stable and the motion vector is trusted; for nearly
stationary geometry (`0.002` to `0.125` pixel motion), it smoothly returns the
history weight from `31/33` toward `0.85`. Existing rejection and clamp rules
remain active. Set `LO_TAA_STATIONARY_HISTORY=0` to disable this path for a
comparison.

`LO_TAA_ACCEPTANCE=1` enables an independent diagnostic resolve from the
candidate history to display. It does not color or alter the normal history
path. The candidate is present in the uncommitted working tree at the time of
this note; no release or user acceptance is implied.

The second candidate adds general static-coverage handling. Its GPU project
passed 811 checks, and the runtime build succeeded. A two-surface ownership
swap fixture using the same `31/33` weight changed peak-to-peak variation from
`128` to `10` and the mean from `132` to `135.875`; these use CPU-uploaded
deterministic inputs and a production GPU consumer, rather than complete
geometry-production validation. Eight safety-rejection classes also passed.

## Evidence so far

- `render-17897018090240431-f9876` (3 frames, AA=3): camera stable
  (`candidate_ready=true`, identical VP, depth alloc 14) but
  `temporal_history_verified=false`. Its `shader.jsonl` only covers f0-f255, so
  no jitter counts exist for the captured frames.
- `render-17897637349644857-f2349` (bell scene): UI stable, only the bell stand
  shimmers, TAA-only. Static check: 45 VS present, 34 whitelisted, 11 not.
- `render-17897643027668846-f31390` (AA=3) vs
  `render-17897644282377816-f36731` (AA=0): same camera (identical VP, 988
  draws). The bell region moves in both; TAA amplifies pre-existing motion.
- `render-17897654353054983-f2111` with `LO_TEMPORAL_LOG_ALL`: f2111/f2112 show
  `reused=false gap=true`, jitter 0/0/0, gates
  `invalid_history|previous_incomplete|epoch_changed`. Root cause of the gap:
  capture readbacks stall past the 250 ms threshold, which resets history and
  bumps the epoch every capture frame. Captures therefore cannot observe
  steady-state TAA.
- Live play log (3491 temporal frames): 2381 frames with `reused=true
  gap=false`, ~1800-1856 jittered draws, 0 misses, 1-2 unknowns. Gates clean
  (`mask=0x0`). Global history blending works; the defect is local.
- Unknown-draw hunt: all four unknowns (`6318`, `81217dc9`, `69349140`,
  `8bbd`) plus earlier suspects (`760a`, `f95a`) are micro-draws (3-12
  indices), none is bell geometry. Bell depth draws are `b030` (first 25
  draws, confirmed via draw-step previews + live draw log).
- Bloom isolation: TAA on + `LO_DISABLE_BLOOM_PREFILTER=1` made flicker worse,
  so the TAA-only bloom prefilter was mitigating, not causing, the shimmer.
  The flicker source is upstream (jittered scene).

### Candidate automated evidence

The GPU test project built once and passed 791 checks. In its 32-phase
stationary-geometry cycle, peak-to-peak variation fell from `10` to `4` and
the mean moved from `127` to `126`; motion recovery, depth rejection, alpha,
and raw/camera-only checks also passed. In the same-instance normal/no-jitter/
no-history-16-frame comparison, the four Bell ROI deltas for normal mode were
`0.93/2.53/1.49/1.19`, while the no-jitter comparison was approximately zero.
The acceptance diagnostic accepted `97.6%`–`99.5%` of frames continuously and
had a color rejection rate below `0.13%`.

These are automated and diagnostic results from `out/bell-resume/gpu-test.log`.

### Exact-stationary and color-clip candidate evidence

The current MV source candidate uses `exactStationary`: identical geometry and
raster inputs plus the actual vertex-shader constant reads must match bit for
bit. Canonical generated-HLSL literals are parsed strictly; relative or
unknown reads fall back to the full bank. Explicitly unused shared values and
pixel-shader flags are excluded, and zero is not treated as a speed threshold.
Fourteen CPU exactness checks and fifteen usage checks passed. A bounded GPU
literal-zero suite passed 543 checks across 32 jitter phases, rigid/skinned
paths and invalid-input protection. These are additional candidate checks; the
older 826-check suite was not rerun.

The live `stationary_color_clip` control defaults to `0`. With stable geometry,
strict zero original motion, valid depth and the other guards satisfied, it
changes an out-of-range color hard rejection into clamp-and-blend; orange
diagnostic pixels identify color accepted after clipping. The color group
passed 25 GPU checks and the new parser field tests passed. The current live
run used `snap=1`, `motion_min=.002` and `colorclip=1`; it remains diagnostic.
In `out/bell-resume/live-tuning/color-off|color-on/result.json`, same-session
32-frame values changed upper `0.60561→0.50872` (16%), video
`0.61189→0.52842` (13.6%) and ground `0.47061→0.31962` (32%); these are not
performance measurements. The exact-source-MV frame 877 evidence records
video and ground at 100% zero and upper at 99.412% zero. The 0.95 stationary
weight comparison was independently tested, gave only about 4% video benefit,
and was restored to `31/33`.

The user has now visually reviewed the exact-stationary MV plus stationary
color-clip candidate and reports that it is clearly steadier, while residual
shimmer remains. This confirms partial visual improvement for the observed Bell
scene but does not establish a complete fix or close the investigation.

### Preliminary real Bell candidate result

The current 32-phase real-scene comparison shows partial improvement only:
the red ROI delta changes from `0.9096` to `0.5652`, cyan from `2.5112` to
`2.1208`, white from `1.4418` to `1.0725`, and green from `1.1751` to
`0.9403`, corresponding to roughly 16%–38% improvement across the reported
regions. The remaining heatmap is concentrated on the outline and is strongly
associated with pixels that experienced history rejection; approximately 17%
of the region contributes 63% of the residual. The Windows runtime candidate
is still being refined, including a stable-grid cross-phase depth-surface
swap investigation.

This preliminary result does not establish a perceptual fix, whole-game
coverage, cross-hardware coverage, release readiness, or user acceptance.

For the second candidate's `LO_TAA_ACCEPTANCE=2` diagnostic, rejection causes
are color-coded without modifying history: blue reactive, yellow current/replay
depth mismatch, black primary/support mismatch, magenta third layer, cyan
invalid or boundary, red accepted, and green color rejected. The current
32-phase visual comparison improved the video ROI from baseline `1.4946` to
`1.13965`, and the upper-beam ROI from `1.30728` to `0.82713`; residuals
remain. The second candidate was also rejected in foreground review: the user
still saw obvious shimmer. Candidates `607dd6f1…` and `3b942ee…` both failed
visual acceptance.

### User visual acceptance result

The user reviewed candidate `607dd6f1…` in the Bell scene and reported that
the shimmer remained obvious and had not improved. The primary visible defect
was the upper crossbar/support metal edge rather than the earlier lower Bell
ROI; a distant ground seam also showed slight shimmer. The matched 4K ROI was
`(x=1062, y=255, w=1326, h=188)` and the video ROI was
`(x=1166, y=299, w=98, h=90)` with match `0.966`. In that video ROI, delta
changed from `1.4946` to `1.2826` (about 14%); jitter-off remained `0`.
Acceptance was `95.45%` with `0.73%` color rejection. This candidate failed
user visual acceptance. The second candidate includes the general
static-coverage fallback, but both candidates still failed visual acceptance.
The local live-debug panel and launcher are available for bounded diagnosis.
Native Continue now has runtime evidence: it loaded `save/user01/save.bin`,
entered the Bell scene, and reported `history_reused=true`, `motion_ready=true`
and `motion_consumed=true` at 3840x2160. The tool itself has not received
separate user acceptance, and the underlying shimmer remains unaccepted.

## Prior verdict before motion-vector and coverage candidates

- The resolve shader (`temporal_aa.cpp`) validates all depth taps, clamps
  history into the current 3x3 neighborhood (active in TAA mode), and
  constrains cubic negative lobes. No defect found in the blend itself.
- Bell scene `f1848` full table: depths `702c`/`b030`, material `4053` slot 7
  (~300 draws, 7 PS pairs), shadow `99c2`/`d55` compensated, all applied,
  0 misses. Mapping and compensation are complete.
- Jitter-off (TAA on, `1 3 0 -1 -1`) stops the flicker but also removes the
  supersampling benefit: expected, and it proves jitter is the driver, not a
  broken blend.
- History weight down to 0.1 changes nothing: the flicker is not ghosting
  proportion. Ruled out as a lever.
- Stable-grid mode (skips display reconstruction, jitter still on) still
  flickers: display reconstruction ruled out.
- The historical pre-motion-vector verdict was that current-frame aliasing on
  thin swaying geometry sampled at changing Halton phases was unfixable without
  motion vectors. Motion-vector replay and consumption now exist, but the two
  current candidates still fail visual acceptance; this historical sentence is
  not a current diagnosis or acceptance claim.
  `LO_TEMPORAL_HISTORY_WEIGHT` stays as an opt-in diagnostic only; default
  0.85 unchanged. AA mode choice remains the user lever.

## HDR note (original Issue #46)

The swap chain is fixed `R8G8B8A8_UNORM` (`video.cpp`); the game emits no HDR
signal and switches no DXGI color space. `HDR_area linear` in bloom logs is an
internal pre-tonemap stage. Washed-out flicker that disappears when monitor HDR
is off points at OS SDR-to-HDR mapping, not a proven game HDR bug. Kept
separate from the TAA work above.

# TAA Bell-Stand Flicker Investigation (Issue #46 adjacent)

Status: investigation open. No shader mapping or compensation change made.
Ledger: all 18 `taa-position` cases remain `needs_review` / `not_implemented` /
`not_validated` / `not_accepted`. This note binds no historical review to new evidence.

## Code changes (opt-in diagnostics only, default behavior unchanged)

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

## Verdict (no code repair)

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
- Remaining mechanism is current-frame aliasing on thin swaying geometry
  sampled at changing Halton phases. Unfixable without motion vectors.
  `LO_TEMPORAL_HISTORY_WEIGHT` stays as an opt-in diagnostic only; default
  0.85 unchanged. AA mode choice remains the user lever.

## HDR note (original Issue #46)

The swap chain is fixed `R8G8B8A8_UNORM` (`video.cpp`); the game emits no HDR
signal and switches no DXGI color space. `HDR_area linear` in bloom logs is an
internal pre-tonemap stage. Washed-out flicker that disappears when monitor HDR
is off points at OS SDR-to-HDR mapping, not a proven game HDR bug. Kept
separate from the TAA work above.

# Portable Vulkan shader pack — review candidate

Base: `menu@257f3866e9f9f5d3e65550c86dce453290cf7ee4`.
This change follows reblue's build/distribution separation; it does not copy its
renderer or change Lost Odyssey's shader translation semantics.

## Implemented

- A separate, read-only `.lospv` distribution artifact for successful guest SPIR-V
  and all fields of `TranslatedShader` needed for rendering. HLSL, translation
  diagnostics and negative compiler records are not shipped.
- Exact SPIR-V binaries are deduplicated by SHA-256, while every `(stage, guest
  hash)` keeps its own metadata. There is no semantic/approximate shader merging.
- Zstandard compression in independent roughly 1 MiB blocks. A single unusually
  large shader can occupy a block up to 16 MiB. The writer streams blocks; the
  reader opens a small index and retains only its most recently decoded block.
  Neither needs to retain the whole decompressed binary corpus.
- Runtime module creation is lazy. Already-created shaders still take the normal
  single-map-lookup hot path. A valid portable pack bypasses startup guest shader
  discovery/translation/DXC. Missing shaders still use the existing local path.
- Before existing PSO preparation takes pointers into the shader maps, all shaders
  needed by those PSO recipes are loaded first. No map insertions during that
  pointer-collection phase. Existing PSO worker scheduling is otherwise unchanged.
- Local success caches, negative caches and schema-2 startup bundles keep their
  current compiler/path identity rules. No cache-version rollback or global
  weakening of `cache::IdentityKey` / `RuntimeIdentity`.
- Portable compatibility includes schema, translator version, options, variant,
  HLSL prelude digest, discovery identity, fixed loaded-XEX range digest and an
  explicit layout revision. No install path or local DXC DLL/SO hash. Producer
  DXC identity is provenance only. The runtime computes the expected contract;
  it never uses the pack's self-reported contract as its expected value.
- Header/index checksums, exact file completion, count/size/range checks, no
  overlapping/gapped blobs, per-block checksum, bounded Zstd frames and SPIR-V
  framing/entrypoint checks. A corrupt lazy record disables the pack and leaves
  the local fallback retryable, without inserting an invalid shader entry.
- Windows ZIP and Linux AppImage staging copy only `shaders/portable_vk.lospv`,
  validate it with the native tool, and retain the Zstandard license. They do not
  scoop up `cache/shaders`, debug output, HLSL, both backends, or old cache versions.

## Applying

The delivery archive contains an application script plus source payload. It does
not contain game shaders, a game executable, game data, or a real exported pack.
Use Python 3.10+:

```sh
python apply_portable_shader_pack.py --repo /path/to/LostOdysseyRecomp --check
python apply_portable_shader_pack.py --repo /path/to/LostOdysseyRecomp --apply
```

The script checks Git blob identities for the reviewed files (normalizing CRLF),
checks every replacement anchor, and makes backups in `.lo-portable-backup`.
It never resets, commits or pushes the working tree. A modified/different base
is rejected by default. After inspecting a local diff, `--allow-compatible-edits`
can be used to retain unrelated edits while still requiring every exact anchor.
`--diff candidate.patch` creates a normal patch against the actual local checkout.
Keep the backup directory out of commits and release payloads.

## Build and CPU tests

The pack library needs static Zstandard. CMake uses an installed static zstd CMake
package, or fetches upstream v1.5.7 pinned to commit
`f8745da6ff1ad1e7bab384bd1f9d742439278e99`. For offline builds, install a static
package or set `FETCHCONTENT_SOURCE_DIR_LO_PACK_ZSTD` to the pinned source tree.
`LO_PACK_FETCH_ZSTD=OFF` makes a missing package a configuration error.

No game assets are required for these targets:

```sh
cmake -S tools/shader_pack -B out/portable-pack -DCMAKE_BUILD_TYPE=Release
cmake --build out/portable-pack --config Release --target LoShaderPackTool LoPortableShaderPackTest LoPortableShaderPackIntegrationTest
ctest --test-dir out/portable-pack -C Release --output-on-failure
```

The original `tools/tests/shader_prebuild_regression.py` remains a fake-DXC/GPU
orchestration test. Its fixture is updated to include the real new integration
methods/library, plus the settings/skip API that menu had already introduced.
It now also needs zstd development headers/library (`-lzstd`). Existing checks
are not deleted or rewritten to assert away a regression.

## Export without recompiling already-valid shaders

Rebuild the runtime with the patch, keep the same game root / local cache / DXC
pair, select Vulkan, and enable normal shader preparation. In PowerShell:

```powershell
$env:LO_SHADER_EXPORT_PACK = "$PWD\shaders\portable_vk.lospv"
# Launch the rebuilt runtime with the normal game arguments.
```

An existing *compatible, complete* startup bundle is validated and streamed into
this exporter. That path does not translate or invoke DXC for its guest shader
records. HLSL is reconstructed one record at a time only to count discarded text,
then discarded again. If the local bundle is missing or incompatible, normal
preparation runs once and the exporter consumes its successes instead.

Export is explicit, and `.lospv` is required as its suffix to avoid overwriting a
local `.bundle`. Deterministic failures are counted but omitted. Transient
compiler/infrastructure failures, cancellation, incomplete discovery and device
module failures discard the temporary export. The destination is atomically
replaced only after completion. No partially completed export is published.

Remove `LO_SHADER_EXPORT_PACK` after export, otherwise every subsequent launch
continues to run the developer export path:

```powershell
Remove-Item Env:LO_SHADER_EXPORT_PACK
```

An export log ends with `portable shader pack published` and exact byte counts.
The file is deliberately not called `startup_vk12_v1.bundle`; old clients cannot
read it. Ship it only with this patched client.

## Measure, verify and stage

```sh
LoShaderPackTool inspect shaders/portable_vk.lospv
LoShaderPackTool verify shaders/portable_vk.lospv
```

`inspect` reads structure/index. `verify` also visits all payload blocks and checks
SPIR-V framing. JSON reports raw bytes, deduplicated bytes, compressed bytes,
index bytes, omitted source/diagnostic bytes and final file size. Omitted HLSL
is the sum of reconstructed input strings; the old bundle shares its prelude,
so this is NOT a measurement of bytes saved from that old file. Use `file_bytes`
for the actual distribution size. The tool explicitly
reports `runtime_compatibility_verified: false`: real executable/XEX matching is
checked by the runtime, not inferred from an artifact's own header.

To stage automatically during normal builds, add this CMake cache option to the
existing build configuration:

```text
-DLO_PORTABLE_SHADER_PACK=/absolute/path/to/portable_vk.lospv
```

It copies the file to `<executable-directory>/shaders/portable_vk.lospv` and builds
`LoShaderPackTool` for packaging verification. Linux install uses `bin/shaders`.
The modified ZIP/AppImage packagers copy and validate this optional file. After
manual export/copy without that CMake option, build `LoShaderPackTool` in the
runtime build tree before running the package script.

For a standalone downloaded pack, put it under `shaders` beside the final game
executable, or set `LO_SHADER_PACK_PATH` to an absolute file path. Lookup is based
on the executable directory, not the process working directory. No unpacking to
hundreds of megabytes in the user's writable cache is necessary.

`LO_NO_PORTABLE_SHADER_PACK=1` is the explicit pack bypass. Full scan, HLSL dump,
failure retry and export also bypass it. Ordinary `skip_shader_prebuild` and
`LO_NO_SHADER_PREPARE` suppress bulk prebuild, but still allow this index-only
pack path and on-demand cache use. The pack contains no negative records.

## Size claim and acceptance limits

No complete real startup bundle or SPIR-V corpus was provided in this session.
Therefore **no real final pack size or game-data compression ratio is measured**.
800–900 MB may be a plausible raw corpus size; it is not an established download
size. The synthetic compression test is a functional test, not a predictor of
Lost Odyssey's compression ratio.

The implementation has native CPU checks for exact byte/metadata roundtrips,
negative-record omission, exact-binary deduplication, lazy reads, relocation,
contract invalidation, malformed/corrupt payloads, interrupted publication and
concurrent reads. The exact production `.inl` is separately compiled against
explicit fake GPU/DXC services. See the delivery validation report for commands
and outcomes.

Not established here: full runtime linking with the private generated game
inputs; actual DXC execution; actual driver module/pipeline creation; Windows,
Steam Deck or ARM execution; full-game shader coverage; FPS, load-time or peak
process-memory improvement. Core parsing checks are not full SPIR-V semantic
validation. Checksums detect corruption, not authorship: use trusted release
provenance/checksums and validate real shaders/drivers before publishing.

Not changed in this first implementation: shader translation/optimization,
specialization-constant conversion, PSO threading policy, builtin host shader
compilation, or removal of DXC from the application. Unlike reblue, this initial
codec uses block Zstd without smol-v. Exporting from a valid local bundle is the
lowest-risk release-generation path; an autonomous all-assets CI shader compiler
is not added here.

Before release, verify one exported pack on Windows Vulkan and Linux/Deck under
different paths, check that covered shaders do not invoke guest DXC, test an
uncovered shader's fallback, and compare real scene images and frame-time traces.

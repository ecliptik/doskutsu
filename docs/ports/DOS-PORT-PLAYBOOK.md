# DOS Port Playbook

Reusable lessons distilled from `doskutsu` (Cave Story / NXEngine-evo on MS-DOS 6.22 via DJGPP + CWSDPMI + statically-linked SDL3 with the DOS backend) for porting other engines to the same target. Engine-agnostic; cite this when starting AGS, engge2, ScummVM, or any other C/C++ engine port to the same hardware tier.

This document is MIT-licensed prose. It carries no upstream code; it summarizes decisions and gotchas in a form a future port author can apply without re-deriving them.

For the doskutsu-specific reasoning behind each lesson, see the citations into `PLAN.md` and `CLAUDE.md` at the end of each section.

---

## What you need before starting

- **Linux (or WSL) host** with `build-essential`, `cmake >= 3.16`, `git`. macOS works but introduces toolchain quirks not covered here.
- **DJGPP cross-compiler.** Installed via Andrew Wu's `build-djgpp` scripts. doskutsu's hub layout puts it at `~/emulators/tools/djgpp/`, with `~/emulators/scripts/update-djgpp.sh` as the install driver. See `~/emulators/docs/DJGPP.md` for the canonical install procedure. GCC 12.2.0 is the doskutsu pin; GCC 10.x has shipped successfully for sibling projects.
- **CWSDPMI.** Charles W Sandmann's DPMI host. Vendored per-project (it ships alongside the binary), not installed system-wide. doskutsu's copy lives at `vendor/cwsdpmi/cwsdpmi.exe` plus the `cwsdpmi.doc` redistribution-terms file.
- **DOSBox-X.** `sudo apt install dosbox-x`. Used for pre-hardware iteration; not built from source.
- **`scrot`, `xdotool`** for visible DOSBox-X automation (screenshots, key injection).

The `~/emulators/` hub is the doskutsu pattern for sharing the DJGPP toolchain across projects: each project symlinks `tools/djgpp -> ~/emulators/tools/djgpp`, and the project Makefile prepends both `bin/` and `i586-pc-msdosdjgpp/bin/` onto `PATH` so a clean `make` works from any shell. The second bin dir matters — that's where `stubedit` and `stubify` live. Reference: `Makefile:25-30` in doskutsu.

---

## What you already have buildable

doskutsu's `build/sysroot/` is a working static-link target for SDL3 (with DOS backend), SDL3_mixer (WAV + OGG-via-stb_vorbis), and SDL3_image (PNG-via-stb_image). Per-stage Makefile targets:

| Target | Produces | Key constraints |
|---|---|---|
| `make sdl3` | `build/sysroot/lib/libSDL3.a` | DOS backend from libsdl-org/SDL PR #15377; `-DSDL_SHARED=OFF -DSDL_STATIC=ON`; toolchain file `vendor/SDL/build-scripts/i586-pc-msdosdjgpp.cmake` |
| `make sdl3-mixer` | `libSDL3_mixer.a` | `SDLMIXER_DEPS_SHARED=OFF` (no dlopen); WAV + Vorbis-via-STB only; everything else off (FLAC/OPUS/MOD/MP3/MIDI) |
| `make sdl3-image` | `libSDL3_image.a` | `SDLIMAGE_DEPS_SHARED=OFF`; PNG-via-STB only; everything else off |

**Recommendation for a new port:** vendor SDL3 + SDL3_mixer + SDL3_image at the same SHAs doskutsu pinned (see `vendor/sources.manifest` for the current pins) and reuse the doskutsu Makefile target shape. The CMake option lists in `Makefile:212-234` (mixer) and `Makefile:252-278` (image) are the load-bearing ones — codecs disabled there are codecs that wouldn't link cleanly under DJGPP or weren't worth the size cost. Enabling more is possible but is upstream territory you'll pay for.

**SIMD-flag train.** SDL3's public `SDL_intrin.h` enables `SDL_SSE_INTRINSICS=1` for any GCC >= 4.9 that *supports* the `target("sse")` attribute, regardless of the target CPU. SDL3 itself disables this in its internal `build_config.h` so its own code compiles, but **downstream consumers** (SDL3_mixer, SDL3_image, your engine) compile without that internal config and pick up the SSE intrinsic paths — which then emit a runtime check that fails on Pentium-class hardware. doskutsu's fix lives in `Makefile:83-85` as `NOSIMD_FLAGS`, propagated via `CMAKE_C_FLAGS` / `CMAKE_CXX_FLAGS` on every consumer. A new port must do the same. Symptom if you forget: `Mix_Init: Need SSE instructions but this CPU doesn't offer it` at runtime on a 486 or P54C.

---

## Vendoring convention: snapshots, not submodules

doskutsu vendors upstream sources as snapshots populated by a clone-and-checkout script (`scripts/fetch-sources.sh`) keyed off `vendor/sources.manifest`. Manifest format is `<name> <url> <ref> <sha>`. Cloned trees are gitignored; the manifest + per-project patches are tracked.

```
vendor/
├── sources.manifest                # URL + ref + pinned SHA per upstream
├── SDL/                            # gitignored; populated by fetch-sources.sh
├── SDL_mixer/                      # gitignored
├── SDL_image/                      # gitignored
└── <engine>/                       # gitignored

patches/
├── SDL/*.patch                     # applied by scripts/apply-patches.sh
├── SDL_mixer/*.patch
├── SDL_image/*.patch
└── <engine>/*.patch
```

**Why snapshots over submodules:** freedom to patch without committing patches into a fork branch. Submodules force you to push patches as commits onto a fork and pin the submodule SHA against the fork — that's a viable workflow for projects that intend to upstream their patches. doskutsu's policy is the opposite (patches stay local; see below), so the lower-friction model wins.

**Trade:** every upstream sync is a manual rebase of the patch series against the new pinned SHA. Numeric patch ordering and clustering by subsystem (see Patch-authoring playbook below) make this tractable.

Reference: `CLAUDE.md § Vendoring`, `vendor/sources.manifest`.

---

## The Path B lesson — `sdl2-compat` does not work on DJGPP

**This is the most expensive lesson in the doskutsu playbook.** If you have an engine using SDL2 and you find yourself reading SDL3-DOS docs, the natural-looking move is to drop in `libsdl-org/sdl2-compat` as a static API shim and avoid migrating the engine source. **Don't.** It cannot be statically linked under DJGPP, for three structural reasons documented in `PLAN.md § Plan Amendments § 2026-04-24`:

1. **`FATAL_ERROR` Linux-only gate** at `vendor/sdl2-compat/CMakeLists.txt:96-98`. The build system explicitly rejects non-Linux targets at configure time when `SDL2COMPAT_STATIC` is requested. Not a flag you can flip — a deliberate guard.
2. **1,291 `IGNORE_THIS_VERSION_OF_*` rename macros** in `vendor/sdl2-compat/src/sdl3_include_wrapper.h`. They exist to support the dynamic-loader path; statically linking forces every one of them to resolve into the binary with no source-level path to disable the rename layer.
3. **~1,500 `SDL_*` symbol multiple-definition collisions.** sdl2-compat re-declares SDL2 entry points that SDL3's dynapi also defines. Under static link, the linker fails with thousands of multiple-definition errors. Resolving this would require an `objcopy --redefine-sym`-driven rename pass over either archive — novel infrastructure that doesn't exist in any DJGPP project, that you'd have to invent and maintain across every SDL3 release.

Both upstreams independently document the assumption that DOS is static-link-only and that sdl2-compat assumes dynamic loading:

- `vendor/sdl2-compat/src/sdl2_compat.c:372` (author comment): *"Obviously we can't use SDL_LoadObject() to load SDL3. :)"*
- `vendor/SDL/src/dynapi/SDL_dynapi.h:73-74` disables SDL3's own dynapi on DOS: `#elif defined(SDL_PLATFORM_DOS) #define SDL_DYNAMIC_API 0  /* DJGPP doesn't support dynamic linking */`

**Operational rule for any SDL2 engine port to DOS:** budget for a direct SDL2 → SDL3 source migration of the engine. The doskutsu migration was sized at 1-2 days for the mechanical pass and a few extra days for the audio refactor (SDL_AudioCVT → SDL_AudioStream); see `patches/nxengine-evo/README.md § 0010-0019` for the per-patch breakdown of what got renamed. The audio cluster (`0013-0017` plus follow-ups `0020`, `0024`) is where the real engineering lives — the renderer/surface/event API renames are largely sed-bait.

The migration is doable; the shim is not. Don't burn a week proving this from scratch.

---

## DOS-specific gotchas checklist

The non-negotiable list. Distilled from `CLAUDE.md § Critical Rules` and the bug roundups in `PLAN.md § Plan Amendments § 2026-04-25`.

### DJGPP / language

- **`fopen(path, "rb")` always.** DJGPP defaults to text mode; CRLF translation silently corrupts binaries (sprites, maps, music, save files). This bites every port that imports a Linux-developed engine.
- **`size_t` is 32-bit on DJGPP.** Modern code may assume 64-bit. Audit every `ftell`, `off_t`, `ssize_t`, and buffer-size computation. A 4 GB asset file isn't realistic on DOS, but a `size_t` truncation can corrupt a 100 KB read silently.
- **No `long double` in printf.** Historical DJGPP libc bugs around 80-bit float formatting. Avoid.
- **Default DPMI stack is 256 KB.** `stubedit <exe> minstack=2048k` as a post-link step on every executable. doskutsu's Makefile applies this to every produced `.exe`. Symptom: deep-recursion or large-stack-buffer crashes that don't reproduce on Linux.
- **No SIMD anywhere.** P54C predates MMX; even the Tier 2 486 fallback has no MMX. Compile with `-march=i486 -mtune=pentium -O2`. SDL3's public-header SIMD trap (above) is the gotcha that bites; engine source rarely needs touching.
- **`-fno-rtti` is safe and a code-size win** if the engine has no `dynamic_cast` / `typeid` (verify with grep). doskutsu's NXEngine-evo had zero hits.
- **`-fno-exceptions` is NOT safe by default.** doskutsu kept exceptions on because NXEngine-evo's `nlohmann::json` parse sites depend on exception propagation to convert "log + skip the malformed asset, keep playing" into "abort the process." For a modder-friendly port, that's the wrong trade. For a port where the engine treats every parse failure as fatal anyway, you can flip it. Audit every `try` / `throw` / `catch` site before disabling.
- **No shared libraries / `SDL_LoadObject`.** SDL3-DOS is static-only; `SDL_LoadObject` is unsupported. Disable all `SDL*_DEPS_SHARED` build options and any plugin-loading code in your engine.

### Threading

- **Cooperative scheduler.** SDL3's DOS backend yields in its event pump and in `SDL_Delay`. There is no preemptive worker-thread infrastructure. **Do not introduce `SDL_CreateThread`, `std::thread`, `pthread_create`, or `std::async` in port glue, audio refactors, or anywhere else.** A port whose upstream uses `std::thread` for audio mixing or asset streaming has to rewrite that code synchronously before the binary will run correctly under SDL3-DOS.
- The doskutsu invariant is "threading-zero." Verify with `grep -rE 'SDL_CreateThread|std::thread|pthread_create|std::async' src/` periodically. NXEngine-evo's #27 audit found zero hits in the upstream source — this is what made the port viable in the first place. An engine that fails this audit will require synchronization rework as a prerequisite to porting.
- See `docs/SDL3-MIGRATION.md § 3` (the "synchronous only" architectural invariant) for the doskutsu reasoning.

### Renderer

- **Software renderer only.** `SDL_RENDERER_ACCELERATED` is unsupported by SDL3-DOS. doskutsu patches `Renderer.cpp:119` to force `SDL_RENDERER_SOFTWARE`. Any engine using `SDL_RENDERER_ACCELERATED` needs the equivalent patch.
- **No OpenGL / Vulkan / Direct3D.** SDL3-DOS does not expose any GL context. `SDL_GL_*`, `SDL_GL_CreateContext`, `SDL_GL_SetAttribute` will compile (the symbols exist in SDL3) but the runtime cannot satisfy them. An engine architected around a GL3.3 core context (engge2, modern AGS-style engines) is **not portable to SDL3-DOS without rewriting the renderer** to use SDL2/3's surface or texture API. This is an engine-rewrite task, not a port task.
- **VESA 1.2+ linear framebuffer required.** Plain VGA without VESA has no linear framebuffer. SDL3-DOS programs the DAC via vendor VBE or UNIVBE.
- **INDEX8 palette regression.** SDL3's `SDL_CreateSurface(...,INDEX8)` no longer attaches a default palette (SDL2 did). doskutsu's `patches/nxengine-evo/0026-sdl3-zoom-index8-palette.patch` fixes this; any engine using paletted surfaces will hit it. Symptom: `SDL_CreateTextureFromSurface` failing with `"src does not have a palette set"` on every paletted asset.

### Audio

- **No audio recording.** Don't accidentally enable `Mix_*` capture APIs.
- **SDL_AudioCVT is gone in SDL3.** Replaced by `SDL_AudioStream`. Any engine using `SDL_BuildAudioCVT` + `SDL_ConvertAudio` for sample-rate or format conversion needs this rewritten as `SDL_CreateAudioStream` + `SDL_PutAudioStreamData` + `SDL_GetAudioStreamData` lifecycle. doskutsu's `patches/nxengine-evo/0013-sdl3-audio-pixtone-audiostream.patch` is the reference.
- **SDL3_mixer's API redesigned.** `Mix_OpenAudio`, `Mix_HookMusic`, `Mix_QuickLoad_RAW` are all replaced by `MIX_CreateMixer`, `MIX_Track`, `MIX_LoadRawAudio`, etc. Not just renames — the lifecycle is genuinely different (decoder objects vs pre-loaded chunks). doskutsu's `0014-0017` cluster shows the per-callsite migration. Volume scale also changed (int 0-128 → float 0.0-1.0; doskutsu's `0020` follow-up wires this).
- **DOSBox-X SB16 detection quirk.** DOSBox-X's emulated SB16 returns `0xFF` on the DSP detection read regardless of timing tuning, so SDL3-DOS audio init fails without a workaround env var. doskutsu sets `SDL_DOS_AUDIO_SB_SKIP_DETECTION=1` in the DOSBox-X launcher (`tools/dosbox-x.conf:97`). **Real hardware MUST NOT set this** — it bypasses the legitimate detection path.
- **Hard-clip on audio device init.** SDL3-DOS reads the `BLASTER` env var (`A220 I5 D1 H5 T6` for the typical SB16 setup). If `BLASTER` is missing or wrong, audio is silent. Document this in your boot-profile docs (see doskutsu's `docs/BOOT.md`).

### Filesystem

- **Long File Names (LFN) are NOT available on plain MS-DOS 6.22.** DOSBox-X masks this when `lfn=true` is set in the config (which doskutsu sets in `tools/dosbox-x.conf:78` to keep iteration fast), but real DOS 6.22 has no LFN driver out of the box. If your engine references files like `wavetable.dat`, `music_dirs.json`, `StageSelect.tsc`, those `fopen()`s succeed under DOSBox-X-with-lfn-on and fail on real hardware as truncated 8.3 names that miss the actual file.
  - Workarounds: (a) load `DOSLFN.COM` in `AUTOEXEC.BAT` before your binary (TSR LFN driver, ~9 KB, GPLv2 — redistributable, ships alongside CWSDPMI in the dist zip); or (b) source-level rename the long-named assets at port time. (a) preserves portability for asset packs across forks; (b) makes the binary self-contained but ramifies through every asset-path reference. doskutsu defers the choice — it's logged for a future Phase 8 decision.
- **CMake `UNIX=1` taint.** DJGPP cross-compiles from a Linux host inherit CMake's `UNIX=1` unless the toolchain file explicitly clears it. doskutsu's `patches/nxengine-evo/0025-cmake-djgpp-data-path.patch` gates `IF(UNIX_LIKE)` on `AND NOT DJGPP` — without it, the build-host's absolute Linux path gets baked into the DOS binary as `DATADIR`, and the engine fatal-exits on a path like `/home/<user>/.../data/font_1.fnt` that obviously doesn't exist on the target machine. Audit every CMake gate using `UNIX`, `UNIX_LIKE`, `LINUX`, `APPLE` for the same trap.
- **Working-directory sensitivity.** SDL3-DOS's `SDL_GetBasePath()` returns `<exe-dir>/`, so engines that resolve assets via `getBasePath() + "data/"` need `data/` co-located with the `.exe`. doskutsu's `make stage` target builds a runtime layout (DOSKUTSU.EXE + CWSDPMI.EXE + data/ symlink) that mirrors the eventual install layout under `C:\DOSKUTSU\` (`Makefile:591-606`). Plan the same staging shape for any port.

Reference: `CLAUDE.md § Critical Rules`, `PLAN.md § Plan Amendments § 2026-04-25`.

---

## Patch-authoring playbook

Distilled from `patches/nxengine-evo/README.md`. Generalizable to any per-upstream patch tree.

### Layout

- `patches/<name>/NNNN-short-description.patch` — `git format-patch` output, applied in lexical order by `scripts/apply-patches.sh` with `LC_ALL=C` for locale-stable sort.
- `patches/<name>/README.md` — documents which patch covers which concern, the cluster boundaries, and any deferred-rebase hints.
- One concern per patch. The migration of an entire codebase is many concerns; one mega-patch fails review and bisection.
- Per-file split is too fine-grained when one concern (e.g., the SDL2→SDL3 mechanical-rename pass) touches multiple files. Group by concern.

### Numeric clusters with reservation gaps

Reserve numeric ranges by category, with gaps for late-discovered issues:

```
0001-0009   build-system + DOS adaptations  (DJGPP, CMake gates, target flags)
0010-0019   API migration                   (SDL2→SDL3 renames, audio refactor)
0020-...    overflow                        (follow-ups when reservation gaps fill)
```

doskutsu started with a 5-patch plan in the `0001-0009` slot and a 9-patch plan in `0010-0019`; both filled completely as integration surfaced new issues, exactly the use the reservation gap was designed for. The `0020-0027` overflow holds Phase 5/7 follow-ups that didn't fit.

### The cascade trap

Slot-N patches authored on a workspace with N+1+ already applied produce wrong-base hunks: the patch describes the post-N+1 state but is meant to apply against the pre-N+1 state. Symptom: the patch applies cleanly during initial author but fails for everyone else.

**Fix discipline:** before regenerating slot N with `git format-patch`, reset the worktree to the state just before slot N. doskutsu uses ad-hoc `git checkout -B` discipline; see the user memory `patch_cascade_trap.md` for the convention.

### Slot numbering

**Pure numeric only — no alpha suffixes** (`0014a-foo.patch` is forbidden). Sort behavior of `find ... | sort` varies across locales (C vs UTF-8); `LC_ALL=C` puts alpha-suffix slots after the same-prefix numeric slot, so `0014a` lands after `0015`, not between `0014` and `0015`. `scripts/apply-patches.sh` enforces `LC_ALL=C` to make the sort stable; the cleaner rule is to avoid alpha suffixes entirely.

Use the next free numeric slot. Reserved gaps remain the preferred home, but content-determined slotting wins when those gaps are full.

### Patches stay LOCAL

doskutsu never upstreams its `patches/<name>/*.patch` files. Policy decision dated 2026-04-25, recorded in `CLAUDE.md § Vendoring`. The trade:

- **Cost:** every upstream sync (rebasing the patch series against a new pinned SHA) is on you. No upstream mainline picks up your fixes.
- **Benefit:** sidesteps `vendor/SDL/CLAUDE.md`'s no-AI-authoring restriction (which scopes to PR-style contributions). AI-assisted patch authoring is fine when the patches stay in your tree.
- **Verdict:** for a one-engine port, freedom-to-patch beats upstream alignment. If a fix is so generally useful that someone wants it upstream, that's a separate human-authored effort outside the port repo.

This policy choice is what makes AI-assisted port work tractable. Without it, every upstream's no-AI-PR rule blocks the obvious productivity path.

### Authoring order

`0001-0009` first (build-system patches against the un-migrated source). Then `0010-0019` (API migration against a tree with `0001-0009` applied). This avoids merge conflicts when migration patches touch lines already adjusted by the build-system patches.

When rerolling against a new upstream SHA: rebase early clusters first, late clusters last. If a later cluster conflicts on lines an earlier cluster already touched, fix the earlier one first — it's the foundation.

---

## Common DOS-port patch categories

What you can expect to need for any C++ engine port. Categories doskutsu actually filed against NXEngine-evo:

| Category | doskutsu patch | What it does |
|---|---|---|
| Drop optional decoder deps | `0001-cmake-drop-jpeg-find-package.patch`, `0007-cmake-drop-png-on-dos.patch` | Remove `find_package(JPEG REQUIRED)`, `find_package(PNG REQUIRED)` etc. when the engine doesn't actually need that codec or when SDL3_image's stb_image backend covers it |
| DJGPP target flags | `0002-cmake-djgpp-target-flags.patch` | `-march=i486 -mtune=pentium -O2`, `NXE_DOS` define, `-fno-rtti` (only if RTTI unused); CMake gate via `if(CMAKE_SYSTEM_NAME STREQUAL "MSDOS" OR CMAKE_CXX_COMPILER MATCHES "msdosdjgpp")` |
| Binary rename | `0003-cmake-binary-rename-doskutsu.patch` | `set_target_properties(<target> PROPERTIES OUTPUT_NAME <8char-name>)` so DOS uppercases to a sensible 8.3 name |
| Force software renderer | `0004-renderer-force-software-renderer.patch` | `SDL_RENDERER_ACCELERATED` → `SDL_RENDERER_SOFTWARE` everywhere it appears |
| Lock display mode | `0005-renderer-lock-320x240-fullscreen.patch` | Pin the engine to one resolution at runtime; widescreen / HD code paths can stay compiled in for future revisit |
| Logging library replacement | `0006-djgpp-spdlog-replacement.patch` | spdlog's `fputws` / `wostream` use is incompatible with DJGPP libc; replace with an `fmt`-backed shim |
| Exclude host-side tools | `0008-cmake-exclude-extract-target-on-djgpp.patch` | NXEngine-evo's data-extraction utility runs on the host (Linux), not the DOS target; gate it out of the DJGPP build |
| POSIX header visibility | `0009-djgpp-posix-headers.patch` | Feature-test macros for DJGPP's POSIX headers |
| Mechanical SDL2 → SDL3 renames | `0010-sdl3-mechanical-renames.patch` | `SDL_RenderCopy` → `SDL_RenderTexture`, `SDL_FreeSurface` → `SDL_DestroySurface`, `SDL_CreateRGBSurface` → `SDL_CreateSurface`, `SDL_FillRect` → `SDL_FillSurfaceRect`, joystick API renames |
| SDL2 → SDL3 event enum renames | `0011-sdl3-event-enum-renames.patch` | `SDL_KEYDOWN` → `SDL_EVENT_KEY_DOWN` (~14 constants); separate patch because the renames are easy to miss in mechanical-rename noise |
| SDL2 → SDL3 keysym renames | `0022-sdl3-keysym-letter-rename.patch` | `SDLK_a`-`SDLK_z` (lowercase) → `SDLK_A`-`SDLK_Z` (uppercase); ~26 constants |
| Renderer property API | `0012-sdl3-renderer-properties.patch` | `SDL_GetRendererInfo` / `SDL_RendererInfo` → `SDL_GetRendererProperties` + `SDL_GetStringProperty`; genuinely different API surface |
| Audio CVT → AudioStream | `0013-sdl3-audio-pixtone-audiostream.patch` | `SDL_BuildAudioCVT` + `SDL_ConvertAudio` → `SDL_AudioStream` lifecycle; touches every synth/resample site |
| SDL3_mixer redesign | `0014-0017-sdl3-mixer-*.patch` cluster | `Mix_OpenAudio` → `MIX_CreateMixer`, `Mix_HookMusic` → `MIX_Track` callback, `Mix_QuickLoad_RAW` → `MIX_CreateAudioDecoder`, `Mix_HookMusicFinished` → MIX_Track equivalent |
| SDL2_image → SDL3_image | `0018-sdl3-image-load.patch` | Mostly signature-drift; the `IMG_*` prefix was kept by upstream |
| `find_package(SDL2)` → `find_package(SDL3)` | `0019-cmake-find-package-sdl3.patch` | The CMakeLists side of the migration |
| Volume scale | `0020-sfx-volume-wireup.patch` | SDL3_mixer is float 0.0-1.0 vs SDL2_mixer's int 0-128; thread the new scale through engine code |
| Include-path migration | `0021-sdl3-additional-include-paths.patch` | `<SDL2/SDL.h>` → `<SDL3/SDL.h>` in spots the mechanical pass missed |
| Disable fmt locale on DJGPP | `0023-djgpp-fmt-disable-locale.patch` | `FMT_USE_LOCALE=0`; DJGPP libc's `std::locale` use crashes at static-init under specific conditions |
| Audio cluster follow-ups | `0024-audio-cluster-followups.patch` | Sample-clamp, callback linkage, miscellaneous corrections after the main audio refactor lands |
| CMake host-path leak fix | `0025-cmake-djgpp-data-path.patch` | `IF(UNIX_LIKE)` gated on `AND NOT DJGPP` so DJGPP cross-compiles don't bake in a Linux absolute path as `DATADIR` |
| INDEX8 palette regression | `0026-sdl3-zoom-index8-palette.patch` | SDL2's `SDL_CreateRGBSurface(INDEX8, ...)` attached a default palette; SDL3's `SDL_CreateSurface(...,INDEX8)` does not; explicit palette attach |
| Logical presentation | `0027-sdl3-renderer-logical-presentation.patch` | `SDL_SetRenderLogicalPresentation(_renderer, 320, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX)` so the 320x240 game renders correctly into the 640x480 SDL3-DOS framebuffer default mode |

The "drop logging library" patch (`0006`) and the "fmt locale" patch (`0023`) are not specific to NXEngine-evo — they apply to any engine that depends on spdlog or fmt. Bank these as reusable.

---

## Asset extraction patterns

doskutsu's pattern (cited as a model):

- **Game data is gitignored and user-supplied.** `docs/ASSETS.md` documents how the user obtains the original (Cave Story 2004 freeware Doukutsu.exe) and runs the extractor. Game data is never in the repo, never in the dist zip. This sidesteps both the upstream's data license terms and the risk that the engine's GPL would attempt to swallow the data through inclusion.
- **Host-side Python extractors mirror the engine's runtime extractor.** `scripts/extract-pxt.py` and `scripts/extract-engine-data.py` are doskutsu-authored Python ports of NXEngine-evo's C++ extraction code (`vendor/nxengine-evo/src/extract/extractpxt.cpp`, `extractstages.cpp`). Operate on file offsets; no heavy toolchain (no Rust, no Wine). Mirror upstream's algorithm verbatim — diff their output against a known-good reference build to confirm byte-equivalence.
- **`make stage` / `make install` deploy the runtime layout.** `data/` symlinked into a staging dir for DOSBox-X iteration, full copy onto a CF mount for real hardware. The eventual on-disk layout matches between Linux iteration, DOSBox-X test, and real hardware.

For a new port, build the same shape: extractors live in `scripts/`, output to `data/`, `data/` is gitignored, asset-extraction docs in `docs/ASSETS.md`. The user-facing readme tells the user to extract assets themselves with a one-line invocation.

---

## DOSBox-X test workflow

doskutsu ships two configs:

- **`tools/dosbox-x.conf`** — parity config. `cycles=fixed 40000`, `core=normal`, `cputype=pentium_slow`. Approximates the PODP83 reference target. Used for playtest gates and any work where real-HW-equivalent timing matters.
- **`tools/dosbox-x-fast.conf`** — fast iteration. `cycles=max`, `core=dynamic`. Runs ~4-8x faster than real hardware. Use only for reaching repro states quickly. Do **not** make performance claims from fast-config measurements.

### Visible vs headless

- **Headless** (`tools/dosbox-run.sh` / `make smoke-fast` style): no window; output via redirected stdout. Used for CI-style smoke tests that just want exit code + stdout. SDL_Log writes to stderr unconditionally; neither MS-DOS COMMAND.COM nor DOSBox-X's built-in shell support `2>&1`, so logging that needs to flow through this path must use `printf` to stdout.
- **Visible** (`tools/dosbox-launch.sh`): brings DOSBox-X up on `DISPLAY=:0` so screenshots and key injection work. Mirrors the Snow / Basilisk II workflow used by sibling projects (Geomys, Flynn). `scrot -u` for screenshots (the `-u` is critical — captures the focused window only). `xdotool type --delay 40` for key injection (zero delay drops keys).
- **Both configs set `quit warning = false`**. DOSBox-X's default would block scripted shutdown on a modal dialog that cannot be answered without X input.

### Non-negotiable rules

- **Don't use ImageMagick `import` for screenshots.** It grabs the X pointer and breaks emulator mouse input. The Snow-era rule generalizes here.
- **Always target `DISPLAY=:0` explicitly** for `scrot` / `xdotool`. Shells invoked by Claude Code may inherit an SSH-forwarded `$DISPLAY` that isn't the user's visible desktop.
- **One DOSBox-X instance at a time.** They contend for the audio device and make `xdotool search --name DOSBox` ambiguous. The launcher refuses a second instance.
- **`pkill -x dosbox-x`** (exact match), not `-f`. The latter false-matches any bash subshell whose cmdline contains the string.

### LFN trap

DOSBox-X defaults `lfn = auto`, which means **disabled** for emulated MS-DOS 6.22. doskutsu sets `lfn = true` in both configs (the `[dos]` section). This makes long filenames work in DOSBox-X — but **only in DOSBox-X**. Real DOS 6.22 has no LFN driver. See the "Filesystem" gotcha above for the deferred decision.

Reference: `docs/HARDWARE.md`, `tools/dosbox-x.conf`.

---

## Real-hardware caveats

- **SDL3 DOS backend has no real-hardware testing upstream.** PR #15377's author explicitly states: *"tested extensively with DevilutionX in DOSBox. But no real hardware testing."* Budget debugging time for VESA quirks, audio DMA edge cases, BIOS-VBE compatibility issues. When DOSBox-X and real HW diverge, **trust real HW** — DOSBox-X is calibration-target, not source-of-truth.
- **CWSDPMI must ship alongside the binary.** It's a separate `.exe` invoked at runtime, not statically linked. Include in every CF deploy and dist zip. The `.doc` redistribution-terms file is required by CWSDPMI's license.
- **Boot profile matters.** doskutsu documents the expected DOS environment in `docs/BOOT.md`: HIMEM.SYS loaded, NOEMS, VESA loaded (vendor VBE TSR or UNIVBE), `BLASTER` env var set, ≥20 MB free XMS, DPMI host on `PATH` or in CWD. A new port mirrors this document for its own target.
- **Don't improvise CONFIG.SYS profiles.** Use whatever profile the target machine already has for similar games. doskutsu runs under the existing g2k `[VIBRA]` profile.

---

## Tier-by-tier hardware targets

doskutsu defines three tiers, captured in `docs/HARDWARE.md`. Carry over for any port targeting Pentium-era hardware:

| Tier | CPU | RAM | Audio | Status |
|---|---|---|---|---|
| **Tier 1 (Reference)** | Pentium OverDrive 83 MHz (PODP5V83, P54C, no MMX, ~Pentium-40 effective) | 48 MB | 22050 Hz stereo S16 | Phase 7 / 8 gates run here |
| **Tier 2 (Achievable Minimum)** | 486DX2-66 with FPU | 16 MB | 11025 Hz mono S16 | Untested until phase-9 work; expected to run with the audio-mode fallback |
| **Tier 3 (Absolute Minimum, stretch)** | 486DX2-50 with FPU | 8 MB | 11025 Hz mono S16, 8bpp indexed | Aspirational; requires the full perf-tuning pass |

Hard floors below Tier 3:

- **No 486SX without a 487 coprocessor.** DJGPP emits x87 code; pure 486SX traps on every FP instruction.
- **VESA 1.2+ required.** No pre-VESA hardware. UNIVBE acceptable.
- **< 4 MB RAM not viable.** Cave Story's extracted assets alone are ~5 MB; engine working set adds more.

The tier model is a planning device — calibrate cycle counts, audio rates, and resolution choices to the tier you're targeting. Tier 1 testing happens against `cycles=fixed 40000` in DOSBox-X (calibrated to PODP83 effective integer throughput). vellm (sibling project) uses `cycles=fixed 90000` for the same g2k machine because vellm is CPU-bound on integer matmul kernels rather than memory-bound on blits — different workloads calibrate to different cycle counts on the same hardware.

---

## Licensing notes

- **GPLv3-compatible deps only.** Acceptable: MIT, BSD-2/3 (not original 4-clause with the advertising clause), zlib, Apache 2.0, public domain, LGPL via static linking with the linking exception. Reject: proprietary, GPL-incompatible.
- **Static-link license inheritance.** A statically-linked binary takes the most-restrictive license among its components. NXEngine-evo's GPLv3 dominates `DOSKUTSU.EXE` even though doskutsu's port glue is MIT. Plan the dist bundle's license file accordingly.
- **DJGPP libc has a runtime-library exception.** Similar to libstdc++'s exception — explicitly permits distributing statically-linked binaries without imposing GPL on downstream user programs. This is what lets commercial DJGPP programs exist. GPLv3-compatible.
- **CWSDPMI is freeware-with-redistribution.** Not statically linked (it's a separate executable invoked at runtime). Same legal posture as shipping glibc alongside a GPL binary. Redistribution terms documented in `cwsdpmi.doc`; ship that alongside.
- **Patches against GPLv3 upstreams** are derivative works and therefore GPLv3, regardless of the host repo's MIT `LICENSE`. Patches against zlib upstreams (SDL, SDL_mixer, SDL_image) stay zlib. The host repo's MIT license describes the original non-derivative code: Makefile, scripts, port glue that doesn't include upstream headers, docs.
- **Game data stays out of the repo and out of the dist zip.** doskutsu's reasoning: avoids ambiguity about whether the engine's GPLv3 attempts to re-license user-supplied data via inclusion. User extracts data themselves per `docs/ASSETS.md`.

doskutsu's component matrix lives in `PLAN.md § Licensing`. Mirror that table for a new port.

---

## Cross-references

- `CLAUDE.md` — project ground rules; `§ Critical Rules` is the source for the gotchas checklist
- `PLAN.md § Plan Amendments § 2026-04-24` — the Path B decision: why sdl2-compat does not work for DJGPP static-link
- `PLAN.md § Plan Amendments § 2026-04-25` — five-bug roundup from Phase 7 prep; the LFN, CMake `UNIX=1`, and INDEX8 palette traps
- `PLAN.md § Licensing` — component license matrix, redistribution checklist
- `docs/HARDWARE.md` — Tier 1/2/3 definitions, DOSBox-X calibration, real-HW boot profile
- `docs/BOOT.md` — recommended DOS boot environment (HIMEM, NOEMS, BLASTER, VESA, CTMOUSE)
- `docs/ASSETS.md` — extraction-pattern reference
- `docs/SDL3-MIGRATION.md` — architectural decisions that govern the migration patch series; the synchronous-only invariant
- `patches/nxengine-evo/README.md` — patch numbering and ordering reference
- `~/emulators/CLAUDE.md` — toolchain-hub convention
- `~/emulators/docs/DJGPP.md` — DJGPP install, layout, gotchas

# DOSKUTSU Implementation Plan

Phased roadmap for porting Cave Story (Doukutsu Monogatari) via NXEngine-evo to MS-DOS 6.22. For the project overview (what, why, how it all fits together), see `DOSKUTSU.md`. For toolchain and build system details, see `CLAUDE.md` and `BUILDING.md`.

---

## Locked Decisions

These are answered and **not** open for re-litigation mid-phase. If a phase reveals one needs to change, flag it explicitly as a plan amendment.

| # | Decision | Chosen |
|---|---|---|
| 1 | Repo hosting | Forgejo primary (`ssh://git@forgejo.ecliptik.com/ecliptik/doskutsu.git`). No GitHub / Codeberg mirrors for now. |
| 2 | Build system | Top-level `Makefile` orchestrating five stages (SDL3 → sdl2-compat → SDL2_mixer + SDL2_image → NXEngine-evo). Each stage is a CMake invocation. |
| 3 | Vendoring | Snapshots under `vendor/<name>/` with `vendor/sources.manifest` pinning SHAs + `patches/<name>/*.patch` applied at build time. Clones are gitignored; manifest + patches are tracked. |
| 4 | `[DOSKUTSU]` CONFIG.SYS profile | **No dedicated profile in this repo.** Run under existing `[VIBRA]`-style SB16+NOEMS boot; suggestions live in `docs/BOOT.md`. |
| 5 | Widescreen / Full HD in NXEngine-evo | **Lock at runtime to 320x240 fullscreen.** Code paths remain compiled in for future revisit. |
| 6 | Cave Story version | EN freeware 2004 (what NXEngine-evo expects by default). |
| 7 | Game data files | Gitignored. `docs/ASSETS.md` documents extraction from the 2004 freeware `Doukutsu.exe`. |
| 8 | DOSBox-X config | Two configs: `tools/dosbox-x.conf` (parity, `cycles=fixed 40000`, approximates PODP83) and `tools/dosbox-x-fast.conf` (`cycles=max`, iteration only). |
| 9 | Binary rename | Rename in CMake via `set_target_properties(nx PROPERTIES OUTPUT_NAME doskutsu)`. Applied by the NXEngine-evo port patch. |
| 10 | `emulators/` hub integration | Symlink `tools/djgpp` to `~/emulators/tools/djgpp` via `scripts/setup-symlinks.sh`. Documented in BUILDING.md for clone-to-build. |

---

## Plan Amendments

This section records mid-flight plan amendments. Each entry is dated and explains what changed and why. The original phase prose remains in place below as historical context — amendments **supersede** sections of the plan, they do not rewrite history.

### 2026-04-24 — Path B: direct SDL3 migration (supersedes original Phase 3 and Phase 4)

**What changed.** Phase 3 (sdl2-compat for DOS) is abandoned. NXEngine-evo will be migrated SDL2 → SDL3 directly in source, then linked against SDL3 (with the DOS backend from PR #15377), SDL3_mixer, and SDL3_image. The `## Fallback path: direct SDL3 migration` section near the bottom of this document is no longer the fallback — it is the active path.

**Why.** Phase 3a (`vendor/sdl2-compat` cloned at `91d36b8d`) and Phase 3b (sdl-engine's audit of the dlopen/dlsym + dynapi infrastructure) found three structural blockers to a static-only DJGPP build of sdl2-compat. All four citations below reference the cloned `vendor/` source trees as of 2026-04-24.

1. **`FATAL_ERROR` Linux-only gate** at `vendor/sdl2-compat/CMakeLists.txt:96-98`. The build system explicitly rejects non-Linux targets at configure time when `SDL2COMPAT_STATIC` is requested — not a flag we can flip, but a deliberate guard reflecting the upstream's design assumption that the shim is loaded dynamically on every supported platform.
2. **1,291 `IGNORE_THIS_VERSION_OF_*` rename macros** in `vendor/sdl2-compat/src/sdl3_include_wrapper.h` (verified by `grep -c IGNORE_THIS_VERSION` against that file). These exist to support the dynamic-loader path; statically linking forces every one of them to resolve into the binary, with no source-level path to disable the rename layer.
3. **Multiple-definition collision for ~1,500 `SDL_*` symbols.** sdl2-compat re-declares SDL2 entry points that SDL3's dynapi also defines. Under static link, the linker sees both and fails with thousands of multiple-definition errors. Resolving this would require an `objcopy --redefine-sym`-driven rename pass over either archive — novel infrastructure that doesn't exist in any prior DJGPP project, and that we would have to invent and maintain.

**The architectural signal.** `vendor/sdl2-compat/src/sdl2_compat.c:372` contains the author's comment:

> *"Obviously we can't use SDL_LoadObject() to load SDL3. :)"*

— an explicit, authoritative acknowledgment that the shim's design depends on dynamic loading. SDL3's own dynapi reaches the same conclusion from the other direction and **disables itself on DOS** at `vendor/SDL/src/dynapi/SDL_dynapi.h:73-74`:

```c
#elif defined(SDL_PLATFORM_DOS)
#define SDL_DYNAMIC_API 0  /* DJGPP doesn't support dynamic linking */
```

Both pieces of upstream code independently encode the assumption that DOS is a static-link-only target. Forcing sdl2-compat into that target violates the architectural premise both libraries were designed around.

**Option A (objcopy-rename infrastructure)** was explored as an alternative to abandoning the path: post-process `libSDL2.a` with `objcopy --redefine-sym` to avoid the dynapi collision. Rejected — the maintenance burden (every SDL3 release potentially adds new symbols to track), the complexity (cross-archive symbol renaming is fragile and silently mis-resolves call sites if the rename table drifts), and the absence of precedent in DJGPP-cross projects all argued against owning that infrastructure. Better to do the direct migration once and live with SDL3 calls in the engine.

**The new task graph.** Phases 3 and 4 below are superseded. Phase 5 narrows to just the build step (the DJGPP patches it formerly contained move to Phase 4'd). The active sequence is:

| Phase | Work | Notes |
|---|---|---|
| **Phase 3' — SDL3_mixer + SDL3_image** | Cross-build the SDL3-native helper libraries via new Makefile targets `make sdl3-mixer` and `make sdl3-image`. Same constrained CMake options as the original Phase 4 (WAV + OGG via stb_vorbis; PNG via stb_image; MP3 / MOD / MIDI / FLAC / Opus / JPEG / WebP / AVIF / TIFF all off). | `vendor/sources.manifest` will gain entries for `SDL3_mixer` and `SDL3_image` pinned to their SDL3-track releases. The release-2.8.x entries for the SDL2 versions become historical. |
| **Phase 4' — NXEngine-evo SDL2 → SDL3 migration + DJGPP port** | Migrate NXEngine-evo's source from the SDL2 API to the SDL3 API and apply the DOS-port patches. Four sub-phases described as categories of work, not a time-sequence — see orthogonality note below the table. | The substantive work of Path B. Operational details (per-patch enumeration, file:line touchpoints, authoring order) live in `patches/nxengine-evo/README.md` and `docs/SDL3-MIGRATION.md`. This table records the *decisions* governing the work; those docs record the *implementation*. |
| Phase 4'a — Audio refactor (Path 1) | The audio refactor cluster (`0013-0017`). Co-owned by sdl-engine + nxengine on a symptom-based boundary — see `docs/SDL3-MIGRATION.md` for the co-ownership convention and the 5-touchpoint implementation spec. **Threading-zero invariant: no `SDL_CreateThread`, no `std::thread`, no worker threads of any kind during this rework. The audio refactor stays synchronous. SDL3-DOS's cooperative scheduler is the constraint we'd violate first.** | Gate: before/after audio capture from the synth path matches reference WAV byte-equivalent (or within documented rounding tolerance). **Two-stage tripwire: at N=4 working days, raise hand for reassessment; at N=7 working days, fall back to Path 2 — a custom SDL2_mixer-subset shim over `SDL_AudioStream`** (NOT Phase 9 lever 6, which stays reserved for SDL3-DOS audio backend *structural* failures, not Path-1 schedule slip). See `docs/SDL3-MIGRATION.md § delta 4` for the tripwire-stage definitions. |
| Phase 4'b — Mechanical renames | The SDL2 → SDL3 mechanical rename pass (renderer/surface API + event enums). See `patches/nxengine-evo/README.md § 0010-0012` for the per-API breakdown and per-patch grouping. | Largely automatable with `sed`; review each diff. |
| Phase 4'c — Library swap | SDL2_image → SDL3_image migration (and any related enum / property updates from the broader API delta). See `patches/nxengine-evo/README.md § 0018-sdl3-image-load.patch` (and § 0011/0012 if related deltas land there). | Capstone for the source migration — once this lands, NXEngine-evo no longer references SDL2 at all and the engine compiles against the SDL3 sysroot. |
| Phase 4'd — DJGPP port patches | DOS-adaptation patches only — things that would apply to any NXEngine-evo DOS build regardless of SDL major version. **Finalized as 5 patches** per the #27 audit's subsystem-gating collapse (haptic + the would-have-been gamepad/sensor/camera/touch/pen prophylactic patches all dropped — zero refs in NXE-evo source) and the audio-init fold-in to Phase 4'a (`Mix_OpenAudio` → `MIX_CreateMixer` is an SDL3_mixer API change, not a DOS-adaptation, so it belongs in the audio cluster). **Decision: `-fno-rtti` yes, `-fno-exceptions` no** — RTTI is unused (zero `dynamic_cast`/`typeid` hits) so `-fno-rtti` is a pure code-size win; exceptions are load-bearing (6 `nlohmann::json` parse sites convert "log + skip malformed asset, keep playing" into "abort" without them — a bad trade for a modder-friendly port). See `patches/nxengine-evo/README.md § 0001-0005` for the DJGPP cluster layout. | Lands as `patches/nxengine-evo/*.patch` applied by `scripts/apply-patches.sh` against the migrated tree. CLAUDE.md § Critical Rules / NXEngine-evo specifics carries the matching `-fno-rtti`-yes-`-fno-exceptions`-no guidance. |
| **Threading baseline: zero** (audit-confirmed invariant) | NXEngine-evo's #27 audit confirmed: zero `SDL_CreateThread`, zero `std::thread`, zero worker threads of any kind in upstream source. **Maintain this invariant through Path B — do not introduce threading in port glue, audio refactor, or anywhere else.** SDL3's DOS backend uses a cooperative scheduler (yields in event pump and `SDL_Delay`); spawning a thread breaks the model and is the first constraint we would violate. This row exists in the table as a permanent invariant, not a phase to complete. | Verifiable at any time by `grep -rE 'SDL_CreateThread\|std::thread\|pthread_create' src/`. |
| **Phase 5 ✓ — Build → `doskutsu.exe`** (closed 2026-04-25 at `484efa7`) | Build/link/post-link only: `make nxengine` consumes the migrated + patched tree from Phase 4', produces `build/doskutsu.exe`, post-link `stubedit doskutsu.exe minstack=2048k`. NXEngine-evo `#include`s SDL3 headers and links `libSDL3.a` + `libSDL3_mixer.a` + `libSDL3_image.a` directly — no sdl2-compat in the link line. | **Build-link gate: passed** (5,866,918 byte binary, COFF + go32 + CWSDPMI autoload, `minstack=2048k`). Title-screen runtime gate folds into Phase 6 (asset extraction in flight). See CHANGELOG `### Phase 5 — Build → DOSKUTSU.EXE (gate passed 2026-04-25)` for the build trajectory + 4-bug roundup. |

**Orthogonality of the Phase 4' sub-phases.** The labels (4'a / 4'b / 4'c / 4'd) name **what** each category of work is, not **when** it happens in calendar time. Engineers work in parallel on the four categories; the resulting `patches/nxengine-evo/*.patch` files apply in **numeric order** (`0001-0018`) regardless of which sub-phase produced them. nxengine can iterate on `0010-sdl3-mechanical-renames` while build-engineer lands `0001-0005` — they are orthogonal in file-locality terms and compose at patch-apply time. See `patches/nxengine-evo/README.md § Authoring order` for the practical guidance on which patches to write first against an un-migrated tree.

**What's preserved from the abandoned path.** `vendor/sdl2-compat` stays cloned at `91d36b8d` per software-architect's "keep it cloned but unbuilt" condition — preserves the option to reconsider if Phase 4'a turns out worse than the audit predicts. `make sdl2-compat` will be removed from the default build target. Phase 3a (#18) and Phase 3b audit (#19) remain completed in the task tracker; their evidence is what produced this amendment. Old tasks #20, #21, #23, #24 deleted as obsolete.

**Documentation downstream.** `CLAUDE.md` is updated in lockstep with this amendment (architecture stack, toolchain section, build system block). `README.md`, `DOSKUTSU.md`, and `BUILDING.md` all still describe the build as a "five-stage" pipeline with sdl2-compat as the second stage; those references remain drift and will be reworked at the Phase 3' gate so they match the new pipeline shape (SDL3 → SDL3_mixer + SDL3_image → NXEngine-evo). The `THIRD-PARTY.md` sdl2-compat row stays — we still ship the cloned source under the same vendoring convention even though we no longer link it.

### 2026-04-25 — Phase 6 closed; Phase 7 partially open (title screen renders nothing visible)

**TL;DR — pickup state for next session.** Phase 6 (Cave Story data) is closed. Phase 7 (DOSBox-X playthrough) hit a wall: the binary now boots cleanly through NXEngine's full init sequence and reaches the main loop / stage 72 (the title screen), `drawSurface` produces zero errors, but the DOSBox-X emulated framebuffer never paints — it stays black. Five distinct pre-title-screen bugs were diagnosed and fixed today in one sweep (commit `44fec06` on `main`). The remaining "framebuffer doesn't show anything" issue is owned for human triage in the next session; it is **not** in NXEngine, it is somewhere in the SDL3-DOS render-vs-present pipeline.

**What's done.**

| Area | Artifact | Why it landed |
|---|---|---|
| Phase 6 closure | `scripts/extract-engine-data.py` produces `data/wavetable.dat` (Organya PCM, 25600 bytes from `Doukutsu.exe` offset `0x110664`) and `data/stage.dat` (95-record stage index transcribed from `vendor/nxengine-evo/src/extract/extractstages.cpp`, 6936 bytes). | Without `stage.dat` the script-engine init fatal-exits on `StageSelect.tsc` lookup; without `wavetable.dat` Organya init reports a non-fatal error and renders no music. |
| Phase 6 closure | `data/base/` subdir convention from the original Phase 6 prose **superseded**. NXEngine's source resolves data via `getPath("Stage/0.pxm")` → `data/Stage/0.pxm` with no `base/` prefix; `docs/ASSETS.md` and the `make install` rule are reconciled. | NXEngine source-truth, not the original plan, is the source of truth for the layout. |
| Phase 7 prep | `make stage` target + `tools/dosbox-launch.sh --stage` flag. Stage produces `build/stage/` with `DOSKUTSU.EXE` + `CWSDPMI.EXE` + `data/` (symlinked); the launcher mounts that as C:. | NXEngine's `getBasePath() + "data/"` resolution requires `data/` co-located with the `.exe`. Repo layout (build/doskutsu.exe vs data/ at repo root) doesn't satisfy that on its own. Same shape as the eventual install layout under `C:\DOSKUTSU\` on real CF. |
| Phase 7 fatal #1 — fixed | `patches/nxengine-evo/0025-cmake-djgpp-data-path.patch` gates `IF(UNIX_LIKE)` on `AND NOT DJGPP`. | DJGPP cross from a Linux host inherits CMake `UNIX=1` unless the toolchain explicitly clears it. Was baking the build-host's absolute Linux path into the DOS binary as `DATADIR`; engine fatal-exited at graphics init on `Error opening font file /home/<user>/.../font_1.fnt`. |
| Phase 7 fatal #2 — fixed | `tools/dosbox-x.conf` and `tools/dosbox-x-fast.conf` set `lfn = true`. | DOSBox-X defaults `lfn = auto` which means **disabled** for emulated MS-DOS 6.22. NXEngine's data has long-named files (`wavetable.dat`, `music_dirs.json`, `StageSelect.tsc`) that fopen()d as truncated 8.3 names and missed; engine fatal-exited at script-engine init. **Real-HW caveat below.** |
| Phase 7 fatal #3 — fixed | `patches/nxengine-evo/0026-sdl3-zoom-index8-palette.patch` explicitly creates and attaches a palette to INDEX8 dst surfaces in `zoom.cpp`. | SDL3's `SDL_CreateSurface(...,INDEX8)` no longer attaches a default palette (SDL2 did). The palette-copy block guarded `if (_src_pal && _dst_pal)` was always false because `SDL_GetSurfacePalette(rz_dst)` returned NULL; downstream `SDL_CreateTextureFromSurface` failed with `"src does not have a palette set"` for every paletted `.pbm`. SDL2→SDL3 migration regression. |
| Phase 7 fatal #4 — fixed (workaround, not root cause) | `tools/dosbox-launch.sh` sets `SET SDL_INVALID_PARAM_CHECKS=0`. | SDL3's `CHECK_TEXTURE_MAGIC` validates textures via a `SDL_FindObject` hash lookup in `vendor/SDL/src/SDL_utils.c`. On DJGPP the lookup fails for textures that were demonstrably just inserted (suspected init-order or alloc-lifecycle bug, not yet root-caused). All valid textures fail validation; `SDL_RenderTexture` returns `"Parameter 'texture' is invalid"` 540+ times per second. The hint flips `SDL_object_validation` to `false` so `ObjectValid()` falls back to non-null check; drawSurface error count drops to 0. **Local-launcher escape hatch only — the underlying SDL3-DOS hash issue should be revisited; logged as future work, not a Phase 7 blocker.** |
| Phase 8 prep | `docs/PHASE8-G2K-CHECKLIST.md` authored — 179 lines covering pre-deployment, CF prep, g2k boot config, first-boot smoke, 30-min play checklist, top-5 failure-mode catalog (real-HW vs DOSBox-X divergences, including the explicit "do NOT set `SDL_DOS_AUDIO_SB_SKIP_DETECTION` on real HW" warning), reporting-back convention. | Ready to follow when Phase 7 closes. |

**The Phase 7 wall — task #53 (open).**

The binary now reaches stage 72 (title screen) cleanly. `build/stage/debug.log` shows: `Renderer::initVideo: using: software renderer` → `Sound system init` → `Organya init done` → `Reading npc.tbl...` → `Loading tilekey.dat.` → `Script engine init.` → `Entering main loop...` → `>> Entering stage 72: 'u'.` Then surfaces load (`Surface::loadImage 'data/Stage/PrtWhite.pbm'`, `data/bk0.pbm`, `data/Npc/NpcKings.pbm` etc., all reporting bpp/format correctly), `SDL_CreateTextureFromSurface` returns valid textures, `SDL_RenderTexture` emits zero errors. But the emulated DOSBox-X framebuffer stays completely black for the entire run.

**Suspected cause (not yet confirmed):** SDL3-DOS's `vendor/SDL/src/video/dos/SDL_dosvideo.c:169` hard-codes the default fullscreen target to 640×480 via `SDL_GetClosestFullscreenDisplayMode(display_id, 640, 480, 0.0f, false, &closest)`, regardless of NXEngine's `SDL_CreateWindow(NXVERSION, 320, 240, SDL_WINDOW_FULLSCREEN)` request at `vendor/nxengine-evo/src/graphics/Renderer.cpp:115`. The renderer-vs-framebuffer scaling/letterboxing interaction may not be engaging in DOSBox-X — the renderer draws into a 320×240 backing surface that never makes it onto the 640×480 framebuffer that DOSBox-X is showing.

**Investigation candidates for next session, in order of cheap-to-try:**
1. Add `SDL_SetRenderLogicalPresentation(_renderer, 320, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX)` after `SDL_CreateRenderer` in `Renderer.cpp:147`. SDL3's logical-presentation API explicitly handles the "render at game size, present at display size" case.
2. Try `window_flags = 0` (windowed) instead of `SDL_WINDOW_FULLSCREEN` in `Renderer.cpp:80` for the DOS branch — confirm it's a fullscreen-mode-selection issue and not something else.
3. Run `tests/sdl3-smoke/sdltest.exe` (already builds and runs in DOSBox-X with output) and compare its present-pipeline behaviour with DOSKUTSU.EXE — `sdltest.c` is doskutsu-authored against the same SDL3-DOS backend and reports framebuffer state via `printf`. If sdltest's framebuffer paints and DOSKUTSU's doesn't, the difference is in NXEngine's renderer setup.
4. If 1-3 don't pinpoint it: instrument `SDL_RenderPresent` with an `SDL_LogDebug` to confirm it's called every frame, then trace into SDL3-DOS's framebuffer flush path (`vendor/SDL/src/video/dos/SDL_dosframebuffer.c`) to see whether the output buffer is populated.

**Reproduction instructions for next session:**

```bash
make stage                                                     # rebuild staging dir
tools/dosbox-launch.sh --fast --stage --exe DOSKUTSU.EXE       # smoke launch
DISPLAY=:0 scrot -u /tmp/dosbox.png                            # capture framebuffer
cat build/stage/debug.log                                      # NXEngine engine log
cat /tmp/dosbox-launch.log                                     # DOSBox-X log
pkill -x dosbox-x                                              # stop
```

Expected on hit: `debug.log` ends with `>> Entering stage 72: 'u'` followed by `Surface::loadImage` success lines, **no** `drawSurface failed` errors, **no** `Failed to initialize` criticals. Screenshot shows DOSBox-X menu bar at top + black inner region. Engine itself is healthy; the question is purely "why is the framebuffer black?"

**Real-HW LFN follow-up (Phase 8 deferred decision).** The `lfn = true` workaround is **DOSBox-X-only**. Plain MS-DOS 6.22 on g2k has no LFN driver. Phase 8 needs either DOSLFN.COM (TSR LFN driver, ~9 KB, GPLv2 — redistributable, would ship alongside `CWSDPMI.EXE` in `dist/doskutsu-cf.zip`) loaded via AUTOEXEC.BAT before `DOSKUTSU.EXE`, or a source-level patch renaming the long-named assets at NXEngine source (large surface area: every file path reference). Decision deferred until we actually run on g2k. Logged as Phase 8 follow-up; not a Phase 7 blocker.

**Background research deliverables (Phase 7 prep).**
- Git-hygiene research (#37) closed: recommendation is option (e) — sealed-tree-per-task helper script + serial dispatch — codifying the existing `patches/nxengine-evo/README.md § Authoring order` policy. Does NOT use `git worktree` (per user caveat). Implementation deferred — research-only. Full memo in agent transcript.
- PXT slot audit closed: 540 `data/pxt/fxNN.pxt not found` warnings during init are **expected**, not extractor bugs. NXEngine iterates `slot=1..0x75` blindly; Cave Story's `SND[]` table is genuinely sparse. `scripts/extract-pxt.py` is byte-identical to upstream's. No fix needed; could optionally bump `LOG_WARN` → `LOG_DEBUG` in a future patch to clean up the log.

**What's preserved.** The five-bug sweep is in `main` at commit `44fec06`. Patches `0025` and `0026` are in `patches/nxengine-evo/`. Diagnostic logging I added to `Surface.cpp` and `Font.cpp` was reverted before commit (it was instrumentation-only, not a fix). The debug.log it produced confirmed that texture loads succeed cleanly on the post-fix path; that data informed the diagnosis above.

---

## Licensing

DOSKUTSU's own source (the port glue in this repo — build system, patches, port-specific code, docs) is **MIT**. But the distributed `DOSKUTSU.EXE` binary statically links NXEngine-evo, which is **GPLv3**. Under GPLv3's linking clause, the combined binary work is **GPLv3**. MIT on our source is still valid and useful (re-use of our patches / build system / docs in other projects is unrestricted), but downstream redistribution of the binary must satisfy GPLv3.

### Component license matrix

| Component | License | How it reaches the user | Binary-linked? | Notes |
|---|---|---|---|---|
| DOSKUTSU source in this repo | **MIT** | source (git) | n/a | `LICENSE` in repo root. Applies to port glue, build scripts, patches, docs. |
| NXEngine-evo | **GPLv3** | statically linked into `DOSKUTSU.EXE` | Yes | The dominant license of the combined binary. Makes `DOSKUTSU.EXE` GPLv3-as-a-whole. |
| SDL3 (with DOS backend) | **zlib** | statically linked | Yes | zlib is GPLv3-compatible. |
| sdl2-compat | **zlib** | statically linked | Yes | zlib. |
| SDL2_mixer (release-2.8.x) | **zlib** | statically linked | Yes | zlib. Vendored stb_vorbis inside is BSD/public-domain. We exclude MP3 / MOD / MIDI / FLAC, so no LGPL / libsmpeg concerns. |
| SDL2_image (release-2.8.x) | **zlib** | statically linked | Yes | zlib. Uses stb_image (public domain / MIT). libpng / libjpeg are OFF in our CMake options. |
| DJGPP libc (`libc.a`) | **modified GPL + historic permissive** | statically linked into `DOSKUTSU.EXE` | Yes | DJGPP libc has a special "GPL with exception for the runtime library" (similar to libstdc++'s runtime-library exception) that explicitly permits distributing statically-linked binaries without imposing GPL on downstream user programs. This is what lets commercial DJGPP programs exist. GPLv3-compatible. |
| CWSDPMI (`cwsdpmi.exe`) | **freeware with redistribution permitted** | **separate executable** shipped alongside | No | **Not linked** into `DOSKUTSU.EXE` — it's a separate DPMI host invoked at runtime. Same legal posture as shipping glibc alongside a GPL binary. Redistribution terms are documented in `vendor/cwsdpmi/cwsdpmi.doc` (must be bundled with the binary). |
| Cave Story game data (`.pxm`, `.pxe`, `.pxa`, `.org`, sprites) | **freeware per Pixel's 2004 terms** | **user-supplied at runtime** | No | Not in this repo. Not in the dist bundle. Users extract from the 2004 `Doukutsu.exe` themselves. This keeps us clear of both Pixel's terms and the NXEngine-evo GPLv3 attempting to swallow game data. |
| NXEngine-evo's bundled engine data (fonts, PBM, JSON) | **GPLv3 (inherited from NXEngine-evo)** | shipped in `DATA\` | No | Cloned from `vendor/nxengine-evo/data/`. Not linked — data files, shipped as-is. GPLv3 terms apply to redistribution. |

### What this means in practice

1. **`LICENSE` in this repo is MIT.** Anyone can copy the Makefile, the DOSBox-X tooling, the port patches, the docs, without GPL obligations.
2. **`DOSKUTSU.EXE` when distributed is effectively GPLv3** (because it links GPLv3 NXEngine-evo). The dist zip (`dist/doskutsu-cf.zip`) must include:
   - A copy of `vendor/nxengine-evo/LICENSE` as `GPLv3.TXT` or similar.
   - A pointer to the full corresponding source (this repo's remote URL + the pinned NXEngine-evo SHA from `vendor/sources.manifest`).
   - The MIT `LICENSE` file (for the port glue that's ours).
   - `CWSDPMI.DOC` (CWSDPMI's own redistribution terms).
3. **CWSDPMI ships alongside, not linked in.** It's a separate `.exe` that `DOSKUTSU.EXE` invokes. Its redistribution terms (bundled `CWSDPMI.DOC` required) are orthogonal to the GPLv3 on the main binary.
4. **Cave Story game data stays out of the repo and out of the dist.** `docs/ASSETS.md` tells users how to obtain and extract it themselves. This avoids:
   - The NXEngine-evo GPLv3 attempting to re-license Pixel's freeware data via inclusion.
   - Redistribution questions around Pixel's 2004 terms.
5. **Our port patches in `patches/nxengine-evo/*.patch` are derivative of NXEngine-evo and are therefore GPLv3**, regardless of our repo's MIT `LICENSE`. This is fine — we license the *patches* (and anything that reads the NXEngine-evo source) implicitly under GPLv3 by virtue of being a derivative work. The MIT `LICENSE` covers our original, non-derivative code: Makefile, scripts, port-glue files that don't include NXEngine-evo headers, docs.
6. **Patches for zlib-licensed upstreams** (SDL, sdl2-compat, SDL_mixer, SDL_image) stay zlib-licensed as derivatives of those projects. That's more permissive than MIT and fine under either.

### Downstream redistribution checklist

When producing `dist/doskutsu-cf.zip` (Phase 8 / release), the Makefile's `dist` target must include:

- `DOSKUTSU.EXE` — the binary
- `CWSDPMI.EXE` — DPMI host
- `CWSDPMI.DOC` — CWSDPMI redistribution terms (required by its license)
- `LICENSE.TXT` — MIT `LICENSE` from this repo (for the port source)
- `GPLV3.TXT` — NXEngine-evo's `LICENSE` file (the dominant license of the binary)
- `README.TXT` — basic run instructions + pointer to source repo + note on how to obtain Cave Story data
- `THIRD-PARTY.TXT` — the attribution matrix from `THIRD-PARTY.md` in DOS-CRLF form

We do **not** include Cave Story game data in the dist. `README.TXT` tells users how to obtain it.

### Tasks added to the plan from this licensing review

- [ ] Phase 5: make sure the `Makefile`'s `dist` target includes `GPLV3.TXT`, `LICENSE.TXT`, `THIRD-PARTY.TXT`, `CWSDPMI.DOC` alongside the binary
- [ ] Phase 5: when cloning NXEngine-evo, copy its `LICENSE` to `vendor/nxengine-evo/LICENSE` (done by `fetch-sources.sh` implicitly — clone pulls the whole tree) and reference it from our dist packaging
- [ ] THIRD-PARTY.md must carry the license matrix from this section, verbatim, and be kept in sync
- [ ] Phase 6: `docs/ASSETS.md` must explicitly note that Cave Story data is freeware-from-Pixel, not redistributed by this project, user-obtained
- [ ] No git hook / CI step yet, but: before cutting a release, manually verify the dist zip contains all five license files (`LICENSE.TXT`, `GPLV3.TXT`, `CWSDPMI.DOC`, `THIRD-PARTY.TXT`, plus any zlib notices referenced in THIRD-PARTY.md)

## Architecture Recap

```
NXEngine-evo (SDL2 C++11 source)
    ↓ links against
libSDL2.a        (libsdl-org/sdl2-compat — source-compatible SDL2 API)
libSDL2_mixer.a  (SDL_mixer release-2.8.x, built against sdl2-compat)
libSDL2_image.a  (SDL_image release-2.8.x, built against sdl2-compat)
    ↓ all forward to
libSDL3.a        (libsdl-org/SDL main, with DOS backend from PR #15377)
    ↓ runs on
DJGPP 12.2.0 + CWSDPMI r7 + DOS 6.22
```

**Why sdl2-compat over direct SDL3 migration:** SDL2→SDL3 audio is real work (`SDL_AudioCVT` family is gone, replaced by `SDL_AudioStream`; touches Pixtone and Organya synth code). Renderer API renamed (`SDL_RenderCopy`→`SDL_RenderTexture`, `SDL_FreeSurface`→`SDL_DestroySurface`, etc., ~30 call sites). Surface format enum reshuffled. sdl2-compat absorbs all of this; it's maintained by libsdl-org and is stable on desktop. Risk: nobody has built sdl2-compat for DJGPP yet. It's pure-C forwarding layer — should port cleanly. If it doesn't, direct SDL3 migration is the documented fallback (~1-2 day detour).

---

## Phase 0 — Prerequisites

Verify the dev host has what's needed. These should already exist on this machine:

- [ ] Linux (or WSL) host for cross-compilation
- [ ] DJGPP cross-toolchain at `~/emulators/tools/djgpp/bin/i586-pc-msdosdjgpp-gcc` (install via `~/emulators/scripts/update-djgpp.sh`)
- [ ] `vendor/cwsdpmi/cwsdpmi.exe` (copy from `~/git/vellm/vendor/cwsdpmi/` or download per `vendor/cwsdpmi/README.md`)
- [ ] DOSBox-X with SB16 + VESA SVGA support (`sudo apt install dosbox-x`)
- [ ] CMake >= 3.16, git, standard build tools
- [ ] `scrot`, `xdotool` (for visible DOSBox-X automation)

Run:
```bash
./scripts/setup-symlinks.sh     # creates tools/djgpp -> ~/emulators/tools/djgpp
make djgpp-check                # verifies DJGPP is callable
```

**Gate:** `tools/djgpp/bin/i586-pc-msdosdjgpp-gcc --version` prints cleanly.

## Phase 1 — Toolchain smoke test

Confirm DJGPP + CWSDPMI + DOSBox-X work end-to-end before we pull in any SDL.

- [ ] `tests/smoketest/hello.c` — minimal hello-world (already scaffolded)
- [ ] `make smoke-fast` — builds `hello.exe`, runs it under DOSBox-X with `dosbox-x-fast.conf`, checks expected stdout
- [ ] `make smoke` — same against parity config

**Gate:** both `make smoke-fast` and `make smoke` print expected output and exit 0.

## Phase 2 — Build SDL3 for DOS

```bash
# In sources.manifest: SDL pinned to a post-PR-#15377 main commit
./scripts/fetch-sources.sh      # clones vendor/SDL if not present, checks out pinned SHA
./scripts/apply-patches.sh      # applies patches/SDL/*.patch (initially empty)
make sdl3                       # cmake + make, installs into build/sysroot/
```

The Makefile target invokes CMake with:
```
-DCMAKE_TOOLCHAIN_FILE=vendor/SDL/build-scripts/i586-pc-msdosdjgpp.cmake
-DSDL_SHARED=OFF -DSDL_STATIC=ON
-DCMAKE_INSTALL_PREFIX=<repo>/build/sysroot
```

Smoke test one of upstream's test programs (`testdraw2.c`, `testaudioinfo.c`) — wrap in `make sdl3-smoke`.

**Gate:** SDL test programs draw, play audio, and read keyboard in DOSBox-X via `tools/dosbox-run.sh`.

**Risks to track:** PR #15377 has no real-HW testing upstream. Budget time here for patches — anything DJGPP-specific that broke gets a `patches/SDL/*.patch`.

## Phase 3 — Build sdl2-compat for DOS

> **⚠️ AMENDED 2026-04-24 — Path B.** This phase is superseded. sdl2-compat was found to be architecturally unbuildable as a static DJGPP target (`FATAL_ERROR` Linux gate, 1,291 IGNORE_THIS_VERSION_OF_* macros, ~1,500 SDL_* multiple-definition collisions; the shim's own source notes *"Obviously we can't use SDL_LoadObject() to load SDL3. :)"*). See `## Plan Amendments § 2026-04-24` above for the full reasoning and the new Phase 3' / 4' / 5 graph. The text below is preserved as historical context for the architectural reasoning that drove the pivot.

**This is the highest-risk phase.** sdl2-compat has no prior DJGPP build.

```bash
make sdl2-compat                # cmake + make, installs into build/sysroot/
```

CMake invocation:
```
-DCMAKE_TOOLCHAIN_FILE=vendor/SDL/build-scripts/i586-pc-msdosdjgpp.cmake
-DSDL2COMPAT_STATIC=ON -DSDL2COMPAT_TESTS=OFF
-DCMAKE_PREFIX_PATH=<repo>/build/sysroot
-DCMAKE_INSTALL_PREFIX=<repo>/build/sysroot
```

If the build fails, triage errors:
- Missing POSIX functions — sdl2-compat assumes a fuller libc than DJGPP may provide. Patch incrementally into `patches/sdl2-compat/`.
- Linker errors around `dlopen` / `dlsym` — sdl2-compat has a dynamic-loader path. Force-disable; SDL3-DOS is static-only anyway.
- CMake toolchain file handling — if `sdl2-compat` doesn't know about DJGPP, reference SDL3's `i586-pc-msdosdjgpp.cmake` via `CMAKE_TOOLCHAIN_FILE`.

**Gate:** a trivial SDL2-API test program (`SDL_Init(SDL_INIT_VIDEO); SDL_CreateWindow(...); SDL_Delay(1000); SDL_Quit();`) links against `-lSDL2` and runs in DOSBox-X.

**Fallback if unfixable in ~1 day:** skip Phases 3-4, go direct SDL3 port in Phase 5 (see "Fallback path" below).

## Phase 4 — Build SDL2_mixer and SDL2_image

> **⚠️ AMENDED 2026-04-24 — Path B.** Superseded by Phase 3' (SDL3_mixer + SDL3_image — direct SDL3-native helpers). See `## Plan Amendments § 2026-04-24` above. The CMake constraints described below (WAV + OGG only via stb_vorbis; PNG only via stb_image; everything else off) carry over verbatim to the SDL3-mixer / SDL3-image equivalents — only the package names and the linkage chain change.

**SDL2_mixer (release-2.8.x).** CMake options constrained to shrink footprint:
```
-DBUILD_SHARED_LIBS=OFF
-DSDL2MIXER_VENDORED=ON
-DSDL2MIXER_OPUS=OFF -DSDL2MIXER_MOD=OFF -DSDL2MIXER_MP3=OFF
-DSDL2MIXER_FLAC=OFF -DSDL2MIXER_MIDI=OFF
-DSDL2MIXER_VORBIS=STB -DSDL2MIXER_WAVE=ON
```

Rationale:
- **WAV / RAW required** — Organya synth output goes through `Mix_QuickLoad_RAW`.
- **OGG via stb_vorbis required** — NXEngine-evo supports custom OGG soundtracks (Remix, etc.).
- Everything else off.

**SDL2_image (release-2.8.x).** PNG only via stb_image; drop libpng, libjpeg, everything else:
```
-DBUILD_SHARED_LIBS=OFF
-DSDL2IMAGE_VENDORED=ON -DSDL2IMAGE_BACKEND_STB=ON
-DSDL2IMAGE_PNG=ON
-DSDL2IMAGE_JPG=OFF -DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_WEBP=OFF -DSDL2IMAGE_AVIF=OFF
```

NXEngine-evo only uses `IMG_Init`, `IMG_INIT_PNG`, `IMG_Load`, `IMG_GetError` — PNG is all we need.

**Gate:** `Mix_OpenAudio` + `IMG_Load` both succeed in a DJGPP test harness under DOSBox-X.

## Phase 5 — Build NXEngine-evo for DOS

> **⚠️ AMENDED 2026-04-24 — Path B.** Phase 5's deliverable (`make nxengine` → `build/doskutsu.exe`) is unchanged. The substitution: NXEngine-evo now links SDL3 + SDL3_mixer + SDL3_image directly instead of going through sdl2-compat → SDL3. The DOS-port patches listed below all still apply; the `find_package` invocations in patch 0001 (drop-jpeg) become SDL3-flavored. Phase 4' (the SDL2 → SDL3 source migration — see `## Plan Amendments § 2026-04-24` above) precedes this phase in the new graph; Phase 5 lands once the engine compiles cleanly against the SDL3 sysroot.

```bash
make nxengine      # consumes build/sysroot/, produces build/doskutsu.exe
```

Required patches (landed as `patches/nxengine-evo/*.patch`):

1. **Drop JPEG dep.** Remove `find_package(JPEG REQUIRED)` from `CMakeLists.txt` (confirmed: NXEngine-evo repo + Cave Story assets have no `.jpg`).
2. **DOS target flags.**
   ```cmake
   if(CMAKE_SYSTEM_NAME STREQUAL "MSDOS" OR CMAKE_CXX_COMPILER MATCHES "msdosdjgpp")
       add_compile_options(-march=i486 -mtune=pentium -O2)
       add_compile_definitions(NXE_DOS)
   endif()
   ```
   Grep for `throw` / `try` / `dynamic_cast` / `typeid` before adding `-fno-exceptions -fno-rtti`; drop those flags if any hits.
3. **Force software renderer** at `src/graphics/Renderer.cpp:119`: `SDL_RENDERER_ACCELERATED` → `SDL_RENDERER_SOFTWARE`.
4. **Lock window to 320x240 fullscreen at runtime.** Keep widescreen / HD code paths compiled in — the lock sits in `Renderer::initVideo` or equivalent. `NXE_DOS` ifdef.
5. **Audio init.** Initial: `Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 2048)`. Fallback (if CPU-starved): `Mix_OpenAudio(11025, AUDIO_S16SYS, 1, 2048)` — matches Cave Story 2004 spec.
6. **Disable `SDL_Haptic` code paths.** DOS backend has no haptic subsystem. `#ifdef NXE_DOS` out the haptic init + calls.
7. **Binary rename.** `set_target_properties(nx PROPERTIES OUTPUT_NAME doskutsu)` → `doskutsu.exe`, which DOS uppercases at runtime to `DOSKUTSU.EXE`.
8. **Post-link stubedit.** Makefile handles this: `stubedit build/doskutsu.exe minstack=2048k`.

**Gate:** `build/doskutsu.exe` links. Running it under `tools/dosbox-launch.sh --exe build/doskutsu.exe` reaches the title screen. (Assumes Phase 6 extracted assets are present.)

## Phase 6 — Cave Story data files

> **Status: closed 2026-04-25 at commit `44fec06`.** The `data/base/` subdir convention described below is **superseded** by `PLAN.md § Plan Amendments § 2026-04-25` — the production layout is `data/Stage/`, `data/Npc/`, etc., with no `base/` prefix. The original prose is preserved as historical context. `docs/ASSETS.md` carries the current procedure.

NXEngine-evo ships engine support data (fonts, PBM backgrounds, JSON metadata) but not Cave Story game assets.

1. Obtain the 2004 freeware `Doukutsu.exe` (English translation). Canonical source: https://www.cavestory.org — verify URL before scripting, the site's layout drifts.
2. Extract assets. Options:
   - `doukutsu-rs` extractor (Rust, modern, maintained): https://github.com/doukutsu-rs/doukutsu-rs
   - NXExtract (older, harder to find)
   - Some NXEngine-evo forks ship pre-extracted `data/base/` — only if source is trusted.
3. Place under `data/base/` in this repo (gitignored). Expected subdirs:
   ```
   data/base/Stage/   (map .pxm, .pxe, .pxa)
   data/base/Npc/     (NPC sprite sheets)
   data/base/org/     (Organya .org music)
   data/base/wav/     (sound effect .wav or .pxt for Pixtone)
   ```
4. Smoke-test under DOSBox-X: title screen → first stage → Quote visible, moves, jumps.

Full procedure lives in `docs/ASSETS.md`.

**Gate:** Title screen loads, first stage reachable, player sprite moves and jumps.

## Phase 7 — DOSBox-X playthrough

> **Status: partially open as of 2026-04-25 commit `44fec06`.** All pre-title-screen fatals are fixed; binary reaches NXEngine main loop + stage 72 (title screen) cleanly. The remaining wall — the DOSBox-X framebuffer doesn't paint visibly even though `drawSurface` succeeds zero errors — is logged as task #53 and detailed in `PLAN.md § Plan Amendments § 2026-04-25` with reproduction steps and investigation candidates. Resume there.

Using `tools/dosbox-x.conf` (parity config, `cycles=fixed 40000`), not the fast config — we want real-HW-like timing here.

Configure DOSBox-X to approximate g2k:
```ini
[cpu]
core      = normal
cputype   = pentium_slow
cycles    = fixed 40000    # approximates PODP83 Pentium-40-class integer throughput

[sblaster]
sbtype    = sb16
sbbase    = 220
irq       = 5
dma       = 1
hdma      = 5

[vesa]
modelist  = compatible

[dosbox]
memsize   = 48
```

(The committed `tools/dosbox-x.conf` sets all of this.)

**Play-test targets:**
- Mimiga Village (dialogue + Organya music stability)
- First Cave / Hermit Gunsmith (combat, sprite-heavy)
- Egg Corridor entry (scrolling + enemies)
- Save/load cycle (disk I/O under DOS)
- ≥30 min continuous session (memory leak / heap-fragmentation shake-out)

**Gate:** zero crashes across the checklist; audio stable; save/load works; no visible corruption.

## Phase 8 — Deploy to g2k (real hardware)

Deployment target: CF card mounted on Linux. The g2k repo has `scripts/push-to-card.sh` as the rsync driver and `sync-manifest.txt` as the file-path list.

1. Stage deployment directory `C:\DOSKUTSU\`:
   ```
   C:\DOSKUTSU\DOSKUTSU.EXE
   C:\DOSKUTSU\CWSDPMI.EXE
   C:\DOSKUTSU\DATA\          (NXEngine-evo engine data)
   C:\DOSKUTSU\DATA\BASE\     (extracted Cave Story assets)
   ```
2. Add paths to g2k's `sync-manifest.txt`.
3. Commit in g2k, push, `scripts/push-to-card.sh --go`.
4. Boot g2k under the `[VIBRA]` profile (SB16 IRQ 5 / DMA 1/5 / base 220; NOEMS; VESA via `M64VBE`; CTMOUSE optional).
5. `C:`, `CD DOSKUTSU`, `DOSKUTSU.EXE`. Watch for:
   - DPMI load error → CWSDPMI not in PATH or CWD
   - VESA mode failure → `M64VBE` not loaded (check AUTOEXEC.BAT)
   - Audio silence → `BLASTER` env var mismatch (`A220 I5 D1 H5 T6` expected)
   - Mouse not detected → CTMOUSE not loaded (optional)

**Do not modify CONFIG.SYS / AUTOEXEC.BAT recklessly on g2k.** The canonical memory map lives in g2k's `README.TXT`; mirror existing profile blocks, don't improvise.

**Gate:** title screen on real hardware; first stage playable; audio stable.

## Phase 9 — Performance tuning

Apply in order if framerate or audio drop on real hardware. Levers 1-4 correspond to the Tier 2 → Tier 3 descent in `docs/HARDWARE.md` (Reference PODP83 → 486DX2-50 / 8 MB as the absolute-minimum stretch target):

1. **Audio 22050 stereo → 11025 mono.** Halves Organya CPU cost. Matches Cave Story's 2004 spec. Mandatory for Tier 2 (486DX2-66) and below.
2. **Renderer: texture path → direct surface path.** Currently: `SDL_Surface` → `SDL_CreateTextureFromSurface` → `SDL_RenderCopy`. Rewrite hot blits to go surface→surface via `SDL_BlitSurface`. Saves the per-frame texture upload.
3. **16bpp → 8bpp indexed.** VESA linear 8bpp is SDL3-DOS's best path (per PR description: "8-bit indexed color with VGA DAC palette programming"). Halves per-pixel bandwidth. Requires palette management for Cave Story sprites. Needed for Tier 3.
4. **Disable per-sprite alpha blending.** Cave Story uses colorkey masking mostly; alpha paths in evo may be over-generalized. Needed for Tier 3.
5. **Working-set reduction for Tier 3 (8 MB RAM).** Lazy-load sprite sheets (don't load all Npc/*.pbm at startup), stream `.pxm` / `.pxe` on stage entry rather than caching globally, drop Organya voice-cache size. Potentially requires port-side patches beyond simple flag flips.
6. **Last resort: switch to original NXEngine C source** (https://github.com/nxengine/nxengine). C, SDL1.2-era, ~1/4 the call-site count. Would need SDL2/SDL3 port but simpler codebase, no Organya re-architecture. Would also revisit Tier 3 assumptions from scratch.

Each lever gets a `docs/PERFORMANCE.md` entry with measured before/after numbers. Tier 3 validation requires real 486DX2-50 / 8 MB hardware which we don't currently have — treat the tier as a research target until somebody runs it.

---

## Fallback path: direct SDL3 migration

> **⚠️ AMENDED 2026-04-24 — Path B.** Per the 2026-04-24 amendment, this is no longer the fallback — it is the active path. The size estimate table below was the basis for the Phase 4' decomposition; refer to `## Plan Amendments § 2026-04-24` above for the current sub-phase structure (Phase 4'a audio refactor / 4'b mechanical renames / 4'c library swap). The "1-2 days" estimate is preserved here as a baseline for tracking actual migration time.

If Phase 3 (sdl2-compat on DJGPP) is unfixable in reasonable time, migrate NXEngine-evo's SDL2 code to SDL3 directly.

Size estimate from the survey in the original plan:

| Change | Call sites | Difficulty |
|---|---|---|
| `SDL_RenderCopy` → `SDL_RenderTexture` | ~9 | Mechanical rename |
| `SDL_FreeSurface` → `SDL_DestroySurface` | ~9 | Mechanical rename |
| `SDL_CreateRGBSurface` → `SDL_CreateSurface` | ~5 | Arg reshuffle (masks → format enum) |
| `SDL_BuildAudioCVT` + `SDL_ConvertAudio` → `SDL_AudioStream` | ~12 | **Real refactor** — touches Pixtone + Organya |
| Event struct field renames | varies | Mechanical |
| `SDL_Init` flag changes | 1 | Trivial |
| SDL2_mixer → SDL3_mixer | all `Mix_*` | Library swap, mostly source-compatible |
| SDL2_image → SDL3_image | 4 | Trivial |

**Estimate:** 1-2 days. The audio CVT → AudioStream migration is the only part requiring real thought.

Upstream migration guide: https://wiki.libsdl.org/SDL3/README/migration

---

## Artifacts produced by successful completion

- `vendor/SDL/` etc. — cloned upstream snapshots at pinned SHAs (gitignored)
- `build/sysroot/` — SDL3 + sdl2-compat + SDL2_mixer + SDL2_image static libs, installed per-project
- `build/doskutsu.exe` — DJGPP-built game binary, stubedit'd to 2048K min stack
- `dist/doskutsu-cf.zip` — CF-ready deploy bundle (`DOSKUTSU.EXE` + `CWSDPMI.EXE` + `DATA\` placeholder + README.TXT + LICENSE.TXT)
- CF card layout: `C:\DOSKUTSU\DOSKUTSU.EXE` + `CWSDPMI.EXE` + `DATA\BASE\`
- Boot on g2k: `[VIBRA]` profile → `C:` → `CD DOSKUTSU` → `DOSKUTSU.EXE` → title screen

---

## Open questions (to resolve as phases complete)

These aren't blocking; flag if you hit one.

- **SDL3 DOS backend real-HW behavior.** PR #15377 was DOSBox-only tested upstream. Expect VESA quirks, audio DMA edge cases. Bug fixes land as local-only `patches/SDL/*.patch` per the SDL-patches-stay-local policy (CLAUDE.md § Vendoring) — we do not upstream them; the maintenance cost of rebasing the patch series against new SDL SHAs is the deliberate trade for sidestepping `vendor/SDL/CLAUDE.md`'s no-AI-authoring-PRs restriction.
- **sdl2-compat static-only build.** We need the static build only; the compat shim's dynamic-loader code path must be disabled cleanly (not just by lying about `dlopen`). Phase 3 work.
- **Organya CPU cost at 22050 stereo on PODP83.** Likely tight. The 11025 mono fallback is prepared; when to trigger is a Phase 7 / 8 judgment call.
- **Heap fragmentation under DPMI over ~30-60 min.** NXEngine-evo's C++11 allocation patterns haven't been profiled under DJGPP. Watch CWSDPMI.SWP growth during Phase 7 sessions.

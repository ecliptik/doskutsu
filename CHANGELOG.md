# Changelog

All notable changes to DOSKUTSU are documented here. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versioning follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Initial repository scaffold per `DOSKUTSU-PLAN.md` (now `PLAN.md`)
- Top-level `Makefile` orchestrating the five-stage build (SDL3 → sdl2-compat → SDL2_mixer + SDL2_image → NXEngine-evo)
- `scripts/setup-symlinks.sh`, `scripts/fetch-sources.sh`, `scripts/apply-patches.sh`
- `tools/dosbox-x.conf` (parity, `cycles=fixed 40000`) and `tools/dosbox-x-fast.conf` (`cycles=max`)
- `tools/dosbox-launch.sh` (visible DOSBox-X launcher) and `tools/dosbox-run.sh` (headless runner)
- `vendor/sources.manifest` pinning the five upstream repos
- `tests/smoketest/hello.c` + `tests/run-smoke.sh` (Phase 0 toolchain gate)
- Documentation: `README.md`, `CLAUDE.md`, `PLAN.md`, `BUILDING.md`, `DOSKUTSU.md`, `TODO.md`, `THIRD-PARTY.md`, `docs/ASSETS.md`, `docs/HARDWARE.md`, `docs/BOOT.md`

### Licensing
- Project source licensed MIT (this repo)
- Analysis of GPLv3 implications of linking NXEngine-evo added to `PLAN.md § Licensing` and `THIRD-PARTY.md`

### Phase 0 — Prerequisites (gate passed: 22da27e)
- `tools/djgpp` symlink to `~/emulators/tools/djgpp` via `scripts/setup-symlinks.sh`
- `vendor/cwsdpmi/cwsdpmi.exe` + `cwsdpmi.doc` vendored (CWSDPMI r7, copied from sibling vellm)
- DJGPP 12.2.0 + binutils confirmed callable via `make djgpp-check`
- DOSBox-X 2025.02.01 confirmed installed and runnable headless

### Phase 1 — Toolchain smoke test (gate passed)
- `make hello` compiles `tests/smoketest/hello.c` with `i586-pc-msdosdjgpp-gcc -march=i486 -mtune=pentium -O2 -Wall`, producing `build/hello.exe` (150,962 bytes), stubedit'd to `minstack=256k` with autoload of `CWSDPMI.EXE`
- `make smoke-fast` — `[smoke] PASS` under `tools/dosbox-x-fast.conf` (`cycles=max`)
- `make smoke` — `[smoke] PASS` under `tools/dosbox-x.conf` parity config (`cycles=fixed 40000`)
- Headless DOSBox-X test harness via `tools/dosbox-run.sh` (silent + exit, captures `STDOUT.TXT` from the ephemeral C: mount)
- First automated correctness gate (per `CLAUDE.md § Correctness Gate` item 1) — passing

### Phase 2 — SDL3 for DOS (gate passed)
- `vendor/sources.manifest` (commit `2dfb737`) — all five upstream SHAs resolved out of `PIN_ME`: SDL `74a7462`, sdl2-compat `91d36b8`, SDL_mixer `2b00802`, SDL_image `67c8f53`, nxengine-evo `1f093d1`. `nxengine-evo` ref corrected `main` → `master` (upstream's actual default branch). SDL `74a7462` verified to contain PR #15377 — `build-scripts/i586-pc-msdosdjgpp.cmake`, `src/video/dos/`, `src/core/dos/` all present.
- `make sdl3` cross-builds SDL3 via DJGPP into `build/sysroot/lib/libSDL3.a` (2,054,564 bytes; 202 `.c.obj` members, 8 of them DOS-backend translation units; `SDL_AUDIO_DRIVER_DOS_SOUNDBLASTER=1` and `SDL_VIDEO_DRIVER_DOSVESA=1` confirmed in the generated `SDL_build_config.h`; no host-platform drivers leaked into the static archive)
- `make sdl3-smoke` — new DOS-backend probe and DOSBox-X harness. Builds `tests/sdl3-smoke/sdltest.c` (doskutsu-authored, MIT, public-API only — upstream test programs aren't reused because patching them would conflict with `vendor/SDL/CLAUDE.md`'s no-AI-code policy) into `build/sdl3-smoke/sdltest.exe` (DJGPP, `minstack=512k`); `tests/run-sdl3-smoke.sh` runs it under headless DOSBox-X and verifies six required output substrings (`SDLTEST-BEGIN:`, `AUDIO-DRIVERS: count=`, `AUDIO-DRIVER: 0 `, `VIDEO-DRIVER:`, `VIDEO-DISPLAYS: count=`, `SDLTEST-END:`). Video gate passes — 34 VESA modes enumerated including all four 320x240 variants (XRGB8888 / RGB565 / XRGB1555 / **INDEX8**, the last is the Phase 9 lever 3 target). Audio driver compiles + bootstraps but `SDL_Init(SDL_INIT_AUDIO)` device pick fails under DOSBox-X — see Known issues.
- `tests/run-smoke.sh` extended with `--contains STRING` and `--capture` (additive — existing `--expected` exact-match path for the Phase 1 hello smoke is unchanged)
- `tools/dosbox-run.sh` extended with `--merge-stderr` (additive — captures stderr alongside stdout, needed because SDL_Log writes to stderr and DOSBox-X's shell doesn't honor `2>&1` cleanly)
- `tests/fixtures/sdl3-modes-dosbox.txt` — VESA mode-list baseline captured under DOSBox-X for Phase 8 real-hardware diff against `M64VBE` on g2k

### Plan amendment — Path B: direct SDL3 migration (2026-04-24)
- **Phase 3 pivoted from sdl2-compat to direct SDL3 migration.** sdl2-compat is architecturally runtime-dynamic; static-linking is structurally infeasible without novel objcopy-rename infrastructure (Option A explored, rejected). Three blockers, all citable against the cloned trees: `vendor/sdl2-compat/CMakeLists.txt:96-98` (`FATAL_ERROR` Linux-only gate on `SDL2COMPAT_STATIC`); `vendor/sdl2-compat/src/sdl3_include_wrapper.h` (1,291 `IGNORE_THIS_VERSION_OF_*` rename macros, verified by `grep -c IGNORE_THIS_VERSION`); ~1,500 `SDL_*` multiple-definition collisions between sdl2-compat's re-declarations and SDL3's dynapi. Plus the architectural-intent comment at `vendor/sdl2-compat/src/sdl2_compat.c:372` (*"Obviously we can't use SDL_LoadObject() to load SDL3. :)"*) and the independent finding that SDL3's dynapi disables itself on DOS at `vendor/SDL/src/dynapi/SDL_dynapi.h:73-74` (`#define SDL_DYNAMIC_API 0  /* DJGPP doesn't support dynamic linking */`). See `PLAN.md § Plan Amendments § 2026-04-24` for the full reasoning.
- **New task graph.** Phase 3' (SDL3_mixer + SDL3_image, with new `make sdl3-mixer` / `make sdl3-image` targets), Phase 4' (NXEngine-evo SDL2 → SDL3 source migration + DJGPP port in four orthogonal sub-categories: 4'a audio refactor, 4'b mechanical renames, 4'c library swap, 4'd DOS-adaptation patches), Phase 5 narrowed to build/link/stubedit. The four 4' sub-categories name what each chunk of work is, not when it happens — engineers work in parallel; patches apply in numeric order. See `PLAN.md § Plan Amendments § 2026-04-24` for the decision record.
- **Operational specs added.** Two new docs ship with this amendment: `docs/SDL3-MIGRATION.md` (architectural brief for the migration — co-ownership boundary, audio-refactor implementation spec, the 5 deltas) and `patches/nxengine-evo/README.md` (patch-layout policy: numeric clusters `0001-0009` for DJGPP build adaptations + `0010-0019` for SDL2→SDL3 migration, reservation gaps, authoring order). Decision-record content stays in PLAN.md; implementation detail lives in those operational docs. PLAN.md amendment cross-references both.
- **Phase 4'a audio refactor (Path 1) — decisions captured here, scope deferred to operational docs.** The rewrite is co-owned by sdl-engine + nxengine on a symptom-based boundary (see `docs/SDL3-MIGRATION.md`). **Two-stage tripwire:** at N=4 working days raise hand for reassessment; at N=7 working days fall back to **Path 2 — custom SDL2_mixer-subset shim over `SDL_AudioStream`** (NOT Phase 9 lever 6, which stays reserved for SDL3-DOS audio backend *structural* failures only).
- **Threading-zero invariant (audit-confirmed).** NXEngine-evo's #27 audit found zero `SDL_CreateThread` / `std::thread` / worker threads in upstream source. The Path B rework maintains this invariant — no threading in port glue, in the audio refactor, or anywhere. SDL3-DOS uses a cooperative scheduler; spawning a thread breaks the model and is the first constraint we would violate. PLAN.md's amendment table carries this as a permanent invariant row, not a phase to complete.
- **Phase 4'd DJGPP-port patch series finalized as 5 patches** after two collapses: (a) `0006-disable-haptic` plus would-have-been gamepad/sensor/camera/touch/pen prophylactic patches dropped — zero references to any of those subsystems in NXEngine-evo per the #27 audit; (b) `0005-audio-init` folds into Phase 4'a's audio cluster — `Mix_OpenAudio` → `MIX_CreateMixer` is an SDL3_mixer API redesign, not a DOS-adaptation. Per-patch list in `patches/nxengine-evo/README.md § 0001-0005`.
- **`-fno-rtti` yes, `-fno-exceptions` no.** Phase 4'd `0002-dos-target-flags` finalized as `-march=i486 -mtune=pentium -O2 -fno-rtti`. RTTI is unused (zero `dynamic_cast`/`typeid` hits) so `-fno-rtti` is a pure code-size win. Exceptions are load-bearing: 6 `nlohmann::json` parse sites depend on exception propagation to convert "log + skip malformed asset, keep playing" into "abort" — bad trade for a modder-friendly port. CLAUDE.md § Critical Rules / NXEngine-evo specifics updated in lockstep.
- **CLAUDE.md updated** to reflect the post-pivot stack: architecture description drops sdl2-compat, "five-stage cross-build" → "four-stage cross-build", build-system block lists four upstream codebases (sdl2-compat annotated as cloned-but-unbuilt per the keep-but-unbuilt condition), Makefile-targets block drops `make sdl2-compat` and renames `make sdl2-mixer`/`make sdl2-image` to `make sdl3-mixer`/`make sdl3-image`.
- **Preserved.** `vendor/sdl2-compat` stays cloned at `91d36b8d` per software-architect's "keep cloned but unbuilt" condition. Tasks #18 (clone) and #19 (audit) remain completed in the tracker — the audit is the evidence for the pivot. Tasks #20, #21, #23, #24 deleted as obsolete. Task #16 (SB16-detection-fix patch authoring) remains active — Path B still uses SDL3's DOS audio backend directly, so the local-only `patches/SDL/0001-sb16-dsp-detection-fix.patch` work governs Phase 7 audio gate readiness.
- **Doc downstream pending.** `README.md`, `DOSKUTSU.md`, `BUILDING.md` still describe a "five-stage build" with sdl2-compat as the second stage; that drift will be reworked at the Phase 3' gate, not in this commit.

### Phase 5 — Build → DOSKUTSU.EXE (gate passed 2026-04-25: `484efa7`)

**The first end-to-end DJGPP + SDL3 build of NXEngine-evo.** `make nxengine` produces `build/doskutsu.exe` — 5,866,918 bytes, COFF + go32 + CWSDPMI autoload, `stubedit minstack=2048k` confirmed. CLAUDE.md § Correctness Gate item 2 met (build-link gate); the title-screen runtime gate folds into Phase 6 (asset extraction now in flight).

- **Build trajectory: 9 attempts, 0% → 1% → 8% → 70% → 96% → 100% PASS.** The trajectory tracks how each obstacle (find_package wiring, header search paths, link-line ordering, late-discovered upstream bugs, locale-stable patch ordering) was successively cleared. Per-attempt detail in build-engineer's Phase 5 closure notes / commit `484efa7` body.
- **Cumulative patch series: 25 local patches** across two repos, four cluster boundaries:
  - `patches/nxengine-evo/0001-0009` — DJGPP build adaptations (5 patches; reservation slots `0006-0009` partially filled by `0006-djgpp-spdlog-replacement.patch` from #35)
  - `patches/nxengine-evo/0010-0019` — SDL3-migration core (cluster filled including the reservation slot at `0019-cmake-find-package-sdl3.patch` from #36)
  - `patches/nxengine-evo/0020-0024` — Phase 5 follow-up patches (overflow from `0010-0019` cluster permitted by README rule's content-determined slotting)
  - `patches/SDL/0001-` — local-only SDL fixes per the 2026-04-25 patches-stay-local policy
  - **Final count: 24 nxengine-evo + 1 SDL = 25 local patches.**

**Pre-existing bug roundup — Path B's hypothesis paying out.** Path B was framed on the premise that *"we'd find bugs by actually doing the migration."* The migration framework forced careful reading of every relevant SDL2 call site, and **four real upstream pre-existing bugs** surfaced during patch cycles — each would have bitten us at runtime under SDL3. Without the migration's careful-reading discipline, these would have been runtime failures (likely months later, on real hardware, hard to diagnose). With it, they were patch-cycle catches:

1. **`main.cpp:306` — silent-init-failure under SDL3 bool return.** SDL2's `SDL_Init` returned `0` on success (`!=0` failure); SDL3 returns `bool`. The unchecked-return idiom silently passed under SDL3's "true means success" semantics, so the binary would have silently failed at the title screen with no diagnostic. **Caught in `0010-sdl3-mechanical-renames` by nxengine.**
2. **`ResourceManager.cpp:62` — `SDL_free` on const-owned `SDL_GetBasePath` return.** SDL2 returned writable storage; SDL3 returns a const-borrowed pointer that must not be freed. Calling `SDL_free` on it under SDL3 would have tripped the allocator. **Caught in `0010` by nxengine.**
3. **`Pixtone::shutdown` variable-shadow leaking ~80% of resampled buffers.** A shadowed variable name caused the cleanup loop to iterate over the wrong vector; ~80% of resampled buffers were leaked rather than freed. Under SDL2 this was a benign shutdown leak; under SDL3's stricter ownership, the parallel cleanup path would have produced `MIX_DestroyAudio` double-frees. **Caught in `0014-sdl3-mixer-pixtone-decoder` by sdl-engine, ratified by nxengine.**
4. **`Organya.cpp:260` — `std::clamp` template-deduction conflict on DJGPP `int32_t`.** DJGPP's `int32_t` is `long int`, not plain `int`; `std::clamp(int32_t_var, 0, 100)` triggers template deduction failure. Silent on Linux/macOS where `int32_t == int`; explicit failure on DJGPP. **Caught in `0024-audio-cluster-followups` by sdl-engine.**

**Build / test infrastructure.**
- `scripts/apply-patches.sh` now forces `LC_ALL=C sort` for locale-stable patch ordering. We hit the alpha-suffix sort trap during Phase 5 (locale-dependent ordering of `0014a-*.patch` relative to `0015-*.patch` produced different patch-application sequences across machines).
- `patches/nxengine-evo/README.md` updated with a "pure numeric slots only" rule (no alpha suffixes) plus an explicit note that cluster overflow into adjacent reserved ranges is acceptable when content-determined slotting requires it (the `0010-0019` cluster overflowing into `0020-0024` is the working example).

### Known build warnings (non-blocking)

- **`-Wformat=` warnings in `src/pause/options.cpp`** — `%d` used for `int32_t`, but `int32_t` is `long int` on DJGPP (should be `%ld`). Same root cause family as the `Organya.cpp:260` clamp finding (Phase 5 bug #4). Cosmetic; could land as a `0025-` follow-up patch or roll into Phase 9 cleanup.
- **`-Wunused-variable` in `Organya.cpp:188`** — `master_volume = 4e-6` declared but unused. Pre-existing latent. Cosmetic.

### Known issues
- **PR #15377 SoundBlaster detection fails under DOSBox-X SB16 emulation.** `SDL_Init(SDL_INIT_AUDIO)` errors out: DSP reset's "data ready" flag goes true but the byte read after reset is not `0xAA`. The audio driver itself compiles and links cleanly; only the runtime device pick under emulation fails. Tracked as task #16 — investigation + authoring of `patches/SDL/0001-sb16-dsp-detection-fix.patch` (local-only, per the SDL-patches-stay-local policy added 2026-04-25; see CLAUDE.md § Vendoring). Real-HW SB16 test deferred to Phase 8. Does not block Phases 3'–5; will be resolved before the Phase 7 playtest gate where audio is required.

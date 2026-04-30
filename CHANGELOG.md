# Changelog

All notable changes to DOSKUTSU are documented here. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versioning follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- **Wave 18 — direct-VESA hot path** (opt-in via `SDL_HINT_DOSKUTSU_DIRECT_VESA=1`, default OFF). Bypasses `SDL_RenderPresent` + `SDL_UpdateWindowSurface` and writes the framebuffer directly to LFB or banked VRAM via `dosmemput`. New SDL3-DOS public API: `SDL_DOSVesaDirectGetState`, `SDL_DOSVesaDirectGetGeneration`, `SDL_DOSVesaDirectPresentFull`, plus thin wrappers around `SwitchBank` / `BankedDosmemput` (`patches/SDL/0034`). Engine integration in `patches/nxengine-evo/0086`.
- `scripts/fetch-vendor-binaries.sh` + `vendor/binaries.manifest` for fetching the four DOS binaries (cwsdpmi.exe, lfndos.exe, doslfn.com, doslfnms.com) on demand from pinned URLs + sha256 hashes. Mirrors the existing `vendor/sources.manifest` discipline. The `make stage` / `make dist` / `make install` / `make dpmi-lfn-smoke` targets auto-invoke it as an order-only prerequisite.
- `make fetch-binaries` target.

### Changed

- `BUILDING.md` moved to `docs/BUILDING.md` so all build/contributor docs live under `docs/`. References updated in `README.md`, `Makefile`, and `scripts/setup-symlinks.sh`.
- README "Status" section now inlines the user-facing performance summary (current real-HW fps, optimization story, remaining levers) so the public face of the project reads cleanly without cross-references to internal team-coordination docs.

### Performance

- **Real-HW measurement on g2k Pentium OverDrive 83 MHz + Cirrus CL-GD5434 + UNIVBE 6.70:** wave-18 direct-VESA delivered **+0.65 fps** on the LFB path (`FORCE_LFB=1` + `DIRECT_VESA=1` → 26.27 fps title) and **+0.27 fps** on the default banked path (`DIRECT_VESA=1` only → 25.89 fps title). Both below the +2-5 fps prediction. The implementation is correct (100/100 successful presents per phase block, zero failures, snapshot generation stable) — the savings are just small because the VRAM write itself dominates the original SDL `present` cost, not SDL dispatch glue.
- **Conclusion:** SDL_RenderPresent + UpdateWindowSurface dispatch glue is ~1.1 ms/flip on title (not the ~5 ms the wave-16.3 modeling suggested). The +20-30 fps to 50 fps must come from the framebuffer flush itself, not SDL-layer surgery — i.e., from chip-specific hardware-blitter offload (Cirrus CL-GD5434 BitBlt engine).

### Removed

- Vendored DOS binaries (`vendor/cwsdpmi/cwsdpmi.exe`, `vendor/lfndos/lfndos.exe`, `vendor/doslfn/{doslfn,doslfnms}.com`) are no longer tracked in git. Use `make fetch-binaries` (or `scripts/fetch-vendor-binaries.sh`) to populate them from upstream. The accompanying license/`.doc`/`.lsm` files remain tracked because their respective licenses require redistribution alongside the binaries.
- `tests/phase8-runs/` (~13 MB of phone-photo JPEGs from g2k runs) removed from tracking. Run captures now live at `/tmp/wave-N/` per the iter loop convention.
- Internal team-coordination plan docs (`docs/PHASE9-NEXT-WAVES-PLAN.md`, `docs/PHASE9-WAVE-14-PLAN.md`, `docs/PHASE9-WAVE-17-18-PLAN.md`, `docs/PHASE9.md`) removed from tracking. Pattern-based `.gitignore` (`/docs/PHASE*-PLAN.md`, etc.) prevents accidental re-tracking.

### Repository

- Git history rewritten via `git filter-repo` (force-pushed 2026-04-30) to purge the above binary artifacts and internal plan docs from prior commits. All commit SHAs from the project's earliest history forward are new. Prior tag `v0.1.0` recreated to point at the rewritten merge commit.

---

## [0.1.0] — 2026-04-30 (Phase 9 — Wave 17 release)

**First playable release.** Real-HW title fps **25.6 fps on the reference Pentium OverDrive 83 MHz / Cirrus CL-GD5434 / UNIVBE 6.70 / Vibra16 hardware** (g2k machine), measured on this binary's PLAY1 release-production config. Cumulative gain since Phase 8 baseline: **0.47 → 25.6 fps = 54× improvement**. Cave Story is now perceptually playable on Pentium-class real DOS hardware.

**Release binary:** `DOSKUTSU.EXE` sha256 `88320e55adf8a79ff0a882d566a80d8b93f153d507726fc3ada85bd2744e21aa`, 6,022,940 bytes, built 2026-04-30 from Phase 9 wave 17.6 patch series.

### Highlights

- Title screen renders cleanly with cloud parallax animation, menu, and decorations
- Music + audio with occasional minor pauses (improves with LFB; full audio polish backlogged)
- Keyboard navigation works after initial ~30s IRQ-1 first-fire delay (parked finding; held keys are reliable workaround)
- Bottom-strip-black bug fixed (incidentally by wave 17.3 cache extension; root cause was legacy `SDL_RenderTexture` clip path, not chip behavior)
- Menu black-boxes around triangle decorations fixed (wave 17.4 alpha cheap fix)
- LFB direct-write path available as opt-in for Cirrus chips with working LFB exposure (`SDL_HINT_DOSKUTSU_FORCE_LFB=1`, +0.4 fps)

### Wave 17 release-candidate iter (final, 2026-04-30) — PLAY1/PLAY2/PLAY3 measurements

| Pass | Config | Real-HW title fps | Notes |
|---|---|---|---|
| PLAY1 (default) | no env vars set | **25.6** | Production config; backdrop cache active, prev-frame diff disabled |
| PLAY2 (opt-in) | `SDL_HINT_DOSKUTSU_FORCE_LFB=1` | 26.0 | LFB direct-write engaged; +0.4 fps lift |
| PLAY3 (experimental) | `FORCE_LFB=1 + PREVFRAME_DIFF=1` | 17.6 | Partial-flush stacked on LFB — regressed; partial-flush stays opt-in only |

PLAY3 conclusively closes the partial-flush direction: same -7-8 fps regression magnitude on LFB as on banked (waves 17.4 banked = -7.1, 17.5 banked = -7.4, this iter LFB = -8.0). The unmodeled cost is in SDL3-DOS's `UpdateWindowSurfaceRects` per-rect dispatch, not bank-switching. No wave 17.7 follow-up; partial-flush mechanism retained as opt-in (`SDL_HINT_DOSKUTSU_PREVFRAME_DIFF=1`) for users on different hardware that might behave differently, but default OFF.

### Phase 9 — Wave 17 release detail

**Cumulative since Phase 9 start:** real-HW title fps **0.47 → 25.6** (54× improvement). Visual + audio + input all functional. Cumulative patch series: ~85 local patches across SDL3-DOS + NXEngine-evo. Per-flip budget at LFB-forced PLAY2 of wave 16.3: 16.4 ms drawcalls + 16.7 ms VRAM flush + 5.0 ms audio/engine glue.

**Real-HW measurement environment:** Gateway 2000 Pentium OverDrive 83 MHz ("g2k"), Cirrus Logic CL-GD5434 PCI 1MB, SciTech UNIVBE 6.70, Vibra16 (SB16-class), MS-DOS 6.22, CWSDPMI r7. All Phase 9 fps numbers below are real-HW measurements.

#### Wave 14 — decoupled engine tick (parked)
- Investigation only; engine-tick decoupling was scoped but parked because input-IRQ delivery was broken (parked finding from wave 13.6.1 — see `phase9_framerate_first.md`). Code paths left intact for post-50fps polish phase.

#### Wave 15 — dirty-rect re-enable via Lever B per-blit destination tracking (default ON, kill via `SDL_HINT_DOSKUTSU_DIRTY_RECTS=0`)
- `patches/nxengine-evo/0071-perf-wave-15-dirty-rect-reenable.patch` re-implements dirty-rect tracking on top of Lever B's per-blit destination tracking. Engine accumulates per-drawcall rects; threshold gate (50% screen) decides partial-vs-full flush.
- Real-HW result: title 10.0 → 13.2 fps. Mechanism is correct but engine's `clearScreen` and `map_draw_backdrop` tile loop both contribute full-screen-ish dirty rects every frame, so the path engages rarely on title — sets up the cache work in wave 17.
- Killswitch `SDL_HINT_DOSKUTSU_DIRTY_RECTS=0` retained.

#### Wave 16 — 8bpp INDEX8 mode + master palette + cmod LUT (the bandwidth-halving wave)
- Switches the production pixel format from RGB565 to INDEX8 8bpp. Cuts framebuffer bytes/frame from 153,600 to 76,800 (-50%).
- Patches `patches/nxengine-evo/0072..0078` introduce `SDL_HINT_DOSKUTSU_PIXEL_FORMAT_8` (default ON, kill via `=0`), Surface INDEX8 fastpath, master palette + per-asset remap LUT, cmod color-mod LUT, and the colorkey-bleed fix.
- Host tooling: `tools/build-master-palette.py` generates `data/master.pal` (768 B) + `data/master.map` (5,383 B; 8.3-clean rename from `master.palmap`).
- Real-HW result: title 13.2 → 25.9 fps (+12.7) — the largest single-wave gain in Phase 9. Lever B / 8bpp / palette / cmod-LUT stacked.
- Wave 16.1 / 16.2 / 16.3 / 16.4 — diagnostic + experiment subseries (see below).

##### Wave 16.1 — banked-flush sentinel diagnostic (`patches/SDL/0028-perf-wave-16-1-banked-flush-sentinel.patch`)
- Adds `SDL_HINT_DOSKUTSU_BANK_SENTINEL=1` (default OFF, opt-in diagnostic). On enable, every 100 banked-mode flushes writes a 16-byte sentinel pattern to bank-1 offset 0 via the production SwitchBank+dosmemput path, then reads it back.
- Real-HW result: `match=1` consistently across all sample iters. **Definitively refutes "banked-mode bank-switch broken on Cirrus" hypothesis.** Bank-switch + dosmemput round-trip works correctly.

##### Wave 16.2 — BankedDosmemput offset rewrite (`patches/SDL/0029-perf-wave-16-2-banked-flush-offset-fix.patch`)
- Defensive rewrite of `BankedDosmemput`'s bank-1 offset math (explicit `bank++ ; off_in_win = 0` counters instead of modulo/division). Originally hypothesized as the bottom-strip-black bug fix.
- Real-HW result: NO-OP. Pre-rewrite math was already correct (computed `src_off=65536 dst_off=0 bytes=11264`). Bank-1 readback still showed all-zeros after the fix landed. Refutes "BankedDosmemput offset arithmetic wrong" hypothesis. Patch retained as a code-clarity improvement; not a behavior change.

##### Wave 16.3 — LFB-force override + LFB sentinel (`patches/SDL/0030`, `0031`)
- `0030` adds `SDL_HINT_DOSKUTSU_FORCE_LFB=1` (default OFF, opt-in) which overrides SDL/0019's "auto-disable LFB on Cirrus" check. Engages LFB direct-write path on Cirrus chips that have working LFB exposure.
- `0031` adds `SDL_HINT_DOSKUTSU_LFB_SENTINEL=1` (default OFF, opt-in diagnostic). One-shot fires on first flip after modeset; samples 5 LFB offsets (0, 65535, 65536, 76799, 131072) — write+readback through same LFB pointer.
- Real-HW results:
  - Force-LFB: title 25.8 → 26.2 fps (+0.4 fps over banked baseline). LFB direct-write is faster than banked dosmemput; bandwidth-bound on PCI write speed (~4.6 MB/s on this Cirrus).
  - Sentinel: ALL FIVE offsets `match=1`. **Definitively refutes "LFB aperture lies past 64KB" hypothesis.** LFB writes work at any offset including past the 64KB boundary.
- Both env vars retained as opt-in; FORCE_LFB is a real perf win for users on Cirrus chips that have working LFB.

##### Wave 16.4 — VBE 0x4F07 + Cirrus chip-direct CRTC display-start fix (`patches/SDL/0032-perf-wave-16-4-display-start-fix.patch`)
- Adds `SDL_HINT_DOSKUTSU_DISPLAY_START_FIX=1` (default OFF, opt-in). Two sequential approaches: VBE 0x4F07 BL=0x80 first, then Cirrus CL-GD5434 CRTC chip-direct register manipulation if VBE rejects.
- Real-HW result: NO-OP. VBE 0x4F07 returned `ax=0x004F ok` but `crtc_pre == crtc_post` for all five logged register indices (0x0C/0x0D/0x13/0x1A/0x1D). Display-start was already 0; pitch register was already correct (0x28 = 40 = 320/8). **Definitively refutes "display controller scan-cap" hypothesis.** Patch retained for other Cirrus configs that may have wrong-register initial state.

#### Wave 17 — backdrop cache + menu visual fix
The fps gains came from waves 17.2 / 17.3 (cache infrastructure) and the visual win came incidentally from wave 17.3's bypass of the legacy SDL_RenderTexture path. Waves 17.4 / 17.5 attempted partial-flush but regressed and are default-OFF in this release.

##### Wave 17.1 — dirty-rect investigation (reverted in 17.3)
- Instrumentation patch revealed that the wave-15 dirty-rect path's coalesce-on-overflow branch always collapses 80+ small per-tile rects into a screen-spanning bounding box. Investigation finding adopted; instrumentation reverted because it added ~0.9 ms/flip overhead with no production value.

##### Wave 17.2 — `map_draw_backdrop` cache (`patches/nxengine-evo/0080-perf-wave-17-2-cache-backdrop-render.patch`)
- Caches the rendered backdrop into a single ~76 KB Surface (lazy-allocated, freed in dtor). `BK_FIXED` scenes (static backdrops) hit the cache 100%. Cache key = `(curmap, backdrop_id, scrolltype, parscroll_x, parscroll_y, indexed_mode_flag, format, dimensions)`.
- Killswitch `SDL_HINT_DOSKUTSU_BACKDROP_CACHE=0` (default ON, kill via `=0`).
- Real-HW result: title fps unchanged (title uses `BK_FASTLEFT_LAYERS`, bypassed by the simple-backdrop path). Cache infrastructure foundation for wave 17.3.

##### Wave 17.3 — extend cache to BK_FASTLEFT_LAYERS (`patches/nxengine-evo/0081-perf-wave-17-3-cache-fastleft-layers.patch`)
- Extends wave 17.2's cache to handle the layered scrolling backdrop used by Cave Story's title screen. New `_render_layered_to_surface` uses raw `SDL_BlitSurface` instead of legacy `blitPatternAcross` (which goes through SDL_RenderTexture queue).
- Real-HW result: title fps `dc_ms` unchanged but bottom-strip-black bug INCIDENTALLY FIXED. The legacy `blitPatternAcross` path was failing to write pixels to the bottom 35 scanlines (cause: SDL_RenderTexture clip-rect or coordinate-truncation bug we didn't fully root-cause). Wave 17.3's bypass writes all pixels everywhere → bottom strip renders.
- Net real-HW: title 25.0 → 25.7 fps after also reverting wave-17.1 instrumentation.
- **Visual win:** clouds fill the full 240 scanlines on title for the first time since the bug emerged in early Phase 8. Confirmed by user real-HW report 2026-04-29.

##### Wave 17.4 — engine-side prev-frame diff (`patches/nxengine-evo/0082`, `patches/SDL/0033`) — REGRESSED
- Engine maintains a prev-frame Surface, runs per-scanline `SDL_memcmp` to compute REAL dirty rects, passes to `SDL_UpdateWindowSurfaceRects`. SDL/0033 makes the `pitches_match` flush branches actually honor the rect list (previously ignored rects and dosmemput'd full frame).
- DOSBox-X smoke showed 70% partial-flush engagement at 44% avg dirty area — predicted +5-10 fps.
- Real-HW result: **-7.1 fps regression** (25.7 → 18.5). Per-rect dosmemput overhead (~3-5 ms × 2-4 rects/flush) dominated the bytes-saved win. Bytes-saved math correct; cost model missed per-call DPMI/banked setup overhead.
- Patches retained, default flipped to OFF in wave 17.6.

##### Wave 17.5 — bbox partial-flush (`patches/nxengine-evo/0084-perf-wave-17-5-bbox-partial-flush.patch`) — REGRESSED
- Replaces wave-17.4's per-rect pattern with ONE dosmemput of the dirty bounding box. Trades fewer-bytes-more-calls for more-bytes-fewer-calls.
- DOSBox-X smoke: bbox lands 45-46% on title, 0/100 frames trip 75% threshold.
- Real-HW result: **-7.4 fps regression** (25.7 → 18.3). Same magnitude as wave 17.4. Effective bandwidth: full-flush 4.4 MB/s, bbox-flush 1.07 MB/s. Per-byte cost is ~4× worse for partial vs full regardless of rect-vs-bbox structure — unmodeled cost in SDL3-DOS partial dispatch path or our diff CPU.
- Patch retained, default OFF as of wave 17.6.

##### Wave 17.6 — flip prev-frame-diff default to OFF (release prep, `patches/nxengine-evo/0085-perf-wave-17-6-prev-frame-diff-default-off.patch`)
- Changes `SDL_HINT_DOSKUTSU_PREVFRAME_DIFF` polarity from opt-out (default ON, regressed) to opt-in (default OFF). Production behavior matches wave-17.3 baseline.
- Wave 17.4/17.5 mechanisms stay in tree as opt-in for users to experiment.

#### Visual fixes (wave 17 cluster)

- **Bottom-strip-black on real HW Cirrus banked mode** — clouds bottom 35 scanlines were rendering as black. Originally hypothesized as banked-mode flush / chip scan-cap; correct cause was legacy SDL_RenderTexture clip-rect bug. Fixed incidentally by wave 17.3's raw-SDL_BlitSurface bypass.
- **Menu black-boxes around triangle decorations** (`patches/nxengine-evo/0083-fix-menu-alpha-opaque-on-index8.patch`) — Cave Story title menu's textbox renders alpha=210 (semi-transparent). In INDEX8 mode this falls through both fast paths into SDL_RenderTexture which silently drops colorkey for INDEX8+alpha+colorkey combo. Cheap fix: literal `210 → 255` in `TextBox::DrawFrame()` at three call sites. Loses cosmetic semi-transparency but original Cave Story shipped fully opaque. Proper fix (`_blit_indexed_alpha` primitive mirroring wave-12.5's RGB565 sibling) deferred to polish phase per `memory/sdl3_index8_alpha_colorkey_bug.md`.

#### Hypothesis tree corrections (this session)

The bottom-strip-black bug went through three wrong interpretations before the right answer surfaced:
- ❌ H-A: display-start origin offset (refuted by audit)
- ❌ H-B: display controller scan-cap at 64KB (refuted by wave-16.4 — registers were already correct)
- ❌ H-C: bank-switch broken (refuted by wave-16.1 sentinel match=1)
- ❌ H-D: aperture-lies-past-64KB (refuted by wave-16.3 sentinel all-five-offsets match=1)
- ✅ Actual: legacy `blitPatternAcross` was simply not writing those pixels. Discovered when wave-17.3's bypass incidentally fixed it.

Lesson: always question "is the engine actually writing those pixels?" before blaming the chip/display side. See `memory/lfb_sentinel_result_hypothesis_b.md`.

#### Known issues (parked for polish phase)

- Keyboard IRQ-1 first-fire delay ~30s after launch (parked from wave 13.6.1)
- Keyboard buffer overflow after ~10 keypresses on real HW (PC speaker beeps; engine drains BIOS buffer at 1 key/flip)
- Audio chop in cmod-heavy scenes (improves with higher fps; LFB-engaged config slightly smoother than banked)
- Graphics options menu shows "640x480" (cosmetic UI bug; actual mode is 320×240 per modeset log)
- Other alpha<255 + colorkey + INDEX8 sites likely affected by SDL3 SW renderer bug — full audit pending in polish phase

#### Documentation

- `docs/BOOT.md` — env var reference updated with all wave 16.x and 17.x hints; PREVFRAME_DIFF moved to dev/diagnostic section after wave 17.6 default-OFF flip

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

### Phase 7 prep — DJGPP data path resolution + runtime staging + engine-data extractor (2026-04-25)

The first end-to-end title-screen launch surfaced a fatal at graphics init: `Error opening font file $REPO_ROOT/build/sysroot/share/nxengine/data/font_1.fnt`. The DOS binary was attempting to `stat()` a Linux-host absolute path baked in at CMake time. Fixing this exposed two adjacent gaps — no co-located runtime layout for testing, and no extractor for the two `Doukutsu.exe`-embedded blobs NXEngine consumes at runtime. All three landed together as Phase 7 preparation.

- **`patches/nxengine-evo/0025-cmake-djgpp-data-path.patch`** — gates the `IF(UNIX_LIKE)` block in NXEngine-evo's `CMakeLists.txt` on `AND NOT DJGPP`. DJGPP cross-from-Linux inherits CMake's `UNIX=1`, so without the gate `HAVE_UNIX_LIKE` and a Linux-host absolute `DATADIR` were both being baked into the DOS binary. With the patch, `ResourceManager::getPath()` falls through to `SDL_GetBasePath() + "data/"`, the DOS-portable layout that matches every other NXEngine-evo platform (Win32 zip, Vita, Switch) and matches the `C:\DOSKUTSU\DATA\` CF-card install target.
- **`make stage` target + `tools/dosbox-launch.sh --stage` flag.** New `Makefile` "Runtime staging" target produces `build/stage/` containing `DOSKUTSU.EXE` + `CWSDPMI.EXE` + a `data/` symlink — the layout NXEngine expects post-patch-0025. The launcher's new `--stage` / `-s` flag runs `make stage` automatically and mounts `build/stage/` as C: instead of the repo root, so real game runs (title screen, playtest, asset-loading smoke runs) get the correct co-located layout. Plain `tools/dosbox-launch.sh` is unchanged and remains correct for SDL-driver probes that don't touch `data/`.
- **`scripts/extract-engine-data.py`.** Doskutsu-authored sibling of `scripts/extract-pxt.py`; produces `data/wavetable.dat` (25600-byte Organya PCM table from offset `0x110664` in the 2004 EN freeware `Doukutsu.exe`) and `data/stage.dat` (6936 bytes, 95-record stage index from offset `0x937B0`, transcribed from `vendor/nxengine-evo/src/extract/extractstages.cpp`). Operates on file offsets only — no Rust toolchain, no DJGPP cross-build, runs on the dev host. `docs/ASSETS.md` Step 3 + verification block updated to cover both files.

### Phase 7 — Title screen renders to framebuffer (gate passed 2026-04-25: `7372374`)

**The framebuffer wall closes.** `make stage` + `tools/dosbox-launch.sh --stage` brings `DOSKUTSU.EXE` up under DOSBox-X to a visible 320×240 NXEngine title screen — the first time the game has produced visible output in DOSBox-X. Six diagnostic + workaround patches in `patches/nxengine-evo/` plus one in `patches/SDL/` got us here. CLAUDE.md § Correctness Gate item 2's runtime gate is met; item 3 (playable content) and the Phase 7 stability gate (#23) defer to real-HW (g2k) testing per Phase 8.

**Diagnostic ladder, in patch order.** Each rung either ruled out a hypothesis or supplied a workaround. `0030` + `0032` are load-bearing; `0027` / `0028` / `0029` / `0031` + `SDL/0002` are defensive or diagnostic.

- **`patches/nxengine-evo/0027-sdl3-renderer-logical-presentation.patch`** — adds `SDL_SetRenderLogicalPresentation(_renderer, 320, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX)` after `SDL_CreateRenderer`. SDL2's implicit `SDL_RenderSetLogicalSize` letterboxing is gone in SDL3; without the explicit opt-in the renderer paints into a 320×240 corner of the framebuffer with no scaling. Structurally correct; *not load-bearing for the wall* (the wall sat further down the pipeline). **Defensive.**
- **`patches/nxengine-evo/0028-log-null-texture-from-silent-create-sites.patch`** — surfaces NULL from the two previously-silent `SDL_CreateTextureFromSurface` sites (`Renderer::initVideo` `_spot_light`, `Font::load` atlas pages). Bisect outcome: both creates succeed; the NULL source is elsewhere. **Diagnostic.**
- **`patches/nxengine-evo/0029-sdl3-set-invalid-param-checks-hint-dos.patch`** — programmatic `SDL_SetHintWithPriority(SDL_HINT_INVALID_PARAM_CHECKS, "0", SDL_HINT_OVERRIDE)` before `SDL_Init`. Tested whether the env-var path was being missed by SDL3's hint-callback init order. Confirmed the env arrives intact and that the "Parameter 'texture' is invalid" flood was NULL-pointer attribution, not validation-hash misses. **Diagnostic.**
- **`patches/nxengine-evo/0030-log-null-texture-in-drawsurface.patch`** — explicit NULL checks + early-return + throttled per-Surface logging at `Renderer::drawSurface` / `drawSurfaceMirrored` / `blitPatternAcross`, replacing the Release-build no-op `assert(src->texture())` that let NULL textures fall through to `SDL_RenderTexture`. Throttles the diagnostic flood **and** confirmed the second cause: even with the NULL flood throttled, the framebuffer stayed pure black, proving an independent present-pipeline bug. **Load-bearing.**
- **`patches/SDL/0002-debug-dosvesa-framebuffer-trace.patch`** — instruments `DOSVESA_UpdateWindowFramebuffer` to discriminate fast-path-guard failure / source-buffer empty / write-but-no-display modes. Output goes to `sdldbg.log` (SDL_Log's stderr-redirect doesn't survive DOSBox-X's shell). Confirmed: 900+ flush calls, all hit normal-path, all return success, framebuffer never paints. Local-only per the patches-stay-local policy. **Diagnostic.**
- **`patches/nxengine-evo/0031-log-renderpresent-and-per-flip-drawcalls.patch`** — `SDL_RenderPresent` return-value check + per-flip drawcall counter in `Renderer::flip()`. Confirmed 1000/1000 present calls return success and drawcall count is non-zero — so the engine is rendering and the present is succeeding; the bug lives below `SDL_RenderPresent` in SDL3-DOS's normal-path framebuffer flush. **Diagnostic.**
- **`patches/nxengine-evo/0032-sdl3-dos-fast-framebuffer-hint.patch`** — programmatic `SDL_SetHintWithPriority(SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER, "1", SDL_HINT_OVERRIDE)` before `SDL_Init`. Opts into SDL3-DOS's fast-path framebuffer flush (system-RAM surface → VRAM via `dosmemput`/`memcpy`; no cursor compositing / palette sync / vsync / page flipping — none of which we need: no software cursor, single surface format, Pentium-class hardware can't hold vsync anyway). **Load-bearing — flipping this hint is what makes the framebuffer paint.**

**Latent SDL3-DOS bug acknowledged but not blocking** (task #24). Patch `0032`'s workaround is empirically what unblocks the framebuffer, but the actual mechanism is **not** "fast-path engagement" — `SDL/0002`'s trace showed 900+ post-flip flushes still hit the normal path. The hint flip causes a side-effect during `fb_state` init that establishes a usable framebuffer for the normal path; without the flip, `fb_state` is left in a state where every normal-path flush silently writes nowhere visible. Root-causing the init-state-leak in upstream SDL3-DOS is **no-action by policy** — local workaround is in place; the upstream bug is not the project's to fix.

**Phase 7 stability gate (#23): deferred.** The 30-min DOSBox-X playthrough is no longer the Phase 7 critical path. DOSBox-X's framebuffer-flush path is known to diverge from real VESA hardware (per task #24's findings) and the 30-min DOSBox-X run does not exercise the path that matters. Real-HW (g2k) per Phase 8 is now the primary stability gate. The visible-title-screen smoke (`tests/run-gameplay-smoke.sh`) is the new automation gate.

**`tests/run-gameplay-smoke.sh`** — new automated smoke. `make stage` + headless `tools/dosbox-launch.sh --stage`, `pkill -x dosbox-x` after a known-good interval, then `grep` the post-exit `build/stage/debug.log` + `sdldbg.log` for expected init markers + absence of `drawSurface failed` / `Failed to initialize` errors. Replaces the manual screenshot loop that drove the `0027`–`0032` + `SDL/0002` triage. `tests/GAMEPLAY-SMOKE.md` carries the baseline expectations + grep-pattern catalog the script enforces. Per the new "DEBUG.LOG only flushes on DOSBox-X exit" finding (CLAUDE.md § SDL3 DOS backend quirks), the smoke must `pkill` *before* grep'ing — mid-run reads return 0 bytes.

**Real-HW test bundle** — `/tmp/doskutsu-cf-2026-04-25.tar.gz` produced for the g2k Phase 8 run (DOSKUTSU.EXE + CWSDPMI.EXE + the extracted `data/` tree, layout matching `C:\DOSKUTSU\` on the CF target). User-facing artifact for the next-session pickup; not in `dist/` (that target is the dist zip with license bundle, scoped to task #12).

### Phase 8 — Real-hardware close (functional gate passed 2026-04-26)

**The first end-to-end run of `DOSKUTSU.EXE` on real Pentium-class DOS hardware.** Engine boots, NXEngine-evo runs through full init, SDL3 software renderer reaches the title-state gameloop, the title screen renders, and the opening cutscene plays back recognizably (Cave Story prologue scene with First Cave pillars and Quote/Sue characters, captured via CRT photo). All on a Gateway 2000 Pentium OverDrive 83 MHz with an ATI Mach64CT PCI video card (with M64VBE.COM TSR loaded), a Sound Blaster 16-class card (Vibra16S CT2490), and CWSDPMI r7. CLAUDE.md § Correctness Gate item 4's functional half is met.

**Performance gate not closed — known limitation.** Real-HW measurement: ~0.47 fps title / ~0.26 fps cutscene. Bottleneck cleanly diagnosed (banked-mode VESA flush, ~614 KB/frame through a 64 KB bank window) and the next-step optimization (LFB lifecycle fix in SDL3-DOS) is documented and scoped. See `docs/PHASE9.md` for the full optimization roadmap and realistic ceiling.

**Patches landed during Phase 8** — diagnostic instrumentation that turned the "blue screen, no information" symptom into a fully attributed perf bottleneck, plus one perf fix (16 bpp cap):

- **`patches/nxengine-evo/0036-djgpp-fsync-debug-log-per-line.patch`** — per-line `fsync(fileno(djgpp_log_fp))` after `fflush` in NXEngine's logger. fflush only commits libc → OS; DOS BUFFERS plus the CF card's write cache hold both data and the directory entry recording new file size, so a hard-reset-after-wedge drops both. fsync routes to DOS INT 21h fn 68h Commit-File, propagating data + metadata to media. Crash-survivable logs are what made the rest of the investigation tractable.
- **`patches/nxengine-evo/0037-diag-env-gated-no-audio-bypass.patch`** — `SET DOSKUTSU_NO_AUDIO=1` skips `SoundManager::init()` so the engine progresses past any SB DSP probe wall. Used to rule out audio init as the perf bottleneck (it wasn't).
- **`patches/nxengine-evo/0038-diag-gameloop-runtick-heartbeat.patch`** — entry log + per-iteration heartbeat in `gameloop()` and `run_tick()`. Confirmed the engine is alive and the gameloop runs the full input/tick/flip/runFade cycle, narrowing the bottleneck to `flip()` itself.
- **`patches/nxengine-evo/0039-diag-time-renderpresent-prune-runtick-bookends.patch`** — times `SDL_RenderPresent` directly with `SDL_GetTicks` brackets and logs every 10th flip with present_ms + drawcall count. The instrumentation that produced the actual perf measurement (~2.8 s/flip at 24 bpp baseline; ~2.14 s after lever 0).
- **`patches/nxengine-evo/0041-perf-cap-vesa-mode-bpp-at-16.patch`** — sets `SDL_HINT_DOS_MAX_BPP="16"` at OVERRIDE priority before `SDL_Init`. Picks VESA mode 0x111 (640×480×16 bpp pitch=1280) instead of 0x112 (640×480×24 bpp pitch=2560) on Mach64. Per-frame VRAM bytes 921 KB → 614 KB. **Measured 1.30× speedup on real HW.** Companion to the SDL hint definition below.
- **`patches/SDL/0003-debug-vbe-controller-mode-info-dump.patch`** — one-shot dump of VBE controller info, full enumerated mode list, and per-mode-set attributes to `/sdldbg.log`. The probe that revealed Mach64 picks 24 bpp by default and uses banked VESA writes despite LFB being available.
- **`patches/SDL/0004-debug-fsync-sdldbg-log.patch`** — same `fsync` discipline applied to `patches/SDL/0002 + 0003`'s diagnostic file output. SDLDBG.LOG was 0 bytes after the first hard-reset capture; this fixed it.
- **`patches/SDL/0006-vbe-max-bpp-hint.patch`** — adds `SDL_HINT_DOS_MAX_BPP` to SDL3-DOS. Numeric cap on VESA mode bpp during enumeration; "16" skips 24/32 bpp modes, "8" filters to INDEX8 only (palette-only games). Companion to NXEngine `0041`. Local-only per the SDL-patches-stay-local policy.

**Patches attempted during Phase 8 but reverted** — kept here as an evidence trail of what was tried:

- **`patches/SDL/0005-debug-sdldbg-log-next-to-exe.patch`** — would have written SDLDBG.LOG alongside DOSKUTSU.EXE (using `SDL_GetBasePath`) instead of at the C: root. Reverted because `SDL_GetBasePath()` called from VESA init in `DOSVESA_CreateDevice` (early in `SDL_Init`) hit `searchpath(NULL)` on DJGPP — `SDL_argv0` isn't set yet for binaries using plain `main()` rather than the `SDL_RunApp` shim. Hard-hung the system on first real-HW boot, no logs produced. Reverted to the C:-root path.
- **LFB-prefer hint (would have been `patches/SDL/0007` + `patches/nxengine-evo/0042`)** — would have flipped SDL3-DOS's banked-vs-LFB preference to use LFB on chipsets where both are available. Mode-set respected the hint correctly under DOSBox-X (`use_lfb=1 banked=0`), but the fast-path init didn't engage: SDL3-DOS does two mode-sets during init and only re-creates the window framebuffer for the first; the second invalidates `fb_state` without re-populating it. Patch 0032's hint flip masks this for banked mode (where `vram_phys=0xA0000` is mode-independent) but breaks for LFB (where `vram_ptr` depends on `data->mapping.address` from the LFB-flagged mode-set). Real fix requires SDL3-DOS surface-lifecycle work — deferred to Phase 8.5 / Phase 9 lever 1. Reverted both patches without shipping.

**Diagnostic infrastructure added to the build:** patches/SDL/0002 + 0003 + 0004's combined output (`SDLDBG.LOG`) gives a complete picture of SDL3-DOS's framebuffer state every boot — VBE controller info, full mode list, per-mode-set attributes, fb_state init values, per-flush write evidence. NXEngine's 0036 + 0038 + 0039 give the application-side gameloop and per-flip timing. Together they enabled the Phase 8 diagnosis and they stay applied for the next iteration's measurements.

**Real-HW capture artifacts:** committed to `tests/phase8-runs/<date>-<tag>/`:
- `tests/phase8-runs/2026-04-26-cutscene-renders/` — first successful real-HW run, 24 bpp baseline (~2.8 s/flip), CRT photos showing Cave Story title + opening cutscene rendering correctly.
- `tests/phase8-runs/2026-04-26-16bpp-perf/` — lever 0 measurement (~2.14 s/flip, 1.30× speedup).

**Documentation added:** `docs/PHASE9.md` carries the full optimization roadmap (levers 1-6, projected speedups, realistic ceiling, recommended order). `README.md` gains a `## Status` section noting current real-HW perf state and a link to PHASE9.md for the optimization story. Internal investigation log lives in `docs/PHASE8-FINDINGS.md` (gitignored — kept on local dev disk).

### Known issues
- **PR #15377 SoundBlaster detection fails under DOSBox-X SB16 emulation.** `SDL_Init(SDL_INIT_AUDIO)` errors out: DSP reset's "data ready" flag goes true but the byte read after reset is not `0xAA`. The audio driver itself compiles and links cleanly; only the runtime device pick under emulation fails. Tracked as task #16 — investigation + authoring of `patches/SDL/0001-sb16-dsp-detection-fix.patch` (local-only, per the SDL-patches-stay-local policy added 2026-04-25; see CLAUDE.md § Vendoring). Real-HW SB16 test deferred to Phase 8. Does not block Phases 3'–5; will be resolved before the Phase 7 playtest gate where audio is required.
- **Real-HW perf is ~0.47 fps title on the Pentium OverDrive 83 MHz reference machine.** Functional gate passed; perf gate is open work. Bottleneck cleanly attributed to SDL3-DOS's banked-mode VESA flush; LFB-engagement fix is the next-step lever (~10× projected). See `docs/PHASE9.md` for the optimization roadmap.

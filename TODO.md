# TODO

Current work is organized by phase (see `PLAN.md`). Mark items complete as they land.

---

## Phase 0 — Prerequisites

- [x] `scripts/setup-symlinks.sh` creates `tools/djgpp` → `~/emulators/tools/djgpp`
- [x] `make djgpp-check` passes
- [x] `vendor/cwsdpmi/cwsdpmi.exe` + `cwsdpmi.doc` present (copied from vellm or downloaded)
- [x] DOSBox-X installed (`dosbox-x -version` works)

## Phase 1 — Toolchain smoke test

- [x] `tests/smoketest/hello.c` compiles via `make hello`
- [x] `make smoke-fast` passes (hello.exe runs under `dosbox-x-fast.conf`)
- [x] `make smoke` passes (hello.exe runs under `dosbox-x.conf` parity)

## Phase 2 — SDL3 for DOS

- [x] `vendor/sources.manifest` pins a concrete SDL SHA post-PR-#15377 (`74a7462`)
- [x] `./scripts/fetch-sources.sh` clones `vendor/SDL` at pinned SHA
- [x] `make sdl3` produces `build/sysroot/lib/libSDL3.a` (2,054,564 bytes; 202 `.c.obj` members, 8 of them DOS-backend TUs; `SDL_AUDIO_DRIVER_DOS_SOUNDBLASTER=1`, `SDL_VIDEO_DRIVER_DOSVESA=1`; no host-platform drivers leaked)
- [x] `make sdl3-smoke` — doskutsu-authored DOS-backend probe (`tests/sdl3-smoke/sdltest.c`, DJGPP `minstack=512k`) runs under headless DOSBox-X via `tests/run-sdl3-smoke.sh`. Video gate passes (34 VESA modes incl. 320x240 XRGB8888 / RGB565 / XRGB1555 / INDEX8); audio driver bootstraps but `SDL_Init(SDL_INIT_AUDIO)` device pick fails under SB16 emulation — see Known issues #16 / #17
- [x] Any DJGPP-specific fixes captured as `patches/SDL/*.patch` — none needed for SDL @ `74a7462`; `patches/SDL/` is empty

## Phase 3' — SDL3_mixer + SDL3_image

> Per the 2026-04-24 Path B amendment, original Phase 3 (sdl2-compat) is abandoned and original Phase 4 is replaced by Phase 3'. See `PLAN.md § Plan Amendments § 2026-04-24` and the "Superseded — historical" section near the bottom of this file.

- [ ] `vendor/sources.manifest` gains `SDL3_mixer` entry pinned to an SDL3-track release SHA
- [ ] `vendor/sources.manifest` gains `SDL3_image` entry pinned to an SDL3-track release SHA
- [ ] `./scripts/fetch-sources.sh` clones both into `vendor/SDL3_mixer/` and `vendor/SDL3_image/`
- [ ] `make sdl3-mixer` produces `build/sysroot/lib/libSDL3_mixer.a` with WAV + OGG (stb_vorbis); MP3 / MOD / MIDI / FLAC / Opus all OFF
- [ ] `make sdl3-image` produces `build/sysroot/lib/libSDL3_image.a` with PNG (stb_image); JPEG / WebP / AVIF / TIFF all OFF
- [ ] Test harness extension: `Mix_OpenAudio` (SDL3 signature) + `IMG_Load` both succeed under DOSBox-X
- [ ] `make sdl2-compat` removed from default build target; sdl2-compat sources stay cloned at `91d36b8d` per "keep cloned but unbuilt" condition

## Phase 4' — NXEngine-evo SDL2 → SDL3 migration + DJGPP port

> Four sub-phases in order. Land each as one or more commits and ideally one or more `patches/nxengine-evo/*.patch` so future upstream syncs stay manageable.

### Phase 4'a — Audio refactor (Path 1)

> **Threading-zero invariant (audit-confirmed, do not violate):** No `SDL_CreateThread`, no `std::thread`, no worker threads of any kind during this rework. The audio refactor stays synchronous. SDL3-DOS's cooperative scheduler is the constraint we'd violate first.
>
> Implementation spec, per-touchpoint enumeration, and per-patch breakdown live in `docs/SDL3-MIGRATION.md` (architectural brief) and `patches/nxengine-evo/README.md § 0010-0019` (audio cluster `0013-0017`). This list is the gate-and-tripwire view only.

- [ ] Audio refactor cluster (`0013-0017`) implemented per `docs/SDL3-MIGRATION.md § delta 4`
- [ ] Headless smoke: synthesized Pixtone tone matches reference WAV byte-equivalent (or within rounding tolerance documented)
- [ ] Verify post-refactor: `grep -rE 'SDL_CreateThread|std::thread|pthread_create' src/` returns nothing
- [ ] **N=4 working days tripwire:** if not on track, raise hand for reassessment
- [ ] **N=7 working days tripwire:** if Path 1 still stalled, fall back to Path 2 (custom SDL2_mixer-subset shim over `SDL_AudioStream`) — NOT Phase 9 lever 6

### Phase 4'b — Mechanical renames

> Per-API breakdown and per-patch grouping live in `patches/nxengine-evo/README.md § 0010-0012`.

- [ ] Mechanical SDL2 → SDL3 rename pass (`0010-0012`) implemented; build cleanly against `libSDL3.a` with no implicit-function-declaration warnings

### Phase 4'c — Library swap

> Per-call-site detail in `patches/nxengine-evo/README.md § 0018-sdl3-image-load.patch` (and `§ 0011/0012` for related enum/property updates if any).

- [ ] SDL2_image → SDL3_image migration landed (`0018`)
- [ ] After this lands: `grep -r "SDL2" src/` returns nothing in NXEngine-evo source

### Phase 4'd — DJGPP port patches

> 5-patch DOS-adaptation series. Per-patch enumeration and authoring-order guidance live in `patches/nxengine-evo/README.md § 0001-0005`. **Decision-record items** kept here because they aren't implementation-detail:

- [ ] `0002-dos-target-flags.patch` flags are `-march=i486 -mtune=pentium -O2 -fno-rtti` only. **Do NOT enable `-fno-exceptions`** — #27 audit found 6 `nlohmann::json` parse sites that depend on exception propagation; without them the survivable "log + skip malformed asset, keep playing" path becomes hard abort, a bad trade for a modder-friendly port. CLAUDE.md § Critical Rules / NXEngine-evo specifics carries the matching guidance.
- [ ] `0006-disable-haptic.patch` and prophylactic gating patches for gamepad/sensor/camera/touch/pen NOT written — #27 audit confirmed zero references to any of those subsystems in NXEngine-evo source, so there is nothing to gate.
- [ ] All five patches (`0001-0005`) applied cleanly by `scripts/apply-patches.sh` against the migrated tree

## Phase 5 — NXEngine-evo → doskutsu.exe ✓ (closed 2026-04-25 at `484efa7`)

> Narrowed by the Path B amendment to the build/link/post-link step only — the DJGPP patches relocated to Phase 4'd. Build gate met; title-screen runtime gate folds into Phase 6 (asset extraction in progress).

- [x] Grep `throw|try|dynamic_cast|typeid` across the codebase to confirm exception/RTTI flag plan from Phase 4'd is still valid
- [x] `make nxengine` produces `build/doskutsu.exe` linking `libSDL3.a` + `libSDL3_mixer.a` + `libSDL3_image.a` directly (no sdl2-compat in the link line) — 5,866,918 bytes, COFF + go32 + CWSDPMI autoload
- [x] `stubedit build/doskutsu.exe minstack=2048k`
- [ ] Title screen reachable in `tools/dosbox-launch.sh --exe build/doskutsu.exe` — gated on Phase 6 asset extraction (in progress); will tick when title-screen runtime gate passes

## Phase 6 — Cave Story assets

- [ ] `docs/ASSETS.md` extraction procedure verified against current cavestory.org
- [ ] `data/Stage/`, `Npc/`, `org/`, `wav/` populated on the dev host
- [x] `scripts/extract-engine-data.py` produces `data/wavetable.dat` (25600 bytes, offset `0x110664`) + `data/stage.dat` (95-record stage index, offset `0x937B0`) from the 2004 EN freeware `Doukutsu.exe` — sibling of `scripts/extract-pxt.py`, transcribed from `vendor/nxengine-evo/src/extract/extractstages.cpp`
- [x] DJGPP data-path baking fix (`patches/nxengine-evo/0025-cmake-djgpp-data-path.patch`) — gates `IF(UNIX_LIKE)` on `AND NOT DJGPP`, so `ResourceManager::getPath()` falls through to `SDL_GetBasePath() + "data/"` instead of stat-ing a Linux-host absolute path
- [x] Runtime staging tooling — `make stage` produces `build/stage/` (binary + CWSDPMI + `data/` symlink), and `tools/dosbox-launch.sh --stage` mounts it as C: for real game runs
- [ ] Title screen → First Cave → Quote visible, moves, jumps

## Phase 7 — DOSBox-X playtest

> **Wall, current session 2026-04-25:** binary boots cleanly through full NXEngine init, reaches main loop + stage 72 (title screen), zero `drawSurface` errors — but DOSBox-X framebuffer stays black. Five pre-title-screen fatals fixed in commit `44fec06`. See `PLAN.md § Plan Amendments § 2026-04-25` for full state and next-session pickup checklist.

- [x] All pre-title-screen fatals fixed: data-path baking (patch 0025), engine-data extraction (`wavetable.dat` + `stage.dat`), DOSBox-X LFN (`lfn = true`), INDEX8 palette (patch 0026), SDL3 texture-magic validation (`SDL_INVALID_PARAM_CHECKS=0`)
- [ ] **Title screen actually renders to DOSBox-X framebuffer** (task #53 — next session). Suspected `SDL_SetRenderLogicalPresentation` missing or SDL3-DOS 640×480-default-fullscreen-vs-renderer mismatch
- [ ] 30-min continuous session under `tools/dosbox-x.conf` (parity cycles)
- [ ] Mimiga Village: dialogue + Organya stable
- [ ] First Cave / Hermit Gunsmith: combat clean, no audio dropout
- [ ] Egg Corridor entry: scrolling smooth, enemies rendered
- [ ] Save/load cycle: `Profile.dat` written, reloaded correctly

## Phase 8 — Real hardware (g2k)

- [ ] `make dist` produces `dist/doskutsu-cf.zip` with LICENSE.TXT, GPLV3.TXT, CWSDPMI.DOC, THIRD-PARTY.TXT
- [ ] g2k `sync-manifest.txt` updated with `C:\DOSKUTSU\*` paths
- [ ] `scripts/push-to-card.sh --go` deploys
- [ ] Boot under `[VIBRA]` profile, title screen reached
- [ ] Phase 7 playtest checklist passes on real HW
- [ ] `bench/results.md` populated with real-HW frame-time + audio-buffer numbers

## Phase 9 — Performance tuning (apply in order if needed)

Levers 1-5 correspond to the descent from Tier 1 (PODP83 / 48 MB, reference) through Tier 2 (486DX2-66 / 16 MB) to Tier 3 (486DX2-50 / 8 MB, absolute minimum stretch target). See `docs/HARDWARE.md § Hardware tiers`.

- [ ] Audio 22050 stereo → 11025 mono (Tier 2+ requires this)
- [ ] Renderer texture path → direct surface path (`SDL_BlitSurface`)
- [ ] 16bpp → 8bpp indexed (Tier 3 requires this; needs Cave Story sprite palette mgmt)
- [ ] Disable per-sprite alpha blending (Tier 3; Cave Story uses colorkey mostly)
- [ ] Working-set reduction: lazy sprite loading, stage streaming, Organya voice cache (Tier 3, 8 MB RAM)
- [ ] Fallback: switch to original NXEngine (C, SDL1.2-era) if evo is architecturally too heavy
- [ ] Real-hardware validation on a 486DX2-50 with 8 MB (no hardware currently available; research target)

---

## Cross-cutting

- [ ] Patches all carry `Subject:` line explaining *why DOS needs this*, not *what the patch does*
- [ ] `THIRD-PARTY.md` stays synchronized with `vendor/sources.manifest`
- [ ] `CHANGELOG.md` updated per phase gate pass
- [ ] `docs/PERFORMANCE.md` captures every Phase 9 experiment with measured before/after

**Phase status:** Phases 0 / 1 / 2 / 5 / 6 closed. Phases 3' / 4'a / 4'b / 4'c / 4'd closed (per task tracker #28, #30, #31, #32, #33). **Phase 5 closed 2026-04-25 at `484efa7`** — first end-to-end DJGPP+SDL3 build. **Phase 6 closed 2026-04-25 at `44fec06`** — `data/` populated with Cave Story freeware + NXEngine engine support data, `wavetable.dat` + `stage.dat` extractor authored, `data/base/` subdir convention superseded. **Phase 7 partially open** — pre-title-screen fatals all fixed (commit `44fec06`); the title-screen-doesn't-render-to-framebuffer wall (#53) is owned for next session. See `PLAN.md § Plan Amendments § 2026-04-25` for the pickup state.

## Known build warnings (non-blocking)

- **`-Wformat=` in `src/pause/options.cpp`** — `%d` used for `int32_t`, but `int32_t` is `long int` on DJGPP (should be `%ld`). Same root cause family as the `Organya.cpp:260` clamp finding (Phase 5 bug #4). Cosmetic; could land as a `0025-` follow-up patch or roll into Phase 9 cleanup.
- **`-Wunused-variable` in `Organya.cpp:188`** — `master_volume = 4e-6` declared but unused. Pre-existing latent. Cosmetic.

## Future / nice-to-have (post-1.0)

- [ ] PicoGUS native backend (bypasses SB16 emulation on g2k's PicoGUS hardware)
- [ ] DreamBlaster S2 (WaveBlaster header) music output option
- [ ] Widescreen mode re-enabled as a runtime option for users on better-than-g2k hardware
- [ ] JP original Cave Story support (font + text encoding different from EN freeware)
- [ ] Cave Story Remix soundtrack (OGG) preset
- [ ] Configurable keybindings (save to `DOSKUTSU.CFG`)
- [ ] Mod support / Cave Story Tweaked integration

## Known issues

- **#16 — SDL3 SoundBlaster detection fails under DOSBox-X SB16 emulation.** DSP reset's "data ready" goes true but the byte read is not `0xAA`. Likely PR #15377 bug in the SB16 detection sequence. Lands as `patches/SDL/0001-sb16-dsp-detection-fix.patch` (local-only — see CLAUDE.md § Vendoring on the patches-stay-local policy). Blocks Phase 7 playtest gate (audio required); does not block Phases 3'–5.

---

## Superseded — historical (Path B amendment 2026-04-24)

These phases were planned but abandoned. See `PLAN.md § Plan Amendments § 2026-04-24` for the architectural reasoning. The checkboxes are preserved as struck-through bullets so the audit trail is intact; do not work them.

### Phase 3 — sdl2-compat for DOS — SUPERSEDED

Replaced by direct SDL3 migration (see Phase 4' above). `vendor/sdl2-compat` stays cloned at `91d36b8d` per software-architect's "keep cloned but unbuilt" condition; `make sdl2-compat` is removed from the default build.

- [x] ~~`./scripts/fetch-sources.sh` clones `vendor/sdl2-compat`~~ — landed as task #18 before the pivot decision; tree retained
- [x] ~~`make sdl2-compat` produces `build/sysroot/lib/libSDL2.a`~~ — proven structurally infeasible by Phase 3b audit (#19); the audit's findings are the evidence for Path B
- [ ] ~~A trivial SDL2-API test program links against `-lSDL2` and runs in DOSBox-X~~ — superseded
- [ ] ~~`dlopen` / `dlsym` code paths cleanly disabled via patch (not by lying about the symbols)~~ — superseded
- [ ] ~~Fallback path documented in `PLAN.md` but not entered~~ — superseded; the "Fallback path" section in PLAN.md is now the active path

### Phase 4 (original) — SDL2_mixer + SDL2_image — SUPERSEDED

Replaced by Phase 3' (SDL3_mixer + SDL3_image — direct SDL3-native helpers). The CMake constraints (WAV + OGG only via stb_vorbis; PNG only via stb_image) carry over verbatim to the SDL3-flavored equivalents.

- [ ] ~~`make sdl2-mixer` produces `build/sysroot/lib/libSDL2_mixer.a` with WAV + OGG (stb_vorbis)~~ — superseded by Phase 3'
- [ ] ~~`make sdl2-image` produces `build/sysroot/lib/libSDL2_image.a` with PNG (stb_image)~~ — superseded by Phase 3'
- [ ] ~~Test harness: `Mix_OpenAudio` + `IMG_Load` both succeed under DOSBox-X~~ — survives in Phase 3' with the SDL3 `Mix_OpenAudio` signature

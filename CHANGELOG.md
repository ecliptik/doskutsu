# Changelog

All notable changes to DOSKUTSU are documented here. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versioning follows [Semantic Versioning](https://semver.org/).

Per-wave performance detail, measurement logs, and analysis live in the project's
internal docs and git history; this file keeps the user-facing summary.

## [Unreleased]

### Added

- **Three selectable music backends**, chosen with the `SDL_HINT_DOSKUTSU_AUDIO_BACKEND`
  environment variable: `opl3` (default -- SB16 / Sound Blaster Pro 2 OPL3 FM synth;
  moving music off the CPU's software mixer is worth ~+8.77 fps at the canonical
  scene), `organya` (the original 2004 `.org` software synthesizer -- faithful
  timbre), and `wb` (WaveBlaster header daughterboard). MIDI source files select
  independently via `SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE` (`wiimidi` / `orgmid`).
- **Render + audio optimization (waves 18-54).** Real-HW median climbed from the
  0.1.0 ~25 fps title to a ~30 fps production floor at the canonical Mimiga Village
  heavy-music scene. Headline wins: OPL3 audio offload, the backdrop render cache +
  skip-when-unchanged, INDEX8 fast-path tile blits, the `mds_clear` coverage LUT,
  and clear-into-backdrop. Every shipped lever has a strict env-var killswitch
  (reference: `docs/internal/BOOT.md`). Levers that measured flat or negative
  (VRAM-resident page-flip, asm opaque-span blit) stay in-tree default-OFF as
  documented failed experiments.
- **The render path is measured-closed at ~30 fps.** Every CPU and Cirrus-chip
  render lever has been measured to conclusion; the remaining gap to Cave Story's
  50 fps design rate is bounded by the reference PC's memory bandwidth. The 50 fps
  push continues via an opt-in **Performance Mode** (`SDL_HINT_DOSKUTSU_PERF_MODE`,
  graduated fidelity reduction; the faithful render stays the default).
- **Diagnostic probes + emulation harnesses.** `make probes` builds standalone
  DJGPP diagnostic binaries under `tests/probes/` (memory bandwidth, DPMI thunk
  cost, cache behavior, Cirrus BLT throughput, and more; gitignored). `tools/86box-*.sh`
  add 86Box tier-2 emulation alongside the DOSBox-X smoke. TAS record/replay gives
  deterministic, hands-off perf measurement.
- **Build + asset tooling.** `scripts/fetch-vendor-binaries.sh` + `vendor/binaries.manifest`
  fetch the DOS runtime binaries (CWSDPMI, long-filename TSRs) on demand
  (`make fetch-binaries`). `scripts/fetch-cs-midi.py` fetches the hardware-MIDI
  soundtrack. `tests/run-gameplay-smoke.sh` enforces a boot-banner emit gate;
  `make patches` verifies the patch series re-applies cleanly.

### Changed

- **KPI: 50 fps median**, matching Cave Story's `GAME_FPS=50` design rate. Below
  it the game runs slightly slow because NXEngine couples logic 1:1 to render.
- **Default music backend is now OPL3** (was organya). The FM-synth offload is the
  single largest fps win; `SDL_HINT_DOSKUTSU_AUDIO_BACKEND=organya` restores the
  original 2004 synthesizer.
- **Direct-VESA framebuffer path default-ON** -- writes the framebuffer straight to
  VRAM, bypassing the SDL present path; `SDL_HINT_DOSKUTSU_DIRECT_VESA=0` reverts.
- Build and contributor documentation consolidated under `docs/`.

### Removed

- Vendored DOS binaries and ~13 MB of real-hardware capture photos are no longer
  tracked in git -- binaries fetch on demand, captures live outside the repo.
- Internal team-coordination plan documents removed from tracking (gitignored).

### Repository

- Git history was rewritten once (2026-04-30, `git filter-repo`) to purge the
  now-untracked binary artifacts and internal docs; all commit SHAs from the
  project's earliest history forward are new.

---

## [0.1.0] -- 2026-04-30 -- first playable release

**Cave Story running on vintage Pentium-class DOS hardware.** Real-HW title screen
at **25.6 fps** on the reference PC (Pentium OverDrive 83 MHz / Cirrus CL-GD5434 /
SB16) -- a 54x improvement over the 0.47 fps first-boot baseline. Music, audio,
keyboard input, the title screen, and the opening cutscene render correctly.

Release binary: `DOSKUTSU.EXE`, 6,022,940 bytes, built from the Phase 9 wave-17.6
patch series.

Built across the Phase 0-9 arc:

- **SDL3 DOS port.** NXEngine-evo cross-compiled with DJGPP against SDL3 (DOS
  backend, [PR #15377](https://github.com/libsdl-org/SDL/pull/15377)) + SDL3_mixer +
  SDL3_image, statically linked, run under CWSDPMI. The engine was migrated SDL2 ->
  SDL3 directly in source (the "Path B" plan amendment) after sdl2-compat proved
  structurally infeasible to static-link on DOS.
- **The framebuffer wall.** First visible title-screen output required opting into
  the SDL3-DOS fast-path framebuffer hint; a ladder of diagnostic patches
  root-caused why normal-path flushes were writing nowhere visible.
- **Performance: 0.47 -> 25.6 fps.** The decisive levers were INDEX8 8bpp mode
  (halving framebuffer bytes per frame), the `map_draw_backdrop` render cache, and
  dirty-rect tracking. Partial-flush approaches were tried and reverted -- per-rect
  dispatch cost exceeded the bytes-saved win -- and remain in-tree as opt-in.
- ~85 local patches across SDL3-DOS and NXEngine-evo, kept as numbered
  `patches/<vendor>/NNNN-*.patch` files rather than vendored modified source.

Known issues at 0.1.0: keyboard IRQ-1 first-fire delay (~30 s after launch);
audio chop in effect-heavy scenes; a cosmetic resolution-label bug in the options
menu (actual mode is 320x240).

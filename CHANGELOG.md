# Changelog

All notable changes to DOSKUTSU are documented here. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versioning follows [Semantic Versioning](https://semver.org/).

Per-wave performance detail, measurement logs, and analysis live in the project's
internal docs and git history; this file keeps the user-facing summary.

## [Unreleased]

The post-v1.0.0 486-class hardware campaign. First release cycle specifically
targeting the slower Pentium-class machine reports -- 486DX2-66, Pentium-OD-83,
486DX-50 / Am5x86 -- where v1.0.0 surfaced three bugs that didn't reproduce at
higher CPU rates: a quit-to-DOS hang during audio teardown, ~1-second stage-load
freezes, and continuous SFX stutter on rapid-fire weapons.

### Fixed

- **Quit-to-DOS hang during audio teardown (Bug 1)** -- the SDL3 audio thread
  could deadlock or wedge during `SDL_AudioQuit` on slow CPUs when the ring
  buffer hadn't drained in time. Fixed across three patches in the
  `patches/SDL/` slot 0066-0068 series: audio-thread hard-park during quit
  + `SDL_DOSAudioPump` mid-gap pump API export + `TryLock` + 100 ms timeout
  backstop on the device close path. Validated across 5 real-HW iters on the
  486DX2-66 reference machine.

- **Stage-load freezes mechanically closed (Bug 3)** -- ~1-second freezes
  on stage transitions traced to CPU-bound MIDI parse work (`curly.mid`'s
  5684-event parse at ~480 PPQ tempo resolution). The existing
  `SDL_HINT_DOSKUTSU_PRELOAD_MIDI=1` killswitch (`patches/nxengine-evo/0112`
  + `0114` from the wave 18-63 arc) eliminates the spike class on real HW
  (count > 500 ms went 4 -> 0; max went 1095 -> 414 ms). Scheduled to flip
  default-ON in v1.0.1.

### Investigated -- continuing

- **Continuous SFX stutter on rapid-fire (Bug 2)** -- two probe iters (v1 +
  v2) confirmed hypothesis C: cooperative-scheduler audio-thread starvation.
  The v2 probe's `[sdl-audiocb]` block emit captured a max single-callback
  latency of 1701 ms during heavy SFX activity, while concurrent Pixtone-
  track active-count stayed at 1-4 (ruling out mix-CPU saturation). The
  main thread spends nearly two seconds in `playSfx` chains before yielding
  back to the audio thread; ring buffer drains during the gap; audio thread
  runs in burst when the scheduler finally yields. Operator perception
  report ("audio drops/pauses during rapid action") cross-confirmed via the
  discriminator table in `docs/internal/PIXTONE-MULTISOURCE-DESIGN.md`
  sec.8. Structural fix authored next: Lever 3, Pixtone PCM mix in the
  SB16 IRQ-5 ISR, bypassing the cooperative scheduler entirely for SFX
  (DOOM DMX-style architecture).

- **Music tempo wobble on slow CPU (Bug 4)** -- same mechanism class as
  Bug 2: MIDI events dispatched in burst when the audio thread runs after
  starvation, warping note timing. Operator report ("music gets out of
  sync"). Closed structurally by the same Lever 3 path that fixes Bug 2,
  or independently via Lever 2b (MidiScheduler tick from timer ISR; design
  authored alongside Lever 3).

### Changed

- **Build infrastructure** -- the Makefile's sdl3 cmake configure step
  adds `-DSDL_TESTS=OFF` to skip SDL3 test executables (loopwave, surround,
  resample, chkkeys). These are not shipped or used in the doskutsu.exe
  link path, and they fail to link against engine-side externs
  (`g_pixtone_active_count`) introduced for the Pixtone probe. Build-config-
  only change; doskutsu.exe behavior unaffected.

- **Smoke gate banner-emit array** -- `tests/run-gameplay-smoke.sh`
  BANNERS array updated to track the new audio-thread + probe mechanisms
  shipped this cycle (47/47/47 parity across REGEX/SEVERITY/LABEL).

### Diagnostic infrastructure

The 486-class campaign added a stack of diagnostic and instrumentation
patches that stay default-OFF in the production binary, available behind
killswitch env-vars for future investigation:

- `patches/SDL/0063-0065` -- WaitDevice ring-drain timeout quit-hang fix,
  SFX-stall instrumentation, DestroyAudioStream quit-hang markers.
- `patches/SDL/0066-0068` -- Bug 1 fix series (audio-thread hard-park +
  pump export + TryLock + timeout backstop).
- `patches/SDL/0069` -- cumulative silent-IRQ counter export
  (`doskutsu_audio_silent_irq_count`); part of the Pixtone probe wiring.
- `patches/SDL/0070` -- v2 Pixtone probe `pix_active` histogram +
  `irq_count` delta fold-in to `[sdl-audiocb]` emit; closes the
  Organya-callback-vs-OPL3-backend wiring gap from v1.
- `patches/SDL_mixer/0002` -- DestroyMixer substep teardown markers.
- `patches/nxengine-evo/0160-0164` -- SFX-stall instrumentation, Approach-A
  device-frames hint plumbing, Leg A audio mid-gap pump, engine data
  cache, Leg B tick-boundary pump.
- `patches/nxengine-evo/0165` -- engine-side Pixtone multi-source probe
  (v1; the SDL-side v2 re-wire in 0070 closes the wiring gap).

All diagnostic patches stay default-OFF unless their killswitch env-var is
set; production binary behavior unchanged from v1.0.0.

### Next

v1.0.1 ship matrix pending the Lever 3 structural fix for Bug 2:

- Bug 1 quit-hang fix: carry forward.
- Bug 3 `PRELOAD_MIDI`: flip to default-ON.
- Bug 2 Lever 3 (Pixtone PCM mix in IRQ-5 ISR): author + gate + operator
  iter; ~2-week scope.
- Bug 4 Lever 2b (MidiScheduler tick from timer ISR): independently
  designed; ships parallel to Lever 3 if operator authorizes the work.
- Lever 1 (DOOM-style channel-priority preemption) + Lever 4 (audio ring
  depth + drain rate): designed and tested; ship dormant as killswitched
  future infrastructure.

## [1.0.0] -- 2026-05-18 -- correct game speed by default

The milestone release: Cave Story plays at its intended speed on the reference
PC. The render rate is hardware-bound near 30 fps, but game logic now advances
on a fixed 50 Hz clock, so movement, animation, and timing run at the speed the
game was authored for -- it no longer feels sluggish. Built on the 0.1.0
foundation across the wave 18-63 arc.

### Added

- **Three selectable music backends**, chosen with the `SDL_HINT_DOSKUTSU_AUDIO_BACKEND`
  environment variable: `opl3` (default -- SB16 / Sound Blaster Pro 2 OPL3 FM synth;
  moving music off the CPU's software mixer is worth ~+8.77 fps at the canonical
  scene), `organya` (the original 2004 `.org` software synthesizer -- faithful
  timbre), and `wb` (WaveBlaster header daughterboard). MIDI source files select
  independently via `SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE` (`wiimidi` / `orgmid`).
- **Render + audio optimization (waves 18-55).** Real-HW median climbed from the
  0.1.0 ~25 fps title to a ~30 fps production floor at the canonical Mimiga Village
  heavy-music scene. Headline wins: OPL3 audio offload, the backdrop render cache +
  skip-when-unchanged, INDEX8 fast-path tile blits, the `mds_clear` coverage LUT,
  and clear-into-backdrop. Every shipped lever has a strict env-var killswitch
  (reference: `docs/internal/BOOT.md`). Levers that measured flat or negative
  (VRAM-resident page-flip, asm opaque-span blit) stay in-tree default-OFF as
  documented failed experiments.
- **The render path is measured-closed at ~30 fps.** Every CPU and Cirrus-chip
  render lever has been measured to conclusion; the remaining gap to Cave Story's
  50 fps design rate is bounded by the reference PC's memory bandwidth and is not
  recoverable in software on the faithful render path. An opt-in **Performance
  Mode** (`SDL_HINT_DOSKUTSU_PERF_MODE`, graduated fidelity reduction) trades
  visual detail for fps; its faithful-tier cuts measured flat on real hardware.
- **Fixed-Timestep mode -- now the default** (`SDL_HINT_DOSKUTSU_FIXED_TIMESTEP`).
  NXEngine couples game logic 1:1 to render, so at a ~30 fps render the game played
  at ~60% of its authored 50 Hz speed ("sluggish"). Fixed-Timestep advances logic
  on a fixed 50 Hz accumulator decoupled from render, so the game plays at correct
  speed at full fidelity, with no visual cost and no render-fps regression --
  real-HW instrumentation confirms steady-state gameplay logic at ~50 Hz. Reaching
  default-ON took fixing a use-after-free crash on the `FIXED_TIMESTEP=1` path, a
  backdrop-rendering flicker, and a textbox/screen-effect timing desync, each found
  and resolved during the wave 56-63 arc. `SDL_HINT_DOSKUTSU_FIXED_TIMESTEP=0`
  reverts to the legacy 1:1-coupled loop, which stays byte-identical to prior
  production.
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

- **KPI: 50 fps median**, matching Cave Story's `GAME_FPS=50` design rate. The
  reference PC is render-bound near 30 fps; Fixed-Timestep mode (now the default)
  closes the *speed* gap so the game plays correctly regardless of the render rate.
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

### Known issues

- With Fixed-Timestep mode (the default), the save-select and stage-select menu
  slide-in animations play at about half speed. Cosmetic;
  `SDL_HINT_DOSKUTSU_FIXED_TIMESTEP=0` restores their original pace.
- A faint background noise is audible on the OPL3 music backend.
- Small patches of a cave's parallax backdrop briefly flicker to black in some
  "valley" terrain. Intermittent and cosmetic.

---

## [0.1.0] -- 2026-04-30 -- first playable release

**Cave Story running on vintage Pentium-class DOS hardware.** Real-HW title screen
at **25.6 fps** on the reference PC (Pentium OverDrive 83 MHz / Cirrus CL-GD5430 /
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

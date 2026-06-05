# Changelog

All notable changes to DOSKUTSU are documented here. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versioning follows [Semantic Versioning](https://semver.org/).

Per-wave performance detail, measurement logs, and analysis live in the project's
internal docs and git history; this file keeps the user-facing summary.

## [1.0.8.1] - 2026-06-05

Quality release for the opt-in 486 Organya backend: removes the remaining gameplay
hitches around weapon-fire and song changes, and adds a deploy-once PCM cache so the
per-song cold-render no longer stalls play on the reference 486. The default backend
is still OPL3/WaveBlaster (auto-detect), unchanged.

### Fixed

- **Polar Star first-fire pause.** The weapon bullet + hit/muzzle/trail caret sprite
  sheets were lazy-decoded from the CF card on the first shot/impact of a session -- a
  one-time hitch on the 486. They are now eager-loaded into the stage-load band so the
  first fire does not decode mid-gameplay. Default-ON; killswitch
  `SDL_HINT_DOSKUTSU_EAGER_ACTION_SHEETS=0`. (`patches/nxengine-evo/0213`.)
- **Organya cold-render stale-music blip.** When an Organya song is cold-rendered (the
  first play of an uncached song), the blocking synth pass briefly starves the audio
  pull; the SB16 ring previously looped stale music for that window. The ring and the
  DMA double-buffer are now zero-flushed before the render, so the gap is clean silence
  instead of a stutter or click. Default-ON; killswitch
  `SDL_HINT_DOSKUTSU_ORG_RENDER_QUIESCE=0`.
  (`patches/nxengine-evo/0214`, `patches/SDL/0092`.)

### Added

- **Organya pre-render-to-cache batch mode.** A one-shot mode renders every Organya
  song to its PCM cache up front, eliminating the ~13-30 s per-song cold-render that
  otherwise occurs on first play on the 486. Run the game once with
  `DOSKUTSU_ORG_PRECACHE_ALL=1` to generate the cache (tier-scoped, e.g.
  `CACHE\11025_1\` for the 11025 Hz mono Tier-2 set -- ~46 MB across 41 songs), then
  copy that cache directory onto the CF card alongside the game; subsequent plays
  fast-load every song with no render stall. The cache is version-keyed to the build,
  so it is regenerated when the binary changes. (`patches/nxengine-evo/0215`.)
  - Cache generation currently runs the DOS binary on a fast machine (DOSBox-X
    headless). A host-native generator that removes the DOSBox dependency is a planned
    fast-follow.

### Known issues

- The 11025 Hz Organya output has an audible high-frequency "scratch" -- a bandwidth
  limit of the playback rate itself, not the synthesizer (internal oversampling does
  not change it). The opt-in 22050 Hz stereo quality tier (planned, v1.0.9) removes it.
- An intermittent hang on quit-to-DOS, in the DOS runtime exit AFTER the SDL audio and
  video teardown completes -- dormant (not reproducing on the reference 486 DX2-66) and
  not yet root-caused (long-standing issue #18). Does not affect gameplay.
- Under Fixed-Timestep (the default), the save-select / stage-select menu slide-in
  animations play at about half speed -- cosmetic.

## [1.0.8] - 2026-06-04

Audio release: 486-class Organya music now plays at real-time tempo, as an opt-in
backend. The v1.0.7 note called Organya gameplay "tempo-limited by synthesizer
throughput" -- calibration on the reference 486 showed that was a misdiagnosis. The
synthesizer has roughly 5.65x headroom; the bottleneck was audio DELIVERY. The SB16
PCM device was running at 44100 Hz (an SDL quality-floor policy) while the Organya
content is 11025 Hz, so the DMA ring drained 4x too fast through a 4x upsampling
resampler. This release opens the device at the content rate and pre-renders each
song to a cached PCM buffer so the playback pull is a near-free memory copy.

### Added / Changed

- **Organya device-rate fix (SDL3-DOS).** The SB16 PCM device now opens at the engine
  content rate (11025 Hz in Tier-2) instead of the SDL 44100 floor, removing a 4x ring
  drain and a 4x logical-to-physical resampler. Default-ON; killswitch
  `SDL_HINT_DOSKUTSU_DOS_AUDIO_DEVICE_RATE_DEFAULT=0` restores the 44100 floor.
  (`patches/SDL/0087`, `0088`, `0090`; `patches/nxengine-evo/0205`.)
- **Organya pre-render with on-disk PCM cache.** Each Organya song loop is synthesized
  once to a static PCM buffer (the per-frame producer is then a memory copy, with no
  synthesis in the playback pull) and written to `CACHE\<NAME>.PCM`, so the first-render
  cost is paid one time per song ever; subsequent plays fast-load from the CF card. A
  song that changes mid-gameplay before it has been cached falls back to live synthesis
  (slower, never a freeze) and is cached at the next stage load so the re-encounter is
  real-time. Default-ON; killswitch `SDL_HINT_DOSKUTSU_ORG_PRERENDER=0` reverts to the
  v1.0.7 live-synth path. The cache is version-keyed on the build fingerprint and
  auto-invalidates on any binary change. (`patches/nxengine-evo/0206`, `0207`, `0208`,
  `0209`.) Confirmed on the reference 486 (DX2-66): real-time Organya tempo at playable
  framerate, with cache write-then-fast-load verified across reboots.

### Notes

- **`opl3` / MIDI remains the recommended default backend** for 486 gameplay
  (`SDL_HINT_DOSKUTSU_AUDIO_BACKEND=opl3`); it is unchanged from v1.0.7 and has the best
  performance. Organya is an opt-in alternative. The OPL3 and WaveBlaster paths are
  byte-identical to v1.0.7 -- the pre-render pump engages only when Organya is the
  active backend.
- Two known Organya caveats on 486-class hardware: the 11025 Hz output has audible
  high-frequency "scratch" (a bandwidth limit of the playback rate itself, not the
  synthesizer -- internal oversampling does not change it), and the first time each song
  plays it renders for roughly 10-25 seconds (a one-time pause, then cached). Both are
  noted for the README polish pass.

## [1.0.7] - 2026-06-02

Bugfix release: the 486-class Organya music hang (Bug 5) is fixed. This is the
golden v1.0.6 surface plus a single SDL3-DOS audio fix; no change to the render
path or to the OPL3 / WaveBlaster audio paths.

### Fixed

- **Bug 5 -- Organya music hang on 486-class CPUs.** On DX2-66-class hardware the
  Organya music path could wedge: the cooperative-scheduler audio producer was
  starved by the main thread during the title/gameplay cooperative slack, and the
  Organya ring stopped being serviced. Fixed by an in-band cooperative yield on the
  SDL3-DOS audio WaitDevice room-path -- the audio thread now yields to the main
  thread per chunk so the ring stays fed. **Default-ON**; killswitch
  `SDL_HINT_DOSKUTSU_DOS_AUDIO_COOP_YIELD=0` restores the pre-fix blocking behavior.
  Confirmed on the reference 486 (DX2-66): a deterministic 110-second Mimiga Village
  heavy-Organya replay runs to completion with a clean exit.

### Notes

- Organya gameplay on 486-class hardware remains tempo-limited by synthesizer
  throughput (a future cheaper-synth effort is planned); **`opl3` is recommended
  for 486 gameplay** (`SDL_HINT_DOSKUTSU_AUDIO_BACKEND=opl3`). The Bug-5 hang fix
  above applies regardless of the selected backend.

## [1.0.6] - 2026-05-29

A small maintenance release on top of v1.0.5. No gameplay change; production render
and audio behavior is identical to v1.0.5 plus one default flip on a debug-path lever.

### Changed

- The backdrop-cache-disabled title-clamp added in v1.0.5
  (`SDL_HINT_DOSKUTSU_BLITPATTERN_CLAMP`) is now **on by default** (set `=0` to
  disable). The default render path (backdrop cache on) is unaffected either way; this
  activates the clamp for the `SDL_HINT_DOSKUTSU_BACKDROP_CACHE=0` debug/fallback
  config as well, where it was confirmed clean on the reference machine.
- The bundled third-party-licenses file is renamed `THIRD-PARTY.TXT` -> `3RDPARTY.TXT`
  so every file in the distribution archive is a strict DOS 8.3 name (no `THIRD-~1.TXT`
  mangling on real hardware). Content unchanged.
- Internal: the release process now mechanically verifies CHANGELOG.md carries an entry
  for the version being tagged. No user-facing effect.

## [1.0.5] - 2026-05-29

A maintenance and hardening release on top of v1.0.4. Production render and audio
behavior is byte-identical to v1.0.4; the changes are build provenance, source and
test hygiene, and an opt-in clamp for a latent out-of-bounds read on the
backdrop-cache-disabled title render path (a debug/fallback config, not the default).

### Fixed

- **Title-screen clouds clipped with the backdrop cache disabled** -- with
  `SDL_HINT_DOSKUTSU_BACKDROP_CACHE=0` (a debug/fallback config), the title
  backdrop dropped its lower clouds on real hardware, from a one-row out-of-bounds
  read in the layered pattern blit. The default render path (backdrop cache on) was
  never affected -- it clips the read harmlessly. New opt-in
  `SDL_HINT_DOSKUTSU_BLITPATTERN_CLAMP=1` clamps the blit's source height to the
  texture bounds; confirmed on the reference machine. Ships off by default (the
  default path has no out-of-bounds read), so v1.0.5 is production-identical to
  v1.0.4.

### Changed

- **Reproducible build stamp** -- the embedded build identifier
  (`binary_sha12` in the run manifest) is now reproducible from the release tag: it
  hashes only the applied patch set's diff content, so the same source always yields
  the same stamp (previously it mixed in the live commit hash, so a rebuild from the
  tag produced a different stamp than the shipped binary).
- **Internal hygiene** -- removed the last non-ASCII character from the active patch
  stack and re-aligned the gameplay smoke-test's banner-label table (no user-facing
  effect).

## [1.0.4] - 2026-05-28

A visual and load-time quality pass on top of v1.0.3. Fixes parallax backdrops
flashing black during camera motion, and eliminates two disk reads that were
deferred to gameplay phase (both caught by the v1.0.3 IO-audit gate). Both fixes
are default-ON with killswitches and were validated on the reference machine
(g2k: Pentium-OverDrive-83 / Cirrus CL-GD5430 / SB16 PnP + DreamBlaster S2).

### Fixed

- **Backdrop flashes black on camera motion** -- parallax backdrops rendered black
  bands during jumps and room entry in caves with terrain dips and water areas,
  filling back in on landing. A sub-region render optimization (the cached-backdrop
  clip) under-covered the backdrop while the camera moved: harmless under the
  emulator's fast render, but exposed on the reference machine's slower
  per-rendered-frame camera delta, where the uncovered rows showed through as
  black. Fixed by `patches/nxengine-evo/0183`
  (`SDL_HINT_DOSKUTSU_THRASH_FULLCOVER`, default-ON): the cached backdrop now
  full-covers on every render path. Operator-confirmed on g2k -- black gone in
  water caves, Mimiga Village, and the Farm-cave entry. Killswitch `=0` restores
  the previous clip behavior.

- **First-frame disk stalls on title and stage entry** -- two reads the IO-audit
  gate flagged as deferred to gameplay phase. The title screen loaded its music and
  backdrop inside the game loop (now reclassified as a scene-load via
  `patches/nxengine-evo/0184`, `SDL_HINT_DOSKUTSU_EAGER_TITLE_IO`), and each
  stage's entry music decoded on the first gameplay frame from its `<CMU>` script
  command (now preloaded during stage load via `patches/nxengine-evo/0185`,
  `SDL_HINT_DOSKUTSU_PRELOAD_STAGE_MUSIC`, which peeks the stage's entry event for
  its first song and starts it before play begins). The IO-audit gate now passes
  with zero non-allowlisted gameplay-phase disk reads; its allowlist was trimmed so
  a regression of either load re-trips the gate. Both default-ON; killswitches `=0`.

## [1.0.3] - 2026-05-28

The post-v1.0.1 cycle. Ships WaveBlaster/DreamBlaster wavetable MIDI (developed
as the untagged "v1.0.2 task #1") and closes the long-standing first-SFX
stage-load pause that was deferred from v1.0.1 as Bug 1. Also adds a permanent
DOSBox-X dev gate that catches the entire class of bug that caused the pause.
All operator-validated on the reference machine (g2k: Pentium-OverDrive-83 /
Cirrus CL-GD5430 / SB16 PnP + DreamBlaster S2).

### Added

- **WaveBlaster / DreamBlaster wavetable MIDI** -- wavetable music now plays
  through a daughterboard on the SB16 WaveBlaster header (DreamBlaster S2 on
  g2k). Root cause of the prior non-function: an over-broad "do not poll MPU-401
  status" generalization. Fixed in `patches/SDL/0080` by polling the bit-6 DRR
  (Data-Read-Ready) flag before every MPU-401 write (UART entry + per byte) and
  flipping `SDL_HINT_DOSKUTSU_AUDIO_WB_DIRECT_PORT` default to ON; the
  auto-detect chain (WB -> OPL3 -> Organya) now selects WB without an env
  override. Operator-validated: distinct, correct wavetable timbre across songs
  in Mimiga Village + First Cave; PicoGUS (USB mode) coexistence confirmed.

- **Permanent gameplay-IO-audit dev gate** -- `make io-gate` (also run by
  `make smoke`) drives DOSBox-X with `SDL_HINT_DOSKUTSU_IO_AUDIT=1` and FAILs
  the build if any disk IO is deferred to gameplay phase (the bug class behind
  the SFX pause). The gate is a sprite-sheet re-decode detector: a sheet decoded
  more than once at `phase=gameplay` means the per-cave flush regressed. Backed
  by `patches/nxengine-evo/0180` (phase-tagged IO-audit hook) +
  `tests/io-audit-gate.sh` + `tests/io-audit-allowlist.txt`. Self-validated to
  have teeth (PASS on the shipped fix, FAIL on a simulated re-decode regression).

### Fixed

- **First-SFX stage-load pause (Bug 1)** -- the ~150-450 ms render-thread freeze
  on the first sound effect (polar star fire, ceiling bonk, etc.) after entering
  a new area. Root-caused after a multi-iteration campaign that ruled out
  cooperative-scheduler starvation, DPMI page-faults (code + PCM data), and the
  audio mix path: `load_stage` calls `Sprites::flushSheets()` on every cave
  entry, deleting all decoded sprite sheets; each sheet is then lazily re-decoded
  (PNG decode + surface alloc from the CF card) on its first blit. Action sprites
  (bullets, carets) only blit when the player fires/bonks, so their sheets
  cold-load mid-gameplay -- the operator-perceived "loads from disk" pause, which
  was the right instinct (it is disk IO, for the sprite, not the sound). Fixed by
  `patches/nxengine-evo/0178` (`SDL_HINT_DOSKUTSU_SKIP_SHEET_FLUSH`, default-ON):
  sprite sheets stay resident across cave transitions instead of being flushed +
  re-decoded. Operator-confirmed on g2k: pause gone, stage-load times unchanged.
  Killswitch `=0` restores the old flush behavior. (An eager-reload alternative
  -- re-decode all sheets up front at load time -- was also built and confirmed
  to fix the pause but added ~10 s to each cave load; it is kept default-OFF as a
  low-RAM option via `SDL_HINT_DOSKUTSU_EAGER_SHEET_RELOAD=1`. The shipped
  resident-sheet approach costs ~5-25 MB RAM, comfortable on the 48 MB target.)

### Changed

- **Smoke gate wiring** -- `make smoke` now also runs the IO-audit re-decode
  gate (`make io-gate`); `tests/run-gameplay-smoke.sh` BANNERS extended for the
  shipped fix + the kept diagnostics, parity maintained across REGEX/SEVERITY/
  LABEL.

### Diagnostic infrastructure

Kept default-OFF behind killswitch env-vars for ongoing investigation (the
refuted fix-candidates and one-off probes from the narrowing campaign were
dropped, leaving a clean stack):

- `patches/nxengine-evo/0175` -- `load_stage` per-phase wall-clock trace
  (`SDL_HINT_DOSKUTSU_LOADSTAGE_TRACE`); owns the shared per-cave counter.
- `patches/nxengine-evo/0176` -- sprite sheet-load trace
  (`SDL_HINT_DOSKUTSU_SHEETLOAD_TRACE`).
- `patches/nxengine-evo/0179` -- frame-spike detector + fire-dispatch path
  bracket (`SDL_HINT_DOSKUTSU_FRAME_SPIKE_DETECT` / `..._FIREPATH_TRACE`); the
  no-fixed-window frame-spike detector is what finally localized Bug 1.

### Known issues

- **Backdrop-cache vertical-scroll black-on-jump** -- in caves with a low
  backdrop area (most visibly water rooms), part of the parallax backdrop
  renders black during a jump (camera-Y pan) and fills in on landing. Traced to
  the DOS-PORT backdrop cache (`SDL_HINT_DOSKUTSU_BACKDROP_CACHE=0` disables it
  and confirms the cause, but costs perf + an incomplete title backdrop, so the
  killswitch is not a ship default). Real fix (cache to cover the vertical
  scroll range) is queued for a later release. Long-standing; not introduced
  here.

- **Backdrop-image + Organya-music lazy loads** -- the IO-audit gate's first run
  flagged two further gameplay-phase disk loads beyond sprites (a parallax
  backdrop image; the Organya music file on a mid-area `<CMU>` change). Allowed
  in the gate allowlist for now; queued as a follow-up. The default OPL3 / WB
  music backends are unaffected.

- **Lever 3 + Organya hard-freeze (Bug 5)** -- carried from v1.0.1. Only
  reachable via the legacy Organya synth + Lever 3; a defensive interlock forces
  Lever 3 off under Organya, so the default backends are unaffected.

## [1.0.1] - 2026-05-22

The post-v1.0.0 486-class hardware campaign. First release cycle specifically
targeting the slower Pentium-class machine reports -- 486DX2-66, Pentium-OD-83,
486DX-50 / Am5x86 -- where v1.0.0 surfaced bugs that didn't reproduce at higher
CPU rates: a quit-to-DOS hang during audio teardown, ~1-second stage-load
freezes, continuous SFX stutter on rapid-fire weapons, and music-tempo wobble.
This release closes the audio bugs and ships their fixes ON by default, all
operator-validated on real hardware (486DX2-66) and cross-CPU benchmarked
(Am5x86-133 / 486DX2-50 / Pentium-OverDrive-83, Cirrus CL-GD5430).

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
  (count > 500 ms went 4 -> 0; max went 1095 -> 414 ms). Available via the
  killswitch (`=1`); the default-ON flip is deferred to a later release.

- **Continuous SFX stutter on rapid-fire (Bug 2)** -- root-caused to
  cooperative-scheduler audio-thread starvation (hypothesis C: the main
  thread spends ~1-2 s in `playSfx` chains before yielding, the ring drains,
  the audio thread runs in burst). Fixed by **Lever 3** -- Pixtone PCM mixed
  directly in the SB16 IRQ-5 ISR (`patches/SDL/0071` + `patches/nxengine-evo/
  0167`), bypassing the cooperative scheduler entirely for the SFX path
  (DOOM DMX-style). Operator-confirmed "stutter gone" on the 486DX2-66.

- **Music-tempo wobble on slow CPU (Bug 4)** -- the same starvation class
  warping MIDI note timing. Fixed by **Lever 2b** -- the MidiScheduler tick
  runs from the SB16 IRQ-5 ISR at the steady ~43 Hz Tier-2 rate
  (`patches/SDL/0072` + `patches/nxengine-evo/0168`), decoupling MIDI tempo
  from the render rate. Operator-confirmed "tempo much improved, no longer
  stutters when SFX happen."

- **SFX pitch + balance under Lever 3 (Bug 6)** -- enabling Lever 3 surfaced
  two follow-on issues, both root-caused by measurement on real hardware (the
  predicted causes were wrong both times): (1) SFX played an octave too high
  because the SB16 device opens at 44100 Hz while the Tier-2 Pixtone master
  renders at 11025 -- the engine now supplies its master rate and SDL computes
  a per-tier consumption divider automatically (`patches/SDL/0073` + `0074` +
  `patches/nxengine-evo/0169`); (2) SFX were too quiet against the OPL3 music
  because the SB16 CT1745 analog mixer was never programmed -- it is now set at
  init (PCM/voice 31, FM 28) to balance SFX against music. Operator-confirmed
  correct pitch + good balance, no stutter.

### Changed

- **Audio fixes ON by default** -- Lever 3 (Pixtone IRQ-mix), Lever 2b
  (MIDI tick from ISR), and the auto-RATEDIV + SB16 mixer balance are all
  default-ON as of `patches/SDL/0075` + `patches/nxengine-evo/0170`, so the
  fixes apply out of the box. Each is independently disablable as a
  killswitch (`SDL_HINT_DOSKUTSU_PIXTONE_IRQ_MIX=0`,
  `SDL_HINT_DOSKUTSU_MIDI_ISR_TICK=0`, `SDL_HINT_DOSKUTSU_SB16_MIXER_PROGRAM=0`);
  the SFX/music balance is tunable via `SDL_HINT_DOSKUTSU_SB16_FM_VOL` /
  `SDL_HINT_DOSKUTSU_SB16_VOICE_VOL` (0..31). The operator-validated default
  balance is FM 28 / voice 31.

- **Build infrastructure** -- the Makefile's sdl3 cmake configure step
  adds `-DSDL_TESTS=OFF` to skip SDL3 test executables (loopwave, surround,
  resample, chkkeys). These are not shipped or used in the doskutsu.exe
  link path, and they fail to link against engine-side externs
  (`g_pixtone_active_count`) introduced for the Pixtone probe. Build-config-
  only change; doskutsu.exe behavior unaffected.

- **Smoke gate banner-emit array** -- `tests/run-gameplay-smoke.sh`
  BANNERS array updated to track the v1.0.1 audio mechanisms + assert the
  ship config on a zero-env boot (L3 + L2b ENABLED both SDL+engine,
  auto-RATEDIV master_rate, SB16 balance), 59/59/59 parity across
  REGEX/SEVERITY/LABEL.

### Performance

- **No fps regression from the audio fixes** -- the v1.0.1 ship config was
  cross-CPU benchmarked on the LP4IP1 board (CPU-swapped), video = Cirrus
  CL-GD5430, same TAS on each. fps_p50 (drop-4): 486DX2-50 **15.4**,
  486DX2-66 **18.9**, Am5x86-133 **32.3**, Pentium-OverDrive-83 **33.3**.
  All-samples medians match the pre-v1.0.1 run within rounding -- the IRQ-mix
  offload did not cost frame rate. (This benchmark TAS is a lighter scene than
  the heavy-music figures in the README Status table; the two are not directly
  comparable. Recorded as the Cirrus baseline for an upcoming S3 ViRGE/DX
  video-card comparison.)

### Known issues

- **Lever 3 + Organya hard-freeze (Bug 5)** -- deferred to v1.0.2. Only
  reachable if a user opts into the legacy Organya synth
  (`SDL_HINT_DOSKUTSU_AUDIO_BACKEND=organya`) AND Lever 3; the default OPL3
  backend is unaffected, and a defensive interlock forces Lever 3 off under
  Organya. The underlying Organya-path freeze itself is a separate v1.0.2
  investigation.

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

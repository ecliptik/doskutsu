# SETUP.EXE and DOSKUTSU.CFG

> `SETUP.EXE` is built by `make setup`, staged by `make stage`, and shipped by
> `make dist` (the release ships the live-audio build). The engine-side config
> loader, the CP437 TUI, the system profiler, the Sound Hardware (BLASTER)
> screen, and the live per-backend audio test are all implemented, unit-tested
> (`make setup-test`), and covered by a headless DOSBox-X end-to-end suite
> (`tests/run-setup-e2e.sh`). See "Running the E2E suite" below.

DOSKUTSU can be configured the classic DOS-game way: run `SETUP.EXE`, pick
options, and it writes a `DOSKUTSU.CFG` file that `DOSKUTSU.EXE` reads
at startup. No need to edit `AUTOEXEC.BAT` or memorize environment
variables -- though those still work and still win (see Precedence below).

> For a player-facing guide to configuring music and sound effects (choosing
> a card, the GUS voice count, MIDI sets, troubleshooting), see
> [docs/SOUND.md](./SOUND.md).

## Running SETUP

```
SETUP.EXE
```

SETUP opens a full-screen menu:

- **System profile** -- what SETUP detected: CPU + FPU, free memory, sound
  card (from the `BLASTER` variable), WaveBlaster / OPL3 availability,
  video / VBE, and a measured video-memory fill speed in KB/s (re-measured
  each launch, not stored; shows "(speed n/a)" if the benchmark cannot run).
  The fill-speed benchmark is **on by default** (one brief graphics flash at
  startup). An earlier version did a VBE linear-framebuffer mode-set that hung
  real hardware (it wedged the S3 ViRGE memory controller); that path was
  removed and the benchmark now does a safe mode-13h (320x200) memory fill,
  validated no-hang on the S3 ViRGE, Cirrus CL-GD5430, and ATI Mach64. Disable
  it with `DOSKUTSU_SETUP_VIDEOBENCH=0` (the profile then shows "(speed n/a)").
- **Sound setup** -- the Sound menu asks two questions on adjacent rows.
  **Music Type** is a value row changed in place with Left/Right: **Organya**
  (Cave Story's built-in software synth -- no sound card needed), **MIDI** (music
  played by a synth on a sound card), or **No Music**. **Select Music Card** then
  says *which* card plays the MIDI: **Auto-detect**, **Sound Blaster (OPL3 FM)**,
  **AdLib (OPL2, music only)**, **WaveBlaster daughterboard**, **General MIDI
  (external module)**, or **Gravis UltraSound**. The card row is always
  selectable; picking a card while the type is Organya or No Music switches the
  type to MIDI. Picking a card walks its setup inline (Sound Blaster port/IRQ/DMA,
  or the **GUS voices** list), then offers to test it straight away. General MIDI
  and WaveBlaster both use the engine's MPU-401 MIDI path (`AUDIO_BACKEND=wb`);
  they differ only in the MPU-401 port and which name SETUP shows -- a SETUP-only
  `MIDI_DEV` key the engine ignores. **Select Sound FX Card** always lists every
  device (Sound Blaster, Gravis UltraSound, No Sound FX); the description warns
  about combinations that need a matching music card, but nothing is blocked.
  A **Music Options** screen holds the per-card extras (MIDI
  music set, GUS voices, GUS high fidelity, Organya pre-render, audio quality),
  showing only the rows that apply to the current choice. It also offers an **Express
  setup** that auto-detects the sound hardware in one step: it warns, probes the
  card (port / IRQ / DMA, DSP version, OPL3, WaveBlaster), shows what it found,
  sets the card + music backend, and offers an immediate music test. Edits are
  live in the session and are committed on Save.
- **Test SFX / Music** -- preview the current sound settings through the real
  audio backend (plays the real Polar Star sound effect and Title theme from
  the installed game data; see "Real Cave Story sounds" below).
- **Input / joystick** -- enable the gameport (off by default).
- **Performance** -- performance mode (faithful / smooth / fast) and the
  fixed 50 Hz timestep.
- **Advanced / troubleshooting** -- compatibility kill switches; only
  touch these if something looks wrong on the hardware.
- **Auto-detect best settings** -- profiles the machine and pre-selects the
  best options; review and override anything before saving.
- **Save and exit** -- writes `DOSKUTSU.CFG` and leaves SETUP. Start the game
  afterward with `DOSKUTSU.EXE` (or by running the shipped `SETUP.BAT`, which
  clears any stale audio environment variables before it hands back to the DOS
  prompt).
- **Quit without saving** -- leaves SETUP, discarding the session (a confirm
  prompt appears when there are unsaved changes).

Run SETUP with no existing `DOSKUTSU.CFG` and it pre-fills the menus with
the recommended settings for the detected hardware as a starting point.

Navigation: Up/Down move between rows; Left/Right (or Space) change the
highlighted value; Enter saves the current row and moves to the next one; ESC
leaves the menu (if anything changed since the last save it first asks
"Save setting?" -- Yes keeps the changes in the session, No reverts them).
Every change applies immediately to the in-memory session -- pick
WaveBlaster, test it, change a volume, re-test. Nothing is written to
disk until **Save and exit** (or F10) at the main menu; **Quit
without saving** discards the whole session. The status bar shows `* UNSAVED`
while the session has unsaved changes.

On startup SETUP also writes its detected hardware to `LOGS\PROFILE.LOG`
(CPU class + MHz estimate, physical RAM, the sound `BLASTER` fields, OPL3 /
WaveBlaster presence, VBE, and the measured `video_speed_kbs` plus
`video_speed_path` = 2 mode-13h / 3 text-B800 / 0 none) -- a plain
`key=value` dump so a real-hardware profile can be captured without
transcribing the screen.

## DOSKUTSU.CFG

A plain text file in the same directory as `DOSKUTSU.EXE`. One `KEY=VALUE`
per line; `;` or `#` starts a comment. Written by SETUP, but safe to edit by
hand. Example:

```
; DOSKUTSU.CFG -- written by SETUP.EXE.
; schema=1
AUDIO_BACKEND=opl3
AUDIO_TIER2=1
SB16_VOICE_VOL=31
SB16_FM_VOL=28
USE_JOYSTICK=0
PERF_MODE=1
FIXED_TIMESTEP=1
```

### Keys

| Key | Values | Meaning |
|---|---|---|
| `AUDIO_BACKEND` | auto / wb / opl3 / organya / adlib / gus / none | Music backend. Written by the **Music Type** row for `organya` / `none`, and by **Select Music Card** for `auto` / `opl3` / `wb` / `adlib` / `gus` (auto = detect). `auto` is omitted from the file so the engine's detection runs. `adlib` = native OPL2 FM on a no-Sound-Blaster card (music only); `gus` = native Gravis Ultrasound GF1 wavetable; `none` = **No Music** (music off, sound effects still play). |
| `MIDI_DEV` | genmidi / waveblaster | SETUP-only discriminator for the two **Select Music Card** rows that both write `AUDIO_BACKEND=wb`: `genmidi` = "General MIDI" (MPU-401 to an external module), `waveblaster` = "WaveBlaster" (daughterboard on the SB header, default). The engine ignores this key -- it only controls which name SETUP shows and which MPU-401 port default it suggests. |
| `MIDI_SET` | orgmid2 / wiimidi / `<dir>` | Which MIDI music set the `wb` / `opl3` / `gus` MIDI backends play: `orgmid2` (shown as "OrgMIDI", our org2mid native-GM conversion, `data/orgmid2/`; **the default**) or `wiimidi` (shown as "WiiWare", the WiiWare arrangements, `data/midi/`). Any other value names a custom `data/<dir>/` drop-in, shown as `Custom (<dir>)` -- this is how the legacy `data/orgmid/` set still plays. SETUP only shows the **MIDI music set** row when a MIDI backend is selected AND at least two sets are installed on disk; otherwise the default applies. Ignored by the Organya backend. |
| `GUS_VOICES` | 14 / 16 / 20 / 24 / 28 / 32 | Gravis Ultrasound active-voice count (only meaningful for `AUDIO_BACKEND=gus`). The GF1 DAC output rate is `617400 / voices`, so the voice count sets the music sample rate: 14 = 44100 Hz (highest fidelity), 16 = 38587 Hz, 20 = 30870 Hz (default -- best balance of fidelity and polyphony), 24 = 25725 Hz (more polyphony), 28 = 22050 Hz (**may be silent** on some PicoGUS cards -- a firmware rate quirk; use any other value), 32 = 19293 Hz. SETUP shows the **GUS voices** row (a "Select GUS Voices" value list) only when the Gravis Ultrasound backend is selected. |
| `GUS_HIFI` | 0 / 1 | Gravis Ultrasound multi-sample fidelity (only meaningful for `AUDIO_BACKEND=gus`). 1 (default) = upload the full multi-sample `.pat` set per instrument for the best fidelity across the keyboard; 0 = a single sample per instrument (the low-on-card-memory fallback). SETUP shows the **GUS high fidelity** row only when the Gravis Ultrasound backend is selected. Written from the **Music options** screen. |
| `SFX_DEVICE` | (omitted) / none | Sound-effects device, written by the **Select Sound FX Device** picker. Omitted = effects ride the music card's native device (the Sound Blaster DAC, or the GF1 on a Gravis card); `none` = **No Sound FX** (effects off, music still plays). The picker is narrowed to the devices the chosen music card can drive (AdLib has no DAC, so it offers only "No Sound FX"). |
| `AUDIO_OFF` | 0 / 1 | Disable all audio. |
| `AUDIO_TIER2` | 0 / 1 | Audio quality tier: 1 = 11025 Hz mono (default, lighter on the CPU), 0 = 22050 Hz stereo (HQ -- true Organya stereo, and it removes the 11025 Hz Organya "scratch", but costs ~4x the SB16 output bandwidth = a real framerate cost on 486-class CPUs). The HQ Organya pre-render uses its own disk cache (`CACHE\22050_2\`, separate from the default `CACHE\11025_1\`); build it with `make org-cache TIER=1`. Shown as the rate (`11025Hz` / `22050Hz`) on the Sound setup screen. |
| `SB16_VOICE_VOL` | 0-31 | SB16 mixer voice (PCM/SFX) level. |
| `SB16_FM_VOL` | 0-31 | SB16 mixer FM (OPL3 music) level. |
| `ORG_PRERENDER` | 0 / 1 | Pre-render Organya music to a disk PCM cache (helps slow 486 CPUs). SETUP auto-enables this when the Organya backend is picked on a sub-Pentium CPU; it can be toggled back off. |
| `USE_JOYSTICK` | 0 / 1 | Enable the gameport (off by default; costs a per-frame BIOS poll). |
| `PERF_MODE` | 0 / 1 / 2 | faithful / smooth / fast. |
| `FIXED_TIMESTEP` | 0 / 1 | Run game logic at the authored 50 Hz (default 1). |
| `AUDIO_WB_DIRECT_PORT` | 0 / 1 | WaveBlaster transport (default 1 = direct port). |
| `DIRTY_RECTS` | 0 / 1 | Dirty-rect rendering (default 1). |
| `PIXEL_FORMAT_8` | 0 / 1 | 8bpp indexed mode (default 1). |
| `FORCE_PUMP_YIELD` | 0 / 1 | Restore the per-pump cooperative yield (default 0). |
| `THRASH_FULLCOVER` | 0 / 1 | Backdrop full-cover fix (default 1). |
| `BLASTER` | e.g. `A220 I5 D1 H5 P330 T6` | Sound hardware: base port / IRQ / 8-bit DMA / 16-bit DMA / MPU-401 MIDI port / card type. Set via the **Sound Hardware** screen. Omitted from the file unless configured (then it is AUTHORITATIVE -- see below). |

These map one-to-one to the engine's existing environment variables /
SDL hints (see [docs/CONFIG.md](./CONFIG.md) for the full player-facing
reference); the config file is simply a more convenient place to set the
player-facing subset. The many diagnostic / instrumentation variables are
intentionally not exposed by SETUP.

### Precedence

For the tuning keys: `environment variable > DOSKUTSU.CFG > built-in default`

An option `SET` in `AUTOEXEC.BAT` or at the DOS prompt
always wins over the file -- handy for one-off testing without editing the
config. If neither the environment nor the file specifies a key, the
built-in production default is used, so a fresh install with no config at
all runs with sensible defaults.

**Troubleshooting -- settings saved in SETUP don't take effect.** Because
the environment wins, a leftover `SET SDL_HINT_DOSKUTSU_*` (forgotten in
`AUTOEXEC.BAT`, or hand-typed earlier in the session) silently overrides
whatever SETUP writes to `DOSKUTSU.CFG` -- pick a music card, save, and
the game still uses the old setting. The fix is to run **`SETUP.BAT`**
instead: it clears the DOSKUTSU audio environment variables and then
launches `SETUP.EXE`. The clears persist for the rest of the DOS session,
so the following `DOSKUTSU` run obeys the config too. Note that typing
plain `SETUP` runs `SETUP.EXE` (DOS resolves `.EXE` before `.BAT`) --
type `SETUP.BAT` by full name to get the cleanup. For a permanent fix,
remove the stale `SET` lines from `AUTOEXEC.BAT`.

**Exception -- `BLASTER` is authoritative.** The one deliberate inversion:
a `BLASTER` line written by SETUP OVERRIDES an ambient `SET BLASTER` from
`AUTOEXEC.BAT` (`DOSKUTSU.CFG > environment` for this key only). This
matches how setup-equipped DOS games worked -- the setup config is the
authoritative description of the sound hardware, and the `AUTOEXEC`
`BLASTER` is just the auto-detect seed. With Sound Hardware left on
auto (the default), SETUP omits the `BLASTER` line entirely and the
ambient `SET BLASTER` is used unchanged.

### Sound Hardware screen

The Sound Hardware screen picks the Sound Blaster I/O port, IRQ, 8-bit
DMA, 16-bit DMA (HDMA), MPU-401 / WaveBlaster MIDI port, and card type (shown
by its traditional name, e.g. `T6 (Sound Blaster 16)`). It is reached **inline**
from whichever picker puts the Sound Blaster into use -- selecting a Sound
Blaster-family music choice (**Organya** or **Auto-detect** in **Select Music
Type**, or **OPL3 FM** / **WaveBlaster** / **General MIDI** in **Select MIDI
Synth**), or selecting "Sound Blaster" in **Select Sound FX Device** (for a
No-Music or non-SB-music setup that still uses the SB for effects). There is no
separate menu entry for it; re-select the card or device to edit the hardware
again. The screen is seeded from the detected `BLASTER` (review and confirm).
All of these ride on the single standard `BLASTER` variable the engine already
reads at startup, so the screen simply composes one `BLASTER=...` line. Turn
"Override AUTOEXEC.BAT" off to fall back to the `AUTOEXEC.BAT` setting.

## Testing sound (per-backend audio test)

The **Test SFX / Music** screen drives the real audio stack in-process to
confirm the sound settings before launching the game. How thoroughly each
path can be verified *automatically* (DOSBox-X, headless) vs. only by ear on
real hardware:

| Backend | Automated verification | Notes |
|---|---|---|
| OPL3 FM | DOSBox-X WAV capture, strict RMS floor | OPL3 is emulated; the captured wave is deterministically non-silent. Fully checkable headless. |
| WaveBlaster MIDI | DOSBox-X best-effort (fluidsynth) + `mpu401` init witness | DOSBox-X has no real MPU-401 daughterboard; the test confirms the MPU-401 init on the configured `BLASTER` P-field (port witness) and, where a software synth is present, an RMS check. True wavetable timbre is a real-HW ear check. |
| Organya synth + PCM precache + SFX | Deterministic device-open witness; audibility is real-HW only | These run through the SB16 DMA path, which DOSBox-X's emulated SB16 does not make audible to a capture under our skip-detection config. The test deterministically confirms the device opens / renders; whether sound actually comes out is the operator's ear check on the SB16. |

In short: OPL3 is the one fully headless-verifiable audible path; WaveBlaster
and the SB16-DMA paths (Organya / PCM / SFX) are verified deterministically by
init/device witnesses in DOSBox and confirmed audibly on real hardware.

### Real Cave Story sounds

The SFX test plays the actual **Polar Star** shot, synthesized at runtime from
the installed `data/pxt/fx20.pxt` (the same Pixtone parameters the game uses).
The music test plays the **Title theme** (`data/midi/curly.mid` from the WiiWare
MIDI set, Step 4.5) through the configured synth backend: real OPL3 voices
(18-voice allocator + GM patch bank) or the WaveBlaster MPU-401. Nothing
Cave-Story-derived is bundled with SETUP -- both are read from the user's data at
runtime. If a file is missing (SETUP run outside a game install), the test falls
back to the plain tone / arpeggio -- the audio path is still exercised either way.

The music test plays a real MIDI tune for the `opl3` and `wb` backends. In
`organya` mode it plays a ~5-second snippet of the real Title theme read from
the game's pre-rendered Organya PCM cache (`CACHE/<rate>_<channels>/CURLY.PCM`,
written by enabling Organya pre-render and running the game once); if no cache
is present it stays the test tone. Plain `pcm` / `auto` modes keep the test
tone. The WaveBlaster path stays blind-init / write-only (it never reads the
MPU status register -- the same hard-freeze-safe rule the game uses).

- `SET SDL_HINT_DOSKUTSU_SETUP_REAL_SFX=0` -- force the plain test tone instead
  of the real Polar Star (default is the real effect). `SETUP.EXE` only; affects
  the audio test, never the game. Strict-match: only the literal `0` disables.
- `SET SDL_HINT_DOSKUTSU_SETUP_REAL_MUSIC=0` -- force the test arpeggio instead
  of the real Title theme (default is the real theme). `SETUP.EXE` only.
  Strict-match: only the literal `0` disables.
- `SET SDL_HINT_DOSKUTSU_SETUP_MIDI_BIOSCLK=1` -- diagnostic (default off): drive
  the MIDI test's scheduler from the BIOS 18.2 Hz clock instead of the SDL
  millisecond clock. Used to isolate a real-hardware MIDI-tempo issue; not
  needed in normal use. Strict-match: only the literal `1` enables.

## Running the E2E suite

`tests/run-setup-e2e.sh` is a fully headless end-to-end regression: it spawns
its own Xvfb + DOSBox-X, drives the real `SETUP.EXE` CP437 TUI by keystroke for
each scenario to write a `DOSKUTSU.CFG`, launches `DOSKUTSU.EXE`, and asserts
the engine's startup banners reflect the configured values (config-load count,
`perf-mode`, `fixed-timestep`, SB16 mixer balance, the authoritative `BLASTER`
MPU-401 port, audio device open, etc.). The host-side unit tests
(`make setup-test`) cover the loader contract (precedence, presence-checked
skip, authoritative overwrite) deterministically without an emulator.

```
make setup-test            # host unit tests (loader + model + recommend matrix)
tests/run-setup-e2e.sh     # headless DOSBox-X SETUP -> DOSKUTSU end-to-end
```

## Appearance toggles

Two environment variables select SETUP's look (set them before launching
SETUP; they affect `SETUP.EXE` only, never the game):

| Variable | Values | Default | Effect |
| --- | --- | --- | --- |
| `DOSKUTSU_SETUP_PALETTE` | `cs` / `classic` | `cs` | Cave-Story (black background, light-cyan titles/borders) vs the classic blue/grey/yellow scheme. |
| `DOSKUTSU_SETUP_TITLEBAR` | `1` / `0` | `1` | `1` = a full-width title bar on the top row (mirroring the bottom status bar); `0` = plain centered title text. |
| `DOSKUTSU_SETUP_POPUP` | `dim` / `shadow` / `fill` / `none` | `dim` | How a pop-over window (Save setting?, Quit?, the audio-test popups) stands out from the screen behind it: `dim` = dim the backdrop (default), `shadow` = a drop shadow, `fill` = a distinct popup background, `none` = no decoration. |

Example: `SET DOSKUTSU_SETUP_PALETTE=classic` then run `SETUP`.

## Best defaults with no config

`DOSKUTSU.EXE` ships tuned for the reference hardware. With no
`DOSKUTSU.CFG` present it behaves exactly as it always has -- the config
file only ever *changes* explicitly chosen defaults. Running SETUP and
"Auto-detect best settings" tailors those defaults to the specific machine
(e.g. OPL3 FM music with a smooth performance mode on a slower CPU, faithful
mode on a faster one).

Auto-detect always recommends **OPL3** for music -- it works on any Sound
Blaster and is the most reliable choice. WaveBlaster MIDI is never selected
automatically even when a daughterboard is detected; choose it explicitly in
**Sound setup** for wavetable music.

# Sound

DOSKUTSU plays Cave Story's music and sound effects on a wide range of DOS
audio hardware -- from a plain Sound Blaster to a WaveBlaster daughterboard, a
General MIDI module, an AdLib/OPL2 card, or a Gravis UltraSound. Choose
the hardware once in `SETUP.EXE` and it writes the choice to `DOSKUTSU.CFG`;
the game reads it at startup. Everything here can also be set by environment
variable / SDL hint (see the quick reference at the bottom and
[docs/CONFIG.md](./CONFIG.md) for the complete list).

This document is the consolidated, player-facing guide to configuring sound.
For the broader SETUP walkthrough see [docs/SETUP.md](./SETUP.md); for every
runtime variable see [docs/CONFIG.md](./CONFIG.md).

## Overview

There are two independent decisions:

1. **How the music plays** -- the **Music Type** row in SETUP's **Sound** menu
   (Organya / MIDI / No Music), plus **Select Music Card** when it is MIDI.
2. **Which device plays the sound effects** -- chosen under **Select Sound FX
   Device**.

Run `SETUP.EXE`, open **Sound setup**, make the two picks, test them with
**Test SFX / Music**, and **Save and exit**. With no config at all the game
auto-detects sensible defaults.

## Choosing the hardware

### Music Type, then Music Card

The Sound menu asks two questions on adjacent rows.

**Music Type** is a value row -- press Left/Right to change it in place:

| Music type | What it means |
|---|---|
| **Organya** | Cave Story's original built-in software synth. **No synth chip needed** -- no OPL3, wavetable, or MIDI module; it renders the music on the CPU and plays it through the Sound Blaster DAC, so a **Sound Blaster is still required**. The exact 2004 sound. |
| **MIDI** | Music played by a synth chip or module. The card row below says which one. |
| **No Music** | Music off; sound effects still play. |

**Select Music Card** then says *which* card plays the MIDI music. It names the
hardware, so there is no guessing which entry matches the installed card:

| Music card | Hardware |
|---|---|
| **Auto-detect** | Probe the installed hardware and choose automatically. The safe default. |
| **Sound Blaster (OPL3 FM)** | The OPL3 (Yamaha YMF262) FM chip on a Sound Blaster 16 / Pro. |
| **AdLib (OPL2, music only)** | A real AdLib / OPL2 card, or a PicoGUS in `/mode adlib`. **Music only** -- no sound effects. |
| **WaveBlaster daughterboard** | A wavetable daughterboard on the Sound Blaster's MIDI header (e.g. DreamBlaster S2). |
| **General MIDI (external module)** | An outboard GM module on the MPU-401 port (e.g. an SC-55-class synth). |
| **Gravis UltraSound** | A Gravis UltraSound, or a PicoGUS in `/mode gus`. |

The card row is **greyed out unless the Music Type is MIDI** -- with Organya or
No Music there is no card to choose. (It still shows the last-used card, so
it is clear what switching back to MIDI would restore.)

Picking a card walks straight into that card's setup: the **Sound Hardware**
screen (port / IRQ / DMA) for the Sound Blaster family, or the **Select GUS
Voices** list for the Gravis. SETUP then offers to play a test to
confirm it works before leaving the screen. Pressing **ESC** in the card picker
backs out without changing anything.

### Select Sound FX Device

Sound effects are the Pixtone-synthesized digital samples (the Polar Star shot,
door chimes, and so on). The FX-device picker is deliberately **narrowed to the
devices the chosen music card can actually drive**, so it is impossible to pick an
impossible combination:

- A **Sound Blaster-family** music choice (OPL3 FM / WaveBlaster / General
  MIDI / Organya / Auto-detect) offers **Sound Blaster** or **No Sound FX**.
- **AdLib** (OPL2) has no DAC, so it offers only **No Sound FX**.
- **Gravis UltraSound** offers **Gravis UltraSound** (the default -- effects on)
  or **No Sound FX** (music only). GUS effects play on the GF1 wavetable
  alongside the music -- see the Gravis section.

The one independent combination worth calling out: with **General MIDI** or
**WaveBlaster** music (which play on an external module or daughterboard), the
sound effects still come from the **Sound Blaster** DAC. That is the only setup
where the music device and the SFX device are genuinely different pieces of
hardware; the picker walks through the SB hardware screen for the effects.

## Music backends

The table below is the full backend list. In SETUP these are reached as a
**Music Type** (Organya / No Music) or, for the rest, as a **Music Card** when
the type is MIDI.

| Music backend | Hardware | What it is |
|---|---|---|
| **Auto-detect** | Any | Probes for a WaveBlaster first, then falls back to Sound Blaster OPL3 FM. The safe default. |
| **General MIDI** | External GM module on the MPU-401 port | Sends MIDI to an outboard General MIDI synth (e.g. an SC-55-class module). Real wavetable timbre off the CPU. |
| **WaveBlaster** | WaveBlaster daughterboard on the SB MIDI header | Same MPU-401 MIDI path as General MIDI, but to a daughterboard on the Sound Blaster's wavetable header (default MPU port 0x330). |
| **OPL3 FM** | Sound Blaster OPL3 (Yamaha YMF262) FM chip | OPL3 FM-synth music plus PCM sound effects on the SB DAC. Works on any Sound Blaster; the most reliable choice. |
| **AdLib** | A real AdLib / OPL2 card, or a PicoGUS in `/mode adlib` | Native OPL2 FM music on a machine with **no Sound Blaster**, clocked off the PIT timer. **Music only** -- a DAC-less OPL card cannot play the digital effects. |
| **Gravis UltraSound** | A Gravis UltraSound, or a PicoGUS in `/mode gus` | Wavetable (sampled-instrument) General MIDI music on the GF1 chip, with **no Sound Blaster** in the machine. Richer than FM and cheap on a slow CPU. See the detailed section below. |
| **Organya** | Sound Blaster (PCM/DMA) | Cave Story's original software synth -- the exact 2004 sound, mixed on the CPU. The only backend whose music cost lands on the CPU; see the [Organya](#organya) and [Performance](#performance) sections. |
| **No Music** | n/a | Music off; sound effects still play (if an FX device is selected). |

Each card maps to one `AUDIO_BACKEND` value: Auto-detect = `auto`, General
MIDI / WaveBlaster = `wb`, Sound Blaster = `opl3`, AdLib = `adlib`, Gravis
UltraSound = `gus`, Organya = `organya`, No Music = `none`. (General MIDI and
WaveBlaster share `wb`; SETUP's `MIDI_DEV` key only records which label to show
and which MPU-401 port default to suggest.)

## Gravis UltraSound

The Gravis backend (`AUDIO_BACKEND=gus`) plays General MIDI music on a real
Gravis UltraSound -- or a PicoGUS booted in `/mode gus` -- in a machine with
**no Sound Blaster**. Unlike the FM cards, the GUS is a *wavetable* card:
General MIDI instruments are real recorded samples loaded into the card's
on-board DRAM, and the GF1 chip mixes its active voices in hardware straight to
its own output. The CPU only sends note events, so the music is both richer and
cheaper on a slow processor. Like the AdLib path it has no Sound Blaster
interrupt, so it clocks playback off the PIT/IRQ-0 system timer.

### Instrument patch set (required)

DOSKUTSU loads the GUS's instruments from the **standard Gravis patch set** --
the `.pat` General MIDI files a normal `ULTRASND` install places under
`%ULTRADIR%\MIDI` (typically `C:\ULTRASND\MIDI`). We **load** the patches from
there; we **ship none**. The driver reads the card's port/IRQ/DMA from the
standard `ULTRASND` variable and the patch directory from `ULTRADIR`:

```
SET ULTRASND=240,3,3,7,7
SET ULTRADIR=C:\ULTRASND
```

A normal Gravis install sets both. If the patch set is missing, the
card is found but instruments play silently.

**The music is only as good as the patch set.** DOSKUTSU maps every General
MIDI program to its canonical Gravis `.pat` name (`acpiano`, `honky`, `epiano2`,
...), so two things about the `ULTRASND` set decide the sound -- for the whole
soundtrack at once, since every song resolves through the same names:

- **Completeness.** Every instrument a song uses needs its `.pat` present, or
  that part plays silent (a thinner arrangement). If tracks sound like they are
  missing instruments, the set is incomplete -- install the full set.
- **Sample quality.** The stock Gravis samples are serviceable, but a few
  (honky-tonk piano, harmonica, and others) sound reedy. Better-sampled sets use
  the **same filenames**, so they drop in with no config change and lift every
  track.

Recommended sets (unzip into `C:\ULTRASND`, keep `ULTRADIR=C:\ULTRASND`; all use
the canonical Gravis filenames, so any works with DOSKUTSU unchanged):

| Set | What it is | Best for |
|-----|-----------|----------|
| **Full Gravis stock** (`dgguspat`) | The complete ~5.6 MB original Gravis patch set | Fixing missing instruments; baseline quality. Start here if tracks sound incomplete. |
| **Pro Patches Lite** | Higher-quality replacements, deliberately compact | **RECOMMENDED on a 1 MB card (g2k-validated).** Better fidelity, and small enough that 5-8 instruments fit per song at full quality. Best overlaid on the stock set so nothing is missing. |
| **EAWPATS** | Large, high-quality GM collection | **NOT recommended on a real 1 MB card.** Its patches are so big the GF1 holds only 2-3 per song, so most of the arrangement goes *silent* -- g2k-measured, a track that plays 5-8 instruments under Pro Patches Lite drops to 2-3 under EAWPATS. It is built for RAM-rich softsynths (TiMidity), not the GF1. |

> **GF1 DRAM ceiling: 1 MB.** The GF1 mixes instruments from on-board DRAM, and
> the chip's 20-bit address space caps that at **1 MB** -- on a real GUS *and* on
> the PicoGUS, which emulates the GF1, so the same architectural limit applies
> (it cannot be raised). DOSKUTSU loads only the instruments each song needs and
> trims multisamples to fit -- but there is a hard limit: **a set whose patches
> are too big overflows the DRAM and whole instruments go SILENT (dropped, not
> just trimmed)**, which is exactly why EAWPATS fails on the GF1 and a compact
> set wins. Enabling GUS **sound effects** makes this tighter still: the effect
> bank uses ~700 KB of the 1 MB, leaving only ~300 KB for a song's instruments --
> so a compact patch set matters even more with SFX on. For the richest
> *music* and can do without effects, run with `SFX_DEVICE=none` to free the
> whole 1 MB for instruments. Pairing a compact set with `pgusinit /gus44k 1` +
> 32 voices (below) gives the best overall result.

### Voice count

The GF1's output sample rate is set by how many voices it mixes:

```
output rate (Hz) = 617400 / voices   (integer-truncated)
```

The voice count is set once when the card opens. So **more voices = more
simultaneous notes, but at a lower sample rate**. The
**Select GUS Voices** screen offers these curated presets:

| Voices | Output rate | Notes |
|---|---|---|
| 14 | 44100 Hz | Highest fidelity, 14-note polyphony. |
| 16 | 38587 Hz | 16-note polyphony. |
| **20** | **30870 Hz** | **Default** -- best balance of fidelity and polyphony. |
| 24 | 25725 Hz | 24-note polyphony. |
| 28 | 22050 Hz | High polyphony, but **may be silent** -- see the warning below. |
| 32 | 19293 Hz | Lowest fidelity, 32-note polyphony. |

The default **20 voices (30870 Hz)** is the g2k-validated sweet spot. Drop to
**14** for the highest fidelity; raise the count if a song needs more
simultaneous notes at the cost of the lower rate.

> **Avoid 28 voices.** 28 voices works out to exactly 22050 Hz, which trips a
> PicoGUS **firmware** quirk: the firmware rescales 28-channel output to 44.1
> kHz internally, which collides with the rate the driver expects and leaves
> the music **dead silent** on affected cards. It is valid, high-polyphony on
> hardware without the quirk (on a real Gravis, 28 is fine), but if GUS music
> goes silent at 28, switch to **any other voice count** -- the 20-voice
> default is safe.

### GUS high fidelity

**GUS high fidelity** is **on by default**. When on, DOSKUTSU uploads the full
multi-sample `.pat` set per instrument, so each note plays from the
nearest-pitched recorded sample -- the best timbre across the keyboard, plus
smooth note releases. Turning it off uses a single sample per instrument (a
low-on-card-memory fallback); set it off only if a song runs the GF1 out of
DRAM.

### Maximum quality (experimental)

A PicoGUS can break the voices-vs-rate trade-off entirely:

```
pgusinit /gus44k 1
```

switches the card to **true 44.1 kHz internal mixing** and rescales each
voice's pitch to match, giving full 44.1 kHz output *regardless of voice
count*. Combine `/gus44k 1` with **32 voices** to run maximum polyphony
(32 simultaneous notes) at full 44.1 kHz fidelity -- the premium config. It is
experimental and PicoGUS-specific.

This is also the best config with **sound effects** enabled on the GUS:
effects share the GF1 voices with the music (see below), so the extra polyphony
keeps a burst of effects from thinning the music. On the default 20-voice
config the effects reserve a few voices for themselves so they never cut music
notes, but the `/gus44k 1` + 32-voice setup gives both the most room.

### Sound effects on the GUS

GUS sound effects **play on the GF1 wavetable** alongside the music -- no Sound
Blaster needed. The FX-device picker offers **Gravis UltraSound** (the default,
effects on) or **No Sound FX** (music only). Cave Story's digital effects are
uploaded once into the card's DRAM as 8-bit samples and play on the GF1's
hardware voices, which they share with the music. Because those voices are
shared, effects are a touch grainier than on a dedicated Sound Blaster, and very
busy scenes may briefly thin the music -- a fair trade on a no-SB card.

Effects and music also **share the card's 1 MB DRAM**: the effect bank takes
~640 KB, leaving ~384 KB for a song's instruments (the split is tuned so busy
songs keep their full percussion). To give the music the whole
1 MB -- richer instruments, at the price of no effects -- pick **No Sound FX**
(or `SFX_DEVICE=none`). This is also why a **compact** patch set matters on the
GUS: see [Instrument patch set](#instrument-patch-set-required) above.

## Sound Blaster

The Sound Blaster backend (`AUDIO_BACKEND=opl3`) plays **OPL3 FM-synth music**
on the Yamaha YMF262 chip and **PCM sound effects** on the card's DAC. It works
on any Sound Blaster and is the most reliable choice -- the auto-detect path
recommends it whenever no WaveBlaster is found.

The card's **base port, IRQ, 8-bit DMA, 16-bit DMA, and MPU-401 MIDI port** are
set on the inline **Sound Hardware** screen (reached automatically on
selecting a Sound Blaster-family card or picking Sound Blaster for effects). The
screen is seeded from the detected `BLASTER` variable; review and confirm. All
of these ride on the single standard `BLASTER` variable the engine already
reads at startup. Two SB16 mixer levels are separately adjustable: **voice
volume** (PCM/SFX) and **FM volume** (OPL3 music), each 0-31.

### PicoGUS in Sound Blaster mode

A PicoGUS booted with `pgusinit /mode sb` presents as a Sound Blaster (OPL3 FM
music, MPU-401/WaveBlaster header, and 8-bit PCM effects), so it drives the
`opl3`, `wb`, and `organya` backends just like a real card. Three things to know:

- **The IRQ and DMA are set by physical jumpers on the card**, not by software.
  The `BLASTER` variable (and SETUP's Sound Hardware screen) **must match the
  jumpers** -- `pgusinit` even prints "(must match jumper settings!)". If they
  disagree, the card detects fine but never fires its interrupt, and **all
  Sound Blaster audio is silent** with no error. Read the jumpers, then set
  `BLASTER` to match (e.g. a PicoGUS jumpered IRQ 7 / DMA 3 needs
  `BLASTER=A220 I7 D3 P330`).
- **`pgusinit /mode sb` does not program the card's port/IRQ/DMA** -- run
  `pgusinit /sbenv` afterwards to push the `BLASTER` values onto the card
  (plain `pgusinit` only validates them).
- **Card type: choose SB Pro 2** (`T4` in `BLASTER`, "Sound Blaster Pro 2.0" in
  SETUP). It advertises the OPL3 the music backend uses, keeps the simple 8-bit
  DSP path (DOSKUTSU forces 8-bit mono, so SB Pro's stereo mode cannot cause the
  old "chipmunk" pitch bug), and avoids the SB16 (`T6`) firmware rule that the
  low and high DMA channels must match. `T6` gives nothing extra on the PicoGUS
  (it is an 8-bit card with no true 16-bit DMA). On a **real** Sound Blaster 16,
  `T6` remains the best choice.

A WaveBlaster daughterboard (e.g. DreamBlaster S2) mounted on the PicoGUS's
wavetable header plays through this same MPU-401 path -- set Music Type to
**MIDI** and pick the **WaveBlaster daughterboard** card.

## Organya

The Organya backend (`AUDIO_BACKEND=organya`) is Cave Story's original software
synth. The `.org` score is mixed on the CPU and played through the **Sound
Blaster DAC** -- no synth chip is involved, which is why it sounds exactly like
2004 on any Sound Blaster, and why it is the one backend whose music cost lands
on the CPU instead of on a chip. Sound effects play on the same SB DAC.

### Pre-render and the PCM cache

**Organya pre-render** is **on by default** -- the **Organya pre-render** row in
SETUP's Sound menu (`ORG_PRERENDER`), greyed out unless the Music Type is
Organya. Rather than synthesizing every audio tick during play, the engine
renders a song's full loop once into a PCM buffer, saves it to disk, and
thereafter feeds the audio device with a memory copy. That is what keeps Organya
playable on 486-class hardware: the per-frame audio cost becomes a copy instead
of live synthesis.

Cached songs are written next to `DOSKUTSU.EXE` in
`CACHE\<rate>_<channels>\*.PCM` -- `CACHE\11025_1\` for the default audio tier,
`CACHE\22050_2\` for HQ (`AUDIO_TIER2=0`). The full 41-song set is roughly 46 MB
at the default tier. Each file is keyed to the exact game binary that rendered
it, so a new release renders a fresh cache rather than replaying a stale one.

On an empty cache there are two one-time costs:

1. **First play of each song renders it.** The first time a song plays it is
   synthesized to the cache, which is a noticeable pause with degraded audio
   during the render. The cost is paid once per song, ever, per card.
2. **New-area stall on a cold cache.** Entering a new area mid-game can briefly
   stall the currently playing song while the next one renders for the first
   time. A song that changes before it has been cached falls back to live
   synthesis -- slower, never a freeze.

Both disappear with a **pre-populated cache**: `make org-cache` renders every
song on the build host, and copying the produced `CACHE\` tree onto the card
next to `DOSKUTSU.EXE` means the target loads PCM from the first boot and never
cold-renders (see [docs/BUILDING.md](./BUILDING.md#pre-render-the-organya-pcm-cache-make-org-cache)).
The MIDI and FM backends have neither cost, which is why OPL3 stays the
recommended default on slow CPUs.

Setting `ORG_PRERENDER=0` returns to live per-tick synthesis: no cache on disk,
no first-run render pass, and the full synth cost on every frame.

## Sound effects

Cave Story's sound effects are digital samples synthesized at runtime from the
game's Pixtone parameters. Where they play depends on the music card:

- On a **Sound Blaster-family** setup they play on the **Sound Blaster DAC**.
  This is true even when the music itself is on a **WaveBlaster** or **General
  MIDI** module -- the effects still come from the SB.
- On **AdLib** there is **no DAC**, so there are **no sound effects** (music
  only).
- On **Gravis UltraSound** sound effects play on the **GF1 wavetable** alongside
  the music (they share the card's 1 MB DRAM -- see [Sound effects on the
  GUS](#sound-effects-on-the-gus)).
- **No Sound FX** turns effects off entirely; music keeps playing.

## MIDI music sets

The MIDI backends (Sound Blaster OPL3, WaveBlaster, General MIDI) can play more
than one arrangement of the score, selected by the **MIDI music set** row:

- **OrgMIDI** (`orgmid2`, **the default**) -- our own conversion of the
  original Organya score, generated by `make convert-music` (see
  [docs/ASSETS.md](./ASSETS.md#the-orgmid-sets-generated-by-org2mid----reproducible-do-not-hand-edit)).
  It maps the Organya drums to **native General MIDI percussion**, so drums
  voice as a full kit.
- **WiiWare** (`wiimidi`) -- Yann van der Cruyssen's polished WiiWare
  re-arrangements (`data/midi/`), fetched by `scripts/fetch-cs-midi.py`. A
  fuller alternate arrangement, but it uses some non-standard percussion notes
  that the GUS backend has to clamp to the nearest drum.

OrgMIDI v2 is the default because on the Gravis UltraSound its native GM drums
voice as distinct percussion, where WiiWare's out-of-range drum notes collapse
to one clamped sound. If a fresh install has not generated `data/orgmid2/` (the
`make convert-music` step), the engine falls back to WiiWare automatically.
Prefer WiiWare's arrangement? Pick it in the SETUP MIDI-set row (or set
`MIDI_SET=wiimidi`).

A custom `.mid` set can also be dropped into a `data/<dir>/` folder; see
"Custom MIDI set" in [docs/ASSETS.md](./ASSETS.md). SETUP shows the MIDI
music-set row only when at least two sets are installed; Organya ignores it.

## Performance

Sound-card choice is close to framerate-neutral. OPL3, AdLib, WaveBlaster,
General MIDI and the Gravis all hand the score to a chip and the CPU only sends
note events, so the music runs off the CPU and costs the render loop almost
nothing. Two sound settings do move the framerate:

| Setting | Cost on the reference PC |
|---|---|
| `AUDIO_BACKEND=organya` instead of a MIDI/FM backend | ~9 fps |
| `AUDIO_TIER2=0` (22050 Hz stereo) instead of the 11025 Hz mono default | ~11 fps in music-heavy scenes |

The Organya figure assumes pre-render on (the default) and a warm PCM cache;
live synthesis on a cold cache costs considerably more, and on 486-class CPUs is
the difference between real-time tempo and a stall -- see [Pre-render and the
PCM cache](#pre-render-and-the-pcm-cache). The Gravis is the cheapest of all on
a slow CPU: the GF1 mixes its voices in hardware, so even a busy arrangement
leaves the CPU free.

Per-CPU medians for each backend are in
[docs/FPS-MATRIX.md](./FPS-MATRIX.md); the full list of framerate-affecting
variables is in [docs/CONFIG.md](./CONFIG.md).

## Quick reference (config keys / env vars)

These are the player-facing sound keys. Each `DOSKUTSU.CFG` key maps to the
SDL hint / environment variable of the matching name; a real DOS `SET` wins
over the file. See [docs/CONFIG.md](./CONFIG.md) for the authoritative table.

| CFG key | Env / SDL hint | Values | Default | Meaning |
|---|---|---|---|---|
| `AUDIO_BACKEND` | `SDL_HINT_DOSKUTSU_AUDIO_BACKEND` | auto / wb / opl3 / organya / adlib / gus / none | auto | Music backend (the **Music Type** row, + **Select Music Card** when the type is MIDI). |
| `GUS_VOICES` | `SDL_HINT_DOSKUTSU_GUS_VOICES` | 14 / 16 / 20 / 24 / 28 / 32 | 20 | GUS voice count; rate = 617400/voices. 28 may be silent. |
| `GUS_HIFI` | `SDL_HINT_DOSKUTSU_GUS_MULTISAMPLE` | 0 / 1 | 1 | GUS multi-sample high fidelity (on by default). |
| `SFX_DEVICE` | `SDL_HINT_DOSKUTSU_SFX_DEVICE` | sb / none (omitted = native DAC) | (native DAC) | Sound FX device (Select Sound FX Device). `none` = effects off. |
| `MIDI_SET` | `SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE` | orgmid2 / wiimidi / `<dir>` | orgmid2 | MIDI arrangement for the OPL3 / WaveBlaster / GM / GUS backends. `orgmid2` = OrgMIDI v2 (default); `wiimidi` = WiiWare. |
| `SB16_VOICE_VOL` | `SDL_HINT_DOSKUTSU_SB16_VOICE_VOL` | 0-31 | 28 | SB16 mixer voice (PCM/SFX) level. |
| `SB16_FM_VOL` | `SDL_HINT_DOSKUTSU_SB16_FM_VOL` | 0-31 | 28 | SB16 mixer FM (OPL3 music) level. |
| `ORG_PRERENDER` | `SDL_HINT_DOSKUTSU_ORG_PRERENDER` | 0 / 1 | 1 | Pre-render Organya songs to a disk PCM cache (Organya only). `0` = live per-tick synthesis. |
| `AUDIO_TIER2` | `SDL_HINT_DOSKUTSU_AUDIO_TIER2` | 0 / 1 | 1 | Audio quality: 1 = 11025 Hz mono (lighter), 0 = 22050 Hz HQ (~11 fps in music-heavy scenes). |
| `AUDIO_OFF` | `DOSKUTSU_NO_AUDIO` | 1 | unset | Disable all audio (music + effects). |
| `MUSIC_OFF` / `SFX_OFF` | `..._MUSIC_OFF` / `..._SFX_OFF` | 1 | unset | Independently disable music / effects (same as No Music / No Sound FX). |

## Troubleshooting: no sound

Work down this checklist:

1. **Right card picked?** Run SETUP -> Sound and confirm **Music Type** is not
   **No Music**, and that **Select Music Card** matches the actual hardware.
2. **Sound FX device set?** Confirm **Select Sound FX Device** is not on **No
   Sound FX** (and remember AdLib and Gravis have no effects today).
3. **Gravis: patches installed?** GUS music needs the Gravis `.pat` set under
   `%ULTRADIR%\MIDI` and `ULTRASND` / `ULTRADIR` set. Missing patches play
   silently.
4. **Gravis: not on 28 voices?** 28 voices = 22050 Hz is silent on some PicoGUS
   cards. Switch to **20** (the default) or any non-28 count.
5. **Sound Blaster: hardware correct?** Check the **Sound Hardware** screen's
   port / IRQ / DMA against the `BLASTER` variable.
5a. **PicoGUS in SB mode: IRQ/DMA match the jumpers?** The PicoGUS asserts its
   interrupt on the physically *jumpered* IRQ/DMA regardless of what `BLASTER`
   says; a mismatch detects fine but is silent. Set `BLASTER` to the jumpers,
   run `pgusinit /sbenv`, and prefer card type SB Pro 2 (`T4`). See [PicoGUS in
   Sound Blaster mode](#picogus-in-sound-blaster-mode).
6. **Volumes up?** Check `SB16_VOICE_VOL` (effects) and `SB16_FM_VOL` (OPL3
   music) are not at 0.
7. **Test it.** Use **Test SFX / Music** in SETUP to confirm each path before
   launching the game.

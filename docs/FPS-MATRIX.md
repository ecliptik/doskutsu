# DOSKUTSU framerate matrix

Render framerate for DOSKUTSU (Cave Story on MS-DOS) across CPU, audio backend, and
video hardware. Use this to set expectations for a given machine and to pick an audio
backend.

## How to read this

- **Render fps is not game speed.** Since v1.0.0, DOSKUTSU runs game logic on a
  fixed 50 Hz timestep that is decoupled from render framerate (Cave Story is authored
  for 50 fps). A machine that renders at 19 fps still steps the game at the intended
  50 Hz -- the world moves at the correct speed; you see fewer drawn frames, not a slow
  game. So treat these numbers as visual smoothness, not difficulty or pacing.
- **Each cell shows two numbers: `median / floor`.** The first is the median rendered
  framerate (`fps_p50`); the second is the worst-5% "stutter floor" (`fps_p05`) -- the
  framerate during the choppiest 5% of the run. Both are measured over a fixed, replayed
  input sequence, so runs are comparable across machines and backends. See "Measurement
  method" below.
- **The floor is what you feel in the dips.** A median of 33 with a floor of 24 is
  consistently smooth; a median of 33 with a floor of 10 means occasional visible
  hitches the median hides. For a machine you care about, read both.
- **Higher is smoother.** As a rough guide on this engine: ~30+ fps reads as smooth,
  ~15-20 fps is clearly playable but visibly choppy, below ~12 fps is rough.

## Measurement method

- **Scene:** Mimiga Village (stage 11) with the parallax backdrop, a representative
  heavy-music gameplay load. The organya backend plays `mura.org` ("Mimiga Town",
  song 9) in this scene. Every cross-CPU cell warps into this identical scene so all
  machines measure the same scene and music.
- **Input:** an identical recorded input sequence is replayed on every machine (TAS
  replay), so the only variable between cells is the hardware and the audio backend --
  not how the level was played.
- **Metric:** `fps_p50` (median rendered framerate) and `fps_p05` (the worst-5%
  stutter floor), both from the engine's per-flip timing samples after dropping the
  warmup (boot/title) frames, by the same method as the historical cross-CPU anchors so
  the cells stay directly comparable.
- **Validity:** a cell needs at least 190 post-warmup timing samples to publish a
  floor (the median is stable with fewer; the floor needs the larger sample). The
  reference replay clears this comfortably; a truncated run is rejected, not published.
- **Floor comparability:** the stutter floor is more sensitive to which part of a
  level is on screen than the median is, so floors are only directly comparable across
  cells that ran the identical input sequence -- which every cell in this matrix does.
  The single worst frame (e.g. a one-time level-load stall) is deliberately excluded
  from the floor; the floor is the recurring choppiness, not the rare spike.
- **Video:** the reference machine uses a Cirrus Logic CL-GD5430 (1 MB) over VESA 1.2+
  (UniVBE). Other video cards have not been characterized; numbers below are Cirrus
  unless noted.

## Reference machines

All four CPU configs are the same Anigma LP4IP1 board with the CPU swapped (the "g2k"
reference machine and its 486-campaign variants):

| Label        | CPU                          | Bus     | Notes                          |
|--------------|------------------------------|---------|--------------------------------|
| POD-83       | Pentium OverDrive 83 MHz     | 33 MHz  | fastest of the four            |
| Am5x86-133   | AMD Am5x86 133 MHz           | 33 MHz  | fast 486-class                 |
| 486DX2-66    | Intel 486DX2 66 MHz          | 33 MHz  | the reference 486 (Tier-2)     |
| 486DX2-50    | Intel 486DX2 50 MHz          | 25 MHz  | slowest; ~20-32% under DX2-66  |

## The matrix -- render fps_p50 (Cirrus CL-GD5430)

Audio backends:
- **organya-prerender** -- the native Cave Story Organya music engine, with the v1.0.8
  device-rate + pre-render path. Opt-in (see the caveat below).
- **OPL3** -- FM synthesis on the SB16's OPL3. The recommended default 486 backend.
- **WaveBlaster** -- a wavetable daughterboard (DreamBlaster S2) on the SB16 WaveBlaster
  header; env opt-in.

Each cell is `median / floor` fps (fps_p50 / fps_p05).

| CPU          | organya-prerender | OPL3 (default) | WaveBlaster |
|--------------|-------------------|----------------|-------------|
| POD-83       | TBD / TBD         | ~33 / TBD      | TBD / TBD   |
| Am5x86-133   | TBD / TBD         | ~32 / TBD      | TBD / TBD   |
| 486DX2-66    | ~19 / TBD         | ~19 / TBD      | TBD / TBD   |
| 486DX2-50    | TBD / TBD         | ~15 / TBD      | TBD / TBD   |

- OPL3 medians: v1.0.1 cross-CPU anchors (POD-83 ~33, Am5x86-133 ~32, 486DX2-66 ~19,
  486DX2-50 ~15). Their floors were not recorded historically and fill in from the
  WORKSTREAM A OPL3 re-confirm runs.
- 486DX2-66 organya-prerender ~19: v1.0.8 reference-486 measurement (real-time tempo at
  playable framerate; the device-rate fix recovered roughly +20% over the pre-v1.0.8
  audio path, and the pre-render path runs at near-full render fps because the per-frame
  audio cost is a memory copy rather than live synthesis).
- **TBD cells** are pending the WORKSTREAM A cross-CPU validation pass (one set of
  real-HW runs across the CPU swaps: organya-prerender / OPL3 / WaveBlaster per CPU);
  this table is filled in when that data returns.

## Organya backend caveat (why OPL3 is the default)

`opl3` / MIDI remains the recommended default backend for 486 gameplay. The
organya-prerender backend is opt-in because it has two first-run costs:

1. **First play of each song renders once.** The first time a song plays it is
   synthesized to a cached PCM buffer, which is a noticeable pause with degraded audio
   *during* the render; after that the song fast-loads from the CF card and plays at
   real-time tempo. The cost is paid one time per song, ever, per CF card.
2. **Fresh-cache new-area stall.** On a freshly-cleared cache, entering a new area mid-
   game can briefly stall the *currently playing* song while the next song renders for
   the first time. A song that changes mid-gameplay before it has been cached falls back
   to live synthesis (slower, but never a freeze).

Both first-run costs are eliminated by a **pre-populated cache**. As of v1.0.8.1 the
full song cache can be pre-rendered on a fast machine and shipped on the CF card
(version-keyed to the build); the 486 then loads PCM from the first boot and never
cold-renders, so neither cost above is paid. The two costs apply only to a
freshly-cleared or self-rendered cache.

OPL3 and WaveBlaster have neither cost and are byte-identical to v1.0.7. Pick
organya-prerender if you want the native Cave Story score and can accept the first-run
render passes; pick OPL3/WaveBlaster for the smoothest, hitch-free audio.

---

*Methodology and per-wave measurement history live in the project's internal docs and
git history. This file is the user-facing summary.*

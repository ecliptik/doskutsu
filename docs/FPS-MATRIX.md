# DOSKUTSU framerate matrix

Render framerate for DOSKUTSU (Cave Story on MS-DOS) across CPU and audio backend.
Use this to set expectations for a given machine and to pick an audio backend.

## How to read this

- **Render fps is not game speed.** DOSKUTSU runs game logic on a
  fixed 50 Hz timestep that is decoupled from render framerate (Cave Story is authored
  for 50 fps). A machine that renders at 19 fps still steps the game at the intended
  50 Hz -- the world moves at the correct speed; the screen draws fewer frames, not a
  slower game. So treat these numbers as visual smoothness, not difficulty or pacing.
- **Higher is smoother.** As a rough guide on this engine: ~30+ fps reads as smooth,
  ~15-20 fps is clearly playable but visibly choppy, below ~12 fps is rough.
- **The floor is what the dips feel like.** A median of 30 with a floor of 23 is
  consistently smooth; the same median with a floor of 10 means occasional visible
  hitches the median hides. Read both for any machine that matters.

## The matrix -- Mimiga Village render fps

Audio backends:

- **organya** -- the native Cave Story Organya music engine, with the device-rate +
  pre-render path. The authentic score; opt-in (see the caveat below).
- **OPL3** -- FM synthesis on the SB16's OPL3. The default backend.
- **AdLib** -- FM synthesis driven by direct port I/O on an OPL2. **Music only** --
  see the caveat below before reading its lead as free.

| CPU          | organya | OPL3 (default) | AdLib |
|--------------|---------|----------------|-------|
| POD-83       | 27.5    | 30.0           | 32.1  |
| Am5x86-133   | 27.4    | 30.0           | 31.8  |
| 486DX2-66    | 19.7    | 22.0           | 24.1  |
| 486DX2-50    | 13.8    | 16.2           | 17.6  |

Ordering is the same on every CPU: AdLib fastest, OPL3 next, organya slowest. The
spread between backends is roughly 3-4 fps and does not reorder anything, so the
backend choice is a smaller lever than the CPU.

The Am5x86-133 and the POD-83 measure within 0.3 fps of each other on every backend.
The two 486DX2 parts fall well below both, and the DX2-50 is the only configuration
that lands under 20 fps on all three backends.

## Stutter floor

The floor is the framerate met or exceeded by 95% of sampled frames -- the recurring
choppiness, not the single worst spike. It is measured over the whole replayed
sequence rather than Mimiga Village alone, so it is a whole-run figure paired with a
scene-specific median.

| CPU          | organya | OPL3 | AdLib |
|--------------|---------|------|-------|
| POD-83       | 20.8    | 23.2 | 23.2  |
| Am5x86-133   | 20.0    | 21.7 | 24.3  |
| 486DX2-66    | 13.6    | 14.7 | 17.2  |
| 486DX2-50    | --      | --   | --    |

486DX2-50 floors are withheld: those runs returned 152-189 post-warmup timing samples
against the 190-sample minimum this document requires before publishing a floor. Their
medians are unaffected and are published above.

## AdLib caveat (why it leads, and why it is not the default)

The AdLib backend does not open a PCM audio device at all -- it drives an OPL2 through
direct port I/O and a timer-interrupt music pump. It therefore plays **music only, with
no sound effects.** Part of its framerate lead over OPL3 is simply the PCM mixing work
it is not doing. It is the right pick for a machine with a bare OPL2 and no DAC, or
where music-only is an acceptable trade for the fastest frame rate; it is not a
strictly better OPL3.

## Organya backend caveat (why OPL3 is the default)

OPL3 remains the recommended default for 486 gameplay. The organya backend is opt-in
because it has two first-run costs:

1. **First play of each song renders once.** The first time a song plays it is
   synthesized to a cached PCM buffer, which is a noticeable pause with degraded audio
   *during* the render; after that the song fast-loads from the CF card and plays at
   real-time tempo. The cost is paid one time per song, ever, per CF card.
2. **Fresh-cache new-area stall.** On a freshly-cleared cache, entering a new area mid-
   game can briefly stall the *currently playing* song while the next song renders for
   the first time. A song that changes mid-gameplay before it has been cached falls back
   to live synthesis (slower, but never a freeze).

Both first-run costs are eliminated by a **pre-populated cache**. The full song cache
can be pre-rendered on a fast machine and shipped on the CF card (version-keyed to the
build); the 486 then loads PCM from the first boot and never cold-renders. The two
costs apply only to a freshly-cleared or self-rendered cache.

OPL3 and AdLib have neither cost.

## Measurement method

- **Scene:** Mimiga Village (stage 11), a representative heavy-music gameplay load. The
  organya backend plays `mura.org` ("Mimiga Town", song 9) here. The replayed sequence
  visits this stage five times; the published median aggregates all five visits, so the
  figure covers roughly 57 seconds of game time in the measured scene rather than a
  single pass.
- **Input:** an identical recorded input sequence is replayed on every machine (TAS
  replay), so the only variable between cells is the hardware and the audio backend --
  not how the level was played. Every cell in both tables ran the same sequence.
- **Median metric:** frames rendered in the measured scene divided by the game time
  spent in it, taken from the engine's per-stage accounting.
- **Floor metric:** the reciprocal of the 95th-percentile frame time, from the engine's
  inter-flip timing samples after dropping warmup (boot/title) frames.
- **Validity:** a cell needs at least 190 post-warmup timing samples to publish a floor.
  The median is stable with fewer. A truncated run is rejected, not published.
- **Video:** all cells above ran an S3 ViRGE over VESA 1.2+ (UniVBE). Earlier
  characterization on a Cirrus Logic CL-GD5430 (1 MB) produced OPL3 medians in the same
  band on the faster two CPUs and lower figures on the two 486DX2 parts; the two sets
  were taken with different engine builds and are not directly comparable. Other video
  cards, including the ATI Mach64, are not characterized here.

## Not characterized

- **WaveBlaster** (DreamBlaster S2 on the SB16 header) is supported but has not been
  measured in this matrix.
- **Cirrus CL-GD5430** cells are not republished under the current method.
- **ATI Mach64** is not represented; that card has open rendering issues tracked
  separately.

---

*Methodology and per-wave measurement history live in the project's internal docs and
git history. This file is the user-facing summary.*

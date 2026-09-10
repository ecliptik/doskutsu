# Audio

## Hardware backends already proven (doskutsu, inherited via `shared/`)

- Sound Blaster 16 compatible PCM (DMA).
- OPL2 / OPL3 FM synthesis.
- MPU-401 / General MIDI / WaveBlaster.
- Gravis UltraSound / PicoGUS.

`shared/audio/midi_sched.{c,h}` is the canonical SMF (Standard MIDI File)
parser + tick scheduler — backend-agnostic via a sink callback struct
(note on/off, CC, program change). Pair it with whichever hardware backend
(`shared/audio/opl2midi.*`, `opl3midi.*`, `gusmidi.*`) fits the target
sound card. Do not write a second MIDI scheduler per port.

## Decode cost vs. render cost

Music decoding can be more expensive than game rendering on a 486. Plan for
multiple tiers rather than one fixed audio pipeline:

```
Pentium-class:  decoded digital soundtrack (e.g. OGG/Vorbis if affordable)
486:            lower-rate preconverted audio, OR MIDI arrangement,
                OR an optional no-music performance mode
```

Hardware MIDI is the highest-leverage option on slow hardware: moving music
playback to an external synthesizer removes essentially all CPU cost from
the host. If a game supports MIDI at all, hardware MIDI should be the
recommended 486 configuration.

## Phasing a new port's audio work

1. **PCM only** — WAV/SFX via Sound Blaster, no compressed audio.
2. **MIDI** — OPL3, MPU-401/WaveBlaster, GUS/PicoGUS via `shared/audio/`.
3. **Compressed audio** (OGG/Vorbis, etc.) only if profiling shows the CPU
   budget allows it; provide a lower-rate transcoded fallback for slow CPUs.
4. Additional formats only when a specific high-value game requires them.

Never let compressed-audio feature completeness degrade 486 performance —
it should always be an opt-in tier, not the default path.

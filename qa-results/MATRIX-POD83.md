# fps matrix -- POD-83 + PicoGUS + S3 ViRGE

Source: `2026-08-10-POD83-picogus-v170-sfx/`, binary `09e449c5a81d`, 13 cells,
13/13 full route. One 102 s reel, identical workload in every cell.

**Report two columns, not one.** `[fps-true]` alone conflates two independent
things: how fast the game loop runs, and how much wall time the cell spends
NOT looping (stage loads, instrument uploads). Flips are near-constant across
cells because the reel is a fixed ~5100 logic ticks; `render_s` is what moves.

    per-loop fps = flips / 102        (102 s = the reel's game time)
    overhead_s   = render_s - 102     (wall seconds spent not looping)

| Cell | Backend | per-loop fps | overhead_s |
|---|---|---|---|
| G23B | GUS 32 voices | **32.3** | **48** |
| G62 | AdLib, SFX off | 32.2 | 18 |
| G22 | GUS 20 voices | 32.1 | 45 |
| G41 / GC5 | AdLib | 32.0 | 19 |
| G23A | GUS 14 voices | 32.0 | 47 |
| G60 | music+SFX off (SB ring still up) | 31.8 | 22 |
| G51 | AUTO | 31.6 | 25 |
| G61 | OPL3, SFX off | 31.5 | 24 |
| GC4 / G31 | OPL3 | 31.3-31.4 | 22 |
| G17 | WaveBlaster | 30.1 | 24 |
| GC3 | Organya | **28.5** | 27 |

## What this says

**GUS has the fastest loop in the table and the worst wall-clock.** Its entire
deficit is ~25 s of extra overhead -- GF1 DRAM `.pat` uploads at song
transitions -- not synthesis cost. That is a load-stall problem and therefore
tractable (preload, cache, upload fewer instruments), unlike a per-frame cost.

**Organya is the only backend with a genuine per-loop penalty**, 28.5 against
~31.5 for the FM backends. Consistent with its historical medians.

**FM music is nearly free.** OPL3 and AdLib sit within ~1 fps of the
music-and-SFX-off cell, and SFX costs ~0.3 fps (G62 vs G41/GC5) -- the small
PC-speaker cost, with the right sign.

**AdLib beats OPL3 on both axes** (+0.6 per-loop, ~3 s less overhead). Its
audio stack is genuinely cheaper: `AUDIO_BACKEND=adlib` omits `SDL_INIT_AUDIO`
entirely -- no DMA, no audio IRQ, no ring maintenance -- leaving only a 120 Hz
PIT ISR doing a few OPL2 port writes.

## Two corrections to earlier readings of this data

**"GUS and AdLib cost 42% of the framerate"** was the corrupted `uclock`
timebase, not a real cost. See `docs/TAS-BENCHMARKING.md`.

**"G60 is the render ceiling"** was wrong: `MUSIC_OFF` and `SFX_DEVICE=none`
are dispatch-level gates, and the log shows the SB device still opens with the
DMA ring running and fed silence. G60 is an SB-machinery baseline, not a
no-audio floor -- which is why AdLib measures *above* it and why that was
never the paradox it looked like. A true floor needs `AUDIO_OFF=1`, the
device-level kill; worth one cell in a future round.

## Caveat

`per-loop fps` divides by the reel's 102 s of game time, so it includes any
pre-reel title flips inside the measurement window. Treat differences under
0.5 fps with care.

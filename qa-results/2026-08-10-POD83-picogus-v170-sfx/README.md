# POD-83 + PicoGUS + S3 ViRGE -- the reference audio sweep

**ROUND 1 -- CANONICAL.** Binary `09e449c5a81d`, kit v170, cache key
`f8d446b4b0e0`. 13 cells, 13/13 complete route.

**This is the POD-83 column of every published table.** Index and round
definitions: [`../README.md`](../README.md). Findings:
[`../../docs/BENCHMARKS.md`](../../docs/BENCHMARKS.md).

Adds `G60` (music and SFX both off), `G61` (OPL3 without SFX) and `G62` (AdLib
without SFX) to the standard sweep, to test whether AdLib's lead comes from its
PC-speaker sound effects. **It does not** -- the gap survives with SFX off.

| Cell | Backend | per-loop fps | overhead_s | median fps |
|---|---|---|---|---|
| `G23B` | GUS, 32 voices | 32.3 | 48 | _pump - invalid_ |
| `G62` | AdLib, SFX off | 32.2 | 18 | _pump - invalid_ |
| `G22` | GUS, 20 voices | 32.1 | 45 | _pump - invalid_ |
| `GC5` | AdLib | 32.0 | 19 | _pump - invalid_ |
| `G23A` | GUS, 14 voices | 32.0 | 47 | _pump - invalid_ |
| `G41` | AdLib | 32.0 | 19 | _pump - invalid_ |
| `G60` | music + SFX off | 31.8 | 22 | 32.3 |
| `G51` | Sound Blaster AUTO | 31.6 | 25 | 33.3 |
| `G61` | OPL3, SFX off | 31.4 | 24 | 32.3 |
| `GC4` | OPL3 FM | 31.4 | 22 | 32.3 |
| `G31` | OPL3 (PicoGUS-SB) | 31.3 | 22 | 32.3 |
| `G17` | WaveBlaster | 30.1 | 24 | 32.3 |
| `GC3` | Organya | 28.5 | 27 | 29.4 |

`per-loop fps = flips / 102`, `overhead_s = render_s - 102`.

Cells marked *pump - invalid* run the PIT/IRQ-0 music timer, which corrupts the
`uclock()` timebase the median is derived from. `per-loop fps` uses an
independent clock and stays valid.

**Known caveat:** AdLib measures 0.8-1.2 fps faster than the music-and-SFX-off
cell, which is impossible as a cost. `G60` is not a silence floor -- the Sound
Blaster device still opens with its DMA ring running and fed silence, while
AdLib skips audio init entirely. Treat sub-2 fps differences among the fast
backends as unestablished; a true floor needs an `AUDIO_OFF=1` cell.

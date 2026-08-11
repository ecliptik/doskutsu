# 486DX2-66 -- full lane C plus extras (2026-08-11)

Binary `09e449c5a81d`, kit v170, cache key `f8d446b4b0e0`, one 102 s `QA.TAS`
reel per cell. **22 cells, 22/22 full route** (`72 20 11 17 11 15 11 19 11 14
11`) -- the cleanest round of the campaign.

Analysis: `../MATRIX-CROSS-CPU.md`.

## What ran

More than lane C. The round covers four groups:

| Group | Cells | Hardware witness |
|---|---|---|
| `PG 3` lane C | `6C3 6C4 631 651 617 622 623A 623B 641 6C5` | PicoGUS (`is_sb16=0 dsp_ver=2`), ViRGE (`vram=4096KB`, LFB) |
| SFX isolation | `660 661 662` | as above |
| `VB 3` | `602 616 617 618 6111 6112` | real Vibra16 / SB16 (`is_sb16=1 dsp_ver=4`) |
| Video lanes | `6C4V 651V` (ViRGE), `6C4C 651C` (Cirrus) | `vram=4096KB` LFB vs `vram=1024KB` banked |

`617` appears in both `PG 3` and `VB 3`; the log shows `is_sb16=1`, so the
banked copy is the Vibra-header run. Both report `audio backend: wb` with
MPU-401 at 0x0330, so the DreamBlaster physically moved with the lane as
`docs/QA-CELL-REFERENCE.md` requires.

Every cell mode-set `0x01F8 320x240 8bpp pitch=320`; only `6C4C`/`651C` show
`use_lfb=0 banked=1`, which is the Cirrus guard firing as designed.

## Results

`per-loop fps = flips / 102`, `overhead_s = render_s - 102`.

| Cell | Backend | per-loop | overhead_s | median |
|---|---|---|---|---|
| `622` | GUS 20 | **24.24** | 53 | pump -- invalid |
| `623B` | GUS 32 | 24.21 | 55 | pump -- invalid |
| `623A` | GUS 14 | 24.19 | 53 | pump -- invalid |
| `662` | AdLib, SFX off | 24.19 | **25** | pump -- invalid |
| `660` | music + SFX off | 23.78 | 42 | 25.0 |
| `6C5` | AdLib | 23.57 | **25** | pump -- invalid |
| `641` | AdLib | 23.56 | **25** | pump -- invalid |
| `651V` | OPL3 (ViRGE) | 23.24 | 43 | 23.8 |
| `6C4` | OPL3 (`C4` anchor) | 23.22 | 43 | 23.8 |
| `651` | AUTO | 23.21 | 43 | 23.8 |
| `631` | OPL3 (PGSB) | 23.20 | 43 | 23.8 |
| `6C4V` | OPL3 (ViRGE) | 23.19 | 42 | 23.8 |
| `661` | OPL3, SFX off | 23.10 | 43 | 23.8 |
| `651C` | OPL3 (**Cirrus**) | 22.51 | 42 | 23.8 |
| `6C4C` | OPL3 (**Cirrus**) | 22.42 | 42 | 23.3 |
| `602` | AUTO (Vibra) | 22.40 | 44 | 22.7 |
| `616` | AUTO (Vibra) | 22.25 | 44 | 22.7 |
| `618` | OPL3 (Vibra) | 22.14 | 45 | 22.2 |
| `617` | WaveBlaster | 21.59 | 47 | 22.7 |
| `6C3` | Organya | 20.04 | 48 | 20.8 |
| `6111` | Organya (Vibra) | 19.10 | 50 | 19.6 |
| `6112` | **Organya-HQ 22050 stereo** | **13.56** | **96** | 13.0 |

**Never quote the median column for `622 623A 623B 641 6C5 662`** -- those
cells run the PIT/IRQ-0 music pump, which reprograms PIT ch0 and corrupts
`uclock()`, SDL's DOS timebase. Marked invalid above rather than omitted so
nobody recomputes them. See `docs/TAS-BENCHMARKING.md`.

## Reading

The POD-83 backend ordering reproduces exactly: GUS has the fastest loop and
the worst wall-clock, AdLib has the lowest overhead of any backend by a wide
margin, Organya is the only backend with a genuine per-loop penalty, and
WaveBlaster sits between OPL3 and Organya.

The one result that is *more* pronounced here than on the POD-83 is AdLib's
overhead advantage: 25 s against OPL3's 43, an 18 s gap where the POD-83
showed 3. Cross-CPU reasoning for that is in `../MATRIX-CROSS-CPU.md`.

`QA-USED.TAS` is the reel that drove the round. Keep it with the logs.

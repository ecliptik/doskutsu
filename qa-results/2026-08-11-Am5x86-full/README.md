# Am5x86-133 -- lane G plus extras (2026-08-11)

Binary `09e449c5a81d`, kit v170, one 102 s `QA.TAS` reel per cell.
**22 cells, 22/22 full route** (`72 20 11 17 11 15 11 19 11 14 11`).

Analysis: `../MATRIX-CROSS-CPU.md`.

## What ran, and in what order

Timestamps place three sweeps back to back on one CF:

| Group | Time | Cells | Hardware witness |
|---|---|---|---|
| `PG 2` | 12:28-12:58 | `AC3 AC4 A31 A51 A60 A61 A17* A22 A23A A23B A41 AC5 A62` | PicoGUS (`is_sb16=0`), ViRGE (`vram=4096KB`, LFB) |
| Video lanes | 13:05-13:15 | `AC4V A51V` (ViRGE), `AC4C A51C` (Cirrus 1024KB, banked) | as above |
| `VB 2` | 13:21-13:33 | `A02 A16 A17 A18 A111 A112` | Vibra16 (`is_sb16=1 dsp_ver=4 highdma=5`) |

`A17` reports `audio backend: wb` with MPU-401 at 0x0330, so the DreamBlaster
moved with the lane.

## `A17` from `PG 2` was overwritten -- known hazard, it fired

Both sweeps write `A17`. `VB 2` ran after `PG 2` without a logback in between,
so the surviving `A17` (13:26, `is_sb16=1`) is the **Vibra-header** run. The
`PG 2` WaveBlaster cell, which ran in the 12:41-12:45 gap on the PicoGUS
header, is gone.

The same thing happened on the DX2-66 (`617` is `is_sb16=1`). The POD-83
escaped it only because `PG 1` was logged back before `VB 1` ran. **A
PicoGUS-header WaveBlaster cell exists for the POD-83 only.**

Cost is small -- the Vibra-header `17` is present on all three CPUs and is the
card-matched cell the cross-CPU comparison should use anyway -- but it is a
real lost cell. To avoid it: pull and clear `LOGS\` between `PG n` and `VB n`,
or rename one of the two cells in the kit.

## Results

| Cell | Backend | per-loop | overhead_s | median |
|---|---|---|---|---|
| `A23A` | GUS 14 | **31.90** | 45 | pump -- invalid |
| `A22` | GUS 20 | 31.85 | 45 | pump -- invalid |
| `A23B` | GUS 32 | 31.84 | 46 | pump -- invalid |
| `A41` | AdLib | 31.82 | 19 | pump -- invalid |
| `A62` | AdLib, SFX off | 31.75 | **18** | pump -- invalid |
| `A60` | music + SFX off | 31.56 | 28 | 32.3 |
| `AC5` | AdLib | 31.54 | **18** | pump -- invalid |
| `A51V` | OPL3 (ViRGE) | 31.44 | 27 | 33.3 |
| `A51` | AUTO | 31.39 | 30 | 32.3 |
| `A61` | OPL3, SFX off | 31.38 | 29 | 32.3 |
| `A31` | OPL3 (PGSB) | 31.35 | 27 | 32.3 |
| `AC4` | OPL3 (`C4` anchor) | 31.32 | 27 | 32.3 |
| `AC4V` | OPL3 (ViRGE) | 31.27 | 27 | 32.3 |
| `AC4C` | OPL3 (**Cirrus**) | 30.15 | 28 | 31.2 |
| `A51C` | OPL3 (**Cirrus**) | 29.95 | 28 | 30.3 |
| `A02` | AUTO (Vibra) | 30.51 | 28 | 31.2 |
| `A16` | AUTO (Vibra) | 30.47 | 29 | 31.2 |
| `A18` | OPL3 (Vibra) | 30.46 | 30 | 31.2 |
| `A17` | WaveBlaster (Vibra) | 29.07 | 32 | 30.3 |
| `AC3` | Organya | 28.23 | 32 | 29.4 |
| `A111` | Organya (Vibra) | 27.13 | 34 | 27.8 |
| `A112` | **Organya-HQ 22050** | **20.13** | **64** | 21.3 |

**Never quote the median column for `A22 A23A A23B A41 AC5 A62`** -- those run
the PIT/IRQ-0 music pump, which corrupts `uclock()`. Marked invalid rather than
omitted so nobody recomputes them.

## Reading

This lane is the one that breaks the two-point story from lane C. The Am5x86
does not sit between the POD-83 and the DX2-66; it sits **on** the POD-83,
matching it to within ~0.1 per-loop fps across six card-matched cells while
carrying 5-9 s more overhead in every one. Sixty percent more clock buys no
render fps at all.

Everything else replicates: backend ordering unchanged, the Cirrus banked
penalty at 1.12-1.49 (near the POD-83's, roughly double the DX2-66's), the
Vibra costing 0.86 against the PicoGUS, the launcher difference at 0.05, and
`A16` picking `opl3` rather than the WaveBlaster for the third time.

## Caveat worth stating

**No log line witnesses the CPU.** The lane is identified only by the operator
passing `2` to the sweep BAT, which sets the log-tag prefix. Video and sound
hardware are each confirmed from log evidence; the CPU is not. Every
cross-CPU conclusion here rests on that one operator assertion being right.

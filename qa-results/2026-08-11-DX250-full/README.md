# 486DX2-50 -- lane H plus extras (2026-08-11)

Binary `09e449c5a81d`, kit v170, one 102 s `QA.TAS` reel per cell.
**22 cells, 22/22 full route.** Same board, CF, ViRGE and PicoGUS as every
other lane; CPU swapped.

Analysis: `../MATRIX-CROSS-CPU.md`.

Run order: `PG 4` 14:57-15:33, video lanes 15:37-15:50, `VB 4` 15:58-16:13.

## Operator concern: thermal. The data does not show it.

The operator reported the CPU running very hot with visible stuttering toward
the end of the session, and flagged the closing Vibra/ViRGE block as suspect.
**Four independent checks say the run is sound**, including that block.

**1. Within-cell drift is negative in every cell.** Comparing the median
`inter_flip_ms` of each cell's first third against its last third, frames get
*faster* over a cell, by 2-8%, in all 22. Throttling would show the opposite.
The largest negative drift (-7.8%) is in `5C3`, the *first* cell of the
session.

**2. Same cell type, 40 minutes apart, does not move.**

    5C4  15:00 -> 17.39      5C4V 15:37 -> 17.42   (later is faster)
    551  15:06 -> 17.39      551V 15:40 -> 17.39   (identical)

**3. The suspect block's scaling sits inside the early block's band.** Ratio to
the DX2-66 for the closing Vibra cells is 0.7462-0.7525; for the opening
PicoGUS cells it is 0.7477-0.7525. Same band.

**4. The strongest check -- the Vibra penalty is not inflated.** If the closing
block were degraded, the Vibra-minus-PicoGUS delta measured inside it would be
too large. It is not; it matches the other three CPUs closely:

    lane A (POD-83)     -0.90
    lane G (Am5x86)     -0.86
    lane C (DX2-66)     -1.08
    lane H (DX2-50)     -0.87

**What the operator most likely saw** is `5111` and `5112`, the last two cells,
which are genuinely the slowest in the entire campaign -- 13.2 and 8.1 fps by
median, against 17.2 for the OPL3 cells. `5112` spends 283 s of wall time on a
102 s reel. That is visible stuttering caused by the workload, not by the CPU.

**Caveat, stated because these checks cannot rule it out.** All four detect
*relative* degradation within the session. A uniform derate present from the
first cell would be invisible to them. Against that: the `C4` anchor landed at
0.7492 against a prediction of 0.7500, and the whole-lane mean sits slightly
*above* 0.75. A throttled machine would sit below.

## The prediction, and the result

Lane H was run to test one falsifiable claim: because the DX2-50 is 2x25 and
the DX2-66 is 2x33.33, **every** clock scales by exactly 0.7500, so a
clock-proportional system should show 0.75 on every metric. The signal is
deviation, not the ratio.

**19 of 22 cells land within +/-1.3% of 0.75.** The `C4` anchor is 0.7492
against 17.41 predicted, 17.39 measured.

But the deviations are not noise -- they are **backend-structured**:

| Group | Ratio | Reading |
|---|---|---|
| OPL3 / AUTO / WB / Vibra | 0.746-0.753 | on 0.75, clock-proportional |
| **AdLib** | **0.756-0.761** | **above** -- fixed cost that does not scale with clock |
| GUS | 0.753-0.755 | slightly above, same direction |
| **Organya** (`5C3`) | **0.7353** | **below** -- superlinear degradation |

A fixed, non-CPU cost component puts the ratio *above* 0.75: if frame time is
`fixed + cpu_work`, then on a 0.75x machine it becomes `fixed + cpu_work/0.75`,
which is less than `frame_time/0.75`, so fps falls by less than the clock does.

**This independently corroborates the lane A/C finding**, by a completely
different route. AdLib runs a 120 Hz PIT ISR doing OPL2 port writes -- ISA-rate
work, fixed regardless of CPU. GUS's overhead is GF1 DRAM upload time, already
identified as bus-bound from the cross-CPU overhead split. Both show up here as
exactly the predicted above-0.75 deviation, on a lane chosen for an unrelated
reason.

**The render path shows no fixed component.** OPL3 and AUTO sit on 0.75, so
there is no detectable non-scaling cost in the render loop at these speeds.
Consistent with, and adding nothing to, the unresolved bus-vs-instruction
question -- as predicted, this lane cannot separate those.

### Overhead, same test

Predicted overhead is `overhead_66 / 0.75`. Most cells come in slightly *below*
prediction, which is the same fixed-component signature seen from the other
side -- CF media latency does not get slower when the CPU does:

| Cell | Backend | DX2-66 | predicted | DX2-50 | deviation |
|---|---|---|---|---|---|
| `5C4` | OPL3 | 43 | 57.3 | 56 | -2.3% |
| `541` | AdLib | 25 | 33.3 | 32 | -4.0% |
| `522` | GUS 20 | 53 | 70.7 | 66 | -6.6% |
| `517` | WaveBlaster | 47 | 62.7 | 60 | -4.3% |
| `5C3` | **Organya** | 48 | 64.0 | **68** | **+6.2%** |
| `5112` | **Organya-HQ** | 96 | 128.0 | **181** | **+41.4%** |

**Organya is the only backend that degrades worse than clock on both axes.**
It is also the only backend with a genuine per-loop penalty in the original
matrix. Treat as suggestive rather than established: the second Organya cell
(`5111`, on the Vibra) sits at 0.7464, i.e. on 0.75, so the two Organya cells
disagree.

## `5112` is an outlier -- do not draw conclusions from it

`5112` measures **1.038** against the DX2-66, meaning the *slower* CPU
outperformed the faster one. That is structurally impossible for identical
work, so something differs between the two runs rather than between the two
CPUs.

It is not thermal: throttling would push the ratio down, not up. The likelier
explanation is that Organya-HQ at 22050 stereo is past the point where either
machine sustains the audio ring, and the resulting stall pattern is not
reproducible. `6112` had the lowest per-loop figure in the whole campaign.

Both `5112` and `6112` need re-measuring before either is quoted. Excluded from
every conclusion above.

## Results

| Cell | Backend | per-loop | overhead_s | median |
|---|---|---|---|---|
| `522` | GUS 20 | **18.30** | 66 | pump -- invalid |
| `562` | AdLib, SFX off | 18.27 | **31** | pump -- invalid |
| `523A` | GUS 14 | 18.25 | 67 | pump -- invalid |
| `523B` | GUS 32 | 18.23 | 67 | pump -- invalid |
| `541` | AdLib | 17.92 | **32** | pump -- invalid |
| `5C5` | AdLib | 17.89 | **32** | pump -- invalid |
| `560` | music + SFX off | 17.78 | 55 | 17.5 |
| `5C4V` | OPL3 (ViRGE) | 17.42 | 57 | 17.2 |
| `5C4` | OPL3 (`C4` anchor) | 17.39 | 56 | 17.2 |
| `551` | AUTO | 17.39 | 57 | 17.2 |
| `551V` | AUTO (ViRGE) | 17.39 | 57 | 17.2 |
| `561` | OPL3, SFX off | 17.38 | 58 | 17.2 |
| `531` | OPL3 (PGSB) | 17.37 | 56 | 17.2 |
| `551C` | AUTO (**Cirrus**) | 17.08 | 58 | 16.7 |
| `5C4C` | OPL3 (**Cirrus**) | 16.90 | 57 | 16.7 |
| `502` | AUTO (Vibra) | 16.76 | 59 | 16.1 |
| `516` | AUTO (Vibra) | 16.67 | 58 | 16.1 |
| `518` | OPL3 (Vibra) | 16.52 | 58 | 16.1 |
| `517` | WaveBlaster (Vibra) | 16.25 | 60 | 15.9 |
| `5C3` | Organya | 14.74 | 68 | 14.1 |
| `5111` | Organya (Vibra) | 14.25 | 70 | 13.2 |
| `5112` | **Organya-HQ** (outlier) | 14.08 | 181 | 8.1 |

**Never quote the median column for `522 523A 523B 541 5C5 562`** -- those run
the PIT/IRQ-0 music pump, which corrupts `uclock()`.

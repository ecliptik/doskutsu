# BASELINE -- pre-0289 rounds, for comparing the next run against

Framerates below come from `inter_flip_ms` medians. **Those are only valid in
non-pump cells.** Any cell running the PIT/IRQ-0 music pump (every `gus` and
`adlib` cell) has a corrupted timebase -- see `docs/TAS-BENCHMARKING.md`. The
giveaway is the 10-40 ms band, where real frames live on this machine: it is
populated in valid cells and exactly empty in corrupted ones.

Patch 0289 adds `[fps-true] flips=... fps_mean=...`, which is clock-independent
and valid everywhere. From the next round on, build the matrix from that.

| Round | Cell | Pump | inter_flip median | 10-40ms band | dur | stages |
|---|---|---|---|---|---|---|
| 2026-08-06 v1.6.5 | GC3 | valid | 29.4 fps | 235 | 129s | 9/11 |
| 2026-08-06 v1.6.5 | GC4 | valid | 32.3 fps | 288 | 126s | 9/11 |
| 2026-08-06 v1.6.5 | G31 | valid | 32.3 fps | 278 | 127s | 11/11 |
| 2026-08-06 v1.6.5 | G51 | valid | 33.3 fps | 287 | 128s | 11/11 |
| 2026-08-06 v1.6.5 | G22 | **INVALID** | 18.9 fps | 0 | 135s | 9/11 |
| 2026-08-06 v1.6.5 | G23A | **INVALID** | 18.9 fps | 0 | 144s | 11/11 |
| 2026-08-06 v1.6.5 | G23B | **INVALID** | 18.9 fps | 0 | 144s | 11/11 |
| 2026-08-06 v1.6.5 | G41 | **INVALID** | 18.9 fps | 0 | 117s | 11/11 |
| 2026-08-06 v1.6.5 | GC5 | **INVALID** | 18.5 fps | 0 | 114s | 9/11 |
| 2026-08-09 v1.6.7 | GC3 | valid | 29.4 fps | 232 | 158s | 9/11 |
| 2026-08-09 v1.6.7 | GC4 | valid | 32.3 fps | 287 | 139s | 11/11 |
| 2026-08-09 v1.6.7 | G31 | valid | 32.3 fps | 289 | 139s | 11/11 |
| 2026-08-09 v1.6.7 | G51 | valid | 32.3 fps | 285 | 139s | 11/11 |
| 2026-08-09 v1.6.7 | G17 | valid | 33.3 fps | 276 | 140s | 11/11 |
| 2026-08-09 v1.6.7 | G22 | **INVALID** | 19.2 fps | 0 | 144s | 9/11 |
| 2026-08-09 v1.6.7 | G23A | **INVALID** | 18.9 fps | 0 | 151s | 11/11 |
| 2026-08-09 v1.6.7 | G23B | **INVALID** | 18.9 fps | 0 | 147s | 10/11 |
| 2026-08-09 v1.6.7 | G41 | **INVALID** | 18.9 fps | 0 | 126s | 11/11 |
| 2026-08-09 v1.6.7 | GC5 | **INVALID** | 18.5 fps | 0 | 124s | 9/11 |

## What is actually established

- **OPL3 / WaveBlaster / Organya framerates are sound** and agree across both
  rounds: OPL3 ~32.3, WaveBlaster ~33.3, Organya ~29.4 by inter_flip median.
- **GUS and AdLib figures here are artifacts.** The ~18.5-19.2 readings are
  1000/54, the BIOS tick period, not a measurement. Corrected via flip counts
  and wall clock, AdLib lands within a couple of fps of OPL3 and GUS about
  4 fps lower, the latter from GF1 DRAM upload stalls at song boundaries.
- **Route completeness improved across the rounds** as the TAS bugs were fixed
  (0283 input hold, 0286 stall discount, 0288 wrap). Cells short of 11/11
  measured less work and are not comparable on framerate.

## Comparing the next run

For non-pump cells, `inter_flip` medians should land within the +/-2 fps noise
floor of the values above -- that is the check that 0289 changed measurement
only, not performance. For pump cells there is nothing valid to compare
against; `[fps-true]` gives them a first honest number.


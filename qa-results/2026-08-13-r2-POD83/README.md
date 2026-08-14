# Round 2 -- POD-83 (reference machine)

**17 cells, 17/17 complete route.** Pulled as `r2f-pod83`.

Binary `fe44805fb603`, reel `4118561edf26`, saves `32529e291e0f` (map 20,
Polar Star). S3 ViRGE + UniVBE 6.70 throughout. PicoGUS in SB mode for
`RB`/`EAR`/`PROVE` (`irq=7`), Vibra16 for `GAP` (`irq=5 dma=5`).

| Cell | Sweep | Backend | per-loop | overhead | median |
|---|---|---|---|---|---|
| `GE3` | EAR | organya | 28.42 | 27 s | 29.4 |
| `GE4` | EAR | opl3 | 30.82 | 22 s | 31.2 |
| `GEA` | EAR | adlib | 32.95 | 14 s | NA_pump |
| `GP0` | PROVE |  | 32.92 | 15 s | 34.5 |
| `GP3` | PROVE | organya | 28.44 | 30 s | 29.4 |
| `GP4` | PROVE | opl3 | 30.92 | 24 s | 32.3 |
| `GP4B` | PROVE | opl3 | 30.87 | 23 s | 32.3 |
| `GP5` | PROVE | adlib | 32.93 | 16 s | NA_pump |
| `GP5K` | PROVE | adlib | 32.08 | 19 s | NA_pump |
| `GR3` | RB | organya | 28.37 | 28 s | 29.4 |
| `GR4` | RB | opl3 | 30.74 | 20 s | 32.3 |
| `GR4B` | RB | opl3 | 30.87 | 21 s | 32.3 |
| `GR5` | RB | adlib | 32.86 | 14 s | NA_pump |
| `GX0` | GAP |  | 32.84 | 13 s | 34.5 |
| `GX8` | GAP | opl3 | 30.76 | 22 s | 32.3 |
| `GXH1` | GAP | organya | 20.45 | 60 s | 21.3 |
| `GXH2` | GAP | organya | 20.42 | 60 s | 21.7 |

`per_loop_fps` = `flips / 102`. `median_fps` is `NA_pump` wherever the
PIT/IRQ-0 music pump is running, because it corrupts the DJGPP `uclock`
the medians are derived from -- use `per_loop_fps` for those cells.

Findings and cross-CPU comparison: [`../MATRIX-ROUND2.md`](../MATRIX-ROUND2.md).

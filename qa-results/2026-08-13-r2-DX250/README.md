# Round 2 -- 486DX2-50

**17 cells, 17/17 complete route.** Pulled as `r2f-dx250`.

Binary `fe44805fb603`, reel `4118561edf26`, saves `32529e291e0f` (map 20,
Polar Star). S3 ViRGE + UniVBE 6.70 throughout. PicoGUS in SB mode for
`RB`/`EAR`/`PROVE` (`irq=7`), Vibra16 for `GAP` (`irq=5 dma=5`).

| Cell | Sweep | Backend | per-loop | overhead | median |
|---|---|---|---|---|---|
| `5E3` | EAR | organya | 14.99 | 67 s | 14.5 |
| `5E4` | EAR | opl3 | 17.40 | 55 s | 17.2 |
| `5EA` | EAR | adlib | 18.46 | 26 s | NA_pump |
| `5P0` | PROVE |  | 18.34 | 25 s | 18.5 |
| `5P3` | PROVE | organya | 14.94 | 69 s | 14.1 |
| `5P4` | PROVE | opl3 | 17.28 | 58 s | 17.2 |
| `5P4B` | PROVE | opl3 | 17.19 | 57 s | 17.2 |
| `5P5` | PROVE | adlib | 18.58 | 27 s | NA_pump |
| `5P5K` | PROVE | adlib | 18.45 | 30 s | NA_pump |
| `5R3` | RB | organya | 14.97 | 68 s | 14.5 |
| `5R4` | RB | opl3 | 17.23 | 55 s | 17.2 |
| `5R4B` | RB | opl3 | 17.29 | 55 s | 17.2 |
| `5R5` | RB | adlib | 18.59 | 26 s | NA_pump |
| `5X0` | GAP |  | 18.09 | 23 s | 18.5 |
| `5X8` | GAP | opl3 | 17.10 | 56 s | 16.7 |
| `5XH1` | GAP | organya | 14.13 | 187 s | 8.0 |
| `5XH2` | GAP | organya | 14.14 | 188 s | 8.0 |

`per_loop_fps` = `flips / 102`. `median_fps` is `NA_pump` wherever the
PIT/IRQ-0 music pump is running, because it corrupts the DJGPP `uclock`
the medians are derived from -- use `per_loop_fps` for those cells.

Findings and cross-CPU comparison: [`../MATRIX-ROUND2.md`](../MATRIX-ROUND2.md).

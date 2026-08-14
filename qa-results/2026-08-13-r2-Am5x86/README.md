# Round 2 -- Am5x86-133

**17 cells, 17/17 complete route.** Pulled as `r2f-am5x86`.

Binary `fe44805fb603`, reel `4118561edf26`, saves `32529e291e0f` (map 20,
Polar Star). S3 ViRGE + UniVBE 6.70 throughout. PicoGUS in SB mode for
`RB`/`EAR`/`PROVE` (`irq=7`), Vibra16 for `GAP` (`irq=5 dma=5`).

| Cell | Sweep | Backend | per-loop | overhead | median |
|---|---|---|---|---|---|
| `AE3` | EAR | organya | 28.46 | 33 s | 29.4 |
| `AE4` | EAR | opl3 | 30.92 | 29 s | 32.3 |
| `AEA` | EAR | adlib | 32.75 | 14 s | NA_pump |
| `AP0` | PROVE |  | 32.52 | 14 s | 34.5 |
| `AP3` | PROVE | organya | 28.52 | 33 s | 30.3 |
| `AP4` | PROVE | opl3 | 31.03 | 27 s | 31.2 |
| `AP4B` | PROVE | opl3 | 31.03 | 27 s | 32.3 |
| `AP5` | PROVE | adlib | 32.78 | 16 s | NA_pump |
| `AP5K` | PROVE | adlib | 31.90 | 20 s | NA_pump |
| `AR3` | RB | organya | 28.22 | 35 s | 30.3 |
| `AR4` | RB | opl3 | 30.89 | 28 s | 32.3 |
| `AR4B` | RB | opl3 | 30.88 | 27 s | 32.3 |
| `AR5` | RB | adlib | 32.54 | 16 s | NA_pump |
| `AX0` | GAP |  | 32.60 | 15 s | 34.5 |
| `AX8` | GAP | opl3 | 30.92 | 28 s | 32.3 |
| `AXH1` | GAP | organya | 20.35 | 68 s | 21.3 |
| `AXH2` | GAP | organya | 20.25 | 70 s | 21.3 |

`per_loop_fps` = `flips / 102`. `median_fps` is `NA_pump` wherever the
PIT/IRQ-0 music pump is running, because it corrupts the DJGPP `uclock`
the medians are derived from -- use `per_loop_fps` for those cells.

Findings and cross-CPU comparison: [`../MATRIX-ROUND2.md`](../MATRIX-ROUND2.md).

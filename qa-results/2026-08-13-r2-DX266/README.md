# Round 2 -- 486DX2-66

**17 cells, 17/17 complete route.** Pulled as `r2f-dx266`.

Binary `fe44805fb603`, reel `4118561edf26`, saves `32529e291e0f` (map 20,
Polar Star). S3 ViRGE + UniVBE 6.70 throughout. PicoGUS in SB mode for
`RB`/`EAR`/`PROVE` (`irq=7`), Vibra16 for `GAP` (`irq=5 dma=5`).

| Cell | Sweep | Backend | per-loop | overhead | median |
|---|---|---|---|---|---|
| `6E3` | EAR | organya | 20.70 | 52 s | 20.8 |
| `6E4` | EAR | opl3 | 22.84 | 44 s | 23.8 |
| `6EA` | EAR | adlib | 24.79 | 21 s | NA_pump |
| `6P0` | PROVE |  | 24.49 | 20 s | 25.6 |
| `6P3` | PROVE | organya | 20.65 | 52 s | 21.3 |
| `6P4` | PROVE | opl3 | 22.87 | 44 s | 23.8 |
| `6P4B` | PROVE | opl3 | 22.92 | 44 s | 23.8 |
| `6P5` | PROVE | adlib | 24.81 | 21 s | NA_pump |
| `6P5K` | PROVE | adlib | 24.30 | 23 s | NA_pump |
| `6R3` | RB | organya | 20.65 | 49 s | 21.3 |
| `6R4` | RB | opl3 | 22.92 | 42 s | 23.8 |
| `6R4B` | RB | opl3 | 22.88 | 42 s | 23.8 |
| `6R5` | RB | adlib | 24.88 | 20 s | NA_pump |
| `6X0` | GAP |  | 24.17 | 22 s | 25.6 |
| `6X8` | GAP | opl3 | 22.69 | 42 s | 23.8 |
| `6XH1` | GAP | organya | 13.29 | 101 s | 11.9 |
| `6XH2` | GAP | organya | 13.26 | 103 s | 12.8 |

`per_loop_fps` = `flips / 102`. `median_fps` is `NA_pump` wherever the
PIT/IRQ-0 music pump is running, because it corrupts the DJGPP `uclock`
the medians are derived from -- use `per_loop_fps` for those cells.

Findings and cross-CPU comparison: [`../MATRIX-ROUND2.md`](../MATRIX-ROUND2.md).

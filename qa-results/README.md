# qa-results -- raw real-hardware QA + benchmark data

Raw logs from real-hardware runs on the reference machine ("g2k"), kept so a
result can be re-examined later rather than re-measured. One directory per
run: `<date>-<cpu>-<sound card>/`.

Each cell log carries, without needing anything else:

- `[fps-true] flips=N render_s=N` -- **the framerate source.** Decompose it:
  `per-loop fps = flips / 102`, `overhead_s = render_s - 102`. A single number
  conflates loop speed with load-stall wall time.
- `inter_flip_ms=` per frame (median + p95) -- valid **only** in a cell whose
  log does NOT contain `PIT/IRQ-0 pump` or `OPL timer pump STARTED`. Every
  `gus` and `adlib` cell runs that pump, which reprograms PIT ch0 and so
  corrupts `uclock()`, SDL's only DOS timebase. Quoting a median from one of
  those cells has produced two retracted findings. Where it is valid it is a
  useful independent second metric.
- `>> Entering stage N` -- the replayed route, for checking TAS fidelity.
  Check the route before trusting any framerate: a truncated route measured
  less work than a complete one.
- the audio backend that actually initialised, vs what the CFG asked for
- `dur=` in the closing runmanifest line
- warnings the operator could not have seen on screen

Full reading rules: `docs/TAS-BENCHMARKING.md`. Cell and lane definitions:
`docs/QA-CELL-REFERENCE.md`.

## Analysis

| Doc | Covers |
|---|---|
| `MATRIX-POD83.md` | Audio-backend matrix, POD-83 + PicoGUS + S3 ViRGE |
| `MATRIX-VIDEO-POD83.md` | Video-card matrix, POD-83: ViRGE vs Cirrus vs Mach64 |
| `BASELINE.md` | Pre-campaign reference figures |

`QA.TAS` is the input recording that drove the run. Keep it with the logs:
without it the routes and durations cannot be interpreted.

These are engine logs and an input recording -- no Cave Story assets.

## Runs

| Directory | Machine | Binary | Notes |
|---|---|---|---|
| `2026-08-06-tas-roundtrip` | POD-83 | 0282 diag | Record-vs-replay trace pair that found the TAS input-hold bug (nx 0283). `TR` = record, `TP` = replay. |
| `2026-08-06-POD83-picogus` | POD-83 + PicoGUS + S3 ViRGE | v1.6.5 `b421e5a52fea` | First benchmark round, 9 cells. 4 of 9 truncated the reel's last two transitions -- see CAMPAIGN-NOTES.md. |
| `2026-08-09-POD83-picogus-v167` | POD-83 + PicoGUS + S3 ViRGE | v1.6.7 `101d95c16522` | 10 cells. OPL3/WB cells clean (full route, 12-16 genuine stall discounts); GUS/AdLib cells corrupted by the 0286 unsigned-wrap bug fixed in 0288 -- 485+ ordinary frames misread as stalls. Treat `GC4 G31 G51 G17` as valid and the rest as superseded. |
| `2026-08-10-POD83-picogus-v170` | POD-83 + PicoGUS + S3 ViRGE | v1.7.0 `09e449c5a81d` | **First fully clean round: 10 of 10 cells replayed the complete route.** Stall counts 3-14, all plausible. `[fps-true]` valid in every cell. Non-pump `inter_flip` medians within +/-1 fps of BASELINE, confirming 0290 changed fidelity and measurement, not performance. **This is the POD-83 column of the matrix.** |
| `2026-08-10-POD83-picogus-v170-sfx` | POD-83 + PicoGUS + S3 ViRGE | v1.7.0 `09e449c5a81d` | 13 cells, 13/13 full route. Adds G60 (silent render ceiling), G61 (OPL3 no-SFX), G62 (AdLib no-SFX) to test whether AdLib's lead comes from PC-speaker SFX. **It does not** -- the gap persists with SFX off. Real finding: GUS and Organya cost ~4 fps against the ceiling; FM backends cost ~0. Anomaly: AdLib measures 0.8-1.2 fps FASTER than total silence, which is impossible as a cost -- treat sub-2 fps differences among the fast backends as unestablished. |


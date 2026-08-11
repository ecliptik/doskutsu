# qa-results -- raw real-hardware benchmark data

Every measurement DOSKUTSU quotes comes from here. Raw engine logs from runs on
the reference machine ("g2k"), kept so a result can be re-examined rather than
re-measured.

**Presentation report: [`docs/BENCHMARKS.md`](../docs/BENCHMARKS.md).** Start
there if you want the findings. This directory is the evidence behind them.

These are engine logs and an input recording -- no Cave Story assets.

---

## Rounds

Data is organised into **rounds**. A round is one binary: every cell in it was
produced by the same `DOSKUTSU.EXE` replaying the same input recording, so
cells within a round are directly comparable and cells across rounds are not,
until a re-baseline establishes they are.

| Round | Binary | Status |
|---|---|---|
| **Round 1** | `09e449c5a81d` (v1.7.0) | **CANONICAL** -- every published figure |
| Pre-round-1 | various | superseded, kept for provenance only |

### Round 1 -- 89 cells, four CPUs, 89/89 complete route

- **Binary:** `DOSKUTSU.EXE` sha256 begins `09e449c5a81d`
- **Organya cache key:** `f8d446b4b0e0`
- **Kit:** v170. **Reel:** `QA.TAS`, ~5100 ticks = 102 s of game time
- Same motherboard, same CF card, same reel in every cell. Only the named
  component changes between datasets.

| Dataset | CPU | Cells | What it covers |
|---|---|---|---|
| `2026-08-10-POD83-picogus-v170-sfx` | POD-83 | 13 | Full audio-backend sweep + SFX isolation. The POD-83 column. |
| `2026-08-11-POD83-vibra-VB1` | POD-83 | 6 | Vibra16 + DreamBlaster. Gives the sound-card comparison. |
| `2026-08-11-POD83-video-lanes` | POD-83 | 4 | ViRGE vs Cirrus CL-GD5430, C4 and G51 each. |
| `2026-08-11-Am5x86-full` | Am5x86-133 | 22 | Full sweep + Vibra + both video cards. |
| `2026-08-11-DX266-full` | 486DX2-66 | 22 | Same, on the 66 MHz part. |
| `2026-08-11-DX250-full` | 486DX2-50 | 22 | Same, on the 50 MHz part. |

Each dataset directory has its own `README.md` with that round's table,
hardware witnesses and caveats.

**[`ROUND1-CELLS.csv`](ROUND1-CELLS.csv) is the canonical extraction** -- all 89
cells, one row each, with the derived `per_loop_fps` and `overhead_s` already
computed. Regenerate it with `./extract-cells.sh` from this directory. Use the
CSV rather than re-parsing logs by hand; two published figures have been wrong
because they were derived by hand instead.

### Excluded from round 1, deliberately

| What | Why |
|---|---|
| `2026-08-11-mach64-corruption` | Lane F aborted. The ATI Mach64's VBE offers no mode below 512x384, so the game drew a 320x240 screen into a 512x384 surface. No usable frame rate; the photographs and the diagnosis are kept. |
| `5112` and `6112` (Organya-HQ) | The 486DX2-50 scored *higher* than the 486DX2-66 on this cell, which cannot be true of identical work. Both figures are suspect and are quoted nowhere. Needs re-measuring. |
| `2026-08-10-POD83-picogus-v170` | Same binary, but superseded by the `-sfx` round which is a superset (13 cells against 10). Kept because it was the first fully clean round. |

### Pre-round-1 -- superseded

Kept for provenance. **None of these carry `[fps-true]`**, which landed in patch
0289, so their frame rates came from a clock that later proved unreliable. Do
not mix them with round 1.

| Directory | Binary | Why superseded |
|---|---|---|
| `2026-08-06-tas-roundtrip` | 0282 diag | Record-vs-replay trace pair that found the TAS input-hold bug (nx 0283). Not a benchmark. |
| `2026-08-06-POD83-picogus` | v1.6.5 `b421e5a52fea` | First round, 9 cells. 4 of 9 truncated the reel. |
| `2026-08-09-POD83-picogus-v167` | v1.6.7 `101d95c16522` | GUS/AdLib cells corrupted by the 0286 unsigned-wrap bug, fixed in 0288. |

---

## Reading a cell log

- **`[fps-true] flips=N render_s=N` is the frame-rate source.** Decompose it:
  `per-loop fps = flips / 102`, `overhead_s = render_s - 102`. A single number
  conflates loop speed with load-stall wall time.
- **`inter_flip_ms=` medians are valid only where the log does NOT contain
  `PIT/IRQ-0 pump` or `OPL timer pump STARTED`.** Every `gus` and `adlib` cell
  runs that timer, which reprograms PIT channel 0 -- the same channel
  `uclock()` reads, and `uclock` is the DOS backend's only timebase. Quoting a
  median from one of those cells produced two retracted findings. The CSV marks
  them `NA_pump`.
- **Check the route before trusting any frame rate.** `>> Entering stage N`
  gives the replayed route; a cell that ended early rendered less work. All 89
  round-1 cells ran the complete route.
- Hardware is witnessed in the log, not assumed: video mode and framebuffer
  path, sound card identity and DMA path. **The CPU is the exception** -- no log
  records it, so a dataset's CPU rests on the operator having told the test
  script which chip was installed.
- `QA.TAS` is the input recording. Keep it with the logs; without it the routes
  and durations cannot be interpreted.

Full reading rules: [`docs/TAS-BENCHMARKING.md`](../docs/TAS-BENCHMARKING.md).
Cell and lane definitions:
[`docs/QA-CELL-REFERENCE.md`](../docs/QA-CELL-REFERENCE.md).

### A provenance defect to know about

The `[RUNMANIFEST]` block in each log emits `binary_sha12=f8d446b4b0e0`. **That
is the Organya cache key, not the binary's hash** -- the binary's sha256 begins
`09e449c5a81d` and appears in no log at all. The two are correlated, because a
source change moves both, but any "same binary" check made against that field
was checking something else. The binary identity for round 1 is recorded above
because the logs cannot supply it.

---

## Analysis

| Doc | Covers |
|---|---|
| [`../docs/BENCHMARKS.md`](../docs/BENCHMARKS.md) | **The report.** Charts, findings, method, limits. |
| `MATRIX-CROSS-CPU.md` | Four CPUs against each other. The campaign's main result. |
| `MATRIX-POD83.md` | Audio-backend matrix on the reference machine. |
| `MATRIX-VIDEO-POD83.md` | Video cards: ViRGE, Cirrus, Mach64. |
| `BASELINE.md` | Pre-campaign reference figures. |

---

## Adding round 2

Round 2 is expected to carry fixes found during round 1. Because those fixes
change the binary, they move the Organya cache key, and **every round-1 number
is strictly from a binary that no longer exists.**

To keep round 1 usable rather than discarding 89 cells:

1. **Land all the changes in one binary.** The expensive part is not the build,
   it is the cache re-render and CF re-populate behind it.
2. **Re-baseline with three cells per CPU**, not one. Each is there to catch a
   different class of change:
   - `C4` (OPL3) -- the control, no music timer, isolates observer effect
   - `C5` (AdLib) -- the music-timer path, which a timebase fix changes and
     `C4` cannot see
   - `C3` (Organya) -- exercises the re-rendered cache, which nothing else does
3. **Two back-to-back runs of one cell** to measure the noise floor directly.
   Round 1's ~0.2 fps figure came from configurations that happened to repeat.
4. **Compare against `ROUND1-CELLS.csv`.** If the control cell agrees within the
   measured noise floor, round 1 carries forward and only the new work is new.
   If it does not, the change altered what is being measured and that has to be
   quantified before anything else is trusted.
5. **Movement in the AdLib cell is a finding about the fix**, not baseline
   drift, and should be recorded as such.

Store round 2 as new `<date>-<machine>-<what>/` directories and add a Round 2
section above. Do not overwrite round-1 directories, and do not mix rounds in
one table without saying which binary produced each column.

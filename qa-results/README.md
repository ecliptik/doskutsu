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
| **Round 2** | `fe44805fb603` | **CURRENT** -- 68 cells, four CPUs |
| **Round 1** | `09e449c5a81d` (v1.7.0) | **CANONICAL** -- most published figures |
| Pre-round-1 | various | superseded, kept for provenance only |

The two rounds are comparable above about 2%. Below that they are not: round
2's OPL3 control measures ~1% low against round 1 on all four CPUs. See
[`MATRIX-ROUND2.md`](MATRIX-ROUND2.md).

### Round 1 -- 89 cells, four CPUs, 89/89 complete route

- **Binary:** `DOSKUTSU.EXE` sha256 begins `09e449c5a81d`
- **Organya cache key:** `f8d446b4b0e0`
- **Kit:** v170
- **Reel:** `QA.TAS` sha256 begins **`4118561edf26b93a`**, 1956 bytes,
  ~5100 ticks = 102 s of game time. Banked as `QA-USED.TAS` (or `QA.TAS`) in
  each round-1 dataset directory; all copies verified identical.
- Same motherboard, same CF card, same reel in every cell. Only the named
  component changes between datasets.

**The round-1 reel is an operator recording, NOT the reel shipped in the kit.**
The payload's `DOSKUTSU/QA.TAS` is a fallback whose sha begins `5c661a8723e5`,
and no round-1 cell used it. `install-qa-v163.sh` stashes an existing
non-fallback `QA.TAS` off the CF, extracts, and restores it -- so a populate
onto a card that already holds the take preserves it.

**That protection depends on the take already being on the card.** Populating a
blank or replaced CF installs the fallback instead, and every cell would then
replay a different reel from round 1 while looking completely normal. Any
future round must either run on a card carrying `4118561edf26b93a` or ship it
as the payload's reel. Check the sha before trusting a comparison; the earlier
superseded rounds used a third reel (`30dd4b7050468eeb`, 2044 bytes), which is
part of why they are not comparable.

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
| ~~`5112` and `6112` (Organya-HQ)~~ | **EXCLUSION REVERSED 2026-08-13.** Round 2 re-ran this cell twice per CPU and both figures reproduce within noise (`5112` 14.08 against 14.13/14.14; `6112` 13.56 against 13.29/13.26). The measurements were never wrong. What misled was the metric: on this cell the HQ cost lands in load-stall overhead rather than loop rate, and does so more as the CPU slows -- so per-loop comparisons across CPUs compare different things. Both figures are restored, with that caveat. |
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

### Round 2 -- 68 cells, four CPUs, 68/68 complete route

- **Binary:** `DOSKUTSU.EXE` sha256 begins `fe44805fb603`
- **Build fingerprint / Organya cache key:** `1f79ce20e4ee`
- **Reel:** `QA.TAS` `4118561edf26`, unchanged from round 1
- **Saves:** `32529e291e0f` -- map 20 'Save Point', Polar Star, both slots.
  Round 1's saves were destroyed; these are rebuilt and validated by replaying
  the reel against the round-1 binary and diffing the stage route.
- **Video:** S3 ViRGE + UniVBE 6.70 on every cell (`oem_string=` witnessed).
- **Sound:** PicoGUS SB mode `irq=7` for `RB`/`EAR`/`PROVE`; Vibra16
  `irq=5 dma=5` for `GAP`.

| Dataset | CPU | Cells | What it covers |
|---|---|---|---|
| `2026-08-13-r2-DX250` | 486DX2-50 | 17 | `RB` `EAR` `PROVE` `GAP` |
| `2026-08-13-r2-DX266` | 486DX2-66 | 17 | same |
| `2026-08-13-r2-Am5x86` | Am5x86-133 | 17 | same |
| `2026-08-13-r2-POD83` | POD-83 | 17 | same |
| `2026-08-13-r2-mach64` | 486DX2-50 | 3 | **not comparable** -- ran on the card's own VBE 2.0 BIOS |

**[`ROUND2-CELLS.csv`](ROUND2-CELLS.csv) is the canonical extraction**, produced
by `extract-cells-r2.sh` -- same script shape as round 1, so both rounds are
derived identically. Findings: [`MATRIX-ROUND2.md`](MATRIX-ROUND2.md).

What round 2 added that round 1 lacked: a measured noise floor per CPU, a
direct A/B of the pump-aware timebase (`PROVE`), a true audio-off floor
(`X0`/`P0`), and the Organya-HQ re-measure that reversed the exclusion above.

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

## Round 2 -- what it did, and what it left open

Round 2 followed the plan below and it worked: one batched binary, re-baselined
against the reel, four CPUs. Recording how it actually went, since the recipe
was written blind.

**What held.** Landing every change in one binary was right -- the expensive
part was never the build, it was the Organya cache re-render and the CF
populate behind it. The three-cell re-baseline (`R4` control, `R5` pump path,
`R3` cache) caught what it was meant to. Running the control **five times
across three sweeps** rather than once is what made a systematic 1% deficit
separable from a 0.20 noise floor; a single control cell per CPU would have
read it as noise, and did, on the first CPU analysed.

**What the recipe missed.**

1. **The reel needs a save, and the save needs auditing.** Round 1's saves were
   destroyed by a populate. The reel opens Load Game and enters map 20; without
   the right save it replays a different run while every log looks ordinary. A
   rebuilt save that reproduced the route perfectly still measured wrong,
   because it carried no weapon and the reel fires throughout -- about 6% less
   draw work per frame. Both states pass every check in the original plan.

2. **Provenance has to be witnessed per cell, not assumed per session.** One
   lane ran on the video card's own BIOS instead of UniVBE and produced a mode
   list, a resolution and a frame rate that compare to nothing. `oem_string=`
   and the sound-path `irq=` are now checked on every pull.

3. **Per-loop fps is not a universal metric.** On Organya-HQ the cost lands in
   overhead rather than loop rate, increasingly so as the CPU slows -- which is
   what got two sound cells wrongly excluded from round 1.

Store further rounds as new `<date>-<machine>-<what>/` directories. Do not
overwrite existing round directories, and do not mix rounds in one table
without saying which binary produced each column.


# The Mach64 30-fps campaign, in reading order

Moved here 2026-09-11 from vcctrl's `docs/README.md`, where it was written
alongside the harness that measured it. This campaign is about the engine's
own render path (`Renderer.cpp`, `main.cpp`), not the harness, so it lives
here instead. Each document answers the question the previous one raised.

| doc | question | headline |
|---|---|---|
| `MACH64-PHASE0-RESULTS.md` | what is a frame made of? | the flip is **17.8%** of it; VRAM write 4.77 ms |
| `MACH64-PHASE0B-RESULTS.md` | what is the other 82%? | the tilemap is **49.8%** — half the frame |
| ↳ same file, TAUD + yield pair | where does audio go? | **2.35 ms**, and 61% of it lands in `fb_yield` |
| `T1-K-PREDICTION.md` | pinned before the data | direction right, magnitude wrong 5x |
| `T1-TILEMAP-RESULTS.md` | do the tilemap levers help? | +1.00, +0.50, and **−9.35** |
| `T1-CONFIRM-RESULTS.md` | do they reproduce, and do they add? | dword **ships**; asm **unpinned**; and the band was never capable |

**Current perf baseline: 27.6 fps** (Round R, eight cells, A-to-A spread 0.10).

Scored with `tools/score-pump.py`, moved here alongside these docs — see its
own docstring for what it does and does not compare.

## RETRACTIONS — do not quote these

- **"±0.1 fps within-session repeatability"** — quoted all week, used as the
  T1 confirming round's acceptance band, and it is **integer truncation in the
  metric** (`main.cpp:1530`), not measurement noise. The system's real same-config
  pair spread is **0 to 16 flips**, and 0.1 fps is 10.3 flips — **narrower than
  the repeatability it was gating.** See `T1-CONFIRM-RESULTS.md` and vcctrl's
  `docs/HARNESS-STANDARD.md` 10.0e. Express a band in counted units and check
  it against the archive before running.
  **And do not quote the 16 as the system's repeatability either** — it is a
  *pair* spread. The accumulated stock population, nine cells over three
  sittings, spans **31 flips (0.30 fps)**, `ACC1` 2827 to `FRS1` 2858. Pair
  spread and population range are the two bands of vcctrl's
  `docs/HARNESS-STANDARD.md` 10.0; say which one you are reading against.
  vcctrl's `docs/lab/PI5-MIGRATION.md` sec. 6 carries the amendment.
- **`ASM_BLIT` "+0.50"** (`T1-TILEMAP-RESULTS.md`). It did not reproduce: the
  confirming round gives **+0.389** against T1's **+0.486**. The lever is real
  (2.5x the noise floor) but its **magnitude is unpinned**, and it adds at most
  a fifth of that on top of `TILE_DWORD_COPY`.
- **`started_rtc_local`** — corrupt in the manifest; never use it for ordering
  or sitting membership. vcctrl's `docs/lab/OPEN-FAULTS.md` §10.
- **"30.2 fps with audio off"** (`MACH64-PHASE0B-RESULTS.md`). A *diag* number.
  The transferable quantity is the **2.35 ms delta**, which applied to the
  clean 27.6 gives **29.5 fps — short of the target.**
- **`stationary_frac` mean 0.11** (`T1-TILEMAP-RESULTS.md`). A **block-size
  artifact**: the same reel at ~10 ticks/block gives 0.004 against ~285
  ticks/block giving 0.11. **The median 0.00 is the defensible statement.** The
  ~0.26 fps adaptive-gate estimate built on it goes too.
- **"no usable Mach64 glass since MQ2"** (vcctrl's `docs/lab/FINDINGS.md` sec.
  36). MQ3's file was 94.5% the *previous* cell's frames. Corrected in place.

**Every one of these was live in a pushed document before being caught.** That
is why this section exists.

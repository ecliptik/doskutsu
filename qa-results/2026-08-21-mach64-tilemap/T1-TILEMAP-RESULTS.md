# T1: three tilemap levers, screened

Measured 2026-08-21. Thirteen cells, all exit 0, one daemon (459551,
`NRestarts 0`), one binary. `perf` class — no instrumentation. Round R's
27.6 fps is the comparable baseline.

Aimed at the largest bucket in the frame: Phase 0b put the tilemap at 17.79 ms
of 35.7 ms, **49.8%**.

---

## Result

    TILE_DWORD_COPY   W1A 27.7  W2B 28.6  W3B 28.7  W4A 27.6   delta +1.00  GAIN
    TILEMAP_CACHE     K1A 27.5  K2B 18.1  K3B 18.2  K4A 27.5   delta -9.35  LOSS
    ASM_BLIT          S1A 27.5  S2B 28.1  S3B 28.1  S4A 27.7   delta +0.50  GAIN

    all six A cells: 27.7 27.6 27.5 27.5 27.5 27.7
    A-to-A spread across the whole sitting: 0.20   (invalid above 0.6)

**The sitting is stable, so every delta from it counts.**

**Combined +1.50 fps -> 29.1 against a 30.0 target.** Below the +2.4 ship
threshold, so the pre-registered outcome is confirming blocks, not a ship.

**The combined figure assumes the two winners are additive and nothing tested
that.** Both touch the tile blit path — `TILE_DWORD_COPY` dispatches whole
opaque tiles to a dword copy, `ASM_BLIT` builds opaque-run tables for
`_blit_indexed` — so they may contend for the same pixels. **A both-on arm is
the only thing that answers it and was in nobody's design.**

---

## The cache: not "it loses", but "this reel puts it at neither end"

Costs measured from KPRV (`diag`, carries the instrument), 18 blocks:

    bg_ms_h   4.88     bg_ms_m  19.37      uncached BG  10.24
    fg_ms_h   9.32     fg_ms_m  24.00      uncached FG   7.02
    bg and fg hit rate: 44.6% EACH -- one gate, both layers, block for block

**A miss costs 1.9x (BG) to 2.4x (FG) the uncached path**, because it renders
into the cache *and* blits it out. Two-layer model against the 27.5 fps
control:

       H        total     fps
     0.0%     43.37 ms   16.0
    25.3%        —       18.2    <- reproduces the observed loss
    44.6%     30.36 ms   20.3    <- KPRV's measured rate; model over-shoots
   100.0%     14.20 ms   30.0    <- a perfect cache reaches the target alone

**So the finding is: the cache is a ~3 ms win above a 62.9% hit rate and a
26 ms catastrophe at zero, and this reel puts it at neither end.**

The lever is not broken. It is being asked to cache a scrolling reel where it
misses most of the time.

**Remaining gap, not closed:** the observed loss needs 25.3% hits; KPRV
measured 44.6%. Different cells, different scene mixes. **The K cells' own hit
rates do not exist — `perf` class carries no instrumentation.** That is a
design limit of the class, not an oversight, and it is why the claim stays
*"below break-even on this reel"* with the threshold attached rather than
becoming a hit-rate number nobody measured.

---

## THE LOAD-BEARING ASSUMPTION: is QA.TAS representative?

`stationary_frac` over 18 blocks: **median 0.00**, with rare fully-stationary
blocks at 1.00 — which is what produces the 100%-hit blocks and the 0-100%
swing.

**RETRACTED: an earlier version of this section quoted "mean 0.11" and used it
to size an adaptive gate at ~0.26 fps. Both figures are withdrawn.**

The mean is a **block-size artifact**, not a property of the reel. Both
counters increment per game tick, so the quantity is hardware-independent — but
the block boundary is per-100-FLIPS, and the flip:tick ratio is not:

    hardware    18 blocks over ~5140 ticks   ~285 ticks/block   mean 0.11
    local (DOSBox-X, same reel)
               505 blocks over ~5140 ticks    ~10 ticks/block   mean 0.004

**A mean of block ratios across a 28x difference in block size is not a
comparable statistic.** One fully-stationary block contributes 1/18 = 0.056 to
the hardware mean and 1/505 = 0.002 to the local one. The distribution is
bimodal — almost all blocks exactly 0.00, rare blocks at 1.00 — and a mean is
the wrong summary for that shape at any block size.

**The median 0.00 is what both runs agree on and it is the defensible
statement.** The two means are not fully reconciled and no explanation is
offered for the residual difference; the comparable quantity would be pooled
stationary-ticks over total-ticks, which neither run emits.

**But that is a fact about the reel, not about the game.** QA.TAS is a scripted
run that moves almost continuously. Real play is stationary far more than 11%
of the time: dialogue, menus, save points, standing to read a sign, the pause
screen. **If actual play sits at 40-60% stationary, the cache is between
break-even and a 3 ms win, and the -9.35 fps measured here overstates what a
player would see.**

**This is the first time the campaign has hit a question where the reel's
representativeness is load-bearing.** QA.TAS was designed for reproducibility,
not for being typical. Recorded as an open question; not resolvable by
argument.

---

## THE ADAPTIVE SAFETY IS INVERTED, AND COULD NOT HAVE FIRED

Machinery exists that is meant to notice a cache that keeps missing and stand
it down. **It did not prevent a 9 fps loss, and tracing it shows it never
could have.** `map.cpp:3094`:

    disable the cache after 10 consecutive frames where
        miss_render_time_ns < threshold_ns

    BG threshold    8.6 ms      measured BG miss   19.37 ms   (2.25x above)
    FG threshold   15.4 ms      measured FG miss   24.00 ms   (1.56x above)

**Both measured miss costs sit ABOVE their thresholds, so the condition is
never true, the counter never increments, and the cache is never stood down.**
The safety could not have fired during T1 no matter how long the round ran.

**And the logic is backwards for the failure we hit.** Its intent is "if a miss
is cheap, the cache is not earning its keep, so disable it" — a guard against a
*mild* loss. But **an expensive miss is precisely when the cache costs the
most**, and this machinery reads an expensive miss as a reason to keep going.
It is structurally blind to the catastrophic case and protects only against
the trivial one.

**Retuning the thresholds cannot fix it.** The cache is a loss when the hit
rate is below break-even, and break-even depends on the costs *and* the hit
rate. The gate looks at miss cost alone, so **no threshold value makes it
correct — it is measuring the wrong quantity.**

That is the same error as a millisecond threshold gating a bandwidth question
(see `MACH64-PHASE0-RESULTS.md`): a well-formed instrument pointed at the wrong
subject. Here it is in the source rather than in a spec.

**Whether to patch it depends on whether the cache is worth saving at all** —
which is the representativeness question below, and it now has a sharper form:
**at 100% hits the cache alone reaches 30.15 fps.** If real play is materially
more stationary than this reel's 11%, the lever recorded here as catastrophic
may be the largest win available.

---

## Prediction post-mortem

A numeric prediction was pinned in this repo (`36f0db7`) **while the K cells
were still running and no fps figure had reached the harness** — `per_loop_fps`
only arrives at the NET-boot collect, so the pre-registration was enforced by
the transport rather than by intent.

    predicted   -1.5 to -2 fps
    observed    -9.35 fps

**Direction right, mechanism right, magnitude wrong by 5x.** Two errors, both
identified by their author:

1. **The model covered half the system.** `TILEMAP_CACHE` gates BOTH layers;
   only the BG break-even was computed. `fg_h=` and `fg_m=` were in the log
   line already in hand.
2. **The patch for (1) was derived from the residual** and came out low by
   7.31 ms against the measured `fg_ms_m` of 24.00.

**A residual can confirm a structure and cannot confirm a value**, because it
was computed to close the gap. It agreed with the measurement on shape — same
~2x ratio, both layers hitting together — and was wrong on magnitude.

---

## KPRV cost two cells and paid for both

`TILEMAP_CACHE` has no decision banner, so its four `perf` cells ran on
`--forbid` alone. `KPRV` is the substitute: a `diag` cell proving the lever
engages, run once, fps discarded.

- **First attempt refused** — the cache counter is gated by
  `MODE_DRAW_SCENE_INSTR`, not `MDS_DECOMP`. The marker had been matched to a
  hint by its shared `mds-` name prefix rather than traced. A positive control
  (`mds-decomp` present 273 times beside `tilemap-cache` absent) separated
  *instrument dead* from *wrong instrument* in minutes.
- **Second attempt passed**, and the K block was stopped 11 seconds into its
  first cell to check `bg_h` non-zero before four cells trusted it — because
  the marker printing proves the instrument ran, not that the cache did.

**The one lever with no attestation produced the -9 fps result.** Had it
silently failed to engage, K would have read flat and been recorded as *"cache
is free"* — a null that closes a question with the wrong answer, which is worse
than either a win or a loss.

---

## What T1 closes and what it carries forward

    CLOSED     TILEMAP_CACHE on this reel. Recorded, not retried.
    CARRIED    TILE_DWORD_COPY +1.00 and ASM_BLIT +0.50, each earning a
               confirming ABBA block before shipping.
    OPEN       additivity of the two winners (both-on arm)
               QA.TAS representativeness for the cache question
               the adaptive `cache_enabled` machinery that did not fire
               T1 does not reach 30 fps on its own at any additivity

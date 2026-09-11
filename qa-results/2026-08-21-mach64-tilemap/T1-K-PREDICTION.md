# Prediction for the TILEMAP_CACHE block, recorded BEFORE the data

**Committed 2026-08-21 while the K cells were still running and no fps figure
of any kind had reached this session.** `per_loop_fps` only arrives at the
NET-boot collect, so this could not have been written with the answer in hand.

Prediction is the benchmarking session's, from `Renderer.cpp:3392`. Recorded
here so it stands or falls as written.

## The mechanism

The cache counters are **per-event means**, not per-block totals:

    g_tilemap_bg_hit_total_ns_block / 1e6 / g_tilemap_bg_cache_hits_block

So from KPRV:

    cached bg draw     4.90 ms
    missed bg draw    19.30 ms
    uncached baseline 10.24 ms   (Phase 0b's mds_tilemap_bg 10.56,
                                  less the ~3% audio-smear correction)

**A miss costs nearly twice the uncached path**, because it renders into the
cache *and* blits it out — the dual-step that made `WORLD_CACHE` lose in
Round O on other hardware.

    break-even hit rate = (19.30 - 10.24) / (19.30 - 4.90) = 62.9%

## The prediction

**K returns a clear LOSS of roughly -1.5 to -2 fps**, past the pre-registered
-0.3 threshold.

    observed hit rates in KPRV: 36, 49, 100, 0   mean 46.25%  -- BELOW break-even

    H = 46.2%  ->  12.64 ms  vs 10.24  ->  +2.40 ms SLOWER
    H = 100%   ->   4.90 ms             ->  -5.34 ms faster
    H =   0%   ->  19.30 ms             ->  +9.06 ms slower

**If K comes back flat or positive, this reasoning is wrong** and the question
becomes which part.

## Stated weaknesses, before the fact

- **Cross-cell inference.** The 4.90/19.30 are from KPRV; the 10.24 from E1D.
  Different cells, different sittings.
- **The blocks disagree wildly** (0% to 100% hits), so a mean of 46% may not
  describe the K cells' scene mix at all.
- **`stationary_frac=0.00` on every observed line** while hits swing 0-100%.
  If nothing is stationary, something other than camera rest is producing
  100%-hit blocks, and nobody knows what. **Open question, not smoothed over.**

## What a null or negative would and would not mean

Break-even at 62.9% makes the reel-composition caveat concrete. **This lever is
not "good" or "bad" — it is good above 63% hits and bad below**, and the reel
swings the full range.

So a flat or negative K result means **"below break-even on this reel"**, not
"the cache does not work". The narrower claim is the honest one, and it now
has a threshold attached rather than being a hedge.

**The fps delta is ground truth. This prediction is refutable by it.**

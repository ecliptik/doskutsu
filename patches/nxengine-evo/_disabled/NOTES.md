# Disabled patches -- historical record

This directory holds nxengine-evo patches that were authored, real-HW
measured, and reverted as a deliberate engineering decision. They are
kept in-tree as a historical record of the architectural finding, but
`scripts/apply-patches.sh` skips them (it uses `find -maxdepth 1`, so
this subdirectory is not visited).

## Why this directory exists (mechanism)

When a patch is reverted by team consensus we have three options for
the slot:

- **(a)** Move the patch file to `_disabled/` and drop the
  corresponding commit from the vendor tree. apply-patches.sh skips
  the subdirectory; the slot stays free for the next experiment OR
  the patch can be re-enabled by moving it back out. **This option
  (a) is what wave-32 used to revert patch 0127.**
- **(b)** Author a new "revert" patch in a higher slot. Cleaner
  provenance (one positive-form patch per intent) but adds patch-count
  noise + every iter handoff has to explain "patch 0129 reverts patch
  0127" which is the same cognitive overhead as the _disabled/ form.
- **(c)** Apply the revert directly in the vendor tree without
  capturing the patch state in `patches/`. Most fragile -- gets re-
  applied on every `scripts/fetch-sources.sh` re-clone.

Option (a) is the workspace convention going forward. The slot number
stays "owned" by the disabled patch; new patches use the next free
slot.

## Disabled patches in this directory

### `0127-perf-phase11-wave31-leverb-output-cache-engine-signaled.patch`

**Disabled** 2026-05-11 (wave-32). See `docs/PHASE11-WAVE-31-FINDINGS.md`
+ `docs/PHASE11-WAVE-32-FINDINGS.md` for the full architectural
analysis.

**Mechanism.** Engine-signaled invalidation cache for the natural-
origin backdrop output: cache the parscroll-independent tiled backdrop
at `(screenWidth+bg_w) x (screenHeight+bg_h)`; on hit, apply parscroll
as a SRC offset in the cache->window blit; engine signals invalidation
on map change.

**Why it worked mechanically (per W31A1 measurement).** Cache hit-rate
was 100.0% (2292 hits / 0 misses across 23 emit blocks post-
stabilization). Engine-signaled invalidation fired correctly on scene
boundaries (`invalidate=N` markers visible at room transitions). The
mechanism was exactly as designed.

**Why it didn't deliver fps (per W31A1 / W32A1 cross-iter).** Caching
the OUTPUT means subsequent flips do `SDL_BlitSurface(cache_buffer ->
window_surface)` instead of `map_draw_backdrop()` rendering. Both are
~76,800-byte INDEX8 operations on PODP83 + Cirrus, both write-
dominant. Per `podp83_membw_real_hw_actuals.md` sysmem memcpy at 76,800
bytes is bandwidth-bound at ~17 MB/s (~4.30 ms). Cache hit saves the
COMPUTATIONAL work of computing parallax pixel-by-pixel (~13 ms) but
ADDS the cache-blit-OUT bandwidth (~13 ms). Net wall-clock: zero.

W31A1 measured 35.71 fps vs wave-30 W30A1 baseline 38.46 fps -- a
-2.75 fps regression. The +4 ms modal-peak shift (24 ms -> 28 ms) was
attributable to Lever B's per-flip cache-tracking overhead (hash
compute + hit/miss accounting + invalidation-check branches) being net
greater than the rendering work it eliminated. W32A1 (this patch
reverted, all else equal) measured 40.00 fps median -- confirming the
revert as the correct ship decision.

**Architectural class.** Same as `wave_17_4_partial_flush_regression.md`:
output-buffer caching does NOT reduce bandwidth-bound work when cache-
blit-OUT bandwidth equals underlying-render bandwidth. Verify the
cache-OUT cost is computationally cheaper than the render before
committing to a cache architecture.

**Coexists in source-of-truth tree at:**
- `_disabled/0127-perf-phase11-wave31-leverb-output-cache-engine-signaled.patch`
  (this file)

**Resurrection conditions.** This patch could be re-enabled if some
future bandwidth lever (e.g. backdrop sub-region partial blit reducing
the cache-OUT bytes by 50-70%, hardware-blit eliminating the dosmemput
cost, 4bpp/RLE backdrop compression) makes the cache-OUT cost cheaper
than the underlying render. Move the .patch file back out of
`_disabled/` and re-run `./scripts/apply-patches.sh nxengine-evo`.

Until then, the `BACKDROP_CACHE` killswitch in `docs/BOOT.md` only
controls the original patch 0080 cache + the wave-30 patch 0126
thrashing-detect bypass -- both of which DO contribute fps gains
because they REMOVE bandwidth-bound transfers rather than just shift
their location.

## Don't add new patches here without a written architectural finding

Reverts should be deliberate and documented. The fps trajectory is a
public artifact; each disabled patch is a learning that informs the
next iter's design space. Drive-by reverts without analysis (a) lose
the institutional memory and (b) tempt re-implementation by the next
session.

# ATI Mach64 -- 320x240 drawn into a 512x384 surface (2026-08-11)

Photographs from QA lane F (`VIDM 1`, POD-83 + PicoGUS + Mach64, binary
`09e449c5a81d`). Game boots and runs, audio fine, rendering wrong and slow.

## ROOT CAUSE (from the logs, not these photos)

**The Mach64's VBE offers no mode smaller than 512x384.** UNIVBE 6.70
enumerates 19 modes and the smallest 8bpp entry is `0x01F3 512x384`; the ViRGE
and Cirrus lists both start at `320x200`. The Mach64 modes report
`attrs=0x00BB`, clearing the VBE double-scan attribute bit that the working
cards set -- and 320x240 is a double-scanned mode. SDL's closest-mode match
correctly picked 512x384, and the engine drew its 320x240 logical screen into
the top-left of that surface. 320/512 = 240/384 = **62.5%**, which is the
undrawn right and bottom margin.

`DOSVESA-MODESET` reported `has_lfb=1 use_lfb=1 banked=0`, `bpp=8` as
requested, and `pitch=512` exactly equal to `w * bpp/8`. **Nothing is
mismatched.** Slowness is separately episodic, not constant: `present`
alternates between 18 ms and quantised 0.5-2.5 s plateaus while the flush
itself is healthy at 7.4 ms.

Full analysis: `qa-results/2026-08-11-POD83-video-lanes/README.md`.
Triage doc: `docs/internal/MACH64-TRIAGE.md` (untracked).

## Corrections to what this file previously claimed

Both were wrong and are recorded here rather than quietly deleted.

**"The flush is writing with a mismatched stride / bytes-per-pixel."**
Refuted by the MODESET line above -- LFB in use, bpp as requested, pitch
exact. The 62.5% drawn area was real evidence, but read as 3/4 and inferred to
a 24-vs-32 bpp mismatch; it is 320/512.

**"This would be a regression -- v1.3.0 was validated on Mach64."** Wrong
premise. That validation covered `SETUP.EXE`'s video bench and UX ("no hang on
S3/Cirrus/Mach64"), i.e. SETUP's own mode-13h/bench path -- **not**
`DOSKUTSU.EXE`'s VESA game path. The game was never validated on this card,
and the mode-pick behaviour dates to v0.1.0. Never-worked, not regressed.

**"Rainbow fringing on glyphs = bytes-per-pixel offset."** The mode is
paletted 8bpp, so per-pixel RGB byte phase cannot slip. That fringing is
almost certainly camera chroma/demosaic artifact on sharp scaled edges.

## Lesson worth keeping

Photo-derived **geometry** (ratios, alignment, shear) held up. Photo-derived
**colour** did not. And the cheapest discriminator for this whole fault class
was sitting in the photos unused: a pitch or bpp mismatch produces **diagonal
shear**, and no photo shows any -- rows are perfectly vertically aligned
throughout. That alone ruled out the diagnosis two sessions independently
reached from the same images.

The log line settled it for zero bench time, which is the triage doc's own
thesis. Cheapest-first ordering was right; the confident reading of the
photos was not.

## Open riders (not part of the root cause)

**The photos are all from one run** -- settled from the logs. Lane F produced
only `GC4M.LOG`; `G51M.LOG` does not exist because the lane was aborted during
`C4`, after 440 flips with the route truncated at stage 17. There was no second
cell and no second attempt for them to span. So the "different attempts"
explanation is out, and a second artifact does exist:

- `IMG_4420` shows the expected left-62.5% box -- consistent.
- `IMG_4417`'s cloud backdrop reaches both bezels **with a continuous, unbroken
  horizon**, and the logo and menu are centred on the full raster. Uninitialised
  VRAM cannot continue a cloud pattern seamlessly, and 320-wide centring would
  not land there.
- The garbled menu letterforms in `IMG_4417` ("2co gene" for "New game") are
  not margin-explainable.

This promotes the second explanation offered below to the leading one: **the
geometry mismatch is not uniform across draw paths.** Backdrop fill and
title/menu centring use the real 512x384 surface dimensions; the map draw is
pinned to 320x240. That also accounts for the stray top-right rectangle -- a
full-surface path showing through where logically-clipped content never lands.

Useful consequence for the fix: the clear and tile paths already handle surface
dimensions correctly, so a centre/letterbox fix is predominantly about blit
placement and camera/viewport bounds, not about teaching every path the real
size.

Withdrawn: the earlier "`IMG_4418`'s inventory box spans ~93% of panel width"
measurement. That photo's surround is black-on-black, so there is no reference
edge to scale against and the figure is unreliable. Its ~8-row banding stands
as an observation but carries no width inference.

Re-check the letterforms once a native 320x240 mode is available (M64VBE
retry). If glyph corruption survives that, there is a third bug.

## Files

`IMG_4415` early boot, sparse mis-placed sprites - `IMG_4417` title screen -
`IMG_4418` inventory - `IMG_4420` Mimiga Village.

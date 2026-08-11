# Video-card matrix -- POD-83 + PicoGUS, three cards

Source: `2026-08-11-POD83-video-lanes/`, binary `09e449c5a81d`, one 102 s reel.
Lanes D (`VIDV 1`, S3 ViRGE), E (`VIDC 1`, Cirrus CL-GD5430), F (`VIDM 1`,
ATI Mach64). Audio is held constant at PicoGUS-SB across all three, so the only
variable is the video card.

Decomposed per `MATRIX-POD83.md`: `per-loop fps = flips / 102`,
`overhead_s = render_s - 102`.

**None of these cells runs the PIT/IRQ-0 music pump** (`pumplines=0` in every
log), so `inter_flip_ms` medians are valid here and are quoted alongside
`[fps-true]` as an independent second metric. The two agree on both the
direction and the size of the card gap, which is what makes the gap credible.

| Cell | Card | Lane | per-loop fps | overhead_s | inter_flip median | Route |
|---|---|---|---|---|---|---|
| GC4V | S3 ViRGE | D | **31.60** | 22 | 31 ms (32.3 fps) | full |
| GC4 | S3 ViRGE | A (`PG 1`) | 31.38 | 22 | 31 ms (32.3 fps) | full |
| GC4C | Cirrus CL-GD5430 | E | **30.40** | 22 | 32 ms (31.2 fps) | full |
| G51V | S3 ViRGE | D | 31.47 | 22 | 30 ms (33.3 fps) | full |
| G51 | S3 ViRGE | A (`PG 1`) | 31.58 | 25 | 30 ms (33.3 fps) | full |
| G51C | Cirrus CL-GD5430 | E | 30.03 | 21 | 32 ms (31.2 fps) | full |
| GC4M | ATI Mach64 | F | -- | -- | -- | **truncated** |

4/4 ViRGE and Cirrus cells ran the full route
(`72 20 11 17 11 15 11 19 11 14 11`). Mach64 is analysed separately below and
contributes no row.

## The launcher is not a variable

`GC4V` and `GC4` are the same card and the same config through two different
launchers (`VIDV 1` vs `PG 1`). They differ by **0.22 per-loop fps** and are
identical on the median. That is inside the sub-0.5 fps caveat, so the `VID*`
BATs measure the same thing the `PG` sweep does, and the three-way card
comparison rests on a checked assumption rather than an assumed one.

## The Cirrus is ~1.2 fps behind the ViRGE, and the reason is our own code

C4: 30.40 vs 31.60 = **-1.20 per-loop fps** (-3.8%). G51: 30.03 vs 31.47 =
-1.44. The median metric independently says -1.1. All well clear of the 0.22
launcher noise.

The cause is in the log, not in the silicon:

    cirrus-lfb-aperture-bug: detected (oem='SciTech Software, Inc.'
      product='SciTech Display Doctor(tm)' vram=1024KB) -> forcing banked path
      (reason: UNIVBE on 1MB card (matches CL-GD5434 + UNIVBE 6.7))
    LFB-decision #1: hint_allow_direct=1 hint_prefer_lfb=1 lfb_available=1
      is_banked_usable=1 -> use_lfb=0

`patches/SDL/0019-fix-cirrus-lfb-aperture-disable.patch` fingerprints UNIVBE on
a 1 MB Cirrus and forces the banked path. The mode itself advertises
`lfb=1 lfb_addr=0x78000000`; the workaround declines it. The ViRGE (4096 KB)
does not match the fingerprint and runs LFB.

So **-1.2 to -1.4 fps is the measured price of the Cirrus LFB workaround** --
the first time that guard's cost has had a number attached.

**That price buys something real. An earlier draft of this file suggested the
guard might be firing spuriously; that was wrong on both counts and is
corrected here.**

The suggestion was that the fingerprint's "matches CL-GD5434" wording might be
keying off the known 5430-probes-as-5434 misdetection
(`memory/g2k_lp4ip1_hardware_identity.md`). It cannot: **the fingerprint never
tests chip identity at all.** It matches on VBE OEM vendor and product strings
plus VRAM size -- exact arm is vendor `SciTech Software, Inc.` + product
containing `Display Doctor` + 1024 KB. The patch says why outright: UNIVBE
rewrites the OEM strings, so chip identity is hidden behind UNIVBE and VRAM
size is used as the proxy. The `(matches CL-GD5434 + UNIVBE 6.7)` text is the
human-readable reason string, not a test.

And the bug it guards was **directly observed on this machine with a control**:
the wave-5 `cirrus5` capture logged 100+ LFB writes at `vram_phys=0x78000000`
with no visible display update, while the banked path on the same card did
update. Aperture and displayed VRAM are decoupled below the BIOS interface.
That is a two-armed observation, not an inference.

There is also no way to run the A/B as-is: `force_banked_path` is set
unconditionally at `SDL_dosvideo.c:167`, and `SDL_HINT_DOS_DISABLE_LFB` pushes
the *same* direction (it forces banked too). Disabling the detection needs an
SDL source change, which is post-matrix gated.

The residual question worth testing eventually is narrower: whether anything
since wave 5 -- display-start handling, the 0076 BIOS-VGA-detect work --
incidentally fixed the decoupling. User guidance does not depend on the answer,
since the ViRGE is the faster card either way.

## The historical bridge, honestly

`QA-CELL-REFERENCE.md` expected `VIDC` to tie the new ViRGE matrix back to the
project's historical ~33 fps POD-83 figure, which was measured on the Cirrus.
It does not reconcile cleanly: the Cirrus measures **31.2 fps median**, below
the ViRGE's 32.3-33.3, so the historical figure sits at the ViRGE level rather
than the Cirrus level it was supposedly taken on.

The gap is not explained by the card. The historical number came from a
different binary and a different (non-TAS, hand-played) route, so it is not the
same measurement. The useful result from lane E is the **same-metric,
same-reel, same-binary card delta above**, which is solid. Treat the historical
~33 fps as an unrelated figure rather than a calibration point.

## Overhead is video-independent

`overhead_s` is 21-22 in every video-lane cell regardless of card, matching the
22 s that lane A's OPL3 cells show. Since the card does not move it, the
decomposition's claim that `overhead_s` is load-stall and audio-side time --
not render time -- holds up under a controlled video swap.

## Lane F (Mach64): no fps row, and the reason is mode selection

See `2026-08-11-POD83-video-lanes/README.md`. In short: the card has no
320x240 mode, the game ran at 512x384, and the cell was aborted after 440
flips with the route truncated at stage 17. No `[fps-true]` line was emitted,
so lane F contributes nothing to this matrix and nothing was inferred from it.

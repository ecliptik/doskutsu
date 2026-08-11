# POD-83 video lanes -- raw logs (2026-08-11)

Binary `09e449c5a81d`, kit v170, one 102 s `QA.TAS` reel. PicoGUS held in sb
mode across all three lanes; only the video card differs.

| Logs | Lane | Card | Outcome |
|---|---|---|---|
| `GC4V` / `G51V` (+`SDL`) | D | S3 ViRGE | full route, both cells |
| `GC4C` / `G51C` (+`SDL`) | E | Cirrus CL-GD5430 | full route, both cells |
| `GC4M` (+`SDL`) | F | ATI Mach64 | aborted; corrupt + slow |

`G51M.LOG` does not exist -- the lane was stopped during `C4`, so the second
cell never ran.

fps analysis: `../MATRIX-VIDEO-POD83.md`.
Mach64 photographs: `../2026-08-11-mach64-corruption/`.

---

## Mach64 root cause: the card has no 320x240 mode

`GC4MSDL.LOG` settles it without spending bench time. UNIVBE 6.70 enumerates
**19 modes** on this card, and the smallest 8bpp mode in the list is
**512x384**:

    DOSVESA-CTRL: VBE 3.0  total_vram=2048_KB  oem_string='Universal VESA VBE 6.70'
    DOSVESA-CTRL: 19 modes enumerated:
       0x01F3 0x0100 0x01E4 0x01D4 0x01C4 0x01B3 0x0101 0x0103 ...
      mode 0x01F3:  512x384  bpp=8  ... attrs=0x00BB ... pitch=512

The ViRGE and Cirrus lists both start with `0x01F9 320x200`, `0x01F8 320x240`,
`0x01F5 400x300`. The Mach64 list has none of them. SDL's closest-mode match
did the right thing with what it was offered and picked 512x384:

    mode-set #1 (entry): requested id=0x01F3 512x384 8bpp
    DOSVESA-MODESET: id=0x01F3 512x384 8bpp pitch=512 attr=0x00BB memmodel=4
      has_lfb=1 use_lfb=1 banked=0 lfb_addr=0x78000000
    WAVE16-MODESET: requested_max_bpp=8 actual_bpp=8 mode=0x01F3 512x384
      pitch=512 raw_bytes_per_frame=196608 fb_base=LFB

**Every hypothesis in the triage plan's Step 1 table comes back clean:**

| Triage check | Actual | Verdict |
|---|---|---|
| `has_lfb=0` / `use_lfb=0` -> VBE provider fault | `has_lfb=1 use_lfb=1` | refuted -- the card got its LFB |
| `banked=1` -> explains slowness | `banked=0`, `fb_base=LFB` | refuted -- not banked |
| `bpp != 8` -> closest-match landed elsewhere | `requested_max_bpp=8 actual_bpp=8` | refuted -- bpp is exactly as asked |
| `pitch != w * bpp/8` -> padded scanlines | `pitch=512`, `512 * 1 = 512` | refuted -- pitch is exact, `pitches_match=1` |

The fault is not the flush, not the bpp, not the pitch, and not a missing
linear framebuffer. It is that **the engine's logical screen is 320x240 while
the surface it was handed is 512x384**, and the geometry mismatch is never
reconciled.

### Why the mode list is short

The Mach64 modes report `attrs=0x00BB`; the ViRGE and Cirrus report `0x019B`.
The difference includes VBE `ModeAttributes` bit 8, "double-scan mode
available", which is set on the working cards and clear on the Mach64.
320x240 and 320x200 are double-scanned modes. A VBE that advertises no
double-scan capability has no small modes to offer, which is exactly the list
observed.

### The screenshots re-read

`IMG_4420` (Mimiga Village) shows the map laid out correctly with **no
diagonal shear**, occupying the top-left of the raster with the right margin
*and* the bottom margin undrawn. That is 320/512 = 62.5% of the width and
240/384 = 62.5% of the height -- a correctly-pitched 320x240 image sitting in
a 512x384 framebuffer.

The original reading in `../2026-08-11-mach64-corruption/README.md` ("3 bytes
written where 4 are scanned out", "pitch mismatch", "banked writes") is
superseded. A stride error would have produced a shear; there is none, and the
log confirms the pitch is correct.

**The mismatch is not uniform across draw paths.** `IMG_4417` (title) shows the
cloud backdrop reaching *both* bezels with a continuous, unbroken horizon, and
the "Cave Story" logo and the menu box centred on the full 512-wide raster --
uninitialised VRAM does not continue a cloud pattern seamlessly, and 320-wide
centring would not land there. So some paths (backdrop fill, title/menu
centring) correctly use the real surface dimensions, while the map/tile draw in
`IMG_4420` covers only 320x240 in the top-left corner.

That makes this a **mixed-geometry** fault rather than margin garbage, and it
narrows the fix: the clear/tile paths already handle the surface dimensions, so
predominantly the blit placement and the camera/viewport bounds need offsetting.
It also explains the stray solid rectangle at top-right of `IMG_4417` -- a
full-surface path showing through where logically-clipped content never lands.

Still unexplained and held as a rider: the menu letterforms in `IMG_4417` are
garbled ("2co gene"), which margin geometry does not account for. Re-check it
under a native 320x240 mode before treating it as a separate bug.

### The slowness is episodic, not constant

The card is on an LFB, so the "banked writes" explanation for slowness is gone
too. `sw-updatesurface-detail` is a steady 7.4 ms (vs 2.9 ms ViRGE, 4.2 ms
Cirrus -- roughly the 2.56x more bytes per frame that 512x384 costs over
320x240). The flush is healthy.

What is not healthy is `present`, and it is **bimodal**:

    flip #80  present=18ms   inter_flip_ms=20     <- 50 fps
    flip #100 present=517ms  inter_flip_ms=554
    flip #110 present=516ms  inter_flip_ms=551
    flip #130 present=19ms   inter_flip_ms=20     <- 50 fps
    flip #140 present=2450ms inter_flip_ms=2486
    flip #440 present=1998ms inter_flip_ms=2163

Healthy frames reach 20 ms -- 50 fps, the KPI, at 512x384. The cell is ruined
by episodic plateaus of 0.5-2.5 s per flip that switch on and off. Quantised
plateau values suggest a capped wait rather than a bandwidth limit; SDL/0116
`VBLANK-BOUND` is enabled in this build and is described as converting a
never-toggling retrace bit into "a finite stall + fall-through". That is a
lead, not a conclusion -- it has not been confirmed against a hardware marker.

The cell was aborted after 440 flips with the route truncated at stage 17
(vs `72 20 11 17 11 15 11 19 11 14 11` complete), and emitted no `[fps-true]`
line. Nothing in this log is usable as a framerate.

### Not a regression -- nothing need have changed at all

The stronger statement is that **DOSKUTSU.EXE's VESA path was never validated
on this card.** The v1.3.0 "g2k-validated S3/Cirrus/Mach64" claim covers
SETUP.EXE: that release was the HQ-audio tier plus SETUP Dark Forces UX Phase
2, and its Mach64 evidence is SETUP's own cross-card-safe mode-13h video bench
("Mach64 path-2 9671 KB/s"), on the DX2-66. Mode 13h is not the VESA path, so
the game's mode selection on this card has no prior validation to regress from.

Consistent with that, `SDL_HINT_DOS_PIN_WINDOW_TO_NATIVE_MODE`
(`patches/SDL/0012`, `patches/nxengine-evo/0046`) -- visible in the log
suppressing the engine's later 640x480 request, and the obvious "what changed"
candidate -- dates to v0.1.0 (phase 9 wave 2). It predates v1.3.0 entirely.

No engine-side regression is indicated, and none needs to be hypothesised.

### Next steps, in order

1. **Operator, no bench time:** confirm what VBE is providing modes for the
   Mach64 -- UNIVBE 6.70 is what the log names. Try ATI's own `M64VBE.COM`
   instead and re-read `DOSVESA-CTRL` for a mode list containing `0x01F8`. If
   a 320x240 mode appears, this is a setup and documentation issue and the fix
   is a line in `docs/BUILDING.md`, not code.

   **Caveat for that retry:** old vendor VBEs often expose small modes as
   banked-only. If `M64VBE.COM` offers `0x01F8` with `lfb=0`, the engine takes
   the banked path on a Mach64 for the first time -- which would also be the
   first real-hardware exercise of SDL/0115's `WinGranularity < WinSize` bank
   walk (v1.6.1, validated only as a no-op on g2k, where `gran == size`).
   Capture `win_gran` and `win_size` from that `DOSVESA-MODESET`, and if
   corruption persists in that configuration, A/B
   `SDL_HINT_DOSKUTSU_BANK_GRAN_FIX=0` **before** concluding `M64VBE.COM`
   "didn't fix it" -- otherwise an 0115 bug on genuine `gran < size` hardware
   would masquerade as the same lane failure.
2. **If no provider offers 320x240:** the engine needs to handle a surface
   larger than its logical screen -- centre or letterbox 320x240 inside
   512x384 and clear the margins. That is real work and should be scoped
   against how much the Mach64 matters.
3. **Only then** chase the episodic `present` plateaus, with
   `SDL_HINT_DOSKUTSU_VBLANK_BOUND=0` as the A/B. They may well disappear with
   the mode fixed.

None of this is a campaign blocker. Lane F is two bonus cells and the matrix
stands without it.

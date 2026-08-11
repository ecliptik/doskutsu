# POD-83 + Vibra16 + S3 ViRGE -- lane B (`VB 1`), 2026-08-11

Binary `09e449c5a81d`, kit v170, one 102 s `QA.TAS` reel per cell.
**6 cells, 6/6 full route** (`72 20 11 17 11 15 11 19 11 14 11`).

Recovered from `/tmp/qa-v163/G/` on 2026-08-11 during the lane-C review: the
run had completed but was never analysed or banked, and the campaign notes
still listed `VB 1` as outstanding. Timestamps 09:06-09:18 place it
immediately after the video lanes in the same operator session.

**This completes Part 1.**

Hardware witness in every cell: `is_sb16=1 force_8bit=0 dsp_ver=4 highdma=5 ->
16-bit-high-DMA path`, i.e. the real Vibra16 rather than the PicoGUS in sb
mode. The DreamBlaster was on the Vibra's own header -- `G17` reports
`audio backend: wb` with MPU-401 found at 0x0330 -- so the cell measured the
path it is meant to measure.

## Results

`per-loop fps = flips / 102`, `overhead_s = render_s - 102`. No cell here runs
the PIT/IRQ-0 music pump, so the medians are valid.

| Cell | CFG | Backend | per-loop | overhead_s | median |
|---|---|---|---|---|---|
| `G02` | AUTO | opl3 | 30.51 | 23 | 31.2 |
| `G18` | OPL3 | opl3 | 30.48 | 22 | 31.2 |
| `G16` | AUTO | opl3 | 30.36 | 21 | 32.3 |
| `G17` | WB | wb | 29.01 | 24 | 30.3 |
| `G111` | ORGANYA | organya | 27.20 | 27 | 27.8 |
| `G112` | ORGHQ 22050 stereo | organya | **20.41** | **55** | 21.3 |

## The Vibra16 costs about 1 fps against the PicoGUS

Same CPU, same video card, same reel, same binary -- only the sound card
differs from lane A (`2026-08-10-POD83-picogus-v170-sfx/`):

| Backend | Vibra16 | PicoGUS-SB | Delta |
|---|---|---|---|
| OPL3 | 30.48 | 31.38 | **-0.90** |
| AUTO | 30.36-30.51 | 31.60 | -1.16 |
| WaveBlaster | 29.01 | 30.10 | -1.09 |
| Organya | 27.20 | 28.50 | -1.30 |

It replicates on the DX2-66 (`2026-08-11-DX266-full/`): `618` OPL3 on the Vibra
is 22.14 against `6C4`'s 23.22 on the PicoGUS, **-1.08**. Consistent sign,
consistent magnitude, two CPUs, five backends -- well outside the 0.22 fps
launcher noise established in `MATRIX-VIDEO-POD83.md`.

There is a mechanism in the log. The two cards take different DMA paths:

    Vibra16:  is_sb16=1 dsp_ver=4 highdma=5 -> 16-bit-high-DMA path
    PicoGUS:  is_sb16=0 dsp_ver=2 highdma=-1 -> 8-bit-low-DMA path

The 16-bit path moves twice the bytes per sample, so more bus traffic and more
per-interrupt work, which is the right shape for a ~1 fps whole-loop cost.

**This is not yet established as a DMA-path cost**, because card and path are
confounded -- these are also physically different cards. `SDL/0106` exposes
`force_8bit`, so one cell running the Vibra with `force_8bit=1` would separate
them: if the gap closes, it is the DMA width; if it persists, it is the card.
Worth a single cell in a future round.

## Organya-HQ is expensive even on the POD-83

`G112` at 20.41 per-loop / 55 overhead is the worst POD-83 cell in the
campaign, against `G111`'s 27.20 / 27 for the same content at 11025 mono. The
DX2-66 figure is 13.56 / 96 (`6112`). The HQ tier costs roughly a quarter of
the framerate on the reference machine, not only on 486-class silicon.
Correctly default-off; worth a docs line if SETUP ever surfaces it prominently.

## `AUTO` does not select the WaveBlaster

`G16` is the cell that asks "auto-detect -- **should** pick the WaveBlaster.
Does it?" It initialises `opl3`, on a machine where `G17` proves the
DreamBlaster is present and working.

But the cell cannot settle its own question: it falls through to the *engine's*
built-in default, which has been OPL3 by design since wave 46 patch 0139, and
never consults SETUP's detection. `616` on the DX2-66 behaves identically. See
`../MATRIX-CROSS-CPU.md`; the cell needs re-scoping against a SETUP-generated
CFG, which ties it to the deferred hands-on SETUP walk.

`G02` and `G16` load the same AUTO config and differ by 0.15 per-loop fps --
another read on run-to-run noise, consistent with the 0.22 figure from the
launcher check.

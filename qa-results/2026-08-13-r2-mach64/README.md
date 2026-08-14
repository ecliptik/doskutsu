# Round 2 -- ATI Mach64 lane (486DX2-50)

**Not comparable to anything else in round 2.** These cells ran with the
**card's own VBE 2.0 BIOS** answering, not UniVBE:

    5C4MSDL.LOG   VBE 2.0  oem_string='ATI MACH64'  product='MACH64CT'
                  22 modes enumerated, total_vram=2048_KB
    (ViRGE cells)  VBE 3.0  oem_string='Universal VESA VBE 6.70'
                  40 modes enumerated, total_vram=4096_KB

UniVBE is per-card on this rig and had not been re-set-up after the card swap.
Its OEM `0x01Fx` mode series -- which is where 320x240 and 512x384 live -- is
absent from the ATI BIOS list entirely, so the engine landed on `0x0101
640x480`, four times the pixels of 320x240.

## What is here

| File | Cell | Outcome |
|---|---|---|
| `5C4M.LOG` | OPL3 on Mach64 | ran, full route, **2.2 fps** |
| `5C4MK.LOG` | `VBLANK_BOUND=0` arm | died at startup, no video |
| `551MK.LOG` | `VBLANK_BOUND=0`, AUTO cfg | froze at startup, no video |

`551M` does not exist -- the sweep was aborted after `5C4M` took 38 minutes.

## `5C4M`

    [fps-true] flips=5032 render_s=2267 fps_mean=2.2
    center-oversized: ENGAGED surface=640x480 logical=320x240 offset=160,120
    center-oversized: flush rect=320x240@160,120 bytes=76800
    DOSVESA-MODESET: id=0x0101 640x480 8bpp pitch=640 attr=0x00BB

**`per_loop_fps` is meaningless for this cell.** The CSV computes it as
`flips / 102` and gets 49.33, because the engine rendered 5032 frames across
the reel's 102.8 s of game time. But the run took 2267 s of wall clock, so the
honest figure is `fps_mean` -- **2.2 fps**. The decomposition assumes overhead
is load-stall time; here it is per-frame render cost, and the metric
misattributes it.

The letterbox patch (`0296`) works: picture centred at 160,120, margins
cleared, flush scoped to the logical rect.

## What the round established

- **100% of drawcalls fall to the software slow path.** `slowpath reason block
  50: windim=1782` of 1782, against **zero** slow-path blocks on the ViRGE with
  `fastpath audit taken=2828 skipped=0`. Confirmed, but it is 6% of the frame.
- **The frame decomposes** as drawcalls 15.1 ms (6%), flush 90.9 ms (35%),
  unattributed residual 155.3 ms (59%) of a 261.3 ms frame. The largest bucket
  is unmeasured.
- **The flush is 23x the ViRGE's for the same 76800 bytes** -- per-row dispatch
  across a 640 pitch, plus direct-VESA standing down (`presents=0`, which is
  `0296`'s own gate, not a BIOS refusal).
- **No `VBLANK_BOUND` A/B exists.** Both `VIDMK` cells died on SB detection
  (`Not a SoundBlaster at port 0x220, last DSP read 0xFF`), not on the video
  path. The `0116` cap is untested.

## Superseded by the UniVBE re-run

A follow-up ran on POD-83 with UniVBE actually loaded, which the driver's own
banner confirms it takes:

    Graphics Chip: ATI Mach64 CT PCI with 2 MB
    Note that the ATI Mach64-CT and Mach64-ET based boards do not support
    double scanning, so all 320x200 and 320x240 resolution modes are not
    available.

So the no-320x240 finding is confirmed by the vendor rather than inferred, and
Path A stays closed on the double-scan question. Everything else measured here
is provisional. Full reasoning in `docs/internal/MACH64-TRIAGE.md`.

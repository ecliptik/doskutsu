# Mach64 boot profile

Adds a **6th boot-menu entry** to the g2k so the Mach64 can be tested with
ATI's own VESA driver instead of UniVBE. The five existing profiles are
untouched and still load UniVBE, so nothing has to be undone afterwards --
picking a different menu entry is the revert.

| File | What |
|---|---|
| `install-mach64.sh` | Installs the driver + boot files on a mounted CF. Idempotent, backs up to `.PREM64`. |
| `AUTOEXEC.BAT.new` | The five original profiles plus `:MACH64`. UniVBE is now skipped for that profile only. |
| `CONFIG.SYS.new` | The five original menu items plus `MACH64`. |
| `README.TSC` | ATI's own documentation, for the switch list. |

`M64VBE.COM` and `VESATEST.EXE` are not committed -- they are ATI's, and come
from the operator's `ati.tar.gz` (`ATI/SUPPORT/64VBE221/`). The install script
expects them beside it.

## Why

Round 1's Mach64 lane failed **under UniVBE 6.70**, whose mode list for this
card starts at `0x01F3 512x384` -- so the engine drew its 320x240 screen into
the top-left corner. The flush, bpp and pitch were all correct; there simply
was no small enough mode.

ATI's driver documents **320x200 and 320x240 as a default-on feature**
(`README.TSC` section 3.0), which is exactly what was missing.

## Two things that would cause a misread

**The mode numbers differ.** M64VBE exposes 320x240 8bpp as **`0x0212`**, not
the `0x01F8` SciTech/UniVBE uses. Match on the resolution in `DOSVESA-CTRL`,
never on the number.

**`VESATEST.EXE` answers the question without the game.** Run
`C:\ATI\SUPPORT\64VBE221\VESATEST` after booting into entry 6; it lists every
mode the card currently offers.

## Loading it

Run it **bare**: `M64VBE`. There is no `install` or `I` switch -- the only
valid ones are `u s d 3 -3 vw -vw acc vga 4 -4 ?`, and an unrecognised
argument prints the help screen without installing anything.

It reports one of:

| | |
|---|---|
| `M64VBE (V2.21) is installed` | loaded |
| `M64VBE is already installed` | already resident |
| `Can not load M64VBE; Mach64 adapter is not detected` | it does not recognise the card |

`M64VBE U` is the resident check: `successfully removed` means it was loaded,
`Can not remove; M64VBE is not resident` means it never was.

**How to tell which VBE is active from `VESATEST` alone:** the card's own ROM
offers 640x400 and up. UniVBE adds 512x384. M64VBE adds 320x200 and 320x240.
A list starting at 640x400 means no TSR is resident at all.

## ATI's documented fallbacks

Unload with `M64VBE U`, then reload with switches:

| Symptom | Reload as |
|---|---|
| image in a corner / partial | `M64VBE VW VGA` -- their fix for "1/4 of the image visible" |
| black screen or hang | `M64VBE VGA` |
| mouse trails | `M64VBE S VGA` |

The first row is worth noting: ATI documents our exact round-1 symptom, with a
cause (an application assuming a VGA Wonder-compatible card) unrelated to the
mode-list finding. If 320x240 turns out to be present and the image is still
wrong, that is the next thing to try.

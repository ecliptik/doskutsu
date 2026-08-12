# Mach64 Path A -- CLOSED, the hardware cannot do 320x240

Round 1 left the Mach64 lane drawing a 320x240 screen into the top-left of a
512x384 surface, because the card's VBE offered no smaller mode. Path A asked
whether a different VBE provider would offer one.

**It cannot. The chip does not support double scanning, and 320x200 / 320x240
are double-scanned modes.** No driver can synthesise them.

## The evidence, three independent ways

**1. SciTech UniVBE 6.70 says so outright** (`univbe-doublescan-warning.jpeg`):

    Graphics Chip: ATI Mach64 CT PCI with 2 MB
    Note that the ATI Mach64-CT and Mach64-ET based boards do not support
    double scanning, so all 320x200 and 320x240 resolution modes are not
    available.

**2. ATI's own driver cannot produce them either.** `M64VBE.COM` v2.21
documents 320x200/320x240 as a default-on feature and reported `320 modes
enabled` while resident (MEM confirmed, 4816 bytes). VESATEST still listed
nothing below 640x400.

**3. The round-1 log said it first.** The Mach64's modes report
`attrs=0x00BB`, clearing VBE ModeAttributes bit 8 -- "double-scan available" --
which the ViRGE and Cirrus both set at `0x019B`. That reading was made from the
log before any of this bench work and is now confirmed by the vendor.

## M64VBE additionally hangs SDL_Init

Worth recording separately, because it is a robustness finding rather than a
mode-list one. With `M64VBE.COM` resident, DOSKUTSU hangs before SDL_Init
completes -- `5C4M-hang-m64vbe.LOG` stops at `tas: replay opened`, which
`main.cpp` documents as the last step before `SDL_Init`, and no SDL log file is
created at all. Byte-identical across runs.

Tried and still hung: default (`M64VBE`), `M64VBE VGA` (ATI's documented fix
for "system hangs"), and `M64VBE -3` (320 modes disabled, which rules out the
OEM modes themselves being what the probe chokes on).

Not worth chasing: M64VBE offers this project nothing even when it works, since
the modes it advertises cannot exist on this silicon.

## What runs

Under UniVBE the lane behaves exactly as in round 1 -- `5C4MSDL.LOG` here
enumerates the same 19 modes, no 320x240, and sets `0x01F3 512x384`. Slow and
visually wrong, as before.

## Consequence

Path B -- centre/letterbox the 320x240 image inside the larger surface -- is
the only route, and was already approved. It needs no ATI driver and helps any
future card whose smallest mode exceeds the logical size.

Scope from the round-1 photographs: the backdrop tiler already paints the full
surface width, while sprite and text draws clip at logical size, so the work is
blit placement plus a one-time margin clear rather than a tiler rewrite.

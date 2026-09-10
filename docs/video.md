# Video

## Resolution hierarchy

```
320x200   best
320x240   excellent
320x400   acceptable
640x480   Pentium-oriented
higher    not an initial DOS target
```

doskutsu targets 320x240. A port whose native resolution is 320x200 (many
DOS-era and DOS-recreation games, e.g. OpenJazz) exercises the VESA path
slightly differently and is worth testing deliberately rather than assumed
identical.

## Presentation path

Preferred:

```
game software framebuffer
       |
 single conversion if necessary
       |
 VESA framebuffer
```

Avoid multiple software scaling stages. Prefer native resolution, no
filtering, integer pixels, direct blit, and dirty rectangles where the
game's redraw pattern benefits from them (a mostly-static-screen game like
an adventure engine benefits far more than a scrolling action game).

## What `shared/patches/sdl3-dos/` already handles

The hard-won VESA/VGA chip-specific work — Cirrus CL-GD5430 banked-blit
quirks, S3 ViRGE linear-framebuffer handling, DAC 6-bit vs. 8-bit
palette-width detection, banked-vs-LFB flush path selection, direct-CRTC
page flip — lives in the shared SDL3 DOS backend patches. A new port should
not need to re-derive any of this; it inherits it by pinning the same
patched SDL3.

## Reduced color depth

8-bit or 16-bit software surfaces are strongly preferred over 32-bit on
486/Pentium-class hardware — see `optimization.md`. Full-screen pixel
copies and pixel-format conversion are consistently the first or second
most expensive thing on this class of hardware.

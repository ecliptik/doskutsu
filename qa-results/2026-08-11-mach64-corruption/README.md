# ATI Mach64 -- corrupted and slow rendering (2026-08-11)

Photographs from QA lane F (`VIDM 1`, POD-83 + PicoGUS + Mach64, binary
`09e449c5a81d`). The game boots and runs and audio is fine, but rendering is
heavily corrupted and very slow.

Kept because the corruption pattern IS the diagnosis:

- `IMG_4417` title screen -- fully recognisable, so mode-set and the engine
  are healthy; note the rainbow fringing on every glyph and the stray solid
  blue rectangle top-right.
- `IMG_4420` Mimiga Village -- map laid out correctly but the image fills only
  ~3/4 of the screen width, right quarter undrawn.
- `IMG_4418` inventory -- regular horizontal black bands through solid panels.
- `IMG_4415` early boot -- sparse mis-placed sprites.

Together: the flush is writing with a stride and/or pixel size that does not
match the mode actually set. Fringing = bytes-per-pixel offset; 3/4 width = 3
bytes written where 4 are scanned out; banding = pitch mismatch; slowness =
consistent with banked writes rather than a linear framebuffer.

Triage plan: `docs/internal/MACH64-TRIAGE.md` (untracked). First step costs no
bench time -- the `DOSVESA-MODESET` line in `GC4M.LOG` / `G51M.LOG` reports
mode, bpp, pitch, `has_lfb` and bank granularity directly.

Prime suspect is the VBE provider, not engine code: `docs/BUILDING.md` already
notes this card may need `M64VBE.COM`, and the SDL3 DOS backend requires
VBE 1.2+ with a linear framebuffer.

NOTE this would be a regression if it proves to be engine-side: v1.3.0 was
validated on this machine across S3, Cirrus and Mach64.

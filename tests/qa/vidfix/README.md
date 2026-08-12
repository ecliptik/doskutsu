# Video-lane BAT refresh

Updates just `VIDV.BAT` / `VIDC.BAT` / `VIDM.BAT` on a mounted CF. Use when the
card was populated before those fixes landed and a full re-populate is not
worth it mid-session.

    scp -r claude:/tmp/vidfix /tmp/ && bash /tmp/vidfix/install-vidfix.sh

The BATs themselves are not committed here -- they live in the kit tree and are
copied in when the tarball is packed. The script expects them beside it.

## What changed in them

`pgusinit` and the PicoGUS `BLASTER` are now opt-in behind a `PG` argument.
Before, every video lane ran `pgusinit /mode sb` unconditionally and then set
`BLASTER=A220 I7 D3 P330 T3`. On a Vibra16 box that prints two
`ERROR: no PicoGUS detected!` lines and, worse, replaces a working BLASTER with
IRQ/DMA values the Vibra cannot use.

    VIDM 4       uses the SB already in the box
    VIDM 4 PG    switches a PicoGUS to SB mode first

The self-relaunch had to forward a second argument for that to work --
`COMMAND /C %0 GO %1` only passed one, so `PG` never arrived.

The banner and the `.NFO` now say which card will actually be used, instead of
always claiming `PicoGUS in SB mode`.

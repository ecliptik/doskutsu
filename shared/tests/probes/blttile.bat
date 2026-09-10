@ECHO OFF
REM BLTTILE.BAT - Cirrus 5434 BitBLT tile-copy card probe.
REM Phase 11 task #30 (probe-engineer). Formally closes the last
REM un-measured render path per MODEX-PLAN sec.4.3.
REM
REM What it does: enters a VESA 8bpp mode, then measures the Cirrus
REM 5434 hardware BitBLT engine -- NORMAL-mode VRAM-to-VRAM tile-rect
REM copy throughput (3 geometries), an A/B vs the CPU colorkey blit,
REM and a transparent-compare candidate-ladder. Results go to
REM BLTTILE.LOG; no operator observation needed (the screen shows
REM transient garbage during the BLT runs -- that is expected).
REM
REM Runtime: ~30 sec total.
REM
REM No env vars. Pure DJGPP. CWSDPMI.EXE must be in the same dir.
REM
REM HAZARD: direct Cirrus chip register programming. Side effects are
REM bounded -- BLT writes carry a 200k-spin watchdog, and the probe
REM atexit-restores text mode. If the screen blanks or hangs for any
REM reason, Ctrl-Alt-Del recovers cleanly.
ECHO Running BLTTILE Cirrus 5434 BitBLT tile-copy probe...
ECHO   HW-IO probe; ~30 sec; transient on-screen garbage is expected.
ECHO   No screen observation needed -- result goes to BLTTILE.LOG.
BLTTILE.EXE
ECHO ----
ECHO Probe finished. BLTTILE.LOG gate lines:
TYPE BLTTILE.LOG | FIND "blttile"
TYPE BLTTILE.LOG | FIND "SELFTEST"

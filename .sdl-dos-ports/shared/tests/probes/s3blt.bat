@ECHO OFF
REM S3BLT.BAT - S3 ViRGE/DX 2D BitBLT engine probe (S3-VIRGE campaign P3).
REM probe-engineer. The ViRGE analog of BLTTILE; the L2 (chip-offload) gate.
REM
REM What it does: detects the S3 ViRGE (PCI 0x5333 + CRTC chip-ID), enters a
REM VESA 8bpp LFB mode, maps the LFB + the new-MMIO window (LFB+0x1000000),
REM then measures the ViRGE hardware 2D BitBLT engine -- VRAM->VRAM tile-rect
REM copy throughput (5 geometries incl. the 16x16 decision size + the 16x256
REM trap-reference), a solid rect-fill, and an A/B vs the CPU colorkey blit.
REM Every BLT is readback-VERIFIED -- an unverified rate is reported UNVERIF,
REM never as a trustable number. Results go to S3BLT.LOG; no observation needed
REM (transient on-screen garbage during the BLT runs is expected).
REM
REM RUN LAST in the iter (highest hang risk = direct MMIO on never-driven
REM silicon). A hang cannot lose earlier HWINV/MEMBW data (fsync per line).
REM
REM Runtime: ~10-30 sec total. No env vars. Pure DJGPP. CWSDPMI.EXE same dir.
REM Invoked with NO args -> auto-detect (the real ViRGE path). The FORCE arg
REM exists only for the DOSBox-X smoke (bypasses detection) -- do NOT use it
REM on real hardware.
REM
REM HAZARD: direct ViRGE 2D-engine MMIO. Bounded -- a 500k-spin idle watchdog
REM per BLT + atexit text-mode restore. If the screen blanks/hangs for any
REM reason, Ctrl-Alt-Del recovers cleanly; S3BLT.LOG keeps whatever completed.
ECHO Running S3BLT S3 ViRGE/DX 2D BitBLT engine probe...
ECHO   HW-IO probe; ~10-30 sec; transient on-screen garbage is expected.
ECHO   No screen observation needed -- result goes to S3BLT.LOG.
S3BLT.EXE
ECHO ----
ECHO Probe finished. Full result in S3BLT.LOG (logback collects it).
REM NOTE: no pipe-to-filter here -- external DOS filter utilities are not
REM always on the iter PATH, and a missing one aborts the on-screen echo.
REM Just dump the whole log; the GATE_16x16 verdict is near the end.
TYPE S3BLT.LOG

@echo off
REM MEMBW.BAT - memory bandwidth probe (path-B decision + 486-class LFB-write).
REM Runtime: ~30-60 sec (sysmem sweep + VESA LFB-write section; longer on a
REM slow 486). Output: MEMBW.OUT (logback collects it).
REM No env vars required. Standalone DJGPP probe; CWSDPMI.EXE same dir.
REM NOTE: the LFB-write section switches to a VESA graphics mode and restores
REM text mode -- a brief screen flicker mid-run is expected, not a fault.
REM
REM v2 note: probe writes MEMBW.OUT directly via fopen (mirrors l1fill.c
REM convention), so output capture works regardless of how operator invokes
REM the probe - bare `membw`, `MEMBW`, `MEMBW.EXE`, or `MEMBW.BAT` all
REM produce the same MEMBW.OUT. This BAT is now optional convenience.
ECHO Running MEMBW probe (writes MEMBW.OUT, ~10-20 sec)...
MEMBW.EXE
ECHO Done.
ECHO ---- MEMBW.OUT ----
TYPE MEMBW.OUT

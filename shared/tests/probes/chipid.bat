@ECHO OFF
REM CHIPID.BAT - Cirrus chip-detect + BLT-engagement forensic dump.
REM Phase 11 wave-27 / iter K, BLTFILL v2 companion.
REM Read-only register dump: CRTC + Sequencer + Cirrus extended GR
REM + VBE info + PCI config space. Only side-effect is SR[0x06]=0x12
REM (Cirrus extension unlock; non-destructive).
REM Runtime: under 1 sec on PODP83 (just register reads, no timing).
REM No env vars required. Pure DJGPP; CWSDPMI.EXE same dir.
ECHO Running CHIPID forensic register dump (BLTFILL v2 companion)...
CHIPID.EXE
ECHO ----
ECHO Probe finished. CHIPID.LOG contents:
TYPE CHIPID.LOG
